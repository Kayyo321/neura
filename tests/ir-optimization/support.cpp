#include "support.hpp"

#include "cli/source_loader.hpp"

#include <algorithm>

namespace neura::testing {
namespace {

std::string_view op_name(IrOp op) {
    switch (op) {
    case IrOp::Initialize: return "initialize";
    case IrOp::Store: return "store";
    case IrOp::Load: return "load";
    case IrOp::Reference: return "reference";
    case IrOp::Receiver: return "receiver";
    case IrOp::Field: return "field";
    case IrOp::Index: return "index";
    case IrOp::Collection: return "collection";
    case IrOp::Integer: return "integer";
    case IrOp::Float: return "float";
    case IrOp::String: return "string";
    case IrOp::Boolean: return "boolean";
    case IrOp::Unary: return "unary";
    case IrOp::Binary: return "binary";
    case IrOp::ShortCircuit: return "short_circuit";
    case IrOp::Call: return "call";
    case IrOp::VirtualCall: return "virtual_call";
    case IrOp::ExternalCall: return "external_call";
    case IrOp::BaseInitialize: return "base_initialize";
    case IrOp::Allocate: return "allocate";
    case IrOp::Length: return "length";
    case IrOp::Append: return "append";
    case IrOp::RemoveAt: return "remove_at";
    case IrOp::Return: return "return";
    case IrOp::Block: return "block";
    case IrOp::Branch: return "branch";
    case IrOp::Loop: return "loop";
    case IrOp::Break: return "break";
    case IrOp::Continue: return "continue";
    case IrOp::Destroy: return "destroy";
    case IrOp::Discard: return "discard";
    case IrOp::Convert: return "convert";
    case IrOp::CollectionVersion: return "collection_version";
    case IrOp::Fail: return "fail";
    case IrOp::Tuple: return "tuple";
    case IrOp::Project: return "project";
    case IrOp::EnumConstruct: return "enum_construct";
    case IrOp::EnumTag: return "enum_tag";
    case IrOp::EnumProject: return "enum_project";
    case IrOp::ComparisonChain: return "comparison_chain";
    case IrOp::ScopedValue: return "scoped_value";
    case IrOp::SelectValue: return "select_value";
    }
    return "<invalid>";
}

std::vector<fs::path> nra_files(const fs::path& directory) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".nra") {
            files.push_back(fs::canonical(entry.path()));
        }
    }
    std::ranges::sort(files);
    return files;
}

void render(std::ostream& out, const IrProgram& program, const std::vector<IrNode>& nodes);

void render(std::ostream& out, const IrProgram& program, const IrNode& node) {
    out << '(' << op_name(node.op);
    if (node.symbol < program.symbols.size()) {
        out << ' ' << program.symbols[node.symbol].name;
    }
    if (node.op == IrOp::String) {
        out << " \"" << node.text << '"';
    } else if (!node.text.empty()) {
        out << ' ' << node.text;
    }
    for (const auto& operand : node.operands) {
        out << ' ';
        render(out, program, operand);
    }
    if (!node.body.empty() || node.op == IrOp::Branch || node.op == IrOp::Loop || node.op == IrOp::Block) {
        out << " {";
        render(out, program, node.body);
        out << '}';
    }
    if (!node.alternative.empty()) {
        out << " else {";
        render(out, program, node.alternative);
        out << '}';
    }
    out << ')';
}

void render(std::ostream& out, const IrProgram& program, const std::vector<IrNode>& nodes) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (i != 0) {
            out << ' ';
        }
        render(out, program, nodes[i]);
    }
}

} // namespace

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

Registration::Registration(const char* suite, const char* name, TestFunction run) {
    registry().push_back({suite, name, run});
}

void fail(std::string_view message, std::source_location where) {
    throw Failure(std::string(fs::path(where.file_name()).filename().string()) + ":" +
                  std::to_string(where.line()) + ": " + std::string(message));
}

IrProgram lower_files(std::vector<fs::path> files, std::vector<fs::path> roots) {
    for (auto& file : files) {
        file = fs::canonical(file);
        roots.push_back(file.parent_path());
    }
    return lower(read_sources(files, roots));
}

IrProgram lower_fixture(const Environment& environment, std::string_view file_name) {
    return lower_files({environment.fixtures() / file_name});
}

std::vector<fs::path> fixture_files(const Environment& environment) {
    return nra_files(environment.fixtures());
}

std::vector<fs::path> example_files(const Environment& environment) {
    return nra_files(environment.examples());
}

std::string qualified_name(const IrProgram& program, SymbolId id) {
    std::string name;
    for (auto current = id; current < program.symbols.size(); current = program.symbols[current].owner) {
        const auto& symbol = program.symbols[current];
        if (symbol.kind == SymbolKind::File) {
            break;
        }
        name = name.empty() ? symbol.name : symbol.name + "." + name;
    }
    return name;
}

SymbolId find_symbol(const IrProgram& program, std::string_view qualified) {
    SymbolId found = no_symbol;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto kind = program.symbols[id].kind;
        if (kind == SymbolKind::Local || kind == SymbolKind::Parameter || kind == SymbolKind::TypeParameter) {
            continue;
        }
        if (qualified_name(program, id) == qualified) {
            check(found == no_symbol, "more than one symbol is named " + std::string(qualified));
            found = id;
        }
    }
    check(found != no_symbol, "no symbol is named " + std::string(qualified));
    return found;
}

const std::vector<IrNode>& body_of(const IrProgram& program, std::string_view qualified) {
    const auto id = find_symbol(program, qualified);
    const auto body = std::ranges::find(program.bodies, id, &IrBody::owner);
    check(body != program.bodies.end(), std::string(qualified) + " has no body");
    return body->instructions;
}

std::vector<IrNode>& body_of(IrProgram& program, std::string_view qualified) {
    const auto& body = body_of(static_cast<const IrProgram&>(program), qualified);
    return const_cast<std::vector<IrNode>&>(body);
}

std::string shape(const IrProgram& program, const std::vector<IrNode>& nodes) {
    std::ostringstream out;
    render(out, program, nodes);
    return out.str();
}

std::string shape(const IrProgram& program, const IrNode& node) {
    std::ostringstream out;
    render(out, program, node);
    return out.str();
}

std::string printed(const IrProgram& program) {
    std::ostringstream out;
    print_ir(out, program);
    return out.str();
}

bool same_symbols(const IrProgram& left, const IrProgram& right) {
    return std::ranges::equal(left.symbols, right.symbols, [](const IrSymbol& a, const IrSymbol& b) {
        return a.kind == b.kind && a.name == b.name && same_span(a.span, b.span) && a.owner == b.owner &&
               a.base == b.base && a.overrides == b.overrides && a.type == b.type &&
               a.parameters == b.parameters && a.generics == b.generics && a.bound == b.bound &&
               a.exported == b.exported && a.constant == b.constant && a.external == b.external &&
               a.has_body == b.has_body;
    });
}

bool same_span(const Span& left, const Span& right) {
    return left.file == right.file && left.line == right.line && left.column == right.column &&
           left.offset == right.offset && left.length == right.length;
}

std::string describe(const Span& span) {
    return fs::path(span.file).filename().string() + ":" + std::to_string(span.line) + ":" +
           std::to_string(span.column);
}

} // namespace neura::testing
