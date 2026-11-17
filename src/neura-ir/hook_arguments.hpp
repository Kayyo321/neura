// Call ordering on the linked, specialized program (Calls030 and Hooks030).
#pragma once

#include "class_hooks.hpp"

namespace neura {

// Lowering records an omitted nil independently of a written nil. The other
// two markers are set only on the specialized argument evaluation/transport.
inline constexpr std::string_view default_argument_nil = "$default_nil";
inline constexpr std::string_view parameter_admission = "$parameter_admission";
inline constexpr std::string_view argument_transport = "$argument_transport";

inline bool transports_argument(const IrNode& node) {
    return (node.op == IrOp::Load || node.op == IrOp::Field || node.op == IrOp::ScopedValue) &&
           node.text == argument_transport;
}

inline bool linked_argument_annotation(const IrNode& node) {
    return transports_argument(node) || (node.op == IrOp::Return && node.text == argument_transport) ||
           (node.op == IrOp::Initialize && node.text == parameter_admission);
}

void prepare_class_hook_arguments(IrProgram& program, const HookIndex& hooks);

} // namespace neura
