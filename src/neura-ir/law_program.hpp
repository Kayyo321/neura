#pragma once

#include "ir.hpp"

#include <map>
#include <set>

namespace neura {

enum class LawCheckOrigin { Source, Artifact };

// The source spans for module inheritance and explicit overrides make
// diagnostics point to the declaration that introduces the obligation.
struct LawCheckSites {
    std::map<SymbolId, Span> bases;
    std::map<SymbolId, Span> declarations;
};

void check_program_laws(IrProgram& program, std::size_t fuel, LawCheckOrigin origin,
                        const LawCheckSites& sites = {});
bool same_law_record(const IrLawRecord& a, const IrLawRecord& b);
std::string qualified_law_name(const IrProgram& program, SymbolId law);
std::set<SymbolId> law_purity_requirements(const IrProgram& program);

} // namespace neura
