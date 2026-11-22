#pragma once

#include "neura-ir/ir.hpp"

#include <unordered_set>

namespace neura::optimization {

// Non-nil handle proofs for the final executable IR. They are local to
// these exact node instances and expire when the program is mutated.
class NilProof {
public:
    explicit NilProof(const IrProgram& program);

    bool proves_non_nil(const IrNode& node) const { return proved_.contains(&node); }

private:
    std::unordered_set<const IrNode*> proved_;
};

} // namespace neura::optimization
