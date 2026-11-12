#include "hints.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace neura::lift {
namespace {

// Operators by code. The order is part of the section's format.
constexpr std::array<std::string_view, 25> operators{
    "!", "~", "index", "neg", "+", "+~", "-", "*", "/", "%", "==", "!=",
    "<", "<=", ">", ">=", "&&", "||", "&", "|", "^", "<<", ">>", "-~", "hash",
};

enum NodeKind : std::uint8_t { InputNode = 0, ImmediateNode = 1, UnaryNode = 2, BinaryNode = 3, ConvertNode = 4 };

constexpr std::uint8_t flag_shared = 1;
constexpr std::uint8_t flag_condition = 2;

// Bounds for a decoded form. They keep a hostile section from asking for
// much memory or deep recursion; the converter's forms are far smaller.
constexpr std::uint32_t max_nodes = 1U << 16;
constexpr std::uint32_t max_dispatch_layouts = 1U << 16;
constexpr std::uint32_t max_depth = 256;

class Output {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u32(std::uint32_t value) {
        for (int i = 0; i < 4; ++i) bytes_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    void u64(std::uint64_t value) {
        for (int i = 0; i < 8; ++i) bytes_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

struct Malformed {};

class Input {
public:
    explicit Input(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint8_t u8() {
        need(1);
        return bytes_[at_++];
    }
    std::uint32_t u32() {
        need(4);
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value |= std::uint32_t{bytes_[at_++]} << (8 * i);
        return value;
    }
    std::uint64_t u64() {
        need(8);
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) value |= std::uint64_t{bytes_[at_++]} << (8 * i);
        return value;
    }
    // A count of records of at least `record` bytes each: never more than
    // the bytes left can hold.
    std::uint32_t count(std::size_t record, std::uint32_t limit) {
        const auto value = u32();
        if (value > limit || value > (bytes_.size() - at_) / std::max<std::size_t>(record, 1)) throw Malformed{};
        return value;
    }
    bool done() const { return at_ == bytes_.size(); }

private:
    void need(std::size_t n) const {
        if (bytes_.size() - at_ < n) throw Malformed{};
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t at_ = 0;
};

std::uint8_t operator_code(const std::string& op) {
    const auto it = std::find(operators.begin(), operators.end(), op);
    if (it == operators.end()) throw std::invalid_argument("a lifted form uses the unknown operator '" + op + "'");
    return static_cast<std::uint8_t>(it - operators.begin());
}

// Numbers the nodes of a form so that each operand precedes its users.
class Numbering {
public:
    std::uint32_t add(const Expression* e) {
        if (const auto it = ids_.find(e); it != ids_.end()) return it->second;
        // Iterative post-order: forms can be deep.
        std::vector<std::pair<const Expression*, std::size_t>> stack{{e, 0}};
        while (!stack.empty()) {
            auto& [node, next] = stack.back();
            if (next < node->operands.size()) {
                const auto* operand = node->operands[next++].get();
                if (!ids_.contains(operand)) stack.emplace_back(operand, 0);
                continue;
            }
            if (!ids_.contains(node)) {
                ids_.emplace(node, static_cast<std::uint32_t>(nodes_.size()));
                nodes_.push_back(node);
            }
            stack.pop_back();
        }
        return ids_.at(e);
    }

    const std::vector<const Expression*>& nodes() const { return nodes_; }
    std::uint32_t id(const Expression* e) const { return ids_.at(e); }

private:
    std::map<const Expression*, std::uint32_t> ids_;
    std::vector<const Expression*> nodes_;
};

std::vector<std::uint8_t> encode_form(const LiftedForm& form) {
    Numbering numbering;
    for (const auto& e : form.schedule) numbering.add(e.get());
    const auto values = form.computation_order();
    for (const auto& [lane, e] : values) numbering.add(e.get());
    if (form.condition) numbering.add(form.condition.get());

    Output out;
    out.u32(static_cast<std::uint32_t>(numbering.nodes().size()));
    for (const auto* node : numbering.nodes()) {
        switch (node->kind) {
        case Expression::Kind::Input:
            out.u8(InputNode);
            out.u32(node->type);
            out.u32(static_cast<std::uint32_t>(node->input));
            break;
        case Expression::Kind::Immediate:
            out.u8(ImmediateNode);
            out.u32(node->type);
            out.u64(node->bits);
            break;
        case Expression::Kind::Unary:
            out.u8(UnaryNode);
            out.u32(node->type);
            out.u8(operator_code(node->op));
            out.u32(numbering.id(node->operands.at(0).get()));
            break;
        case Expression::Kind::Convert:
            out.u8(ConvertNode);
            out.u32(node->type);
            out.u32(numbering.id(node->operands.at(0).get()));
            break;
        case Expression::Kind::Binary:
            out.u8(BinaryNode);
            out.u32(node->type);
            out.u8(operator_code(node->op));
            out.u32(numbering.id(node->operands.at(0).get()));
            out.u32(numbering.id(node->operands.at(1).get()));
            break;
        }
    }
    out.u8(static_cast<std::uint8_t>((form.shared ? flag_shared : 0) | (form.condition ? flag_condition : 0)));
    out.u32(static_cast<std::uint32_t>(form.schedule.size()));
    for (const auto& e : form.schedule) out.u32(numbering.id(e.get()));
    out.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto& [lane, e] : values) {
        out.u32(lane);
        out.u32(numbering.id(e.get()));
    }
    if (form.condition) {
        out.u32(numbering.id(form.condition.get()));
        out.u32(static_cast<std::uint32_t>(form.exit_when_true.value_or(0)));
    }
    out.u32(static_cast<std::uint32_t>(form.dispatch.size()));
    for (const auto& [exit, layouts] : form.dispatch) {
        out.u32(static_cast<std::uint32_t>(exit));
        out.u32(static_cast<std::uint32_t>(layouts.size()));
        for (const auto layout : layouts) out.u32(layout);
    }
    return out.take();
}

std::shared_ptr<const LiftedForm> decode_form(Input& in) {
    auto form = std::make_shared<LiftedForm>();
    const auto node_count = in.count(9, max_nodes);
    std::vector<ExpressionPtr> nodes;
    std::vector<std::uint32_t> depths;
    nodes.reserve(node_count);
    std::uint32_t depth = 0;
    const auto operand = [&](Input& r) {
        const auto index = r.u32();
        // Operands precede their users, so a form is acyclic. Nesting is
        // bounded as the type check bounds it, which also bounds the
        // recursion that releases a form.
        if (index >= nodes.size() || depths[index] >= max_depth) throw Malformed{};
        depth = std::max(depth, depths[index] + 1);
        return nodes[index];
    };
    const auto op = [&](Input& r) {
        const auto code = r.u8();
        if (code >= operators.size()) throw Malformed{};
        return std::string(operators[code]);
    };
    for (std::uint32_t i = 0; i < node_count; ++i) {
        depth = 0;
        const auto kind = in.u8();
        const auto type = in.u32();
        switch (kind) {
        case InputNode: nodes.push_back(input(type, in.u32())); break;
        case ImmediateNode: nodes.push_back(immediate(type, in.u64())); break;
        case UnaryNode: {
            auto name = op(in);
            nodes.push_back(unary(std::move(name), type, operand(in)));
            break;
        }
        case ConvertNode: nodes.push_back(convert(type, operand(in))); break;
        case BinaryNode: {
            auto name = op(in);
            auto left = operand(in);
            nodes.push_back(binary(std::move(name), type, std::move(left), operand(in)));
            break;
        }
        default: throw Malformed{};
        }
        depths.push_back(depth);
    }
    const auto flags = in.u8();
    if ((flags & ~(flag_shared | flag_condition)) != 0) throw Malformed{};
    form->shared = (flags & flag_shared) != 0;
    const auto schedule = in.count(4, max_nodes);
    for (std::uint32_t i = 0; i < schedule; ++i) form->schedule.push_back(operand(in));
    const auto values = in.count(8, max_nodes);
    for (std::uint32_t i = 0; i < values; ++i) {
        const auto lane = in.u32();
        auto value = operand(in);
        if (!form->values.emplace(lane, value).second) throw Malformed{};
        form->order.emplace_back(lane, std::move(value));
    }
    if ((flags & flag_condition) != 0) {
        form->condition = operand(in);
        form->exit_when_true = in.u32();
    }
    const auto dispatch = in.count(8, max_dispatch_layouts);
    for (std::uint32_t i = 0; i < dispatch; ++i) {
        const auto exit = in.u32();
        const auto layouts = in.count(4, max_dispatch_layouts);
        std::vector<std::uint32_t> list;
        for (std::uint32_t j = 0; j < layouts; ++j) list.push_back(in.u32());
        if (!form->dispatch.emplace(exit, std::move(list)).second) throw Malformed{};
    }
    return form;
}

} // namespace

nvr::RawSection encode_hints(std::vector<BlockHint> hints) {
    std::sort(hints.begin(), hints.end(), [](const BlockHint& a, const BlockHint& b) {
        return std::pair(a.function, a.block) < std::pair(b.function, b.block);
    });
    std::vector<std::vector<std::uint8_t>> forms;
    // Keyed by the form's bytes as a string: comparing byte vectors here
    // draws a false -Wstringop-overread from GCC 14's library.
    std::map<std::string, std::uint32_t> form_index;
    std::vector<std::uint32_t> entry_forms;
    for (std::size_t i = 0; i < hints.size(); ++i) {
        if (i > 0 && hints[i - 1].function == hints[i].function && hints[i - 1].block == hints[i].block) {
            throw std::invalid_argument("two lift hints for one block");
        }
        auto bytes = encode_form(hints[i].form);
        const auto [it, inserted] = form_index.emplace(std::string(bytes.begin(), bytes.end()),
                                                       static_cast<std::uint32_t>(forms.size()));
        if (inserted) forms.push_back(std::move(bytes));
        entry_forms.push_back(it->second);
    }
    Output out;
    out.u32(hint_version);
    out.u32(static_cast<std::uint32_t>(forms.size()));
    auto section = out.take();
    for (const auto& form : forms) section.insert(section.end(), form.begin(), form.end());
    Output entries;
    entries.u32(static_cast<std::uint32_t>(hints.size()));
    for (std::size_t i = 0; i < hints.size(); ++i) {
        entries.u32(hints[i].function);
        entries.u32(hints[i].block);
        entries.u32(entry_forms[i]);
    }
    const auto tail = entries.take();
    section.insert(section.end(), tail.begin(), tail.end());
    nvr::RawSection raw;
    raw.tag = hint_tag;
    raw.data = std::move(section);
    return raw;
}

HintTable decode_hints(std::span<const std::uint8_t> section) {
    HintTable table;
    try {
        Input in(section);
        if (in.u32() != hint_version) return {};
        const auto form_count = in.count(14, max_nodes * 16);
        std::vector<std::shared_ptr<const LiftedForm>> forms;
        forms.reserve(form_count);
        for (std::uint32_t i = 0; i < form_count; ++i) forms.push_back(decode_form(in));
        const auto entries = in.count(12, 0xffffffffU);
        std::pair<std::uint32_t, std::uint32_t> previous{0, 0};
        for (std::uint32_t i = 0; i < entries; ++i) {
            const std::pair key{in.u32(), in.u32()};
            const auto form = in.u32();
            if (form >= forms.size() || (i > 0 && !(previous < key))) throw Malformed{};
            previous = key;
            table.emplace(key, forms[form]);
        }
        if (!in.done()) throw Malformed{};
    } catch (const Malformed&) {
        return {};
    }
    return table;
}

HintTable decode_hints(const nvr::Image& image) {
    for (const auto& section : image.other_sections) {
        if (section.tag != hint_tag) continue;
        auto table = decode_hints(section.data);
        std::erase_if(table, [&](const auto& entry) {
            const auto [function, block] = entry.first;
            return function >= image.functions.size() || block >= image.functions[function].blocks.size();
        });
        return table;
    }
    return {};
}

bool strip_hints(nvr::Image& image) {
    return std::erase_if(image.other_sections, [](const nvr::RawSection& s) { return s.tag == hint_tag; }) != 0;
}

} // namespace neura::lift
