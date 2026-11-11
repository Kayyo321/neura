#pragma once

// Lowering marks `last` bindings with the binding.last attribute.
#include "attributes.hpp"
#include "util/diagnostic.hpp"

#include <set>
#include <span>
#include <string>

namespace neura {

struct Parameter;
struct Statement;

// A deferred `last` instance field (`last name: T;`) that a constructor must
// assign exactly once.
struct DeferredField {
    std::string name;
    Span declaration;
};

struct DefiniteAssignmentOptions {
    // An unshadowed `instance` names the constructor's receiver.
    bool instance_is_receiver = true;
    // An unshadowed `trap_with_message(...)` statement ends its path.
    bool trap_is_builtin = true;
    // The constructor whose exits must assign every field in `fields`.
    const Span* constructor = nullptr;
};

// The definite-assignment analysis for deferred `last` bindings
// (CoreLanguage.md, "Deferred assignment"). It runs on one function body
// before lowering, over the syntax, and checks the deferred `last` locals
// declared in it, outside nested lambdas, which lowering checks with their
// own bodies. Inside a constructor it also checks the class's deferred
// `last` fields. Each binding must be assigned exactly once, outside any loop
// that does not also contain its declaration, before it is read or captured
// by a closure; a constructor must assign every deferred field on each path
// that returns. Violations throw E0440 to E0443 and E0446.
//
// The result holds the assignments that perform the single assignment;
// lowering stores to the constant binding only through them.
std::set<const Statement*> check_definite_assignment(std::span<const Parameter> parameters,
                                                     std::span<const Statement> body,
                                                     std::span<const DeferredField> fields = {},
                                                     DefiniteAssignmentOptions options = {});

} // namespace neura
