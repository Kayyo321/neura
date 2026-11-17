#include "value_layout.hpp"

#include "aggregate.hpp"
#include "containment.hpp"
#include "float.hpp"
#include "integer.hpp"

#include <algorithm>
#include <stdexcept>

namespace neura {

namespace {

constexpr std::size_t saturated_words = std::size_t{1} << 32;

std::size_t saturating_add(std::size_t a, std::size_t b) {
    return std::min(a + b, saturated_words);
}

const IrType tag_type{TypeKind::Integer, "uint32", no_symbol, {}};

} // namespace

std::string type_identity(const IrType& type) {
    std::string key = std::to_string(static_cast<int>(type.kind)) + ":" + type.name + ":" + std::to_string(type.symbol);
    if (!type.arguments.empty()) {
        key += "(";
        for (const auto& argument : type.arguments) key += type_identity(argument) + ",";
        key += ")";
    }
    return key;
}

bool inline_value(const IrProgram& program, const IrType& type) {
    if (type.kind == TypeKind::Tuple || type.kind == TypeKind::Any) return true;
    if (type.kind == TypeKind::Object) return HookIndex(program).carrying(type);
    return type.kind == TypeKind::Enum && type.symbol < program.symbols.size() &&
           has_enum_payload(program, type.symbol);
}

ValueLayouts::ValueLayouts(const IrProgram& program) : program_(program), hooks_(program) {
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind == SymbolKind::Class && item.name == "$class") any_reference_ = item.type;
        if (item.kind != SymbolKind::Variant || item.owner == no_symbol) continue;
        variants_[item.owner].push_back(id);
        if (!item.payload_types.empty()) payload_enums_.insert(item.owner);
    }
}

bool ValueLayouts::inline_value(const IrType& type) const {
    if (type.kind == TypeKind::Tuple || type.kind == TypeKind::Any) return true;
    return hooks_.carrying(type) || (type.kind == TypeKind::Enum && payload_enums_.contains(type.symbol));
}

const std::vector<SymbolId>& ValueLayouts::variants(SymbolId enumeration) const {
    static const std::vector<SymbolId> none;
    const auto found = variants_.find(enumeration);
    return found == variants_.end() ? none : found->second;
}

std::string ValueLayouts::word_key(const IrType& word) {
    switch (word.kind) {
    case TypeKind::Integer:
        if (const auto format = specified_integer_format(word))
            return (format->is_signed ? "i" : "u") + std::to_string(format->bits);
        return "?" + word.name;
    case TypeKind::Float:
        if (const auto format = float_format(word))
            return *format == bfloat16 ? "bf16" : "f" + std::to_string(format->width());
        return "?" + word.name;
    case TypeKind::Bool:
        return "b";
    case TypeKind::String:
        return "t";
    case TypeKind::Enum:
        // A fieldless enum is its uint32 ordinal.
        return "u32";
    case TypeKind::Object:
        return "o" + std::to_string(word.symbol);
    case TypeKind::Array:
    case TypeKind::List: {
        const std::string prefix = word.kind == TypeKind::Array ? "A" : "L";
        if (word.arguments.size() != 1) break;
        const auto& element = word.arguments.front();
        if (!inline_value(element)) return prefix + "(" + word_key(element) + ")";
        // A tuple of one word is stored as that word.
        const IrType* single = &element;
        while (single->kind == TypeKind::Tuple && single->arguments.size() == 1) single = &single->arguments.front();
        if (!inline_value(*single)) return prefix + "(" + word_key(*single) + ")";
        // The element's aggregate is named by the element's type: it may hold
        // a collection of itself, which a key built from its words could not.
        return prefix + "[" + type_identity(element) + "]";
    }
    default:
        break;
    }
    return "x" + std::to_string(static_cast<int>(word.kind)) + ":" + word.name;
}

void ValueLayouts::enter(SymbolId enumeration) {
    if (active_.insert(enumeration).second) return;
    // The enum holds itself inline. The containment check reports this
    // first; a conversion route that skips it still never recurses forever.
    const auto cycles = find_containment_cycles(program_);
    if (!cycles.empty()) throw containment_diagnostic(program_, cycles.front());
    throw std::logic_error("an enum contains itself: " + program_.symbols[enumeration].name);
}

ValueLayouts::Counts ValueLayouts::counts(const IrType& type) {
    const auto add = [](Counts& into, const Counts& from) {
        for (const auto& [key, count] : from) {
            const auto found = std::ranges::find(into, key, &Counts::value_type::first);
            if (found == into.end()) into.emplace_back(key, count);
            else found->second = saturating_add(found->second, count);
        }
    };
    if (!inline_value(type)) return {{word_key(type), 1}};
    if (type.kind == TypeKind::Object) return {{word_key(type), 1}, {"u32", 1}};
    Counts result;
    if (type.kind == TypeKind::Any) {
        result = {{"u32", 1}, {"u64", 1}, {"t", 1}};
        add(result, counts(any_reference_));
        return result;
    }
    if (type.kind == TypeKind::Tuple) {
        for (const auto& element : type.arguments) add(result, counts(element));
        return result;
    }
    if (const auto found = enum_counts_.find(type.symbol); found != enum_counts_.end()) return found->second;
    enter(type.symbol);
    // Each type's pool holds as many words as the variant that needs the most.
    Counts pools;
    for (const auto variant : variants(type.symbol)) {
        Counts own;
        for (const auto& payload : program_.symbols[variant].payload_types) add(own, counts(payload));
        for (const auto& [key, count] : own) {
            const auto found = std::ranges::find(pools, key, &Counts::value_type::first);
            if (found == pools.end()) pools.emplace_back(key, count);
            else found->second = std::max(found->second, count);
        }
    }
    active_.erase(type.symbol);
    result.emplace_back("u32", 1);
    add(result, pools);
    enum_counts_.emplace(type.symbol, result);
    return result;
}

std::size_t ValueLayouts::value_words(const IrType& type) {
    if (!inline_value(type)) return 1;
    std::size_t total = 0;
    for (const auto& [key, count] : counts(type)) total = saturating_add(total, count);
    return total;
}

const ValueLayout& ValueLayouts::layout(const IrType& type, const Span& where) {
    auto key = type_identity(type);
    if (const auto found = layouts_.find(key); found != layouts_.end()) return found->second;
    auto built = build(type, where);
    return layouts_.emplace(std::move(key), std::move(built)).first->second;
}

ValueLayout ValueLayouts::build(const IrType& type, const Span& where) {
    ValueLayout result;
    if (!inline_value(type)) {
        result.words = {type};
        result.suffixes = {""};
        return result;
    }
    if (type.kind == TypeKind::Any) {
        result.form = ValueLayout::Form::Any;
        result.words = {tag_type, IrType{TypeKind::Integer, "uint64", no_symbol, {}},
                        IrType{TypeKind::String, "string", no_symbol, {}}, any_reference_};
        result.suffixes = {".tag", ".bits", ".text", ".reference"};
        result.carries_policy = hooks_.carrying(any_reference_);
        if (result.carries_policy) {
            result.words.push_back(tag_type);
            result.suffixes.push_back(".reference.policy");
        }
        return result;
    }
    if (type.kind == TypeKind::Object) {
        result.form = ValueLayout::Form::Reference;
        result.carries_policy = true;
        result.words = {type, tag_type};
        result.suffixes = {"", ".policy"};
        return result;
    }
    if (type.kind == TypeKind::Tuple) {
        result.form = ValueLayout::Form::Tuple;
        result.parts = type.arguments;
        for (std::size_t i = 0; i < type.arguments.size(); ++i) {
            const auto& inner = layout(type.arguments[i], where);
            result.carries_policy |= inner.carries_policy;
            auto& positions = result.elements.emplace_back();
            for (std::size_t word = 0; word < inner.words.size(); ++word) {
                positions.push_back(result.words.size());
                result.words.push_back(inner.words[word]);
                result.suffixes.push_back("." + std::to_string(i) + inner.suffixes[word]);
            }
        }
        if (result.words.empty()) throw Diagnostic(where, "a tuple without elements has no value").with_code("E0802");
        return result;
    }

    enter(type.symbol);
    result.form = ValueLayout::Form::Enum;
    result.variants = variants(type.symbol);
    // The pools in order of first use: each type's key, the type of its first
    // word, and the most words of that type any one variant needs.
    struct Pool {
        std::string key;
        IrType type;
        std::size_t count = 0;
        std::size_t start = 0;
    };
    std::vector<Pool> pools;
    // The words of each variant's payloads, with each word's pool.
    struct Word {
        std::size_t payload;
        std::size_t pool;
        std::string suffix;
    };
    std::vector<std::vector<Word>> uses(result.variants.size());
    for (std::size_t v = 0; v < result.variants.size(); ++v) {
        const auto& variant = program_.symbols[result.variants[v]];
        std::vector<std::size_t> own;
        for (std::size_t p = 0; p < variant.payload_types.size(); ++p) {
            result.parts.push_back(variant.payload_types[p]);
            const auto& inner = layout(variant.payload_types[p], where);
            result.carries_policy |= inner.carries_policy;
            for (std::size_t w = 0; w < inner.words.size(); ++w) {
                const auto key = word_key(inner.words[w]);
                auto found = std::ranges::find(pools, key, &Pool::key);
                if (found == pools.end()) {
                    pools.push_back({key, inner.words[w], 0, 0});
                    found = pools.end() - 1;
                }
                const auto pool = static_cast<std::size_t>(found - pools.begin());
                if (own.size() <= pool) own.resize(pool + 1, 0);
                found->count = std::max(found->count, ++own[pool]);
                uses[v].push_back({p, pool, variant.name + "." + std::to_string(p) + inner.suffixes[w]});
            }
        }
    }
    active_.erase(type.symbol);
    result.words.push_back(tag_type);
    for (auto& pool : pools) {
        pool.start = result.words.size();
        result.words.insert(result.words.end(), pool.count, pool.type);
    }
    std::vector<std::vector<std::string>> names(result.words.size());
    for (std::size_t v = 0; v < result.variants.size(); ++v) {
        auto& payloads = result.payloads.emplace_back(program_.symbols[result.variants[v]].payload_types.size());
        std::vector<std::size_t> cursor(pools.size(), 0);
        for (const auto& use : uses[v]) {
            const auto position = pools[use.pool].start + cursor[use.pool]++;
            payloads[use.payload].push_back(position);
            names[position].push_back(use.suffix);
        }
    }
    result.suffixes.push_back(".tag");
    for (std::size_t i = 1; i < names.size(); ++i) {
        std::string suffix = ".";
        for (std::size_t u = 0; u < names[i].size(); ++u) suffix += (u == 0 ? "" : "|") + names[i][u];
        result.suffixes.push_back(std::move(suffix));
    }
    return result;
}

} // namespace neura
