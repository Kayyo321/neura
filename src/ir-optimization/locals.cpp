#include "locals.hpp"

#include <algorithm>

namespace neura::optimization {
namespace {

void scan(const IrProgram& program, const std::vector<IrNode>& nodes, std::map<SymbolId, LocalUse>& uses);

void scan_node(const IrProgram& program, const IrNode& node, std::map<SymbolId, LocalUse>& uses) {
    if (node.op == IrOp::Store && node.operands.size() == 2 && node.operands.front().op == IrOp::Load &&
        is_local(program, node.operands.front().symbol)) {
        ++uses[node.operands.front().symbol].stores;
        scan(program, node.operands.front().operands, uses);
        scan_node(program, node.operands.back(), uses);
        return;
    }
    if (is_local(program, node.symbol)) {
        auto& use = uses[node.symbol];
        if (node.op == IrOp::Load) {
            ++use.loads;
        } else if (node.op == IrOp::Initialize) {
            ++use.initializes;
            use.initializer = &node;
        } else {
            use.other = true;
        }
    }
    scan(program, node.operands, uses);
    scan(program, node.body, uses);
    scan(program, node.alternative, uses);
}

void scan(const IrProgram& program, const std::vector<IrNode>& nodes, std::map<SymbolId, LocalUse>& uses) {
    for (const auto& node : nodes) scan_node(program, node, uses);
}

void walk(std::vector<IrNode>& nodes, const std::function<void(std::vector<IrNode>&)>& visit, bool region) {
    for (auto& node : nodes) {
        const bool statements = node.op == IrOp::Block || node.op == IrOp::Branch || node.op == IrOp::Loop ||
                                node.op == IrOp::TaskScope;
        walk(node.operands, visit, false);
        walk(node.body, visit, statements);
        walk(node.alternative, visit, statements);
    }
    if (region) visit(nodes);
}

} // namespace

bool is_local(const IrProgram& program, SymbolId symbol) {
    if (symbol >= program.symbols.size()) return false;
    const auto kind = program.symbols[symbol].kind;
    return kind == SymbolKind::Local || kind == SymbolKind::Parameter;
}

std::map<SymbolId, LocalUse> scan_locals(const IrProgram& program, const std::vector<IrNode>& body) {
    std::map<SymbolId, LocalUse> uses;
    scan(program, body, uses);
    return uses;
}

bool same_expression(const IrNode& left, const IrNode& right) {
    const auto same = [](const std::vector<IrNode>& a, const std::vector<IrNode>& b) {
        return std::ranges::equal(a, b, same_expression);
    };
    return left.op == right.op && left.type == right.type && left.symbol == right.symbol && left.text == right.text &&
           left.type_arguments == right.type_arguments && same(left.operands, right.operands) &&
           same(left.body, right.body) && same(left.alternative, right.alternative);
}

std::size_t node_count(const IrNode& node) {
    std::size_t count = 1;
    for (const auto* region : {&node.operands, &node.body, &node.alternative}) {
        for (const auto& child : *region) count += node_count(child);
    }
    return count;
}

void each_statement_region(std::vector<IrNode>& body, const std::function<void(std::vector<IrNode>&)>& visit) {
    walk(body, visit, true);
}

bool function_body(const IrProgram& program, const IrBody& body) {
    return body.owner < program.symbols.size() && program.symbols[body.owner].kind == SymbolKind::Function;
}

} // namespace neura::optimization
