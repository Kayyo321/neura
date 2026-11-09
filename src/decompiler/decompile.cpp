#include "decompile.hpp"

#include "nvr/names.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace neura::decompiler {
namespace {

using nvr::absent;

std::string type_name(const nvr::Image& image, std::uint32_t index) {
    if (index >= image.types.size()) {
        return "t" + std::to_string(index);
    }
    const auto& t = image.types[index];
    switch (t.kind) {
    case nvr::TypeKind::Void: return "void";
    case nvr::TypeKind::Bool: return "bool";
    case nvr::TypeKind::Unsigned: return "uint" + std::to_string(t.width);
    case nvr::TypeKind::Signed: return "int" + std::to_string(t.width);
    case nvr::TypeKind::Text: return "text";
    case nvr::TypeKind::Bytes: return "bytes";
    case nvr::TypeKind::Object: return "L" + std::to_string(t.reference);
    case nvr::TypeKind::Array: return type_name(image, t.reference) + "[]";
    case nvr::TypeKind::List: return type_name(image, t.reference) + "[list]";
    case nvr::TypeKind::Float: return "float" + std::to_string(t.width);
    case nvr::TypeKind::BFloat: return "bfloat16";
    }
    return "t" + std::to_string(index);
}

std::string quoted(const std::string& text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += c;
        } else if (c == '\n') {
            result += "\\n";
        } else if (c == '\t') {
            result += "\\t";
        } else if (c == '\r') {
            result += "\\r";
        } else {
            result += c;
        }
    }
    return result + "\"";
}

std::string constant_text(const nvr::Image& image, std::uint32_t index) {
    const auto& constant = image.constants.at(index);
    const auto& t = image.types.at(constant.type);
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < constant.payload.size() && i < 8; ++i) {
        bits |= std::uint64_t{constant.payload[i]} << (8 * i);
    }
    if (t.kind == nvr::TypeKind::Text) {
        return quoted(std::string(constant.payload.begin(), constant.payload.end()));
    }
    Expression e;
    e.kind = Expression::Kind::Immediate;
    e.type = constant.type;
    e.bits = bits;
    return render(image, e, {});
}

const char* reason_name(std::uint32_t reason) {
    switch (reason) {
    case 1: return "arithmetic failure";
    case 2: return "bounds failure";
    case 3: return "program failure";
    case 4: return "invalid dispatch";
    case 5: return "collection modified during iteration";
    default: return "undefined reason";
    }
}

class Printer {
public:
    Printer(const nvr::Image& image, const std::optional<nvr::Names>& names, bool provenance)
        : image_(image), names_(names) {
        if (provenance && image.source_map) {
            for (const auto& location : *image.source_map) {
                locations_[{location.function, location.block}] = location.path + ":" +
                                                                    std::to_string(location.line) + ":" +
                                                                    std::to_string(location.column);
            }
        }
    }

    std::string slot(std::uint32_t, std::uint32_t slot) const {
        return "s" + std::to_string(slot);
    }

    std::string slot_comment(std::uint32_t function, std::uint32_t slot) const {
        if (names_ && function < names_->slots.size() && slot < names_->slots[function].size() &&
            !names_->slots[function][slot].empty()) {
            return names_->slots[function][slot];
        }
        return {};
    }

    std::string function_name(std::uint32_t function) const {
        return "f" + std::to_string(function);
    }

    std::string value(std::uint32_t function, const nvr::Value& value, const LiftedBlock& lifted,
                      const std::vector<std::string>& inputs) const {
        switch (value.kind) {
        case nvr::ValueKind::Slot: return slot(function, value.index);
        case nvr::ValueKind::Constant: return constant_text(image_, value.index);
        case nvr::ValueKind::Lanes:
            if (const auto found = lifted.values.find(value.index); found != lifted.values.end()) {
                return render(image_, *found->second, inputs);
            }
            return "lanes[" + std::to_string(value.index) + ".." +
                   std::to_string(value.index + nvr::lane_count(image_.types.at(value.type)) - 1) + "]:" +
                   type_name(image_, value.type);
        }
        return "?";
    }

    std::string values(std::uint32_t function, const std::vector<nvr::Value>& items, const LiftedBlock& lifted,
                       const std::vector<std::string>& inputs) const {
        std::string result;
        for (std::size_t i = 0; i < items.size(); ++i) {
            result += (i == 0 ? "" : ", ") + value(function, items[i], lifted, inputs);
        }
        return result;
    }

    std::string exit(std::uint32_t function, const nvr::Exit& exit, const LiftedBlock& lifted,
                     const std::vector<std::string>& inputs) const {
        std::string assignments;
        for (const auto& assignment : exit.assignments) {
            assignments += (assignments.empty() ? "" : ", ") + slot(function, assignment.slot) + " := " +
                           value(function, assignment.value, lifted, inputs);
        }
        const auto with = [&](std::string text) {
            return assignments.empty() ? text : "{ " + assignments + " } " + text;
        };
        const auto result = [&](std::uint32_t slot_index) {
            return slot_index == absent ? std::string() : slot(function, slot_index) + " := ";
        };
        const auto& args = exit.arguments;
        switch (exit.kind) {
        case nvr::ExitKind::Continue:
            return with("goto b" + std::to_string(exit.resume));
        case nvr::ExitKind::Call:
            return with(result(exit.result_slot) + "call " + function_name(exit.target) + "(" +
                        values(function, args, lifted, inputs) + "); goto b" + std::to_string(exit.resume));
        case nvr::ExitKind::Return:
            return with(args.empty() ? "return" : "return " + value(function, args[0], lifted, inputs));
        case nvr::ExitKind::Trap:
            return with("trap " + std::to_string(exit.reason) + " (" + reason_name(exit.reason) + ")");
        case nvr::ExitKind::Host: {
            const auto& import = image_.imports.at(exit.target);
            return with(result(exit.result_slot) + "host " + quoted(import.name) + " v" +
                        std::to_string(import.interface_version) + "(" + values(function, args, lifted, inputs) +
                        "); goto b" + std::to_string(exit.resume) + " | on error " +
                        (exit.error_slot == absent ? std::string() : slot(function, exit.error_slot) + " := code; ") +
                        "goto b" + std::to_string(exit.error_resume));
        }
        case nvr::ExitKind::State: {
            const auto a = [&](std::size_t i) { return value(function, args.at(i), lifted, inputs); };
            std::string operation;
            switch (exit.operation) {
            case nvr::StateOp::AllocateObject: operation = "new L" + std::to_string(exit.immediate); break;
            case nvr::StateOp::ReadTag: operation = "tag(" + a(0) + ")"; break;
            case nvr::StateOp::ReadField: operation = a(0) + "." + field(exit, args) ; break;
            case nvr::StateOp::WriteField: operation = a(0) + "." + field(exit, args) + " = " + a(1); break;
            case nvr::StateOp::FreeObject: operation = "free " + a(0); break;
            case nvr::StateOp::ReadGlobal: operation = global(exit.immediate); break;
            case nvr::StateOp::WriteGlobal: operation = global(exit.immediate) + " = " + a(0); break;
            case nvr::StateOp::AllocateCollection:
                operation = type_name(image_, exit.immediate) + " [" + values(function, args, lifted, inputs) + "]";
                break;
            case nvr::StateOp::ReadLength: operation = "length(" + a(0) + ")"; break;
            case nvr::StateOp::ReadElement: operation = a(0) + "[" + a(1) + "]"; break;
            case nvr::StateOp::WriteElement: operation = a(0) + "[" + a(1) + "] = " + a(2); break;
            case nvr::StateOp::AppendElement: operation = "append(" + a(0) + ", " + a(1) + ")"; break;
            case nvr::StateOp::RemoveElement: operation = "remove_at(" + a(0) + ", " + a(1) + ")"; break;
            case nvr::StateOp::ConstructText:
                operation = type_name(image_, exit.immediate) + "(" + a(0) + ")";
                break;
            case nvr::StateOp::ReadVersion: operation = "version(" + a(0) + ")"; break;
            }
            return with(result(exit.result_slot) + operation + "; goto b" + std::to_string(exit.resume));
        }
        }
        return "?";
    }

    std::string field(const nvr::Exit& exit, const std::vector<nvr::Value>& args) const {
        const auto& type = image_.types.at(args.at(0).type);
        const auto layout = type.kind == nvr::TypeKind::Object ? type.reference : absent;
        std::string text = "field" + std::to_string(exit.immediate);
        if (names_ && layout < names_->fields.size() && exit.immediate < names_->fields[layout].size() &&
            !names_->fields[layout][exit.immediate].empty()) {
            text += " /* " + names_->fields[layout][exit.immediate] + " */";
        }
        return text;
    }

    std::string global(std::uint32_t index) const {
        std::string text = "g" + std::to_string(index);
        if (names_ && index < names_->globals.size() && !names_->globals[index].empty()) {
            text += " /* " + names_->globals[index] + " */";
        }
        return text;
    }

    std::string location(std::uint32_t function, std::uint32_t block) const {
        const auto found = locations_.find({function, block});
        return found == locations_.end() ? std::string() : found->second;
    }

private:
    const nvr::Image& image_;
    const std::optional<nvr::Names>& names_;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::string> locations_;
};

std::vector<std::uint32_t> successors(const nvr::Block& block) {
    std::vector<std::uint32_t> result;
    for (const auto& exit : block.exits) {
        switch (exit.kind) {
        case nvr::ExitKind::Continue:
        case nvr::ExitKind::Call:
        case nvr::ExitKind::State: result.push_back(exit.resume); break;
        case nvr::ExitKind::Host:
            result.push_back(exit.resume);
            result.push_back(exit.error_resume);
            break;
        default: break;
        }
    }
    return result;
}

// Loop headers: targets of edges from blocks they dominate.
std::map<std::uint32_t, std::vector<std::uint32_t>> loops(const nvr::Function& function) {
    const auto n = function.blocks.size();
    std::vector<std::set<std::uint32_t>> dominators(n);
    std::set<std::uint32_t> all;
    for (std::uint32_t b = 0; b < n; ++b) {
        all.insert(b);
    }
    for (std::uint32_t b = 0; b < n; ++b) {
        dominators[b] = b == 0 ? std::set<std::uint32_t>{0} : all;
    }
    std::vector<std::vector<std::uint32_t>> predecessors(n);
    for (std::uint32_t b = 0; b < n; ++b) {
        for (const auto next : successors(function.blocks[b])) {
            if (next < n) {
                predecessors[next].push_back(b);
            }
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (std::uint32_t b = 1; b < n; ++b) {
            std::set<std::uint32_t> meet;
            bool first = true;
            for (const auto p : predecessors[b]) {
                if (first) {
                    meet = dominators[p];
                    first = false;
                } else {
                    std::set<std::uint32_t> kept;
                    std::set_intersection(meet.begin(), meet.end(), dominators[p].begin(), dominators[p].end(),
                                          std::inserter(kept, kept.begin()));
                    meet = std::move(kept);
                }
            }
            meet.insert(b);
            if (meet != dominators[b]) {
                dominators[b] = std::move(meet);
                changed = true;
            }
        }
    }
    std::map<std::uint32_t, std::vector<std::uint32_t>> result;
    for (std::uint32_t b = 0; b < n; ++b) {
        for (const auto next : successors(function.blocks[b])) {
            if (next < n && dominators[b].contains(next)) {
                result[next].push_back(b);
            }
        }
    }
    return result;
}

const char* evidence_label(const LiftedBlock& lifted) {
    switch (lifted.evidence) {
    case Evidence::None: return "decoded";
    case Evidence::Structural: return "lifted: structural";
    case Evidence::Exhaustive: return "lifted: exhaustive";
    case Evidence::Circuit: return "circuit";
    }
    return "?";
}

} // namespace

Decompilation decompile(const nvr::Image& image, const DecompileOptions& options) {
    Decompilation result;
    const auto names = options.provenance ? nvr::decode_names(image) : std::nullopt;
    const Printer printer(image, names, options.provenance);
    std::ostringstream out;
    for (std::uint32_t f = 0; f < image.functions.size(); ++f) {
        std::vector<LiftedBlock> lifted;
        for (std::uint32_t b = 0; b < image.functions[f].blocks.size(); ++b) {
            lifted.push_back(lift_block(image, f, b, options.lift));
            auto& coverage = result.coverage;
            ++coverage.blocks;
            switch (lifted.back().evidence) {
            case Evidence::None: ++coverage.decoded; break;
            case Evidence::Structural: ++coverage.structural; break;
            case Evidence::Exhaustive: ++coverage.exhaustive; break;
            case Evidence::Circuit: ++coverage.circuit; break;
            }
        }
        result.blocks.push_back(std::move(lifted));
    }

    out << "// Neura decompilation: the typed reconstruction form (docs/Decompiler.md).\n"
        << "// Recovery levels: [decoded] read from the image tables; [lifted: structural] the block's\n"
        << "// graph is identical to the graph regenerated from the shown operations; [lifted: exhaustive]\n"
        << "// the shown operations equal the graph on every input; [circuit] the graph is shown as lanes;\n"
        << "// [reconstructed] control structure recovered from the block graph. Comments marked\n"
        << "// 'provenance' come from optional metadata and do not describe behavior.\n";
    out << "image NVR 1." << std::max(image.minor_version, nvr::required_minor_version(image))
        << ", graph profile 1, entry " << printer.function_name(image.entry) << "\n";
    const auto& c = result.coverage;
    out << "// blocks: " << c.blocks << " (" << c.decoded << " decoded, " << c.structural << " lifted structurally, "
        << c.exhaustive << " lifted exhaustively, " << c.circuit << " circuits)\n\n";

    for (std::uint32_t i = 0; i < image.imports.size(); ++i) {
        const auto& import = image.imports[i];
        out << "import i" << i << " " << quoted(import.name) << " v" << import.interface_version << " (";
        for (std::size_t p = 0; p < import.parameter_types.size(); ++p) {
            out << (p == 0 ? "" : ", ") << type_name(image, import.parameter_types[p]);
        }
        out << ") -> " << type_name(image, import.success_type) << " | error " << type_name(image, import.error_type)
            << "\n";
    }
    for (std::uint32_t i = 0; i < image.layouts.size(); ++i) {
        const auto& layout = image.layouts[i];
        out << "layout L" << i;
        if (layout.parent != absent) {
            out << " : L" << layout.parent;
        }
        out << " {";
        for (std::size_t j = 0; j < layout.fields.size(); ++j) {
            out << (j == 0 ? " " : ", ") << "field" << j << ": " << type_name(image, layout.fields[j]);
        }
        out << " }";
        if (names && i < names->layouts.size() && !names->layouts[i].empty()) {
            out << "  // provenance: " << names->layouts[i];
        }
        out << "\n";
    }
    for (std::uint32_t i = 0; i < image.globals.size(); ++i) {
        const auto& global = image.globals[i];
        out << "global " << printer.global(i) << ": " << type_name(image, global.type) << " = "
            << (global.initializer == absent ? std::string("<uninitialized until start>")
                                             : constant_text(image, global.initializer))
            << "\n";
    }

    for (std::uint32_t f = 0; f < image.functions.size(); ++f) {
        const auto& function = image.functions[f];
        out << "\nfunction " << printer.function_name(f) << "(";
        for (std::uint32_t p = 0; p < function.parameter_count; ++p) {
            out << (p == 0 ? "" : ", ") << printer.slot(f, p) << ": " << type_name(image, function.frame_types[p]);
        }
        out << ") -> " << type_name(image, function.result_type);
        if (names && f < names->functions.size() && !names->functions[f].empty()) {
            out << "  // provenance: " << names->functions[f];
        }
        out << "\n  frame:";
        for (std::uint32_t s = 0; s < function.frame_types.size(); ++s) {
            out << " " << printer.slot(f, s) << ":" << type_name(image, function.frame_types[s]);
            if (const auto name = printer.slot_comment(f, s); !name.empty()) {
                out << "(" << name << ")";
            }
        }
        out << "\n";
        const auto headers = loops(function);
        for (std::uint32_t b = 0; b < function.blocks.size(); ++b) {
            const auto& block = function.blocks[b];
            const auto& lifted = result.blocks[f][b];
            out << "  b" << b << ": [" << evidence_label(lifted) << "]";
            if (const auto found = headers.find(b); found != headers.end()) {
                ++result.coverage.loops;
                out << " [reconstructed: loop header; back edges from";
                for (const auto from : found->second) {
                    out << " b" << from;
                }
                out << "]";
            }
            if (options.provenance) {
                if (const auto where = printer.location(f, b); !where.empty()) {
                    out << "  // provenance: " << where;
                }
            }
            out << "\n";
            std::vector<std::string> inputs;
            for (const auto& value : block.inputs) {
                inputs.push_back(value.kind == nvr::ValueKind::Slot ? printer.slot(f, value.index)
                                                                    : constant_text(image, value.index));
            }
            if (lifted.evidence == Evidence::Circuit) {
                const auto& graph = image.graphs.at(block.graph);
                out << "    graph g" << block.graph << " (" << graph.nodes.size() << " neurons) reads ";
                for (std::size_t i = 0; i < inputs.size(); ++i) {
                    out << (i == 0 ? "" : ", ") << inputs[i];
                }
                if (block.exits.size() > 1) {
                    out << "; selector lanes from " << block.selector_start;
                }
                out << "\n";
            }
            for (const auto& [reason, expression] : lifted.checks) {
                out << "    check " << render(image, *expression, inputs) << " -> trap " << reason << " ("
                    << reason_name(reason) << ")\n";
            }
            if (!lifted.dispatch.empty()) {
                for (const auto& [exit, layouts] : lifted.dispatch) {
                    out << "    when tag in {";
                    for (std::size_t i = 0; i < layouts.size(); ++i) {
                        out << (i == 0 ? "" : ", ") << "L" << layouts[i];
                    }
                    out << "}: " << printer.exit(f, block.exits[exit], lifted, inputs) << "\n";
                }
                out << "    otherwise: " << printer.exit(f, block.exits.back(), lifted, inputs) << "\n";
                continue;
            }
            if (lifted.condition && lifted.exit_when_true) {
                std::vector<const nvr::Exit*> normal;
                for (const auto& exit : block.exits) {
                    if (exit.kind != nvr::ExitKind::Trap) {
                        normal.push_back(&exit);
                    }
                }
                const auto taken = *lifted.exit_when_true;
                out << "    if " << render(image, *lifted.condition, inputs) << " then "
                    << printer.exit(f, *normal[taken], lifted, inputs) << "\n    else "
                    << printer.exit(f, *normal[1 - taken], lifted, inputs) << "\n";
                continue;
            }
            for (std::size_t e = 0; e < block.exits.size(); ++e) {
                const auto& exit = block.exits[e];
                if (block.exits.size() > 1) {
                    out << "    exit " << e << ": ";
                } else {
                    out << "    ";
                }
                out << printer.exit(f, exit, lifted, inputs) << "\n";
            }
        }
    }
    result.text = out.str();
    return result;
}

nvr::Image resynthesize(const nvr::Image& image, const Decompilation& decompilation) {
    auto result = image;
    for (std::uint32_t f = 0; f < image.functions.size(); ++f) {
        for (std::uint32_t b = 0; b < image.functions[f].blocks.size(); ++b) {
            const auto& lifted = decompilation.blocks.at(f).at(b);
            nvr::Graph graph;
            const auto block = regenerate(image, f, b, lifted, graph);
            if (!block) {
                continue;
            }
            auto regenerated = *block;
            regenerated.graph = static_cast<std::uint32_t>(result.graphs.size());
            result.graphs.push_back(std::move(graph));
            result.functions[f].blocks[b] = std::move(regenerated);
        }
    }
    return result;
}

} // namespace neura::decompiler
