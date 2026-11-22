#pragma once

#include "ir.hpp"

#include <functional>
#include <memory>

namespace neura {

class Diagnostic;

// Rechecks proof-only records against this compilation's executable NeuraIR.
// A cached verdict is local to this session and concrete module receiver;
// stored verdicts and unfolding footprints never stand in for verification.
class LawCheckSession {
public:
    using InheritedFailure = std::function<void(const IrLawRecord&, SymbolId, const std::vector<SymbolId>&,
                                                const Diagnostic&)>;
    explicit LawCheckSession(const IrProgram& program, std::size_t fuel = default_proof_fuel,
                             InheritedFailure inherited_failure = {});
    ~LawCheckSession();

    // Check a law (also validates the type of an abstract statement) and
    // return the concrete definitions unfolded directly or through lemmas.
    // An inherited record is checked with the supplied module receiver.
    std::vector<SymbolId> check(const IrLawRecord& law, SymbolId module = no_symbol);

private:
    struct Checker;
    std::unique_ptr<Checker> checker_;
};

} // namespace neura
