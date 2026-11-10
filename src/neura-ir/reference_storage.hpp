#pragma once

#include "ir.hpp"

namespace neura {

bool is_primitive_reference(const IrType& type);

// Replace provisional address expressions with ordinary object operations.
// The whole linked program must be present so each referent uses one layout.
void materialize_reference_storage(IrProgram& program);

enum class ReferencePreparation { None, Parameter, Field, GlobalBox, GlobalValue };

// A boxed parameter is prepared before its function body. Addressable
// fields get their boxes before construction, and globals before startup.
ReferencePreparation reference_preparation(const IrNode& node);

} // namespace neura
