#include "lifetime.hpp"

namespace neura {

bool uses_lifetime_operations(const std::vector<IrNode>& nodes) {
    for (const auto& node : nodes) {
        if (lifetime_operation(node.op)) return true;
        for (const auto* region : {&node.operands, &node.body, &node.alternative})
            if (uses_lifetime_operations(*region)) return true;
    }
    return false;
}

std::size_t count_yields(const std::vector<IrNode>& nodes) {
    std::size_t count = 0;
    for (const auto& node : nodes) {
        if (node.op == IrOp::Yield) ++count;
        for (const auto* region : {&node.operands, &node.body, &node.alternative}) count += count_yields(*region);
    }
    return count;
}

} // namespace neura
