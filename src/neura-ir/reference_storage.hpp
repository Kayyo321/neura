#pragma once

#include "ir.hpp"

namespace neura {

bool is_primitive_reference(const IrType& type);

// Materialize checked addresses as reference-box operations. Local and
// parameter boxes carry a region placement marker, and globals use static
// storage. The whole linked program shares each primitive's box layout.
void materialize_reference_storage(IrProgram& program);

enum class ReferencePreparation { None, Parameter, Field, GlobalBox, GlobalValue };

// A boxed parameter is prepared before its function body. Addressable
// fields get their boxes before construction, and globals before startup.
ReferencePreparation reference_preparation(const IrNode& node);

} // namespace neura
