// Source tags (docs/lang/Tags.md): the registry, the validation of tags at a
// declaration, the use-site checks, and the whole-program checks.
#include "tags.hpp"

#include "attributes.hpp"

#include "util/diagnostic_codes.hpp"
#include "util/source_map.hpp"
#include "util/suggest.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <ostream>
#include <tuple>
#include <utility>

namespace neura {
namespace {

} // namespace

// Inside tag_targets, tags.def's target names (Class, Parameter, ...) mean
// the masks rather than the syntax types of the same names.
namespace tag_targets {
namespace {
constexpr TagStatus implemented = TagStatus::Implemented;
constexpr TagStatus informational = TagStatus::Informational;
constexpr TagStatus reserved = TagStatus::Reserved;

constexpr TagInfo registry[] = {
#define NEURA_TAG(name, id, group, targets, status, repeats, arguments, since, meaning, alternative) \
    {TagId::name, #name, TagGroup::group, (targets), status, repeats, arguments, since, meaning, alternative},
#include "tags.def"
};
} // namespace
} // namespace tag_targets

namespace {

using tag_targets::registry;

constexpr std::uint32_t bit(TagTarget target) { return 1u << static_cast<unsigned>(target); }

// Pairs of tags that cannot appear on one declaration.
constexpr std::pair<TagId, TagId> conflicts[] = {
    {TagId::Inline, TagId::NoInline},
    {TagId::Final, TagId::Sealed},
    {TagId::Hot, TagId::Cold},
    {TagId::Likely, TagId::Unlikely},
    {TagId::Stable, TagId::Experimental},
    {TagId::ThreadSafe, TagId::NotThreadSafe},
    {TagId::Blocking, TagId::NonBlocking},
    {TagId::Unroll, TagId::NoUnroll},
};

std::string in_quotes(std::string_view name) { return "'" + std::string(name) + "'"; }

// The declarations being resolved or lowered, innermost last (TagScope).
thread_local std::vector<SymbolId> frames;

bool valid(const IrProgram& program, SymbolId id) { return id != no_symbol && id < program.symbols.size(); }

// Whether `inner` is `outer` or declared inside it.
bool within(const IrProgram& program, SymbolId inner, SymbolId outer) {
    for (auto scope = inner; valid(program, scope); scope = program.symbols[scope].owner)
        if (scope == outer) return true;
    return false;
}

// The declaration a use is judged from: the innermost declaration being
// resolved or lowered when the use lies inside it, else the lexical owner.
SymbolId effective_scope(const IrProgram& program, SymbolId scope) {
    if (!frames.empty()) {
        const auto top = frames.back();
        if (valid(program, top) && (!valid(program, scope) || within(program, top, scope))) return top;
    }
    return scope;
}

std::vector<const IrAttribute*> tags_of(const IrSymbol& symbol, TagId tag) {
    std::vector<const IrAttribute*> result;
    for (const auto* attribute : find_attributes(symbol, AttributeId::Tag)) {
        const auto& items = attribute->value.items;
        if (items.size() == 4 && items[0].integer == static_cast<std::uint16_t>(tag)) result.push_back(attribute);
    }
    return result;
}

// Whether `scope` or a declaration around it carries `tag`.
bool inside_tagged(const IrProgram& program, SymbolId scope, TagId tag) {
    for (auto id = scope; valid(program, id); id = program.symbols[id].owner)
        if (has_tag(program.symbols[id], tag)) return true;
    return false;
}

SymbolId file_of(const IrProgram& program, SymbolId id) {
    for (; valid(program, id); id = program.symbols[id].owner)
        if (program.symbols[id].kind == SymbolKind::File) return id;
    return no_symbol;
}

std::string_view kind_word(const IrProgram& program, SymbolId id) {
    switch (tag_target(program, id)) {
    case TagTarget::Module: return "module";
    case TagTarget::Class: return "class";
    case TagTarget::Interface: return "interface";
    case TagTarget::Enum: return "enum";
    case TagTarget::Variant: return "variant";
    case TagTarget::Function: return "function";
    case TagTarget::Method: return "method";
    case TagTarget::Field: return "field";
    case TagTarget::Global: return "binding";
    case TagTarget::Local: return "local binding";
    case TagTarget::Parameter: return "parameter";
    }
    return "declaration";
}

std::string article(std::string_view noun) {
    const bool vowel = !noun.empty() && std::string_view("aeiou").find(noun.front()) != std::string_view::npos;
    return std::string(vowel ? "an " : "a ") + std::string(noun);
}

std::string type_name(char type) {
    switch (type) {
    case 't': return "string";
    case 'i': return "integer";
    case 'b': return "bool";
    case 'x': return "number";
    case 'n': return "name";
    case 'w': return "warning";
    default: return "literal";
    }
}

std::string_view literal_kind(TagArgumentSyntax::Kind kind) {
    switch (kind) {
    case TagArgumentSyntax::Kind::String: return "a string";
    case TagArgumentSyntax::Kind::Integer: return "an integer";
    case TagArgumentSyntax::Kind::Float: return "a floating-point number";
    case TagArgumentSyntax::Kind::Boolean: return "a bool";
    case TagArgumentSyntax::Kind::Name: return "a name";
    case TagArgumentSyntax::Kind::List: return "a list";
    }
    return "a literal";
}

std::string normalized_warning(std::string text) {
    std::ranges::replace(text, '_', '-');
    return text;
}

// The literal value of one argument checked against its parameter.
IrAttributeValue argument_value(const TagInfo& tag, const TagParameter& parameter,
                                const TagArgumentSyntax& argument) {
    using Kind = TagArgumentSyntax::Kind;
    const auto mismatch = [&] {
        auto error = Diagnostic(argument.span, "#" + std::string(tag.name) + " expects " +
                                                   article(type_name(parameter.type)) + " for " +
                                                   in_quotes(parameter.name) + ", found " +
                                                   std::string(literal_kind(argument.kind)))
            .with_code("E0905").label("expected " + article(type_name(parameter.type)));
        if (parameter.type == 't' && argument.kind != Kind::String && argument.kind != Kind::List) {
            if (const auto text = source_text(argument.span))
                error.suggest("write it as a string", argument.span, "\"" + *text + "\"",
                              Applicability::MaybeIncorrect);
        }
        error.help("the signature is #" + tag_signature(tag));
        return error;
    };
    switch (parameter.type) {
    case 't':
        if (argument.kind != Kind::String) throw mismatch();
        return attribute_text(argument.text);
    case 'b':
        if (argument.kind != Kind::Boolean) throw mismatch();
        return attribute_bool(argument.text == "true");
    case 'n':
        if (argument.kind != Kind::Name && argument.kind != Kind::String) throw mismatch();
        return attribute_text(argument.text);
    case 'w': {
        if (argument.kind != Kind::Name && argument.kind != Kind::String) throw mismatch();
        const auto* warning = find_warning(normalized_warning(argument.text));
        if (!warning) {
            auto error = Diagnostic(argument.span, "unknown warning " + in_quotes(argument.text) + " in #" +
                                                       std::string(tag.name))
                .with_code("E0915").label("not a warning name or code");
            std::vector<std::string> names;
            for (const auto& entry : warning_codes) names.emplace_back(entry.name);
            if (const auto close = closest_name(normalized_warning(argument.text), names)) {
                auto spelling = *close;
                if (argument.kind == Kind::String) spelling = "\"" + spelling + "\"";
                else std::ranges::replace(spelling, '-', '_');
                error.suggest("there is a warning with a similar name", argument.span, spelling,
                              Applicability::MaybeIncorrect);
            }
            error.note("`neura explain --list` lists the warning codes, and their names follow -W");
            throw error;
        }
        return attribute_text(std::string(warning->code));
    }
    case 'i':
    case 'x':
        if (argument.kind == Kind::Integer) {
            std::int64_t value = 0;
            const auto& text = argument.text;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size())
                throw Diagnostic(argument.span, "the integer " + text + " does not fit in 64 bits")
                    .with_code("E0905").label("out of range for #" + std::string(tag.name));
            return attribute_signed(value);
        }
        if (parameter.type == 'x' && argument.kind == Kind::Float) return attribute_text(argument.text);
        throw mismatch();
    default:
        switch (argument.kind) {
        case Kind::Boolean: return attribute_bool(argument.text == "true");
        case Kind::Integer: {
            std::int64_t value = 0;
            const auto& text = argument.text;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error == std::errc{} && end == text.data() + text.size()) return attribute_signed(value);
            return attribute_text(text);
        }
        case Kind::List: throw mismatch();
        default: return attribute_text(argument.text);
        }
    }
}

std::vector<TagArgument> bind_arguments(const TagInfo& tag, const TagSyntax& syntax) {
    const auto parameters = tag_parameters(tag);
    std::vector<TagArgument> result;
    std::vector<const TagArgumentSyntax*> given(parameters.size(), nullptr);
    std::size_t position = 0;
    bool named = false;
    const std::string written = "#" + std::string(tag.name);
    for (const auto& argument : syntax.arguments) {
        std::size_t index = 0;
        if (!argument.name.empty()) {
            const auto found = std::ranges::find(parameters, argument.name, &TagParameter::name);
            if (found == parameters.end()) {
                auto error = Diagnostic(*argument.name_span,
                                        written + " has no argument " + in_quotes(argument.name))
                    .with_code("E0902").label("unknown argument");
                std::vector<std::string> names;
                for (const auto& parameter : parameters) names.push_back(parameter.name);
                if (const auto close = closest_name(argument.name, names))
                    error.suggest("there is an argument with a similar name", *argument.name_span, *close,
                                  Applicability::MaybeIncorrect);
                error.help(parameters.empty() ? written + " takes no arguments"
                                              : "the signature is #" + tag_signature(tag));
                throw error;
            }
            index = static_cast<std::size_t>(found - parameters.begin());
            if (given[index] && !parameters[index].rest) {
                throw Diagnostic(*argument.name_span, "the argument " + in_quotes(argument.name) + " of " + written +
                                                          " is given twice")
                    .with_code("E0904").label("given again here").label(given[index]->span, "first given here");
            }
            named = true;
        } else {
            if (named) {
                throw Diagnostic(argument.span, "a positional argument of " + written + " follows a named one")
                    .with_code("E0902").label("positional argument after a named argument")
                    .help("name it, or move it before the named arguments");
            }
            if (parameters.empty()) {
                throw Diagnostic(argument.span, written + " takes no arguments").with_code("E0902")
                    .label("unexpected argument").help("write " + written + " without parentheses");
            }
            const bool rest = parameters.back().rest;
            if (position >= parameters.size() && !rest) {
                throw Diagnostic(argument.span, written + " takes " + std::to_string(parameters.size()) +
                                                    (parameters.size() == 1 ? " argument" : " arguments"))
                    .with_code("E0902").label("unexpected argument")
                    .help("the signature is #" + tag_signature(tag));
            }
            index = std::min(position, parameters.size() - 1);
            if (given[index] && !parameters[index].rest) {
                throw Diagnostic(argument.span, "the argument " + in_quotes(parameters[index].name) + " of " +
                                                    written + " is given twice")
                    .with_code("E0904").label("given again here").label(given[index]->span, "first given here");
            }
            ++position;
        }
        const auto& parameter = parameters[index];
        if (!given[index]) given[index] = &argument;
        if (argument.kind == TagArgumentSyntax::Kind::List) {
            if (!parameter.rest) {
                throw Diagnostic(argument.span, written + " expects one " + type_name(parameter.type) + " for " +
                                                    in_quotes(parameter.name) + ", found a list")
                    .with_code("E0905").label("expected " + article(type_name(parameter.type)));
            }
            for (const auto& item : argument.items)
                result.push_back({parameter.name, argument_value(tag, parameter, item)});
        } else {
            result.push_back({parameter.name, argument_value(tag, parameter, argument)});
        }
    }
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        const auto& parameter = parameters[i];
        const bool empty_rest = parameter.rest && parameter.at_least_one &&
            std::ranges::none_of(result, [&](const TagArgument& item) { return item.name == parameter.name; });
        if ((parameter.required && !given[i]) || empty_rest) {
            throw Diagnostic(syntax.span, written + " needs " +
                                              (parameter.rest ? "at least one " + type_name(parameter.type) + " for "
                                                              : "the argument ") + in_quotes(parameter.name))
                .with_code("E0903").label("missing " + in_quotes(parameter.name))
                .help("the signature is #" + tag_signature(tag));
        }
    }
    return result;
}

// The warning codes an #Allow or #Deny tag names.
std::vector<std::string> warning_arguments(const IrAttribute& attribute) {
    std::vector<std::string> result;
    for (const auto& argument : attribute.value.items[2].items)
        if (argument.items.size() == 2) result.push_back(argument.items[1].text);
    return result;
}

struct WarningRule {
    bool allowed = false;
    bool denied = false;
    Span at;
};

WarningRule warning_rule(const IrProgram& program, SymbolId scope, std::string_view code) {
    for (auto id = scope; valid(program, id); id = program.symbols[id].owner) {
        for (const auto* attribute : find_attributes(program.symbols[id], AttributeId::Tag)) {
            const auto& items = attribute->value.items;
            if (items.size() != 4) continue;
            const auto tag = static_cast<TagId>(items[0].integer);
            if (tag != TagId::Allow && tag != TagId::Deny) continue;
            const auto names = warning_arguments(*attribute);
            if (std::ranges::find(names, code) == names.end()) continue;
            return {tag == TagId::Allow, tag == TagId::Deny, items[3].span};
        }
    }
    return {};
}

std::optional<Diagnostic> scoped_warning(const IrProgram& program, SymbolId scope, Diagnostic warning) {
    const auto rule = warning_rule(program, scope, warning.code());
    if (rule.allowed) return std::nullopt;
    if (rule.denied) {
        warning.edit().severity = Severity::Error;
        warning.label(rule.at, "the warning is denied here");
    }
    return warning;
}

// The library an #Internal declaration belongs to: the standard library, or
// the directory of the declaring file with its subdirectories.
struct Library {
    bool standard = false;
    std::filesystem::path directory;
};

std::optional<Library> library_of(const IrProgram& program, SymbolId id) {
    const auto file = file_of(program, id);
    if (!valid(program, file)) return std::nullopt;
    const auto& symbol = program.symbols[file];
    if (!symbol.library_path.empty()) return Library{true, {}};
    return Library{false, std::filesystem::path(symbol.name).lexically_normal().parent_path()};
}

bool contains(const std::filesystem::path& directory, const std::filesystem::path& inner) {
    auto at = inner.begin();
    for (const auto& part : directory) {
        if (part.empty()) continue;
        if (at == inner.end() || *at != part) return false;
        ++at;
    }
    return true;
}

// A member named `name` declared beside `target`, for a replacement.
SymbolId sibling(const IrProgram& program, SymbolId target, std::string_view name) {
    const auto owner = program.symbols[target].owner;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& candidate = program.symbols[id];
        if (id != target && candidate.owner == owner && candidate.name == name &&
            candidate.kind != SymbolKind::Local && candidate.kind != SymbolKind::Parameter)
            return id;
    }
    return no_symbol;
}

bool simple_name(std::string_view text) {
    if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text.front())) || text.front() == '_')) return false;
    return std::ranges::all_of(text, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

void deprecated_use(const IrProgram& program, SymbolId target, const Span& use, SymbolId scope,
                    const NameLookup& lookup, std::string_view what) {
    const auto& item = program.symbols[target];
    const auto as_of = tag_text(item, TagId::Deprecated, "as_of").value_or("");
    const auto reason = tag_text(item, TagId::Deprecated, "reason").value_or("");
    const auto instead = tag_text(item, TagId::Deprecated, "use_instead").value_or("");
    std::string message = std::string(what) + in_quotes(item.name) + " is deprecated";
    if (!as_of.empty()) message += " since " + as_of;
    if (!reason.empty()) message += ": " + reason;
    auto warning = Diagnostic::warning(use, "deprecated", std::move(message)).with_code("W0001");
    if (const auto at = tag_span(item, TagId::Deprecated))
        warning.label(*at, in_quotes(item.name) + " is marked deprecated here");
    if (!instead.empty()) {
        bool suggested = false;
        const auto replacement = sibling(program, target, instead);
        const auto text = source_text(use);
        if (simple_name(instead) && valid(program, replacement) && text) {
            if (*text == item.name) {
                // A bare name: the replacement must resolve to the sibling here.
                if (lookup && lookup(instead) == replacement) {
                    warning.suggest("use " + in_quotes(instead) + " instead", use, instead,
                                    Applicability::MachineApplicable);
                    suggested = true;
                }
            } else if (text->size() > item.name.size() && text->ends_with(item.name) &&
                       (*text)[text->size() - item.name.size() - 1] == '.') {
                // A member or qualified name: the sibling is reached the same way.
                const auto& sibling_symbol = program.symbols[replacement];
                const auto owner = item.owner;
                if (sibling_symbol.exported || within(program, scope, owner)) {
                    if (const auto span = source_span(use.file, use.offset + use.length - item.name.size(),
                                                      item.name.size())) {
                        warning.suggest("use " + in_quotes(instead) + " instead", *span, instead,
                                        Applicability::MachineApplicable);
                        suggested = true;
                    }
                }
            }
        }
        if (!suggested) warning.help("use " + in_quotes(instead) + " instead");
    }
    report_scoped_warning(program, scope, std::move(warning));
}

void experimental_use(const IrProgram& program, SymbolId target, const Span& use, SymbolId scope,
                      std::string_view what) {
    const auto& item = program.symbols[target];
    const auto feature = tag_text(item, TagId::Experimental, "feature").value_or("");
    std::string message = std::string(what) + in_quotes(item.name) + " is experimental";
    if (!feature.empty()) message += ": it belongs to the preview feature " + in_quotes(feature);
    auto warning = Diagnostic::warning(use, "experimental", std::move(message)).with_code("W0002");
    if (const auto at = tag_span(item, TagId::Experimental))
        warning.label(*at, in_quotes(item.name) + " is marked experimental here");
    warning.note("an experimental declaration may change or disappear without a deprecation period");
    report_scoped_warning(program, scope, std::move(warning));
}

void internal_use(const IrProgram& program, SymbolId target, const Span& use, SymbolId scope) {
    const auto declared = library_of(program, target);
    const auto used = library_of(program, scope);
    if (!declared || !used) return;
    if (declared->standard ? used->standard : !used->standard && contains(declared->directory, used->directory))
        return;
    const auto& item = program.symbols[target];
    auto error = Diagnostic(use, std::string(kind_word(program, target)) + " " + in_quotes(item.name) +
                                     " is internal to its library")
        .with_code("E0911").label("used outside the library that declares it");
    if (const auto at = tag_span(item, TagId::Internal))
        error.label(*at, in_quotes(item.name) + " is marked internal here");
    if (declared->standard) {
        error.note("an #Internal declaration of the standard library is usable only inside the standard library");
    } else {
        error.note("an #Internal declaration is usable only from files in " +
                   in_quotes(declared->directory.generic_string()) + " and its subdirectories");
    }
    throw error;
}

} // namespace

std::span<const TagInfo> tag_registry() { return registry; }

const TagInfo* find_tag(std::string_view name) {
    for (const auto& tag : registry)
        if (tag.name == name) return &tag;
    return nullptr;
}

const TagInfo* find_tag(std::uint16_t id) {
    for (const auto& tag : registry)
        if (static_cast<std::uint16_t>(tag.id) == id) return &tag;
    return nullptr;
}

std::vector<TagParameter> tag_parameters(const TagInfo& tag) {
    std::vector<TagParameter> result;
    std::string_view rest = tag.arguments;
    while (!rest.empty()) {
        const auto comma = rest.find(',');
        auto entry = rest.substr(0, comma);
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        TagParameter parameter;
        const auto colon = entry.find(':');
        parameter.name = std::string(entry.substr(0, colon));
        auto type = entry.substr(colon + 1);
        parameter.type = type.front();
        type.remove_prefix(1);
        if (type.starts_with('=')) {
            parameter.required = false;
            parameter.default_value = std::string(type.substr(1));
        } else if (type == "?") {
            parameter.required = false;
        } else if (type == "+" || type == "*") {
            parameter.required = false;
            parameter.rest = true;
            parameter.at_least_one = type == "+";
        }
        result.push_back(std::move(parameter));
    }
    return result;
}

std::string tag_signature(const TagInfo& tag) {
    std::string result(tag.name);
    const auto parameters = tag_parameters(tag);
    if (parameters.empty()) return result;
    result += '(';
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        const auto& parameter = parameters[i];
        if (i != 0) result += ", ";
        result += parameter.name + ": " + type_name(parameter.type);
        if (parameter.rest) result += parameter.at_least_one ? "..." : "...?";
        else if (parameter.default_value)
            result += " = " + (parameter.type == 't' || parameter.type == 'n' ? "\"" + *parameter.default_value + "\""
                                                                                : *parameter.default_value);
        else if (!parameter.required) result += '?';
    }
    return result + ')';
}

std::string_view tag_group_name(TagGroup group) {
    switch (group) {
    case TagGroup::Lifecycle: return "API lifecycle";
    case TagGroup::Correctness: return "Correctness";
    case TagGroup::Optimization: return "Optimization";
    case TagGroup::Concurrency: return "Concurrency";
    case TagGroup::Testing: return "Testing";
    case TagGroup::Documentation: return "Documentation";
    case TagGroup::Interop: return "Interop and layout";
    case TagGroup::Diagnostics: return "Diagnostics control";
    case TagGroup::Security: return "Security";
    case TagGroup::Serialization: return "Serialization";
    case TagGroup::Resources: return "Resource management";
    case TagGroup::Numerics: return "Numerics";
    }
    return {};
}

std::string_view tag_status_name(TagStatus status) {
    switch (status) {
    case TagStatus::Implemented: return "implemented";
    case TagStatus::Informational: return "informational";
    case TagStatus::Reserved: return "reserved";
    }
    return {};
}

std::string_view tag_target_name(TagTarget target) {
    switch (target) {
    case TagTarget::Module: return "module";
    case TagTarget::Class: return "class";
    case TagTarget::Interface: return "interface";
    case TagTarget::Enum: return "enum";
    case TagTarget::Variant: return "enum variant";
    case TagTarget::Function: return "function";
    case TagTarget::Method: return "method";
    case TagTarget::Field: return "field";
    case TagTarget::Global: return "file or module binding";
    case TagTarget::Local: return "local binding";
    case TagTarget::Parameter: return "parameter";
    }
    return {};
}

std::string describe_tag_targets(std::uint32_t mask) {
    using namespace tag_targets;
    if (mask == Declarations) return "declarations";
    if (mask == (Declarations & ~Variant)) return "declarations other than enum variants";
    if (mask == (Declarations | Local | tag_targets::Parameter)) return "declarations, local bindings, and parameters";
    static constexpr std::pair<std::uint32_t, std::string_view> names[] = {
        {Module, "modules"}, {Class, "classes"}, {Interface, "interfaces"}, {Enum, "enums"},
        {Variant, "enum variants"}, {Function, "functions"}, {Method, "methods"}, {Field, "fields"},
        {Global, "file and module bindings"}, {Local, "local bindings"}, {tag_targets::Parameter, "parameters"},
    };
    std::vector<std::string_view> parts;
    for (const auto& [flag, name] : names)
        if (mask & flag) parts.push_back(name);
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) result += i + 1 == parts.size() ? (parts.size() == 2 ? " and " : ", and ") : ", ";
        result += parts[i];
    }
    return result;
}

void print_tag(std::ostream& out, const TagInfo& tag) {
    out << "#" << tag_signature(tag) << "\n";
    out << "    status:  " << tag_status_name(tag.status) << (tag.repeats ? ", repeatable" : "") << ", since "
        << tag.since << "\n";
    out << "    applies to " << describe_tag_targets(tag.targets) << "\n";
    out << "    " << tag.meaning << "\n";
    if (!tag.alternative.empty()) out << "    instead: " << tag.alternative << "\n";
}

void print_tag_catalogue(std::ostream& out) {
    std::optional<TagGroup> group;
    std::size_t count[3] = {};
    for (const auto& tag : registry) {
        if (group != tag.group) {
            if (group) out << "\n";
            group = tag.group;
            out << tag_group_name(tag.group) << "\n\n";
        }
        print_tag(out, tag);
        ++count[static_cast<unsigned>(tag.status)];
    }
    out << "\n"
        << std::size(registry) << " tags: " << count[0] << " implemented, " << count[1] << " informational, "
        << count[2] << " reserved. Reserved tags are rejected (E0908) until the compiler does what they promise.\n";
}

bool has_tag(const IrSymbol& symbol, TagId tag) { return !tags_of(symbol, tag).empty(); }

std::optional<std::string> tag_text(const IrSymbol& symbol, TagId tag, std::string_view argument) {
    const auto found = tags_of(symbol, tag);
    if (found.empty()) return std::nullopt;
    for (const auto& item : found.front()->value.items[2].items) {
        if (item.items.size() == 2 && item.items[0].text == argument) {
            const auto& value = item.items[1];
            switch (value.kind) {
            case AttributeValueKind::Text: return value.text;
            case AttributeValueKind::Bool: return std::string(value.integer ? "true" : "false");
            case AttributeValueKind::Signed: return std::to_string(attribute_signed_value(value));
            case AttributeValueKind::Unsigned: return std::to_string(value.integer);
            default: return std::nullopt;
            }
        }
    }
    if (const auto* info = find_tag(static_cast<std::uint16_t>(tag))) {
        for (const auto& parameter : tag_parameters(*info))
            if (parameter.name == argument) return parameter.default_value;
    }
    return std::nullopt;
}

std::optional<Span> tag_span(const IrSymbol& symbol, TagId tag) {
    const auto found = tags_of(symbol, tag);
    if (found.empty()) return std::nullopt;
    return found.front()->value.items[3].span;
}

TagTarget tag_target(const IrProgram& program, SymbolId id) {
    const auto& symbol = program.symbols[id];
    const auto owner_kind = [&](SymbolId owner) {
        return valid(program, owner) ? program.symbols[owner].kind : SymbolKind::File;
    };
    auto owner = symbol.owner;
    const bool static_owner = valid(program, owner) && is_static_scope(program.symbols[owner]);
    if (static_owner) owner = program.symbols[owner].owner;
    switch (symbol.kind) {
    case SymbolKind::Module: return TagTarget::Module;
    case SymbolKind::Class: return TagTarget::Class;
    case SymbolKind::Interface: return TagTarget::Interface;
    case SymbolKind::Enum: return TagTarget::Enum;
    case SymbolKind::Variant: return TagTarget::Variant;
    case SymbolKind::Function: {
        const auto kind = owner_kind(owner);
        return kind == SymbolKind::Class || kind == SymbolKind::Interface ? TagTarget::Method : TagTarget::Function;
    }
    case SymbolKind::Field: return TagTarget::Field;
    case SymbolKind::Local: return TagTarget::Local;
    case SymbolKind::Parameter: return TagTarget::Parameter;
    default: return static_owner ? TagTarget::Field : TagTarget::Global;
    }
}

void check_tags(const std::vector<TagSyntax>& tags, TagTarget target, const Span& declaration) {
    IrProgram scratch;
    IrSymbol symbol{};
    symbol.kind = SymbolKind::Parameter;
    scratch.symbols.push_back(symbol);
    attach_tags(scratch, 0, target, tags, declaration);
}

void attach_tags(IrProgram& program, SymbolId id, TagTarget target, const std::vector<TagSyntax>& tags,
                 const Span& declaration, std::span<const SymbolId> also) {
    if (tags.empty()) return;
    struct Accepted {
        const TagInfo* info;
        const TagSyntax* syntax;
        std::vector<TagArgument> arguments;
    };
    std::vector<Accepted> accepted;
    for (const auto& tag : tags) {
        const auto* info = find_tag(tag.name);
        if (!info) {
            auto error = Diagnostic(tag.name_span, "unknown tag #" + tag.name).with_code("E0900")
                .label("not a registered tag").arg("kind", "tag").arg("name", tag.name);
            std::vector<std::string> names;
            for (const auto& entry : registry) names.emplace_back(entry.name);
            if (const auto close = closest_name(tag.name, names)) {
                const auto distance = edit_distance(tag.name, *close);
                error.arg("suggestion", *close);
                error.suggest("there is a tag with a similar name: #" + *close, tag.name_span, *close,
                              distance <= 2 ? Applicability::MachineApplicable : Applicability::MaybeIncorrect);
            }
            error.note("`neura tags` lists every tag");
            throw error;
        }
        if (!(info->targets & bit(target))) {
            throw Diagnostic(tag.name_span, "#" + tag.name + " does not apply to " +
                                                article(tag_target_name(target)))
                .with_code("E0901").label("#" + tag.name + " applies to " + describe_tag_targets(info->targets))
                .label(declaration, "this is " + article(tag_target_name(target)));
        }
        auto arguments = bind_arguments(*info, tag);
        if (!info->repeats) {
            for (const auto& earlier : accepted) {
                if (earlier.info == info)
                    throw Diagnostic(tag.span, "#" + tag.name + " is given twice").with_code("E0906")
                        .label("given again here").label(earlier.syntax->span, "first given here")
                        .help("remove one of them");
            }
        }
        for (const auto& earlier : accepted) {
            for (const auto& [a, b] : conflicts) {
                if ((earlier.info->id == a && info->id == b) || (earlier.info->id == b && info->id == a))
                    throw Diagnostic(tag.span, "#" + tag.name + " conflicts with #" + std::string(earlier.info->name))
                        .with_code("E0907").label("#" + tag.name + " given here")
                        .label(earlier.syntax->span, "#" + std::string(earlier.info->name) + " given here")
                        .help("keep one of them");
            }
            const bool opposite = (earlier.info->id == TagId::Allow && info->id == TagId::Deny) ||
                                  (earlier.info->id == TagId::Deny && info->id == TagId::Allow);
            if (!opposite) continue;
            for (const auto& mine : arguments) {
                for (const auto& theirs : earlier.arguments) {
                    if (mine.value.text != theirs.value.text) continue;
                    throw Diagnostic(tag.span, "#" + tag.name + " conflicts with #" + std::string(earlier.info->name) +
                                                   " for warning " + mine.value.text)
                        .with_code("E0907").label("#" + tag.name + " given here")
                        .label(earlier.syntax->span, "#" + std::string(earlier.info->name) + " given here")
                        .help("keep one of them");
                }
            }
        }
        if (info->status == TagStatus::Reserved) {
            throw Diagnostic(tag.span, "#" + tag.name + " is reserved: the compiler does not do what it promises yet")
                .with_code("E0908").label("reserved tag")
                .note("#" + tag.name + ": " + std::string(info->meaning))
                .help(std::string(info->alternative))
                .note("reserved tags are rejected so that no program relies on a promise the compiler does not keep");
        }
        accepted.push_back({info, &tag, std::move(arguments)});
    }
    std::vector<SymbolId> targets{id};
    targets.insert(targets.end(), also.begin(), also.end());
    for (const auto symbol : targets) {
        if (!valid(program, symbol)) continue;
        for (const auto& item : accepted) {
            add_attribute(program.symbols[symbol], AttributeId::Tag,
                          tag_payload(static_cast<std::uint16_t>(item.info->id), item.syntax->name, item.arguments,
                                      item.syntax->span));
            if (item.info->id == TagId::NoInline) {
                set_attribute(program.symbols[symbol], AttributeId::InlineDirective,
                              attribute_unsigned(static_cast<std::uint64_t>(InlineDirective::Never)));
            } else if (item.info->id == TagId::Inline &&
                       tag_text(program.symbols[symbol], TagId::Inline, "always") == "true") {
                set_attribute(program.symbols[symbol], AttributeId::InlineDirective,
                              attribute_unsigned(static_cast<std::uint64_t>(InlineDirective::Always)));
            }
        }
    }
}

TagScope::TagScope(SymbolId symbol) { frames.push_back(symbol); }
TagScope::~TagScope() { frames.pop_back(); }

void check_tagged_use(const IrProgram& program, SymbolId target, const Span& use, SymbolId scope,
                      const NameLookup& lookup) {
    if (!valid(program, target)) return;
    const auto& item = program.symbols[target];
    if (item.attributes.empty()) return;
    scope = effective_scope(program, scope);
    const std::string what = std::string(kind_word(program, target)) + " ";
    if (has_tag(item, TagId::Internal)) internal_use(program, target, use, scope);
    if (has_tag(item, TagId::Deprecated) && !within(program, scope, target) &&
        !inside_tagged(program, scope, TagId::Deprecated))
        deprecated_use(program, target, use, scope, lookup, what);
    if (has_tag(item, TagId::Experimental) && !within(program, scope, target) &&
        !inside_tagged(program, scope, TagId::Experimental))
        experimental_use(program, target, use, scope, what);
}

void check_discarded_result(const IrProgram& program, const IrNode& value, const Span& statement, SymbolId scope) {
    if (value.op != IrOp::Call && value.op != IrOp::VirtualCall && value.op != IrOp::ExternalCall &&
        value.op != IrOp::CallClosure)
        return;
    if (value.type.kind == TypeKind::Void) return;
    SymbolId tagged = no_symbol;
    bool by_type = false;
    if (valid(program, value.symbol) && has_tag(program.symbols[value.symbol], TagId::NoDiscard)) {
        tagged = value.symbol;
    } else if (valid(program, value.type.symbol) && has_tag(program.symbols[value.type.symbol], TagId::NoDiscard)) {
        tagged = value.type.symbol;
        by_type = true;
    }
    if (tagged == no_symbol) return;
    const auto& item = program.symbols[tagged];
    const auto reason = tag_text(item, TagId::NoDiscard, "reason").value_or("");
    std::string message = by_type ? "unused value of #NoDiscard type " + in_quotes(item.name)
                                  : "unused result of #NoDiscard " + std::string(kind_word(program, tagged)) + " " +
                                        in_quotes(item.name);
    if (!reason.empty()) message += ": " + reason;
    auto warning = Diagnostic::warning(value.span, "unused-result", std::move(message)).with_code("W0003")
        .label("the result is discarded");
    if (const auto at = tag_span(item, TagId::NoDiscard))
        warning.label(*at, in_quotes(item.name) + " is marked #NoDiscard here");
    if (const auto start = source_span(statement.file, statement.offset, 0); start && user_source(statement.file))
        warning.suggest("bind the result to '_' to discard it on purpose", *start, "let _ = ",
                        Applicability::MaybeIncorrect);
    else
        warning.help("bind the result to '_' to discard it on purpose: let _ = ...;");
    report_scoped_warning(program, effective_scope(program, scope), std::move(warning));
}

void report_scoped_warning(const IrProgram& program, SymbolId scope, Diagnostic warning) {
    if (auto result = scoped_warning(program, scope, std::move(warning))) report_warning(std::move(*result));
}

std::optional<Diagnostic> scoped_tag_warning(const IrProgram& program, SymbolId scope, Diagnostic warning) {
    return scoped_warning(program, scope, std::move(warning));
}

void finish_tags(const IrProgram& program, SymbolId first, std::size_t mark,
                 const std::function<std::optional<TagDeclarationSpans>(SymbolId)>& spans) {
    // The module or file that directly holds a sealed type.
    const auto home = [&](SymbolId type) {
        auto owner = program.symbols[type].owner;
        while (valid(program, owner) && program.symbols[owner].kind != SymbolKind::File &&
               (program.symbols[owner].kind != SymbolKind::Module || is_static_scope(program.symbols[owner])))
            owner = program.symbols[owner].owner;
        return owner;
    };
    const auto check_supertype = [&](SymbolId id, SymbolId super, const Span& at, bool base) {
        if (!valid(program, super)) return;
        const auto& parent = program.symbols[super];
        const auto& child = program.symbols[id];
        if (base && has_tag(parent, TagId::Final)) {
            auto error = Diagnostic(at, "class " + in_quotes(child.name) + " extends the final class " +
                                            in_quotes(parent.name))
                .with_code("E0912").label("extends a final class");
            if (const auto tag = tag_span(parent, TagId::Final))
                error.label(*tag, in_quotes(parent.name) + " is marked #Final here");
            error.help("extend another class, or hold a " + in_quotes(parent.name) + " in a field instead");
            throw error;
        }
        if (has_tag(parent, TagId::Sealed) && !within(program, id, home(super))) {
            const auto where = home(super);
            const auto place = valid(program, where) && program.symbols[where].kind == SymbolKind::Module
                ? "module " + in_quotes(program.symbols[where].name) : std::string("its file");
            auto error = Diagnostic(at, std::string(kind_word(program, id)) + " " + in_quotes(child.name) +
                                            (base ? " extends " : " implements ") + "the sealed " +
                                            std::string(kind_word(program, super)) + " " + in_quotes(parent.name) +
                                            " outside " + place)
                .with_code("E0914").label("outside the declaring module");
            if (const auto tag = tag_span(parent, TagId::Sealed))
                error.label(*tag, in_quotes(parent.name) + " is marked #Sealed here");
            error.note("a #Sealed type is extended or implemented only inside the module or file that declares it");
            throw error;
        }
    };
    const auto checks = [&] {
        for (SymbolId id = first; id < program.symbols.size(); ++id) {
            const auto& item = program.symbols[id];
            if (item.kind != SymbolKind::Class && item.kind != SymbolKind::Interface &&
                item.kind != SymbolKind::Function)
                continue;
            const auto declared = spans(id);
            if (!declared) continue;
            if (item.kind == SymbolKind::Class && valid(program, item.base))
                check_supertype(id, item.base, declared->base.value_or(declared->name), true);
            if (item.kind == SymbolKind::Class || item.kind == SymbolKind::Interface) {
                for (std::size_t i = 0; i < item.interfaces.size(); ++i) {
                    const auto at = i < declared->interfaces.size() ? declared->interfaces[i] : declared->name;
                    check_supertype(id, item.interfaces[i].symbol, at, false);
                }
            }
            if (item.kind != SymbolKind::Function) continue;
            if (valid(program, item.overrides)) {
                const auto& overridden = program.symbols[item.overrides];
                if (has_tag(overridden, TagId::Final)) {
                    auto error = Diagnostic(declared->name, "method " + in_quotes(item.name) +
                                                                " overrides a final method")
                        .with_code("E0913").label("overrides a final method");
                    if (const auto tag = tag_span(overridden, TagId::Final))
                        error.label(*tag, in_quotes(overridden.name) + " is marked #Final here");
                    error.help("give the method another name, or remove #Final from the base method");
                    throw error;
                }
                if (has_tag(overridden, TagId::Deprecated) && !inside_tagged(program, id, TagId::Deprecated)) {
                    const auto as_of = tag_text(overridden, TagId::Deprecated, "as_of").value_or("");
                    const auto reason = tag_text(overridden, TagId::Deprecated, "reason").value_or("");
                    std::string message = "method " + in_quotes(item.name) + " overrides a deprecated method";
                    if (!as_of.empty()) message += ", deprecated since " + as_of;
                    if (!reason.empty()) message += ": " + reason;
                    auto warning =
                        Diagnostic::warning(declared->name, "deprecated", std::move(message)).with_code("W0001");
                    if (const auto tag = tag_span(overridden, TagId::Deprecated))
                        warning.label(*tag, in_quotes(overridden.name) + " is marked deprecated here");
                    const auto instead = tag_text(overridden, TagId::Deprecated, "use_instead");
                    if (instead && !instead->empty())
                        warning.help("override " + in_quotes(*instead) + " instead");
                    report_scoped_warning(program, id, std::move(warning));
                }
            }
            if (item.type.kind == TypeKind::Void && has_tag(item, TagId::NoDiscard) &&
                item.overload_group == no_symbol) {
                const auto tag = tag_span(item, TagId::NoDiscard);
                throw Diagnostic(tag.value_or(declared->name), "#NoDiscard on " +
                                                                   std::string(kind_word(program, id)) + " " +
                                                                   in_quotes(item.name) + ", which returns nothing")
                    .with_code("E0916").label("there is no result to discard")
                    .label(declared->name, in_quotes(item.name) + " returns void")
                    .help("remove #NoDiscard, or return a value");
            }
        }
    };
    // A check that fails is reported together with the warnings that #Deny
    // made errors, in source order.
    std::vector<Diagnostic> errors;
    try {
        checks();
    } catch (const Diagnostic& error) {
        errors.push_back(error);
    }
    if (auto* sink = DiagnosticSink::current(); sink && sink->size() > mark) {
        sink->deduplicate(mark);
        auto denied = sink->take_errors(mark);
        errors.insert(errors.end(), denied.begin(), denied.end());
    }
    if (errors.empty()) return;
    std::ranges::stable_sort(errors, [](const Diagnostic& a, const Diagnostic& b) {
        return std::tie(a.span.file, a.span.offset) < std::tie(b.span.file, b.span.offset);
    });
    auto error = errors.front();
    for (std::size_t i = 1; i < errors.size(); ++i) error.also(errors[i]);
    throw error;
}

} // namespace neura
