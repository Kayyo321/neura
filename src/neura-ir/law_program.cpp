#include "law_program.hpp"

#include "law_core.hpp"
#include "util/source_map.hpp"

#include <algorithm>
#include <set>
#include <tuple>

namespace neura {
namespace {

using LawKey = std::pair<SymbolId, SymbolId>;

bool same_term(const LawTerm& a, const LawTerm& b) {
    return std::tie(a.kind, a.type, a.symbol, a.receiver, a.index) ==
               std::tie(b.kind, b.type, b.symbol, b.receiver, b.index) &&
           std::ranges::equal(a.terms, b.terms, same_term);
}

bool same_proof(const LawProof& a, const LawProof& b) {
    return std::tie(a.kind, a.symbol, a.receiver, a.index, a.bindings) ==
               std::tie(b.kind, b.symbol, b.receiver, b.index, b.bindings) &&
           std::ranges::equal(a.terms, b.terms, same_term) &&
           std::ranges::equal(a.proofs, b.proofs, same_proof);
}

bool same_abbreviation(const IrLawAbbreviation& a, const IrLawAbbreviation& b) {
    return a.name == b.name && a.binders == b.binders && same_term(a.value, b.value);
}

const Span& location(const std::map<SymbolId, Span>& spans, SymbolId id, const IrSymbol& fallback) {
    const auto found = spans.find(id);
    return found == spans.end() ? fallback.span : found->second;
}

[[noreturn]] void invalid_law(const Span& span, std::string text) {
    throw Diagnostic(span, std::move(text)).with_code("E1409");
}

std::vector<SymbolId> lineage(const IrProgram& program, SymbolId module) {
    std::vector<SymbolId> chain;
    std::set<SymbolId> seen;
    for (auto current = module; current != no_symbol; current = program.symbols[current].base) {
        if (current >= program.symbols.size() || program.symbols[current].kind != SymbolKind::Module ||
            !seen.insert(current).second)
            invalid_law(program.symbols[module].span, "invalid module inheritance in a law record");
        chain.push_back(current);
    }
    return chain;
}

bool has_abstract_method(const IrProgram& program, const std::vector<SymbolId>& chain,
                         const std::vector<std::vector<SymbolId>>& children) {
    std::set<SymbolId> implemented;
    for (const auto scope : chain) {
        for (const auto id : children[scope]) {
            const auto& member = program.symbols[id];
            if (member.kind != SymbolKind::Function || !member.exported) continue;
            if (!member.has_body && !member.external && !implemented.contains(id)) return true;
            if (!member.has_body && !member.external) continue;
            for (auto override_of = member.overrides; override_of != no_symbol;
                 override_of = program.symbols[override_of].overrides) {
                if (!implemented.insert(override_of).second) break;
            }
        }
    }
    return false;
}

std::set<SymbolId> concrete_providers(const IrProgram& program) {
    const auto& symbols = program.symbols;
    std::set<SymbolId> result;
    std::set<SymbolId> bases;
    std::set<SymbolId> entry_requirements;
    for (SymbolId id = 0; id < symbols.size(); ++id) {
        const auto& item = symbols[id];
        if (item.kind == SymbolKind::Module && item.base != no_symbol) bases.insert(item.base);
        if (item.kind != SymbolKind::Function || item.name != "main" || item.owner >= symbols.size()) continue;
        const auto& entry = symbols[item.owner];
        if (entry.kind != SymbolKind::Module || entry.name != "Entry" || entry.owner >= symbols.size()) continue;
        const auto& library = symbols[entry.owner];
        if (library.name == "Program" && library.owner < symbols.size() &&
            symbols[library.owner].library_path == "std/program.nra")
            entry_requirements.insert(id);
    }
    for (const auto& item : symbols) {
        if (item.kind != SymbolKind::Function || item.name != "main" || !item.has_body) continue;
        std::set<SymbolId> visited;
        for (auto related = item.overrides; related < symbols.size() && visited.insert(related).second;
             related = symbols[related].overrides) {
            if (!entry_requirements.contains(related)) continue;
            for (SymbolId module = 0; module < symbols.size(); ++module) {
                if (symbols[module].kind != SymbolKind::Module || bases.contains(module)) continue;
                const auto chain = lineage(program, module);
                if (std::ranges::find(chain, item.owner) != chain.end()) result.insert(module);
            }
            break;
        }
    }
    // A qualified module receiver fixes the provider. An unqualified call
    // through the current receiver can still dispatch in a derived module.
    const auto visit = [&](const auto& self, const IrNode& instruction) -> void {
        if (instruction.op == IrOp::VirtualCall && !instruction.operands.empty()) {
            const auto& receiver = instruction.operands.front();
            if (receiver.op == IrOp::Reference && receiver.type.kind == TypeKind::Module)
                result.insert(receiver.type.symbol);
        }
        for (const auto* region : {&instruction.operands, &instruction.body, &instruction.alternative})
            for (const auto& child : *region) self(self, child);
    };
    for (const auto& body : program.bodies)
        for (const auto& instruction : body.instructions) visit(visit, instruction);
    return result;
}

} // namespace

std::string qualified_law_name(const IrProgram& program, SymbolId id) {
    std::string result = program.symbols[id].name;
    for (auto owner = program.symbols[id].owner;
         owner < program.symbols.size() && program.symbols[owner].kind == SymbolKind::Module;
         owner = program.symbols[owner].owner)
        result = program.symbols[owner].name + "." + result;
    return result;
}

std::set<SymbolId> law_purity_requirements(const IrProgram& program) {
    std::set<SymbolId> requirements;
    for (const auto& law : program.laws) {
        if (law.status != LawStatus::Abstract) continue;
        const auto walk = [&](const auto& self, const LawTerm& term) -> void {
            if (term.kind == LawTermKind::Call && term.symbol < program.symbols.size()) {
                const auto& function = program.symbols[term.symbol];
                if (function.kind == SymbolKind::Function && function.owner == law.owner &&
                    !function.has_body && !function.external)
                    requirements.insert(term.symbol);
            }
            for (const auto& child : term.terms) self(self, child);
        };
        walk(walk, law.statement);
        for (const auto& abbreviation : law.abbreviations) walk(walk, abbreviation.value);
    }
    return requirements;
}

bool same_law_record(const IrLawRecord& a, const IrLawRecord& b) {
    return a.law_symbol == b.law_symbol && a.owner == b.owner && a.status == b.status &&
           a.introductions == b.introductions && same_term(a.statement, b.statement) &&
           std::ranges::equal(a.abbreviations, b.abbreviations, same_abbreviation) &&
           a.footprint == b.footprint && a.certificate.has_value() == b.certificate.has_value() &&
           (!a.certificate || same_proof(*a.certificate, *b.certificate));
}

void check_program_laws(IrProgram& program, std::size_t fuel, LawCheckOrigin origin,
                        const LawCheckSites& sites) {
    if (program.laws.empty()) return;
    const auto& symbols = program.symbols;
    std::map<LawKey, IrLawRecord*> records;
    std::vector<std::vector<SymbolId>> children(symbols.size());
    for (SymbolId id = 0; id < symbols.size(); ++id) {
        const auto owner = symbols[id].owner;
        if (owner < symbols.size()) children[owner].push_back(id);
    }
    for (auto& law : program.laws) {
        const auto& span = law.statement.span;
        if (law.law_symbol >= symbols.size() || symbols[law.law_symbol].kind != SymbolKind::Law ||
            law.owner >= symbols.size() || symbols[law.owner].kind != SymbolKind::Module ||
            !records.emplace(LawKey{law.law_symbol, law.owner}, &law).second)
            invalid_law(span, "invalid or repeated law record");
        if (law.status == LawStatus::TrustedAxiom)
            throw Diagnostic(span, "trusted law axioms are deferred to Phase 2").with_code("E1421");
        if (law.certificate.has_value() != (law.status == LawStatus::Proven))
            invalid_law(span, "law status and proof certificate disagree");
        std::set<std::string_view> names;
        for (const auto& abbreviation : law.abbreviations)
            if (!names.insert(abbreviation.name).second)
                invalid_law(abbreviation.value.span, "duplicate law abbreviation");
    }
    for (SymbolId id = 0; id < symbols.size(); ++id) {
        const auto& item = symbols[id];
        if (item.kind != SymbolKind::Law) continue;
        if (!item.constant || !records.contains({id, item.owner}))
            invalid_law(item.span, "law symbol is missing its declaration record");
    }
    const auto concrete = concrete_providers(program);
    for (const auto& [key, record] : records) {
        const auto& item = symbols[key.first];
        if (item.owner == key.second) {
            if (record->status == LawStatus::Abstract && symbols[key.second].constant)
                throw Diagnostic(item.span, "closed module cannot declare abstract law '" +
                                            qualified_law_name(program, key.first) + "'").with_code("E1416");
            if (record->status == LawStatus::Abstract && concrete.contains(key.second))
                throw Diagnostic(item.span, "concrete module has no proof for '" +
                                            qualified_law_name(program, key.first) + "'").with_code("E1403")
                    .help("add a checked '| proof:' to the declaration");
            continue;
        }
        const auto chain = lineage(program, key.second);
        const auto original = records.find({key.first, item.owner});
        if (!item.exported || original == records.end() ||
            std::ranges::find(chain, item.owner) == chain.end() || record->status != LawStatus::Proven ||
            record->introductions != original->second->introductions ||
            !std::ranges::equal(record->abbreviations, original->second->abbreviations, same_abbreviation) ||
            !same_term(record->statement, original->second->statement))
            invalid_law(record->statement.span, "a fulfillment must prove the inherited law's unchanged statement");
    }

    const auto inherited_failure = [&](const IrLawRecord& law, SymbolId module,
                                       const std::vector<SymbolId>& footprint, const Diagnostic& failure) {
        if (failure.code() == "E1406" || failure.code() == "E1414" || failure.code() == "E1415") return;
        SymbolId nearest_override = no_symbol;
        SymbolId changed_dependency = no_symbol;
        const auto depends_on = [&](SymbolId member) {
            std::set<SymbolId> seen;
            for (auto original = symbols[member].overrides;
                 original < symbols.size() && seen.insert(original).second; original = symbols[original].overrides)
                if (std::ranges::find(footprint, original) != footprint.end()) return true;
            return false;
        };
        for (const auto scope : lineage(program, module)) {
            if (scope == law.owner || changed_dependency != no_symbol) break;
            for (const auto member : children[scope]) {
                if (symbols[member].overrides == no_symbol) continue;
                if (nearest_override == no_symbol) nearest_override = member;
                if (!depends_on(member)) continue;
                changed_dependency = member;
                break;
            }
        }
        const auto changed = changed_dependency != no_symbol ? changed_dependency : nearest_override;
        if (changed == no_symbol) return;
        const auto& replacement = symbols[changed];
        const auto name = qualified_law_name(program, law.law_symbol);
        throw Diagnostic(location(sites.declarations, changed, replacement),
                         "override of '" + replacement.name + "' invalidated the proof of '" + name + "'")
            .with_code("E1406").arg("law", name)
            .label(failure.span, failure.what()).label(symbols[law.law_symbol].span, "inherited law declared here")
            .help("prove the unchanged statement with 'let.proof'");
    };
    LawCheckSession checker(program, fuel, inherited_failure);
    const auto verify = [&](IrLawRecord& law, SymbolId module) {
        try {
            return checker.check(law, module);
        } catch (Diagnostic& error) {
            if (!error.argument("law")) error.arg("law", qualified_law_name(program, law.law_symbol));
            throw;
        }
    };
    for (auto& law : program.laws) {
        auto verified = verify(law, law.owner);
        std::ranges::sort(verified);
        if (origin == LawCheckOrigin::Artifact && verified != law.footprint)
            invalid_law(law.statement.span, "law unfolding footprint disagrees with verified certificate");
        law.footprint = std::move(verified);
    }

    for (SymbolId module = 0; module < symbols.size(); ++module) {
        if (symbols[module].kind != SymbolKind::Module || symbols[module].base == no_symbol) continue;
        const auto chain = lineage(program, module);
        const bool abstract = !concrete.contains(module) && has_abstract_method(program, chain, children);
        std::set<SymbolId> seen;
        for (const auto base : chain) {
            if (base == module) continue;
            for (const auto law_id : children[base]) {
                const auto& law = symbols[law_id];
                if (law.kind != SymbolKind::Law || !law.exported || !seen.insert(law_id).second) continue;
                const auto explicit_proof = records.find({law_id, module});
                auto inherited = records.find({law_id, base});
                for (const auto ancestor : chain) {
                    if (ancestor == module) continue;
                    const auto candidate = records.find({law_id, ancestor});
                    if (candidate != records.end()) {
                        inherited = candidate;
                        break;
                    }
                }
                for (const auto child : children[module]) {
                    if (symbols[child].name != law.name) continue;
                    throw Diagnostic(location(sites.declarations, child, symbols[child]),
                                     "inherited law '" + qualified_law_name(program, law_id) +
                                     "' cannot be overridden or shadowed").with_code("E1405")
                        .label(law.span, "inherited law declaration").help("supply a proof with 'let.proof'");
                }
                if (explicit_proof != records.end()) continue;
                auto& record = *inherited->second;
                if (record.status == LawStatus::Abstract) {
                    if (abstract && !symbols[module].constant) continue;
                    const auto& span = location(sites.bases, module, symbols[module]);
                    auto error = Diagnostic(span, "missing proof for inherited law '" +
                                                  qualified_law_name(program, law_id) + "'").with_code("E1404")
                        .label(law.span, "abstract law declared here");
                    const auto& declaration = symbols[module].span;
                    if (declaration.length >= 2) {
                        const auto insertion = source_span(declaration.file, declaration.offset + declaration.length - 2, 0);
                        if (insertion) {
                            const auto proof = "let.proof " + law.name + ": { /* proof */ };";
                            error.suggest("insert a proof skeleton", *insertion,
                                          insertion->column == 1 ? "    " + proof + "\n" : proof + " ",
                                          Applicability::HasPlaceholders);
                        }
                    }
                    throw error;
                }
                // A proof that unfolds no definitions can still depend on the
                // totality of a call whose implementation was overridden.
                (void)verify(record, module);
            }
        }
    }
}

} // namespace neura
