#pragma once

#include "ir.hpp"

#include <optional>
#include <string>

namespace neura {

// docs/design/Reflect030.md. The trusted file std/reflect/core/intrinsics.nra
// declares the bodyless functions of Std.Reflect.Core.Intrinsics. Before
// specialization, prepare_reflection turns every call of one of them into a
// Reflect node (text: the function's name, operands: the arguments, type
// arguments: the call's), so that specialization substitutes its types like
// any other node. It returns whether the program reflects at all.
bool prepare_reflection(IrProgram& program);

// After specialization: computes the reflection universe of the program,
// synthesizes the box classes, the descriptor table and the dynamic access
// functions, and replaces every Reflect node with existing operations. A
// program without Reflect nodes is left exactly as it is.
void expand_reflection(IrProgram& program);

// Why a value of `type` cannot be held by a Std.Reflect dynamic value (E1301):
// a primitive reference, a scope-bound value or an array, which lives in its
// region; nothing when it can. Specialization asks at each Reflect node so
// that the error names the instantiation that reached it.
std::optional<std::string> reflection_hold_problem(const IrProgram& program, const IrType& type);

// Why `type` cannot be the interface of a Std.Reflect.Proxy.DynamicProxy
// (E1300): not an interface, or one with a property, a static requirement, or
// a generic or variadic method; nothing when it can.
std::optional<std::string> reflection_proxy_problem(const IrProgram& program, const IrType& type);

} // namespace neura
