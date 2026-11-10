#pragma once

#include <set>
#include <span>
#include <string>

namespace neura {

struct Declaration;
struct Statement;

struct ReferenceFieldInitialization {
    std::string name;
    bool has_initializer;
};

struct ReferenceConstructorOptions {
    bool instance_is_receiver = true;
    bool trap_is_builtin = true;
    bool initializes_base = false;
};

// Classifies assignments to the constructor's own reference fields before
// lowering assigns an expected type to the source. The result contains only
// the assignments that bind an uninitialized field rather than writing
// through its existing reference.
std::set<const Statement*> check_reference_constructor(
    const Declaration& constructor, std::span<const ReferenceFieldInitialization> fields,
    ReferenceConstructorOptions options = {});

// Instance initializers run in declaration order, before the constructor body.
void check_reference_field_initializers(const Declaration& cls,
                                        std::span<const ReferenceFieldInitialization> fields,
                                        bool instance_is_receiver);

} // namespace neura
