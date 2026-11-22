#include "law_core.hpp"

#include "aggregate.hpp"
#include "attributes.hpp"
#include "class_hooks.hpp"
#include "nominal.hpp"
#include "purity.hpp"
#include "tags.hpp"

#include <array>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <unordered_map>

namespace neura {
namespace {

using Term = LawTermKind;
using Step = LawProofKind;
using Value = std::size_t;
inline constexpr Value no_value = std::numeric_limits<Value>::max();
inline constexpr std::size_t max_depth = 512;
const IrType bool_type{TypeKind::Bool, "bool", no_symbol, {}};
const IrType arm_index_type{TypeKind::Integer, "uint32", no_symbol, {}};

enum class Head { Open, Boolean, Constructor, Nil, Call, Project, If, Not, Equal, EnumTag, Index };

struct Normal {
    Head head;
    IrType type;
    SymbolId symbol = no_symbol;
    SymbolId receiver = no_symbol;
    std::size_t index = 0;
    std::vector<Value> parts{};
    bool non_nil = false;
};

struct Proposition {
    Term kind = Term::Empty;
    IrType type{};
    Value left = no_value;
    Value right = no_value;
    std::vector<Proposition> parts{};
    // Binders close over value bindings only. Hypotheses are proof objects,
    // never values that can occur in a proposition's expression.
    const LawTerm* body = nullptr;
    SymbolId module = no_symbol;
    std::uint32_t binder = law_no_binding;
    std::map<std::uint32_t, Value> values{};
};

struct Scope {
    SymbolId module = no_symbol;
    std::map<std::uint32_t, Value> values{};
    std::map<std::uint32_t, Proposition> assumptions{};
    std::map<Value, Value> refinements{};
    std::map<Value, Value> parents{};
    // A false constructor test rules out that constructor for its subject.
    std::map<Value, std::set<SymbolId>> excluded{};
    std::set<Value> non_nil{};
};

struct Execution {
    Scope scope;
    std::map<SymbolId, Value> locals;
    Value receiver = no_value;
    SymbolId initializing_class = no_symbol;
};

struct Frame {
    SymbolId law;
    SymbolId module;
    std::vector<Value> arguments;
};

bool quantifier(Term kind) {
    return kind == Term::Forall || kind == Term::NullableForall;
}

[[noreturn]] void ill_typed(const Span& at, const std::string& message) {
    throw Diagnostic(at, message).with_code("E1409");
}

[[noreturn]] void partial(const Span& at, const std::string& operation) {
    throw Diagnostic(at, "a total law cannot use " + operation + " because it may trap")
        .with_code("E1411").help("use an expression that cannot trap; outcome proofs are deferred to Phase 2");
}

void type_key(std::string& key, const IrType& type) {
    key += std::to_string(static_cast<unsigned>(type.kind)) + ":" + std::to_string(type.symbol) + ":";
    key += std::to_string(type.name.size()) + ":" + type.name + "[";
    for (const auto& part : type.arguments) type_key(key, part);
    key += ']';
}

std::string show_type(const IrType& type) {
    if (type.kind != TypeKind::Tuple) return type.name.empty() ? "value" : type.name;
    std::string result = "[";
    for (std::size_t i = 0; i < type.arguments.size(); ++i) {
        if (i) result += ", ";
        result += show_type(type.arguments[i]);
    }
    return result + ']';
}

} // namespace

struct LawCheckSession::Checker {
    Checker(const IrProgram& program, std::size_t fuel) : program(program), budget(fuel) {
        for (const auto& body : program.bodies) {
            bodies[body.owner].push_back(&body);
            if (body.owner >= program.symbols.size() || program.symbols[body.owner].kind == SymbolKind::Function)
                continue;
            for (const auto& instruction : body.instructions) {
                if (instruction.op == IrOp::Initialize && instruction.operands.size() == 1)
                    initializers[instruction.symbol].push_back(&instruction.operands.front());
            }
        }
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            const auto& item = program.symbols[id];
            if (item.kind == SymbolKind::Function || item.kind == SymbolKind::Global || item.kind == SymbolKind::Field)
                members[{item.owner, item.name}].push_back(id);
        }
    }

    struct Run {
        explicit Run(Checker& session) : session(session), fuel(session.budget) {}

        Checker& session;
        std::size_t fuel;
        std::size_t depth = 0;
        std::size_t next_open = 0;
        std::vector<Normal> normals;
        std::unordered_map<std::string, Value> intern;
        std::map<SymbolId, std::size_t> unfolded;
        std::set<SymbolId> footprint;
        // Termination checks are for a particular function and module
        // receiver. The latter determines the runtime override targets.
        struct Function {
            SymbolId symbol;
            SymbolId module;
            std::vector<bool> non_nil;
            auto operator<=>(const Function&) const = default;
        };
        struct ActiveFunction {
            Function key;
            std::vector<Value> arguments;
        };
        std::vector<ActiveFunction> functions;
        std::set<Function> validated;
        struct CallEdge {
            Function target;
            bool decreasing;
            Span span;
        };
        std::map<Function, std::vector<CallEdge>> function_edges;
        Frame frame{no_symbol, no_symbol, {}};

        struct Guard {
            Run& run;
            explicit Guard(Run& run, const Span& at) : run(run) {
                run.tick(at);
                if (++run.depth > max_depth) run.exhausted(at);
            }
            ~Guard() { --run.depth; }
        };

        [[noreturn]] void exhausted(const Span& at) const {
            SymbolId hottest = no_symbol;
            std::size_t count = 0;
            for (const auto& [id, steps] : unfolded) {
                if (steps <= count) continue;
                hottest = id;
                count = steps;
            }
            auto error = Diagnostic(at, "proof normalization fuel exhausted (budget " +
                                           std::to_string(session.budget) + ")")
                .with_code("E1415");
            if (hottest != no_symbol)
                error.note("most frequently unfolded definition: " + session.name(hottest));
            throw error;
        }

        void tick(const Span& at) {
            if (!fuel) exhausted(at);
            --fuel;
        }

        const Normal& normal(Value id) const { return normals[id]; }

        Value make(Normal node) {
            std::string key = std::to_string(static_cast<unsigned>(node.head)) + ":" +
                std::to_string(node.symbol) + ":" + std::to_string(node.receiver) + ":" +
                std::to_string(node.index) + (node.non_nil ? "+" : "-");
            type_key(key, node.type);
            for (const auto part : node.parts) key += ":" + std::to_string(part);
            const auto [found, fresh] = intern.try_emplace(std::move(key), normals.size());
            if (fresh) normals.push_back(std::move(node));
            return found->second;
        }

        Value open(const IrType& type, bool non_nil = false) {
            return make({Head::Open, type, no_symbol, no_symbol, next_open++, {},
                         non_nil && type.kind == TypeKind::Object});
        }

        Value boolean(bool value) {
            return make({Head::Boolean, bool_type, no_symbol, no_symbol, value ? 1u : 0u});
        }

        Value number(std::size_t value) {
            return make({Head::Index, arm_index_type, no_symbol, no_symbol, value});
        }

        Value known(Value value, const Scope& scope) const {
            auto found = scope.refinements.find(value);
            while (found != scope.refinements.end() && found->second != value) {
                value = found->second;
                found = scope.refinements.find(value);
            }
            return value;
        }

        bool non_nil(Value value, const Scope& scope) const {
            value = known(value, scope);
            return normal(value).non_nil || scope.non_nil.contains(value);
        }

        void refine(Scope& scope, Value from, Value to) const {
            const auto source = known(from, scope);
            const auto target = known(to, scope);
            if (source != target) scope.refinements[source] = target;
        }

        std::vector<SymbolId> variants(const IrType& type) const {
            std::vector<SymbolId> result;
            for (SymbolId id = 0; id < session.program.symbols.size(); ++id) {
                const auto& symbol = session.program.symbols[id];
                if (symbol.kind == SymbolKind::Variant && symbol.owner == type.symbol) result.push_back(id);
            }
            return result;
        }

        std::vector<IrType> payload(const IrType& type, SymbolId variant, const Span& at) const {
            const auto& value = session.symbol(variant, at);
            if (type.kind != TypeKind::Enum || value.kind != SymbolKind::Variant || value.owner != type.symbol)
                ill_typed(at, "constructor " + value.name + " does not belong to " + show_type(type));
            const auto& enumeration = session.symbol(type.symbol, at);
            std::vector<IrType> parts;
            for (const auto& item : value.payload_types)
                parts.push_back(substitute_type(item, enumeration.generics, type.arguments));
            return parts;
        }

        std::vector<SymbolId> fields(const IrType& type) const {
            std::vector<SymbolId> result;
            for (SymbolId id = 0; id < session.program.symbols.size(); ++id) {
                const auto& symbol = session.program.symbols[id];
                if (symbol.kind == SymbolKind::Field && session.derives(type.symbol, symbol.owner))
                    result.push_back(id);
            }
            return result;
        }

        bool supported(const IrType& type, std::set<SymbolId>& active, const Span& at) {
            Guard guard(*this, at);
            if (type == bool_type) return true;
            if (type.kind == TypeKind::Tuple) {
                if (type.symbol != no_symbol) return false;
                return std::ranges::all_of(type.arguments, [&](const IrType& part) { return supported(part, active, at); });
            }
            if (type.kind != TypeKind::Object && type.kind != TypeKind::Enum) return false;
            const auto& nominal = session.symbol(type.symbol, at);
            const auto required = type.kind == TypeKind::Enum ? SymbolKind::Enum : SymbolKind::Class;
            if (nominal.kind != required || type.name != nominal.type.name ||
                nominal.generics.size() != type.arguments.size()) return false;
            if (required == SymbolKind::Class) {
                if (!has_tag(nominal, TagId::Immutable)) return false;
                session.check_immutable(type, at);
            }
            for (const auto& argument : type.arguments)
                if (!supported(argument, active, at)) return false;
            if (!active.insert(type.symbol).second) return true;
            if (required == SymbolKind::Enum) {
                for (const auto variant : variants(type)) {
                    for (const auto& item : payload(type, variant, at)) {
                        if (!supported(item, active, at)) return false;
                    }
                }
            }
            active.erase(type.symbol);
            return true;
        }

        void require_type(const IrType& type, const Span& at) {
            std::set<SymbolId> active;
            if (!supported(type, active, at))
                ill_typed(at, "type " + show_type(type) + " is not supported in Phase 1 laws");
        }

        void expect(Value input, const IrType& type, const Span& at) const {
            if (normal(input).type != type)
                ill_typed(at, "expected " + show_type(type) + ", found " + show_type(normal(input).type));
        }

        void shape(const LawTerm& term, std::size_t count) const {
            if (term.terms.size() != count) ill_typed(term.span, "malformed law term");
        }

        void shape(const LawProof& proof, std::size_t values, std::size_t proofs) const {
            if (proof.terms.size() != values || proof.proofs.size() != proofs)
                ill_typed(proof.span, "malformed law certificate step");
        }

        void bind(Scope& scope, std::uint32_t binder, Value value, const Span& at) {
            if (binder >= law_wildcard_arm || scope.assumptions.contains(binder) ||
                !scope.values.try_emplace(binder, value).second)
                ill_typed(at, "duplicate or invalid value binder in law");
        }

        void assume(Scope& scope, std::uint32_t binder, Proposition proposition, const Span& at) {
            if (binder == law_no_binding) return;
            if (binder == law_wildcard_arm || scope.values.contains(binder) ||
                !scope.assumptions.try_emplace(binder, std::move(proposition)).second)
                ill_typed(at, "duplicate or invalid proof binder in law");
        }

        Value conditional(Value condition, Value yes, Value no) {
            if (yes == no) return yes;
            const auto& test = normal(condition);
            if (test.head == Head::Boolean) return test.index ? yes : no;
            const auto first = normal(yes);
            const auto second = normal(no);
            if (first.head == Head::Constructor && second.head == Head::Constructor &&
                first.type.kind == TypeKind::Object && first.type == second.type) {
                auto result = first;
                for (std::size_t i = 0; i < first.parts.size(); ++i)
                    result.parts[i] = conditional(condition, first.parts[i], second.parts[i]);
                return make(std::move(result));
            }
            const bool non_nil = normal(yes).non_nil && normal(no).non_nil;
            return make({Head::If, normal(yes).type, no_symbol, no_symbol, 0, {condition, yes, no}, non_nil});
        }

        Value negate(Value input) {
            const auto& value = normal(input);
            if (value.head == Head::Boolean) return boolean(!value.index);
            if (value.head == Head::Not) return value.parts.front();
            return make({Head::Not, bool_type, no_symbol, no_symbol, 0, {input}});
        }

        Value value_eq(Value left, Value right, const Scope& scope) {
            if (left == right) return boolean(true);
            const auto& a = normal(left);
            const auto& b = normal(right);
            if ((a.head == Head::Nil && non_nil(right, scope)) || (b.head == Head::Nil && non_nil(left, scope)))
                return boolean(false);
            if (a.head == b.head) {
                if (a.head == Head::Boolean || a.head == Head::Index) return boolean(a.index == b.index);
                if (a.head == Head::Constructor && a.parts.empty() && b.parts.empty()) return boolean(a.symbol == b.symbol);
            }
            return make({Head::Equal, bool_type, no_symbol, no_symbol, 0, {left, right}});
        }

        Value project(Value receiver, std::size_t index, SymbolId variant, const IrType& type,
                      const Scope& scope, const Span& at) {
            receiver = known(receiver, scope);
            const auto& value = normal(receiver);
            if (value.head == Head::Constructor) {
                if (index >= value.parts.size() || (variant != no_symbol && value.symbol != variant))
                    partial(at, "a projection outside its constructor");
                if (value.parts[index] == no_value) partial(at, "an uninitialized field");
                return value.parts[index];
            }
            if (variant != no_symbol || (value.type.kind == TypeKind::Object && !non_nil(receiver, scope)))
                partial(at, "a projection without a constructor or non-nil receiver");
            return make({Head::Project, type, variant, no_symbol, index, {receiver}});
        }

        Value nil_as(Value neighbour, const IrType& declared, const Span& at) {
            const auto& context = normal(neighbour).type;
            if (declared.kind != TypeKind::Object || context.kind != TypeKind::Object)
                ill_typed(at, "nil comparison requires a class reference");
            return make({Head::Nil, context});
        }

        Value value(const LawTerm& term, const Scope& scope, Value hole = no_value) {
            Guard guard(*this, term.span);
            require_type(term.type, term.span);
            switch (term.kind) {
            case Term::Variable: {
                shape(term, 0);
                const auto found = scope.values.find(term.index);
                if (found == scope.values.end()) ill_typed(term.span, "unbound value in law");
                expect(found->second, term.type, term.span);
                return found->second;
            }
            case Term::Boolean:
                shape(term, 0);
                if (term.index > 1 || term.type != bool_type) ill_typed(term.span, "invalid bool literal in law");
                return boolean(term.index != 0);
            case Term::Nil:
                shape(term, 0);
                if (term.type.kind != TypeKind::Object) ill_typed(term.span, "nil requires a class type in law");
                return make({Head::Nil, term.type});
            case Term::Hole:
                shape(term, 0);
                if (hole == no_value) ill_typed(term.span, "a rewrite hole is only valid in a motive");
                expect(hole, term.type, term.span);
                return hole;
            case Term::Constructor: {
                const auto types = term.type.kind == TypeKind::Tuple && term.symbol == no_symbol
                    ? term.type.arguments : payload(term.type, term.symbol, term.span);
                shape(term, types.size());
                std::vector<Value> parts;
                for (std::size_t i = 0; i < types.size(); ++i) {
                    const auto part = value(term.terms[i], scope, hole);
                    expect(part, types[i], term.terms[i].span);
                    parts.push_back(part);
                }
                return make({Head::Constructor, term.type, term.symbol, no_symbol, 0, std::move(parts)});
            }
            case Term::Constant:
                shape(term, 0);
                return constant(term.symbol, term.receiver, term.type, scope, term.span, true);
            case Term::Construct: {
                std::vector<Value> inputs;
                for (const auto& child : term.terms) inputs.push_back(value(child, scope, hole));
                return construct(term.type, term.symbol, inputs, scope, Mode::Validate, term.span);
            }
            case Term::Call: {
                std::vector<Value> inputs;
                for (const auto& child : term.terms) inputs.push_back(value(child, scope, hole));
                return call(term.symbol, term.receiver, term.type, std::move(inputs), scope, term.span);
            }
            case Term::Project: {
                shape(term, 1);
                const auto input = value(term.terms.front(), scope, hole);
                const auto& type = normal(input).type;
                if (type.kind == TypeKind::Object) {
                    const auto members = fields(type);
                    const auto found = std::ranges::find(members, term.symbol);
                    if (found == members.end()) ill_typed(term.span, "unknown immutable field in law projection");
                    const auto& field = session.symbol(term.symbol, term.span);
                    if (term.type != field.type) ill_typed(term.span, "wrong field type in law projection");
                    return project(input, static_cast<std::size_t>(found - members.begin()), no_symbol, term.type,
                                   scope, term.span);
                }
                const auto parts = type.kind == TypeKind::Tuple && term.symbol == no_symbol
                    ? type.arguments : payload(type, term.symbol, term.span);
                if (term.index >= parts.size() || term.type != parts[term.index])
                    ill_typed(term.span, "wrong payload type or index in law projection");
                return project(input, term.index, term.symbol, term.type, scope, term.span);
            }
            case Term::If: {
                shape(term, 3);
                const auto condition = value(term.terms[0], scope, hole);
                expect(condition, bool_type, term.terms[0].span);
                auto yes = scope;
                auto no = scope;
                specialize_condition(yes, condition, true);
                specialize_condition(no, condition, false);
                const auto when_true = value(term.terms[1], yes, hole);
                const auto when_false = value(term.terms[2], no, hole);
                expect(when_true, term.type, term.terms[1].span);
                expect(when_false, term.type, term.terms[2].span);
                return conditional(known(condition, scope), when_true, when_false);
            }
            case Term::Not: {
                shape(term, 1);
                const auto operand = value(term.terms.front(), scope, hole);
                expect(operand, bool_type, term.span);
                if (term.type != bool_type) ill_typed(term.span, "not has a bool result");
                return negate(known(operand, scope));
            }
            case Term::And:
            case Term::Or:
            case Term::BooleanEqual: {
                shape(term, 2);
                if (term.type != bool_type) ill_typed(term.span, "boolean operator must have a bool result");
                // Ordinary NeuraIR's comparison may widen a written nil to
                // the umbrella class type. Only the other operand's static
                // class matters when testing a reference for nil.
                if (term.kind == Term::BooleanEqual) {
                    const bool left_nil = term.terms[0].kind == Term::Nil;
                    const bool right_nil = term.terms[1].kind == Term::Nil;
                    if (left_nil != right_nil) {
                        const auto& nil = term.terms[left_nil ? 0 : 1];
                        shape(nil, 0);
                        const auto other = value(term.terms[left_nil ? 1 : 0], scope, hole);
                        const auto empty = nil_as(other, nil.type, nil.span);
                        return value_eq(known(other, scope), empty, scope);
                    }
                }
                const auto left = value(term.terms[0], scope, hole);
                auto right_scope = scope;
                if (term.kind != Term::BooleanEqual)
                    specialize_condition(right_scope, left, term.kind == Term::And);
                const auto right = value(term.terms[1], right_scope, hole);
                if (term.kind == Term::BooleanEqual) {
                    const auto type = normal(left).type;
                    expect(right, type, term.terms[1].span);
                    const bool nil_test = type.kind == TypeKind::Object &&
                        (term.terms[0].kind == Term::Nil || term.terms[1].kind == Term::Nil);
                    if (!nil_test && type.kind == TypeKind::Object) session.forbidden(term.span, "uses object identity");
                    if (!nil_test && type.kind != TypeKind::Bool &&
                        (type.kind != TypeKind::Enum || has_enum_payload(session.program, type.symbol)))
                        ill_typed(term.span, "runtime equality in a law requires bool, a fieldless enum or a nil test");
                    return value_eq(known(left, scope), known(right, scope), scope);
                }
                expect(left, bool_type, term.terms[0].span);
                expect(right, bool_type, term.terms[1].span);
                return term.kind == Term::And ? conditional(known(left, scope), right, boolean(false))
                                              : conditional(known(left, scope), boolean(true), right);
            }
            default:
                ill_typed(term.span, "a proposition cannot be used as a runtime value in a law");
            }
        }

        Proposition equality(Value left, Value right) const {
            return {Term::Equal, normal(left).type, left, right};
        }

        Proposition form(const LawTerm& term, const Scope& scope, Value hole = no_value) {
            Guard guard(*this, term.span);
            switch (term.kind) {
            case Term::Equal: {
                shape(term, 2);
                const auto left = value(term.terms[0], scope, hole);
                const auto right = value(term.terms[1], scope, hole);
                expect(left, term.type, term.terms[0].span);
                expect(right, term.type, term.terms[1].span);
                return equality(left, right);
            }
            case Term::Empty:
                shape(term, 0);
                return {};
            case Term::Either:
                shape(term, 2);
                [[fallthrough]];
            case Term::All: {
                if (term.terms.empty()) ill_typed(term.span, "a conjunction must contain a claim");
                Proposition proposition{term.kind};
                for (const auto& child : term.terms) proposition.parts.push_back(form(child, scope, hole));
                return proposition;
            }
            case Term::Implies: {
                shape(term, 2);
                Proposition proposition{term.kind};
                proposition.binder = term.index;
                for (const auto& child : term.terms) proposition.parts.push_back(form(child, scope, hole));
                return proposition;
            }
            case Term::Forall:
            case Term::NullableForall:
            case Term::Exists: {
                shape(term, 1);
                require_type(term.type, term.span);
                if (term.kind == Term::NullableForall && term.type.kind != TypeKind::Object)
                    ill_typed(term.span, "for? requires an immutable class type");
                Proposition proposition{term.kind, term.type};
                proposition.binder = term.index;
                proposition.body = &term.terms.front();
                proposition.module = scope.module;
                proposition.values = scope.values;
                auto inner = scope;
                bind(inner, term.index, open(term.type, term.kind == Term::Forall), term.span);
                form(term.terms.front(), inner, hole);
                // A motive with a bound proposition would need to close over
                // its hole as well, so instantiate with the current value.
                proposition.left = hole;
                return proposition;
            }
            default:
                ill_typed(term.span, "expected a proposition in law");
            }
        }

        Proposition instantiate(const Proposition& quantified, Value input, const Scope& scope, const Span& at) {
            expect(input, quantified.type, at);
            if (quantified.kind == Term::Forall && quantified.type.kind == TypeKind::Object &&
                !non_nil(input, scope))
                ill_typed(at, "the quantifier requires a non-nil immutable instance");
            auto inner = scope;
            inner.values = quantified.values;
            inner.module = quantified.module;
            // The proposition's value bindings have their own lexical scope.
            inner.assumptions.clear();
            bind(inner, quantified.binder, input, at);
            return form(*quantified.body, inner, quantified.left);
        }

        void specialize_condition(Scope& scope, Value subject, bool result) {
            subject = known(subject, scope);
            const auto condition = normal(subject);
            if (condition.type != bool_type || condition.head == Head::Boolean) return;
            refine(scope, subject, boolean(result));
            if (condition.head == Head::Not) specialize_condition(scope, condition.parts[0], !result);
            if (condition.head != Head::Equal) return;
            auto [left, right] = std::pair{known(condition.parts[0], scope), known(condition.parts[1], scope)};
            if (normal(left).head != Head::EnumTag && normal(right).head == Head::EnumTag) std::swap(left, right);
            if (normal(left).head == Head::EnumTag && normal(right).head == Head::Index) {
                const auto inspected = normal(left).parts[0];
                const auto type = normal(inspected).type;
                const auto choices = variants(type);
                const auto tag = normal(right).index;
                if (tag >= choices.size()) return;
                refine_constructor(scope, inspected, choices[tag], result);
            } else if (normal(left).type.kind == TypeKind::Enum) {
                if (normal(left).head == Head::Constructor && normal(right).head != Head::Constructor)
                    std::swap(left, right);
                if (normal(right).head == Head::Constructor && normal(right).parts.empty())
                    refine_constructor(scope, left, normal(right).symbol, result);
            } else if (normal(left).type.kind == TypeKind::Object) {
                if (normal(left).head == Head::Nil) std::swap(left, right);
                if (normal(right).head != Head::Nil) return;
                if (result) refine(scope, left, right);
                else scope.non_nil.insert(left);
            } else if (normal(left).type == bool_type) {
                if (normal(left).head == Head::Boolean && normal(right).head != Head::Boolean)
                    std::swap(left, right);
                if (normal(right).head == Head::Boolean)
                    specialize_condition(scope, left, (normal(right).index != 0) == result);
            }
        }

        void refine_constructor(Scope& scope, Value subject, SymbolId constructor, bool matches) {
            auto base = known(subject, scope);
            const auto type = normal(base).type;
            if (normal(base).head == Head::Constructor) return;
            if (!matches) {
                auto& excluded = scope.excluded[base];
                excluded.insert(constructor);
                auto remaining = no_symbol;
                for (const auto variant : variants(type)) {
                    if (excluded.contains(variant)) continue;
                    if (remaining != no_symbol) return;
                    remaining = variant;
                }
                if (remaining == no_symbol) return;
                constructor = remaining;
            }
            const auto& declaration = session.program.symbols[constructor];
            const auto parts = payload(type, constructor, declaration.span);
            std::vector<Value> projected;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                const auto part = make({Head::Project, parts[i], constructor, no_symbol, i, {base}});
                if (parts[i] == type) scope.parents.emplace(part, base);
                projected.push_back(part);
            }
            refine(scope, base, make({Head::Constructor, type, constructor, no_symbol, 0, std::move(projected)}));
        }

        bool smaller(Value candidate, Value root, const Scope& scope) const {
            for (;;) {
                const auto found = scope.parents.find(candidate);
                if (found == scope.parents.end()) return false;
                candidate = found->second;
                if (candidate == root) return true;
            }
        }

        bool decreases(const std::vector<Value>& arguments, const std::vector<Value>& roots, const Scope& scope) const {
            if (arguments.size() != roots.size()) return false;
            for (std::size_t i = 0; i < roots.size(); ++i) {
                if (arguments[i] == roots[i]) continue;
                return smaller(arguments[i], roots[i], scope);
            }
            return false;
        }

        Value enum_tag(Value input, const Scope& scope) {
            const auto receiver = known(input, scope);
            const auto& value = normal(receiver);
            if (value.head == Head::Constructor) return number(enum_ordinal(session.program, value.symbol));
            return make({Head::EnumTag, arm_index_type, no_symbol, no_symbol, 0, {receiver}});
        }

        enum class Mode { Validate, Unfold };

        std::optional<Value> known_boolean(Value condition, const Scope& scope) const {
            const auto head = known(condition, scope);
            if (normal(head).head == Head::Boolean) return head;
            return std::nullopt;
        }

        Value simple_eq(const IrNode& node, Value left, Value right, const Scope& scope) {
            const auto& type = normal(left).type;
            if (normal(right).type != type) ill_typed(node.span, "comparison operand type mismatch in law call");
            const bool nil_test = type.kind == TypeKind::Object &&
                (node.operands[0].op == IrOp::Nil || node.operands[1].op == IrOp::Nil);
            if (!nil_test && type.kind == TypeKind::Object) session.forbidden(node.span, "uses object identity");
            if (!nil_test && type != bool_type && type != arm_index_type &&
                (type.kind != TypeKind::Enum || has_enum_payload(session.program, type.symbol)))
                ill_typed(node.span, "this runtime comparison is reserved for Phase 2 laws");
            auto result = value_eq(known(left, scope), known(right, scope), scope);
            return node.text == "!=" ? negate(result) : result;
        }

        Value pure_value(const IrNode& node, const Execution& state, Mode mode) {
            Guard guard(*this, node.span);
            const auto operand = [&](std::size_t at) {
                if (at >= node.operands.size()) ill_typed(node.span, "malformed pure function expression in law");
                return pure_value(node.operands[at], state, mode);
            };
            switch (node.op) {
            case IrOp::Boolean:
                return boolean(node.text == "true");
            case IrOp::Integer: {
                // Match lowering uses an internal uint32 arm index. No
                // machine-number computation participates in Phase 1.
                const auto index = aggregate_index(node.text);
                if (node.type == arm_index_type && index && *index <= std::numeric_limits<std::uint32_t>::max())
                    return number(*index);
                ill_typed(node.span, "machine numbers in laws are reserved for Phase 2");
            }
            case IrOp::Nil:
                require_type(node.type, node.span);
                return make({Head::Nil, node.type});
            case IrOp::Receiver:
                if (state.receiver == no_value) ill_typed(node.span, "law body has no immutable receiver");
                if (state.initializing_class != no_symbol)
                    session.forbidden(node.span, "an immutable constructor exposes its receiver before completion");
                return state.receiver;
            case IrOp::Load:
            case IrOp::Reference: {
                if (const auto found = state.locals.find(node.symbol); found != state.locals.end()) {
                    if (found->second == no_value) partial(node.span, "an uninitialized local");
                    return found->second;
                }
                const auto& declaration = session.symbol(node.symbol, node.span);
                if (declaration.kind == SymbolKind::Variant && declaration.payload_types.empty())
                    return make({Head::Constructor, node.type, node.symbol});
                if (declaration.kind == SymbolKind::Local || declaration.kind == SymbolKind::Parameter)
                    partial(node.span, "an uninitialized local");
                if (declaration.kind != SymbolKind::Global)
                    session.forbidden(node.span, "reads a binding without a compile-time immutable value");
                auto receiver = declaration.owner;
                if (!node.operands.empty()) {
                    const auto& qualifier = node.operands.front();
                    if (qualifier.type.kind != TypeKind::Module)
                        session.forbidden(node.span, "reads a binding through a non-constant receiver");
                    if (qualifier.op == IrOp::Receiver) receiver = state.scope.module;
                    else if (qualifier.op == IrOp::Reference) receiver = qualifier.type.symbol;
                    else session.forbidden(node.span, "reads a binding through a dynamic module receiver");
                }
                return constant(node.symbol, receiver, node.type, state.scope, node.span, mode == Mode::Unfold);
            }
            case IrOp::EnumConstruct:
            case IrOp::Tuple: {
                std::vector<Value> parts(node.operands.size());
                const auto order = enum_field_order(node.text, parts.size());
                if (!order) ill_typed(node.span, "malformed constructor fields in law call");
                for (std::size_t i = 0; i < parts.size(); ++i) parts[(*order)[i]] = operand(i);
                return make({Head::Constructor, node.type, node.symbol, no_symbol, 0, std::move(parts)});
            }
            case IrOp::Project:
            case IrOp::EnumProject: {
                const auto index = aggregate_index(node.text);
                if (!index) ill_typed(node.span, "malformed projection in law call");
                return project(operand(0), *index, node.op == IrOp::EnumProject ? node.symbol : no_symbol,
                               node.type, state.scope, node.span);
            }
            case IrOp::Field: {
                const auto receiver = state.initializing_class != no_symbol && !node.operands.empty() &&
                    node.operands.front().op == IrOp::Receiver ? state.receiver : operand(0);
                require_type(normal(receiver).type, node.span);
                const auto members = fields(normal(receiver).type);
                const auto found = std::ranges::find(members, node.symbol);
                if (found == members.end()) ill_typed(node.span, "unknown immutable field in law function");
                return project(receiver, static_cast<std::size_t>(found - members.begin()), no_symbol,
                               node.type, state.scope, node.span);
            }
            case IrOp::EnumTag:
                return enum_tag(operand(0), state.scope);
            case IrOp::Unary:
                if (node.text == "!") return negate(known(operand(0), state.scope));
                if (node.text == "-") partial(node.span, "checked negation");
                ill_typed(node.span, "this unary operation is reserved for Phase 2 laws");
            case IrOp::Binary:
                if (node.text == "==" || node.text == "!=") {
                    if (node.operands.size() != 2) ill_typed(node.span, "malformed comparison in law call");
                    const bool left_nil = node.operands[0].op == IrOp::Nil;
                    const bool right_nil = node.operands[1].op == IrOp::Nil;
                    if (left_nil != right_nil) {
                        const auto& nil = node.operands[left_nil ? 0 : 1];
                        const auto other = operand(left_nil ? 1 : 0);
                        const auto empty = nil_as(other, nil.type, nil.span);
                        return simple_eq(node, left_nil ? empty : other, right_nil ? empty : other, state.scope);
                    }
                    const auto left = operand(0);
                    const auto right = operand(1);
                    return simple_eq(node, left, right, state.scope);
                }
                partial(node.span, "operation '" + node.text + "'");
            case IrOp::Branch:
            case IrOp::SelectValue: {
                const auto condition = operand(0);
                const auto region = [&](bool yes) {
                    auto local = state;
                    specialize_condition(local.scope, condition, yes);
                    return scoped(yes ? node.body : node.alternative, std::move(local), mode, node.span);
                };
                if (const auto test = known_boolean(condition, state.scope)) return region(normal(*test).index != 0);
                const auto yes = region(true);
                const auto no = region(false);
                return conditional(condition, yes, no);
            }
            case IrOp::ShortCircuit: {
                const auto condition = operand(0);
                const bool conjunction = node.text == "&&";
                const auto right = [&] {
                    auto local = state;
                    specialize_condition(local.scope, condition, conjunction);
                    return scoped(node.body, std::move(local), mode, node.span);
                };
                if (const auto test = known_boolean(condition, state.scope))
                    return (normal(*test).index != 0) == conjunction ? right() : *test;
                return conjunction ? conditional(condition, right(), boolean(false))
                                   : conditional(condition, boolean(true), right());
            }
            case IrOp::ScopedValue:
                return scoped(node.body, state, mode, node.span);
            case IrOp::Allocate: {
                std::vector<Value> inputs;
                for (const auto& child : node.operands) inputs.push_back(pure_value(child, state, mode));
                return construct(node.type, node.symbol, inputs, state.scope, mode, node.span);
            }
            case IrOp::Call:
            case IrOp::VirtualCall: {
                if (!node.type_arguments.empty())
                    ill_typed(node.span, "generic functions in laws are reserved for Phase 3");
                SymbolId receiver = no_symbol;
                std::size_t first = 0;
                if (node.op == IrOp::Call) {
                    const auto owner = session.symbol(node.symbol, node.span).owner;
                    if (session.is_module(owner)) receiver = owner;
                } else {
                    if (node.operands.empty()) ill_typed(node.span, "a virtual law call requires a receiver");
                    const auto& target = node.operands.front();
                    if (target.type.kind == TypeKind::Module) {
                        if (target.op == IrOp::Reference) receiver = target.type.symbol;
                        else if (target.op != IrOp::Receiver)
                            ill_typed(node.span, "dynamic module dispatch is not supported in laws");
                        first = 1;
                    } else if (target.type.kind != TypeKind::Object) {
                        ill_typed(node.span, "this receiver dispatch is not supported in a Phase 1 law");
                    }
                }
                std::vector<Value> inputs;
                for (std::size_t i = first; i < node.operands.size(); ++i) inputs.push_back(operand(i));
                return call(node.symbol, receiver, node.type, std::move(inputs), state.scope, node.span,
                            mode == Mode::Validate, false);
            }
            case IrOp::ExternalCall:
            case IrOp::Spawn:
            case IrOp::SchedulerCall:
            case IrOp::TaskScope:
            case IrOp::ClassHash:
            case IrOp::ClassText:
            case IrOp::CallClosure:
                session.forbidden(node.span, "uses a host, scheduler, object identity, or indirect call operation");
            case IrOp::Index:
            case IrOp::Convert:
            case IrOp::ClassCast:
            case IrOp::Fail:
            case IrOp::TrapMessage:
                partial(node.span, "a checked memory, conversion, or explicit trap operation");
            default:
                ill_typed(node.span, "the pure function uses an operation not supported in Phase 1 laws");
            }
        }

        using Continuation = std::function<Value(Execution)>;

        void set_field(Execution& state, SymbolId field, Value input, const Span& at) {
            const auto& member = session.symbol(field, at);
            if (member.kind != SymbolKind::Field || member.owner != state.initializing_class || !member.constant)
                session.forbidden(at, "a constructor writes outside its own immutable fields");
            auto object = normal(state.receiver);
            const auto members = fields(object.type);
            const auto found = std::ranges::find(members, field);
            if (found == members.end()) ill_typed(at, "constructor field is outside its receiver layout");
            const auto index = static_cast<std::size_t>(found - members.begin());
            if (object.parts[index] != no_value) partial(at, "a repeated initialization of an immutable field");
            expect(input, member.type, at);
            object.parts[index] = input;
            state.receiver = make(std::move(object));
        }

        Value constructed(const Execution& state, const Span& at) {
            const auto& object = normal(state.receiver);
            const auto members = fields(object.type);
            for (std::size_t i = 0; i < members.size(); ++i) {
                if (session.symbol(members[i], at).owner == state.initializing_class && object.parts[i] == no_value)
                    partial(at, "construction that can leave an immutable field uninitialized");
            }
            return state.receiver;
        }

        Value statements(const std::vector<IrNode>& body, std::size_t at, std::size_t end, Execution state,
                         Mode mode, const Continuation& continuation) {
            if (at == end) return continuation(std::move(state));
            const auto& node = body[at];
            Guard guard(*this, node.span);
            const auto remaining = [&](Execution changed) {
                return statements(body, at + 1, end, std::move(changed), mode, continuation);
            };
            switch (node.op) {
            case IrOp::Return:
                if (state.initializing_class != no_symbol && node.operands.empty())
                    return constructed(state, node.span);
                if (node.operands.size() != 1) ill_typed(node.span, "a function in a law must return a pure value");
                if (state.initializing_class != no_symbol)
                    ill_typed(node.span, "an immutable constructor cannot return a value");
                return pure_value(node.operands.front(), state, mode);
            case IrOp::Initialize:
            case IrOp::Store: {
                const bool init = node.op == IrOp::Initialize;
                if (!init && state.initializing_class != no_symbol && node.operands.size() == 2) {
                    const auto& place = node.operands.front();
                    if (place.op == IrOp::Field && place.operands.size() == 1 &&
                        place.operands.front().op == IrOp::Receiver) {
                        const auto input = pure_value(node.operands.back(), state, mode);
                        set_field(state, place.symbol, input, node.span);
                        return remaining(std::move(state));
                    }
                }
                if ((!init && (node.operands.size() != 2 || node.operands[0].op != IrOp::Load)) ||
                    (init && node.operands.size() > 1))
                    session.forbidden(node.span, "writes through a non-local binding");
                const auto destination = init ? node.symbol : node.operands.front().symbol;
                const auto& decl = session.symbol(destination, node.span);
                if (decl.kind != SymbolKind::Local && decl.kind != SymbolKind::Parameter)
                    session.forbidden(node.span, "writes a global or object field");
                const auto assigned = node.operands.empty() ? no_value : pure_value(node.operands.back(), state, mode);
                state.locals[destination] = assigned;
                return remaining(std::move(state));
            }
            case IrOp::Block:
                return statements(node.body, 0, node.body.size(), std::move(state), mode, remaining);
            case IrOp::Branch: {
                if (node.operands.size() != 1) ill_typed(node.span, "malformed branch in law call");
                const auto condition = pure_value(node.operands.front(), state, mode);
                const auto run = [&](bool yes) {
                    auto local = state;
                    specialize_condition(local.scope, condition, yes);
                    const auto& region = yes ? node.body : node.alternative;
                    return statements(region, 0, region.size(), std::move(local), mode, remaining);
                };
                if (const auto test = known_boolean(condition, state.scope)) return run(normal(*test).index != 0);
                const auto yes = run(true);
                const auto no = run(false);
                return conditional(condition, yes, no);
            }
            case IrOp::Discard:
                if (node.operands.size() != 1) ill_typed(node.span, "malformed discard in law call");
                pure_value(node.operands.front(), state, mode);
                return remaining(std::move(state));
            case IrOp::Loop:
                throw Diagnostic(node.span, "imperative loops in laws require Phase 4 loop induction").with_code("E1413");
            default:
                pure_value(node, state, mode);
                return remaining(std::move(state));
            }
        }

        Value scoped(const std::vector<IrNode>& body, Execution state, Mode mode, const Span& at) {
            if (body.empty()) ill_typed(at, "missing result in law call");
            const auto finish = [&](Execution local) { return pure_value(body.back(), local, mode); };
            return statements(body, 0, body.size() - 1, std::move(state), mode, finish);
        }

        Value construct(const IrType& type, SymbolId init, const std::vector<Value>& inputs,
                        const Scope& scope, Mode mode, const Span& at) {
            require_type(type, at);
            if (type.kind != TypeKind::Object || !type.arguments.empty())
                ill_typed(at, "law construction requires a non-generic immutable class value");
            auto fields_to_initialize = std::vector<Value>(fields(type).size(), no_value);
            const auto receiver = make({Head::Constructor, type, type.symbol, no_symbol, 0,
                                        std::move(fields_to_initialize), true});
            return initialize_class(type.symbol, init, inputs, receiver, scope, mode, at);
        }

        SymbolId default_constructor(SymbolId cls) const {
            for (SymbolId id = 0; id < session.program.symbols.size(); ++id) {
                const auto& member = session.program.symbols[id];
                if (member.owner == cls && member.kind == SymbolKind::Function &&
                    member.name == "init" && member.parameters.empty()) return id;
            }
            return no_symbol;
        }

        Value initialize_class(SymbolId cls, SymbolId init, const std::vector<Value>& inputs,
                               Value receiver, const Scope& scope, Mode mode, const Span& at) {
            Guard guard(*this, at);
            const auto& declaration = session.symbol(cls, at);
            if (declaration.kind != SymbolKind::Class || !has_tag(declaration, TagId::Immutable) ||
                !declaration.generics.empty())
                ill_typed(at, "a law can construct only supported immutable class values");
            Execution state{scope, {}, receiver, cls};
            const std::vector<IrNode>* instructions = nullptr;
            if (init == no_symbol) init = default_constructor(cls);
            if (init != no_symbol) {
                const auto& function = session.symbol(init, at);
                const auto body = session.bodies.find(init);
                if (function.owner != cls || function.name != "init" || function.kind != SymbolKind::Function ||
                    function.parameters.size() != inputs.size() || function.variadic != VariadicKind::None ||
                    function.external || body == session.bodies.end() || body->second.size() != 1)
                    ill_typed(at, "invalid immutable constructor signature");
                instructions = &body->second.front()->instructions;
                for (std::size_t i = 0; i < inputs.size(); ++i) {
                    const auto parameter = function.parameters[i];
                    expect(inputs[i], session.symbol(parameter, at).type, at);
                    state.locals.emplace(parameter, inputs[i]);
                }
            } else if (!inputs.empty()) {
                ill_typed(at, "the implicit constructor does not accept arguments");
            }

            std::size_t start = 0;
            if (declaration.base != no_symbol && session.symbol(declaration.base, at).name != "$class") {
                std::vector<Value> arguments;
                auto selected = no_symbol;
                if (instructions) {
                    if (instructions->empty() || instructions->front().op != IrOp::BaseInitialize)
                        ill_typed(at, "a derived constructor must initialize its base first");
                    const auto& base = instructions->front();
                    selected = base.symbol;
                    for (const auto& argument : base.operands)
                        arguments.push_back(pure_value(argument, state, mode));
                    start = 1;
                }
                state.receiver = initialize_class(declaration.base, selected, arguments, state.receiver, scope, mode, at);
            }
            if (const auto body = session.bodies.find(cls); body != session.bodies.end()) {
                for (const auto* part : body->second) {
                    for (const auto& initializer : part->instructions) {
                        if (initializer.op != IrOp::Initialize || initializer.operands.size() != 1)
                            ill_typed(initializer.span, "unsupported initialization in an immutable constructor");
                        const auto input = pure_value(initializer.operands.front(), state, mode);
                        set_field(state, initializer.symbol, input, initializer.span);
                    }
                }
            }
            const auto finish = [&](Execution placed) {
                return constructed(placed, at);
            };
            return instructions
                ? statements(*instructions, start, instructions->size(), std::move(state), mode, finish)
                : finish(std::move(state));
        }

        Value execute(Function function, const std::vector<Value>& arguments, Scope scope, Mode mode, const Span& at) {
            scope.module = function.module;
            Execution state{std::move(scope), {}};
            const auto& declaration = session.symbol(function.symbol, at);
            const bool method = session.program.symbols[declaration.owner].kind == SymbolKind::Class;
            if (method) state.receiver = arguments.front();
            for (std::size_t i = 0; i < declaration.parameters.size(); ++i)
                state.locals.emplace(declaration.parameters[i], arguments[i + (method ? 1 : 0)]);
            const auto found = session.bodies.find(function.symbol);
            if (found == session.bodies.end() || found->second.size() != 1)
                ill_typed(at, "law call does not have one pure function body");
            const auto& nodes = found->second.front()->instructions;
            const auto fail = [&](Execution) -> Value { partial(at, "a function that may finish without a result"); };
            return statements(nodes, 0, nodes.size(), std::move(state), mode, fail);
        }

        bool allow_abstract = false;

        void record_unfold(SymbolId id) {
            ++unfolded[id];
            const auto& symbol = session.program.symbols[id];
            if ((symbol.kind == SymbolKind::Function || symbol.kind == SymbolKind::Global) &&
                !has_tag(symbol, TagId::Final) &&
                symbol.owner < session.program.symbols.size() &&
                session.program.symbols[symbol.owner].kind == SymbolKind::Module &&
                !session.program.symbols[symbol.owner].constant)
                footprint.insert(id);
        }

        // Do not run module startup, inline a first-use computation, or rely
        // on an initialized global unless its written value is itself a
        // checked tree of constants from the Phase 1 value language.
        Value literal(const IrNode& node) {
            Guard guard(*this, node.span);
            require_type(node.type, node.span);
            switch (node.op) {
            case IrOp::Boolean:
                if (node.operands.empty() && (node.text == "true" || node.text == "false"))
                    return boolean(node.text == "true");
                break;
            case IrOp::Nil:
                if (node.operands.empty() && node.type.kind == TypeKind::Object)
                    return make({Head::Nil, node.type});
                break;
            case IrOp::Reference:
                if (node.symbol < session.program.symbols.size() && node.operands.empty() &&
                    session.program.symbols[node.symbol].kind == SymbolKind::Variant &&
                    payload(node.type, node.symbol, node.span).empty())
                    return make({Head::Constructor, node.type, node.symbol});
                break;
            case IrOp::Tuple:
            case IrOp::EnumConstruct: {
                const auto types = node.op == IrOp::Tuple ? node.type.arguments : payload(node.type, node.symbol, node.span);
                std::vector<Value> parts(types.size());
                const auto order = enum_field_order(node.text, parts.size());
                if (!order || parts.size() != node.operands.size())
                    ill_typed(node.span, "malformed literal constructor in law constant");
                for (std::size_t i = 0; i < parts.size(); ++i) {
                    const auto field = (*order)[i];
                    parts[field] = literal(node.operands[i]);
                    expect(parts[field], types[field], node.operands[i].span);
                }
                return make({Head::Constructor, node.type, node.symbol, no_symbol, 0, std::move(parts)});
            }
            default:
                break;
            }
            session.forbidden(node.span, "reads a constant whose initializer is not a Phase 1 literal value");
        }

        Value constant(SymbolId declared, SymbolId receiver, const IrType& type, const Scope& scope, const Span& at,
                       bool unfold) {
            const auto& item = session.symbol(declared, at);
            if (item.kind != SymbolKind::Global || !item.constant || item.type != type)
                session.forbidden(at, "reads storage that is not a compile-time constant");
            const auto static_value = std::ranges::find(session.program.static_constants, declared, &IrStaticConstant::field);
            if (static_value != session.program.static_constants.end()) return literal(static_value->value);
            SymbolId binding = declared;
            if (session.is_module(item.owner)) {
                if (receiver == no_symbol)
                    receiver = session.derives(scope.module, item.owner) ? scope.module : item.owner;
                if (!session.is_module(receiver) || !session.derives(receiver, item.owner))
                    ill_typed(at, "module receiver does not provide constant '" + item.name + "'");
                binding = session.target(declared, receiver);
            }
            const auto& actual = session.symbol(binding, at);
            if (actual.kind != SymbolKind::Global || !actual.constant || actual.type != type)
                session.forbidden(at, "reads an overridden binding that is not a compile-time constant");
            const auto initial = session.initializers.find(binding);
            if (initial == session.initializers.end() || initial->second.size() != 1)
                session.forbidden(at, "reads a constant without one known initializer");
            const auto result = literal(*initial->second.front());
            expect(result, type, at);
            if (unfold) record_unfold(binding);
            return result;
        }

        Value call(SymbolId declared, SymbolId receiver, const IrType& result, std::vector<Value> inputs,
                   const Scope& scope, const Span& at, bool validate = true, bool require_tag = true) {
            const auto& function = session.symbol(declared, at);
            const auto& owner = session.symbol(function.owner, at);
            const bool method = owner.kind == SymbolKind::Class;
            const std::size_t first = method ? 1 : 0;
            if (function.kind != SymbolKind::Function || !function.generics.empty() ||
                function.variadic != VariadicKind::None || function.type != result ||
                function.parameters.size() + first != inputs.size())
                ill_typed(at, "wrong callable signature in law for '" + function.name + "'");
            if (require_tag && !has_tag(function, TagId::Pure))
                session.forbidden(at, "calls '" + function.name + "' without a checked .Pure declaration", declared);
            require_type(result, at);
            for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                const auto& argument = session.symbol(function.parameters[i], at);
                expect(inputs[i + first], argument.type, at);
            }
            SymbolId target = declared;
            if (owner.kind == SymbolKind::Module) {
                if (receiver == no_symbol)
                    receiver = session.derives(scope.module, function.owner) ? scope.module : function.owner;
                if (!session.is_module(receiver) || !session.derives(receiver, function.owner))
                    ill_typed(at, "module receiver does not provide '" + function.name + "'");
                target = session.target(declared, receiver);
            } else if (method) {
                const auto& type = normal(inputs.front()).type;
                require_type(type, at);
                if (type.kind != TypeKind::Object || !session.derives(type.symbol, function.owner) ||
                    receiver != no_symbol || !owner.generics.empty())
                    ill_typed(at, "method has an incompatible immutable receiver");
                if (!non_nil(inputs.front(), scope)) partial(at, "a method call on a possibly nil receiver");
                if (!session.dispatch) session.dispatch = std::make_unique<DispatchTargets>(session.program);
                const auto& targets = session.dispatch->members(type.symbol, function.name);
                if (targets.size() != 1 || targets.front() != declared || type.symbol != function.owner)
                    ill_typed(at, "Phase 1 laws require a statically resolved immutable method");
                receiver = session.module_of(function.owner);
            } else if (owner.kind != SymbolKind::File || receiver != no_symbol) {
                ill_typed(at, "this function receiver is not supported in a Phase 1 law");
            }
            const auto& actual = session.symbol(target, at);
            if (actual.kind != SymbolKind::Function || actual.type != result ||
                actual.parameters.size() + first != inputs.size() ||
                !actual.generics.empty() || actual.variadic != VariadicKind::None)
                ill_typed(at, "overridden function has an incompatible law signature");
            for (std::size_t i = 0; i < actual.parameters.size(); ++i)
                expect(inputs[i + first], session.symbol(actual.parameters[i], at).type, at);
            if (actual.host_binding || actual.external)
                session.forbidden(at, "calls external function '" + actual.name + "'", target);
            if (!actual.has_body) {
                if (!allow_abstract)
                    partial(at, "the bodyless function '" + actual.name + "' without a termination guarantee");
            } else if (validate) {
                validate_function(function_key(target, receiver, inputs, scope), inputs, scope, at);
            }
            return make({Head::Call, result, target, receiver, 0, std::move(inputs)});
        }

        Function function_key(SymbolId symbol, SymbolId module, const std::vector<Value>& inputs, const Scope& scope) const {
            Function key{symbol, module, {}};
            for (const auto input : inputs) key.non_nil.push_back(non_nil(input, scope));
            return key;
        }

        void check_termination() {
            // A non-decreasing edge is admissible only when it cannot take
            // part in a cycle. Include calls to already visited functions:
            // otherwise a later path could hide a growing recursive edge.
            for (const auto& [source, edges] : function_edges) {
                for (const auto& edge : edges) {
                    if (edge.decreasing) continue;
                    std::vector<Function> pending{edge.target};
                    std::set<Function> visited;
                    while (!pending.empty()) {
                        tick(edge.span);
                        const auto current = pending.back();
                        pending.pop_back();
                        if (current == source)
                            throw Diagnostic(edge.span, "recursive call to '" + session.name(edge.target.symbol) +
                                                       "' does not decrease to a structural subterm").with_code("E1413");
                        if (!visited.insert(current).second) continue;
                        const auto neighbors = function_edges.find(current);
                        if (neighbors == function_edges.end()) continue;
                        for (const auto& next : neighbors->second) pending.push_back(next.target);
                    }
                }
            }
        }

        void validate_function(Function key, const std::vector<Value>& inputs, const Scope& scope, const Span& at) {
            if (!functions.empty()) {
                const auto& caller = functions.back();
                function_edges[caller.key].push_back({key, decreases(inputs, caller.arguments, scope), at});
            }
            if (validated.contains(key) || std::ranges::find(functions, key, &ActiveFunction::key) != functions.end())
                return;
            const auto& symbol = session.program.symbols[key.symbol];
            if (!session.purity) session.purity = std::make_unique<PurityAnalysis>(session.program);
            if (!session.purity->pure(key.symbol))
                session.forbidden(at, "calls a function that is not pure", key.symbol);
            std::vector<Value> open_inputs;
            for (std::size_t i = 0; i < inputs.size(); ++i)
                open_inputs.push_back(open(normal(inputs[i]).type, key.non_nil[i]));
            functions.push_back({key, open_inputs});
            const auto checked = execute(key, open_inputs, Scope{key.module}, Mode::Validate, at);
            expect(checked, symbol.type, at);
            functions.pop_back();
            validated.insert(key);
            if (functions.empty()) check_termination();
        }

        Value immediate(Value id, const Scope& scope, const Span& at) {
            Guard guard(*this, at);
            id = known(id, scope);
            const auto head = normal(id);
            switch (head.head) {
            case Head::Call:
                if (!session.program.symbols[head.symbol].has_body) return id;
                record_unfold(head.symbol);
                return known(execute(function_key(head.symbol, head.receiver, head.parts, scope), head.parts, scope, Mode::Unfold, at), scope);
            case Head::If: {
                const auto condition = whnf(head.parts[0], scope, at);
                return conditional(condition, head.parts[1], head.parts[2]);
            }
            case Head::Not:
                return negate(whnf(head.parts[0], scope, at));
            case Head::Equal: {
                const auto left = whnf(head.parts[0], scope, at);
                const auto right = whnf(head.parts[1], scope, at);
                return value_eq(left, right, scope);
            }
            case Head::EnumTag:
                return enum_tag(whnf(head.parts[0], scope, at), scope);
            case Head::Project: {
                const auto receiver = known(head.parts[0], scope);
                if (normal(receiver).head == Head::Constructor)
                    return project(receiver, head.index, head.symbol, head.type, scope, at);
                return id;
            }
            default:
                return id;
            }
        }

        Value whnf(Value current, const Scope& scope, const Span& at) {
            Guard guard(*this, at);
            for (;;) {
                const auto next = immediate(current, scope, at);
                if (next == current) return current;
                current = next;
            }
        }

        bool equivalent(Value left, Value right, const Scope& scope, const Span& at) {
            Guard guard(*this, at);
            left = known(left, scope);
            right = known(right, scope);
            if (left == right) return true;
            const auto a = normal(left);
            const auto b = normal(right);
            if (a.type != b.type) return false;
            if (a.head == Head::If && b.head == Head::If && equivalent(a.parts[0], b.parts[0], scope, at)) {
                const auto condition = whnf(a.parts[0], scope, at);
                if (normal(condition).head == Head::Boolean) {
                    const std::size_t branch = normal(condition).index ? 1 : 2;
                    return equivalent(a.parts[branch], b.parts[branch], scope, at);
                }
                auto when_true = scope;
                specialize_condition(when_true, a.parts[0], true);
                specialize_condition(when_true, b.parts[0], true);
                auto when_false = scope;
                specialize_condition(when_false, a.parts[0], false);
                specialize_condition(when_false, b.parts[0], false);
                return equivalent(a.parts[1], b.parts[1], when_true, at) &&
                       equivalent(a.parts[2], b.parts[2], when_false, at);
            }
            if (a.head == b.head && a.symbol == b.symbol && a.receiver == b.receiver && a.index == b.index &&
                a.parts.size() == b.parts.size()) {
                bool equal = true;
                for (std::size_t i = 0; i < a.parts.size() && equal; ++i)
                    equal = equivalent(a.parts[i], b.parts[i], scope, at);
                if (equal) return true;
            }
            const auto reduced_left = whnf(left, scope, at);
            const auto reduced_right = whnf(right, scope, at);
            if (left == reduced_left && right == reduced_right) return false;
            return equivalent(reduced_left, reduced_right, scope, at);
        }

        bool equivalent(const Proposition& a, const Proposition& b, const Scope& scope, const Span& at) {
            Guard guard(*this, at);
            if (a.kind != b.kind || a.type != b.type || a.parts.size() != b.parts.size()) return false;
            if (a.kind == Term::Equal)
                return equivalent(a.left, b.left, scope, at) && equivalent(a.right, b.right, scope, at);
            if (a.body && b.body) {
                const auto common = open(a.type, a.kind == Term::Forall);
                const auto left = instantiate(a, common, scope, at);
                const auto right = instantiate(b, common, scope, at);
                return equivalent(left, right, scope, at);
            }
            for (std::size_t i = 0; i < a.parts.size(); ++i)
                if (!equivalent(a.parts[i], b.parts[i], scope, at)) return false;
            return true;
        }

        std::string show(Value id, const Scope& scope, const Span& at, std::size_t level = 0) {
            if (level >= 16) return "…";
            const auto value = normal(whnf(id, scope, at));
            switch (value.head) {
            case Head::Boolean: return value.index ? "true" : "false";
            case Head::Nil: return "nil";
            case Head::Open: return "value " + std::to_string(value.index + 1);
            case Head::Index: return std::to_string(value.index);
            case Head::Project:
                // A case refinement can itself contain a projection of the
                // refined object. Keep that cyclic display shallow.
                return "field " + std::to_string(value.index + 1);
            default:
                break;
            }
            std::string result;
            if (value.symbol != no_symbol) result = session.name(value.symbol);
            else if (value.head == Head::If) result = "if";
            else if (value.head == Head::Not) result = "not";
            else if (value.head == Head::Equal) result = "==";
            else if (value.head == Head::EnumTag) result = "tag";
            if (value.parts.empty()) return result;
            result += '(';
            for (std::size_t i = 0; i < value.parts.size(); ++i) {
                if (i) result += ", ";
                result += show(value.parts[i], scope, at, level + 1);
            }
            return result + ')';
        }

        std::string show(const Proposition& proposition, const Scope& scope, const Span& at) {
            switch (proposition.kind) {
            case Term::Equal: {
                const auto left = show(proposition.left, scope, at);
                const auto right = show(proposition.right, scope, at);
                return "{ " + left + " == " + right + " : " + show_type(proposition.type) + " }";
            }
            case Term::Empty: return "Empty";
            case Term::All: return "a conjunction of " + std::to_string(proposition.parts.size()) + " claims";
            case Term::Either: return "a disjunction";
            case Term::Implies: return "an implication";
            case Term::Exists: return "an existential over " + show_type(proposition.type);
            default: return "a universal claim over " + show_type(proposition.type);
            }
        }

        void require_goal(const Proposition& goal, const Proposition& offered, const Scope& scope, const Span& at) {
            if (equivalent(goal, offered, scope, at)) return;
            ill_typed(at, "expected " + show(goal, scope, at) + ", found " + show(offered, scope, at));
        }

        std::string difference(Value a, Value b, const Scope& scope, const Span& at) {
            Guard guard(*this, at);
            const auto left = normal(whnf(a, scope, at));
            const auto right = normal(whnf(b, scope, at));
            if (left.head == right.head && left.symbol == right.symbol && left.index == right.index &&
                left.parts.size() == right.parts.size()) {
                for (std::size_t i = 0; i < left.parts.size(); ++i) {
                    if (equivalent(left.parts[i], right.parts[i], scope, at)) continue;
                    return difference(left.parts[i], right.parts[i], scope, at);
                }
            }
            const auto lhs = show(a, scope, at);
            const auto rhs = show(b, scope, at);
            return lhs + " differs from " + rhs;
        }

        void reflexivity(const Proposition& goal, const Scope& scope, const Span& at) {
            if (goal.kind != Term::Equal)
                ill_typed(at, "{==} requires an equality goal, found " + show(goal, scope, at));
            if (equivalent(goal.left, goal.right, scope, at)) return;
            const auto left = show(goal.left, scope, at);
            const auto right = show(goal.right, scope, at);
            auto error = Diagnostic(at, "reflexivity failed: " + left + " differs from " + right)
                .with_code("E1401").note("goal: " + show(goal, scope, at))
                .note("first difference: " + difference(goal.left, goal.right, scope, at));
            for (const auto& [from, to] : scope.refinements) {
                const auto empty = Scope{scope.module};
                error.note("case refinement: " + show(from, empty, at) + " = " + show(to, scope, at));
            }
            throw error;
        }

        [[noreturn]] void incomplete(const Proposition& goal, const Scope& scope, const Span& at) {
            auto error = Diagnostic(at, "unproven goal at the end of the proof").with_code("E1403")
                .note("remaining goal: " + show(goal, scope, at));
            for (const auto& [binding, item] : scope.assumptions)
                error.note("hypothesis " + std::to_string(binding + 1) + ": " + show(item, scope, at));
            throw error;
        }

        const Proposition& assumption(const LawProof& proof, const Scope& scope) const {
            shape(proof, 0, 0);
            const auto found = scope.assumptions.find(proof.index);
            if (found == scope.assumptions.end()) ill_typed(proof.span, "unbound hypothesis in law proof");
            return found->second;
        }

        Proposition apply(const LawProof& proof, const Scope& scope) {
            const auto& declaration = session.symbol(proof.symbol, proof.span);
            if (declaration.kind != SymbolKind::Law) ill_typed(proof.span, "proof application requires a law");
            auto module = proof.receiver;
            if (module == no_symbol)
                module = session.derives(scope.module, declaration.owner) ? scope.module : declaration.owner;
            if (!session.derives(module, declaration.owner) || !session.is_module(module))
                ill_typed(proof.span, "the proof's receiver does not provide law '" + declaration.name + "'");
            if (!declaration.exported && !session.derives(scope.module, declaration.owner))
                ill_typed(proof.span, "law '" + declaration.name + "' is private to its declaring module");
            const auto& record = session.find_law(proof.symbol, module, proof.span);
            auto statement_scope = scope;
            statement_scope.module = module;
            statement_scope.values.clear();
            statement_scope.assumptions.clear();
            auto goal = form(record.statement, statement_scope);
            std::vector<Value> arguments;
            std::size_t used_proofs = 0;
            std::size_t used_values = 0;
            for (std::uint32_t i = 0; i < record.introductions; ++i) {
                if (quantifier(goal.kind)) {
                    if (used_values == proof.terms.size())
                        ill_typed(proof.span, "law '" + declaration.name + "' needs another quantified argument");
                    const auto input = value(proof.terms[used_values++], scope);
                    goal = instantiate(goal, input, scope, proof.span);
                    arguments.push_back(input);
                } else if (goal.kind == Term::Implies) {
                    if (used_proofs == proof.proofs.size())
                        ill_typed(proof.span, "law '" + declaration.name + "' needs a witness for its given clause");
                    prove(proof.proofs[used_proofs++], goal.parts[0], scope);
                    auto consequent = goal.parts[1];
                    goal = std::move(consequent);
                } else {
                    ill_typed(proof.span, "invalid leading binder count for law '" + declaration.name + "'");
                }
            }
            if (used_values != proof.terms.size() || used_proofs != proof.proofs.size())
                ill_typed(proof.span, "too many arguments for law '" + declaration.name + "'");
            if (frame.law == proof.symbol && frame.module == module) {
                if (!decreases(arguments, frame.arguments, scope))
                    throw Diagnostic(proof.span, "recursive law application does not decrease to a structural subterm")
                        .with_code("E1413").label("a quantified enum argument must be a matched payload subterm");
            } else {
                if (record.status == LawStatus::Abstract)
                    throw Diagnostic(proof.span, "law '" + declaration.name + "' has no proof in this module")
                        .with_code("E1404").label(declaration.span, "this law requires a proof");
                const auto dependencies = session.check(record, module, proof.span);
                footprint.insert(dependencies.begin(), dependencies.end());
            }
            return goal;
        }

        Proposition evidence(const LawProof& proof, const Scope& scope) {
            Guard guard(*this, proof.span);
            switch (proof.kind) {
            case Step::Assumption:
                return assumption(proof, scope);
            case Step::Apply:
                return apply(proof, scope);
            case Step::Sym: {
                shape(proof, 0, 1);
                const auto fact = evidence(proof.proofs[0], scope);
                if (fact.kind != Term::Equal) ill_typed(proof.span, "sym requires an equality witness");
                return equality(fact.right, fact.left);
            }
            case Step::Trans: {
                shape(proof, 0, 2);
                const auto first = evidence(proof.proofs[0], scope);
                const auto second = evidence(proof.proofs[1], scope);
                if (first.kind != Term::Equal || second.kind != Term::Equal ||
                    !equivalent(first.right, second.left, scope, proof.span))
                    ill_typed(proof.span, "trans requires equalities with the same middle term");
                return equality(first.left, second.right);
            }
            case Step::Cong: {
                shape(proof, 1, 1);
                const auto fact = evidence(proof.proofs[0], scope);
                if (fact.kind != Term::Equal) ill_typed(proof.span, "cong requires an equality witness");
                const auto left = value(proof.terms[0], scope, fact.left);
                const auto right = value(proof.terms[0], scope, fact.right);
                return equality(left, right);
            }
            default:
                ill_typed(proof.span, "this proof step needs a goal before it can be checked");
            }
        }

        bool has_hole(const LawTerm& term) const {
            if (term.kind == Term::Hole) return true;
            return std::ranges::any_of(term.terms, [&](const LawTerm& child) { return has_hole(child); });
        }

        Proposition rewrite(const LawProof& proof, const Proposition& goal, const Scope& scope) {
            shape(proof, 1, 1);
            if (!has_hole(proof.terms[0]))
                throw Diagnostic(proof.span, "rewrite requires an explicit motive with a '_' hole").with_code("E1402");
            const auto fact = evidence(proof.proofs[0], scope);
            if (fact.kind != Term::Equal)
                ill_typed(proof.span, "rewrite requires an equality witness, found " + show(fact, scope, proof.span));
            const auto before = proof.kind == Step::Rewrite ? fact.left : fact.right;
            const auto after = proof.kind == Step::Rewrite ? fact.right : fact.left;
            const auto expected = form(proof.terms[0], scope, before);
            if (!equivalent(goal, expected, scope, proof.span)) {
                auto error = Diagnostic(proof.span, "rewrite motive does not match the current goal")
                    .with_code("E1402").note("witness: " + show(fact, scope, proof.span))
                    .note("instantiated motive: " + show(expected, scope, proof.span))
                    .note("current goal: " + show(goal, scope, proof.span));
                if (goal.kind == Term::Equal && expected.kind == Term::Equal && goal.type == expected.type) {
                    const bool left_matches = equivalent(goal.left, expected.left, scope, proof.span);
                    error.note("first difference: " + (left_matches
                        ? difference(goal.right, expected.right, scope, proof.span)
                        : difference(goal.left, expected.left, scope, proof.span)));
                }
                throw error;
            }
            return form(proof.terms[0], scope, after);
        }

        void inject(const LawProof& proof, Scope& scope) {
            shape(proof, 0, 1);
            const auto fact = evidence(proof.proofs[0], scope);
            if (fact.kind != Term::Equal)
                throw Diagnostic(proof.span, "inject requires a constructor equality witness, found " +
                                             show(fact, scope, proof.span)).with_code("E1408");
            const auto left = normal(whnf(fact.left, scope, proof.span));
            const auto right = normal(whnf(fact.right, scope, proof.span));
            if (left.head != Head::Constructor || right.head != Head::Constructor || left.symbol != right.symbol ||
                left.parts.size() != right.parts.size() || left.parts.size() != proof.bindings.size() ||
                proof.bindings.empty())
                throw Diagnostic(proof.span, "inject requires equal constructors and one binding for each payload")
                    .with_code("E1408").note("witness: " + show(fact, scope, proof.span));
            for (std::size_t i = 0; i < proof.bindings.size(); ++i)
                assume(scope, proof.bindings[i], equality(left.parts[i], right.parts[i]), proof.span);
        }

        bool contradicts(const Proposition& fact, const Scope& scope, const Span& at) {
            if (fact.kind != Term::Equal) return false;
            const auto left = normal(whnf(fact.left, scope, at));
            const auto right = normal(whnf(fact.right, scope, at));
            if (left.head != right.head) return false;
            if (left.head == Head::Boolean) return left.index != right.index;
            return left.head == Head::Constructor && left.symbol != right.symbol;
        }

        void proof_match(const LawProof& proof, const Proposition& goal, const Scope& scope) {
            if (proof.terms.empty()) {
                if (proof.proofs.empty()) ill_typed(proof.span, "proof match is missing its subject");
                const auto fact = evidence(proof.proofs.front(), scope);
                if (fact.kind != Term::Either) ill_typed(proof.span, "proof match requires a disjunction witness");
                std::array<bool, 2> covered{};
                for (std::size_t i = 1; i < proof.proofs.size(); ++i) {
                    const auto& arm = proof.proofs[i];
                    shape(arm, 0, 1);
                    if (arm.kind != Step::Arm || arm.index > 1 || covered[arm.index] || arm.bindings.size() != 1)
                        ill_typed(arm.span, "expected one Left or Right disjunction arm with a proof binder");
                    covered[arm.index] = true;
                    auto branch = scope;
                    assume(branch, arm.bindings.front(), fact.parts[arm.index], arm.span);
                    prove(arm.proofs[0], goal, branch);
                }
                if (!covered[0] || !covered[1])
                    throw Diagnostic(proof.span, std::string("non-exhaustive proof match, missing ") +
                                                 (!covered[0] ? "Left" : "Right")).with_code("E1407");
                return;
            }
            if (proof.terms.size() != 1) ill_typed(proof.span, "proof match requires one scrutinee");
            const auto subject = value(proof.terms[0], scope);
            const auto type = normal(subject).type;
            const auto inspected = whnf(subject, scope, proof.span);
            const auto inspected_head = normal(inspected);
            const bool boolean_match = type == bool_type;
            if (!boolean_match && type.kind != TypeKind::Enum)
                ill_typed(proof.terms[0].span, "proof match requires bool or enum, found " + show_type(type));
            auto choices = boolean_match ? std::vector<SymbolId>{0, 1} : variants(type);
            if (inspected_head.head == Head::Boolean) choices = {inspected_head.index};
            if (inspected_head.head == Head::Constructor) choices = {inspected_head.symbol};
            if (const auto found = scope.excluded.find(inspected); found != scope.excluded.end())
                std::erase_if(choices, [&](SymbolId item) { return found->second.contains(item); });
            std::set<SymbolId> covered;
            const LawProof* fallback = nullptr;
            for (const auto& arm : proof.proofs) {
                shape(arm, 0, 1);
                if (arm.kind != Step::Arm) ill_typed(arm.span, "expected a proof match arm");
                if (arm.index == law_wildcard_arm) {
                    if (fallback || !arm.bindings.empty()) ill_typed(arm.span, "invalid wildcard proof match arm");
                    fallback = &arm;
                    continue;
                }
                const auto variant = boolean_match ? arm.index : arm.symbol;
                const auto types = boolean_match ? std::vector<IrType>{} : payload(type, variant, arm.span);
                if ((boolean_match && (arm.index > 1 || arm.symbol != no_symbol)) ||
                    arm.bindings.size() != types.size() || !covered.insert(variant).second)
                    ill_typed(arm.span, "invalid or repeated proof match arm");
                if (std::ranges::find(choices, variant) == choices.end()) continue;
                auto branch = scope;
                if (boolean_match) {
                    specialize_condition(branch, subject, variant != 0);
                    specialize_condition(branch, inspected, variant != 0);
                } else {
                    refine_constructor(branch, inspected, variant, true);
                    if (subject != inspected) refine(branch, subject, known(inspected, branch));
                    for (std::size_t i = 0; i < types.size(); ++i) {
                        if (arm.bindings[i] == law_no_binding) continue;
                        bind(branch, arm.bindings[i], project(inspected, i, variant, types[i], branch, arm.span), arm.span);
                    }
                }
                prove(arm.proofs[0], goal, branch);
            }
            for (const auto variant : choices) {
                if (covered.contains(variant)) continue;
                if (!fallback) {
                    const auto name = boolean_match ? (variant ? "true" : "false") : session.name(variant);
                    throw Diagnostic(proof.span, "non-exhaustive proof match, missing " + name).with_code("E1407");
                }
                auto branch = scope;
                if (boolean_match) {
                    specialize_condition(branch, subject, variant != 0);
                    specialize_condition(branch, inspected, variant != 0);
                } else {
                    refine_constructor(branch, inspected, variant, true);
                    if (subject != inspected) refine(branch, subject, known(inspected, branch));
                }
                prove(fallback->proofs[0], goal, branch);
            }
        }

        void proof_if(const LawProof& proof, const Proposition& goal, const Scope& scope) {
            shape(proof, 1, 2);
            const auto condition = value(proof.terms[0], scope);
            expect(condition, bool_type, proof.span);
            const auto reduced = whnf(condition, scope, proof.span);
            for (const bool matches : {true, false}) {
                if (normal(reduced).head == Head::Boolean && (normal(reduced).index != 0) != matches) continue;
                auto branch = scope;
                specialize_condition(branch, condition, matches);
                specialize_condition(branch, reduced, matches);
                assume(branch, proof.index, equality(condition, boolean(matches)), proof.span);
                prove(proof.proofs[matches ? 0 : 1], goal, branch);
            }
        }

        std::vector<Value> domain(const IrType& type) {
            if (type == bool_type) return {boolean(false), boolean(true)};
            if (type.kind == TypeKind::Enum && !has_enum_payload(session.program, type.symbol)) {
                std::vector<Value> result;
                for (const auto variant : variants(type)) result.push_back(make({Head::Constructor, type, variant}));
                return result;
            }
            return {};
        }

        std::optional<bool> closed_truth(const Proposition& proposition, const Scope& scope, const Span& at) {
            Guard guard(*this, at);
            switch (proposition.kind) {
            case Term::Equal: {
                if (equivalent(proposition.left, proposition.right, scope, at)) return true;
                if (contradicts(proposition, scope, at)) return false;
                return std::nullopt;
            }
            case Term::Empty:
                return false;
            case Term::Implies: {
                const auto premise = closed_truth(proposition.parts[0], scope, at);
                if (premise == false) return true;
                const auto result = closed_truth(proposition.parts[1], scope, at);
                if (premise == true || result == true) return result;
                return std::nullopt;
            }
            case Term::All:
            case Term::Either: {
                const bool conjunction = proposition.kind == Term::All;
                bool unknown = false;
                for (const auto& part : proposition.parts) {
                    const auto result = closed_truth(part, scope, at);
                    if (result == !conjunction) return !conjunction;
                    if (!result) unknown = true;
                }
                if (unknown) return std::nullopt;
                return conjunction;
            }
            case Term::Forall:
            case Term::NullableForall:
            case Term::Exists: {
                const auto choices = domain(proposition.type);
                if (choices.empty()) return std::nullopt;
                const bool universal = quantifier(proposition.kind);
                bool unknown = false;
                for (const auto alternative : choices) {
                    const auto inner = instantiate(proposition, alternative, scope, at);
                    const auto result = closed_truth(inner, scope, at);
                    if (result == !universal) return !universal;
                    if (!result) unknown = true;
                }
                if (unknown) return std::nullopt;
                return universal;
            }
            default:
                return std::nullopt;
            }
        }

        void decide(const Proposition& goal, const Scope& scope, std::size_t index, const Span& at) {
            Guard guard(*this, at);
            if (index < frame.arguments.size()) {
                const auto argument = frame.arguments[index];
                if (normal(known(argument, scope)).head != Head::Open)
                    return decide(goal, scope, index + 1, at);
                const auto choices = domain(normal(argument).type);
                if (choices.empty())
                    ill_typed(at, "decide requires finite bool or fieldless enum arguments in Phase 1");
                for (const auto candidate : choices) {
                    auto branch = scope;
                    refine(branch, argument, candidate);
                    decide(goal, branch, index + 1, at);
                }
                return;
            }
            for (const auto& [_, hypothesis] : scope.assumptions)
                if (closed_truth(hypothesis, scope, at) == false) return;
            const auto result = closed_truth(goal, scope, at);
            if (result == true) return;
            if (!result) ill_typed(at, "decide could not reduce the claim on this finite case");
            auto error = Diagnostic(at, "decide found a counterexample to the law").with_code("E1401")
                .note("remaining claim: " + show(goal, scope, at));
            for (std::size_t i = 0; i < frame.arguments.size(); ++i)
                error.note("argument " + std::to_string(i + 1) + " = " + show(frame.arguments[i], scope, at));
            throw error;
        }

        void calc(const LawProof& proof, const Proposition& goal, const Scope& scope) {
            if (goal.kind != Term::Equal || proof.proofs.empty() || proof.terms.size() != proof.proofs.size() + 1)
                ill_typed(proof.span, "calc requires an equality goal and at least one justified step");
            auto previous = value(proof.terms.front(), scope);
            expect(previous, goal.type, proof.terms.front().span);
            if (!equivalent(goal.left, previous, scope, proof.span))
                ill_typed(proof.terms.front().span, "calc starts with a different value from the goal");
            for (std::size_t i = 0; i < proof.proofs.size(); ++i) {
                const auto next = value(proof.terms[i + 1], scope);
                expect(next, goal.type, proof.terms[i + 1].span);
                prove(proof.proofs[i], equality(previous, next), scope);
                previous = next;
            }
            if (!equivalent(goal.right, previous, scope, proof.span))
                ill_typed(proof.terms.back().span, "calc ends with a different value from the goal");
        }

        void prove(const LawProof& proof, const Proposition& goal, const Scope& scope) {
            prove_sequence(std::span<const LawProof>(&proof, 1), goal, scope, proof.span);
        }

        void prove_sequence(std::span<const LawProof> steps, Proposition goal, Scope scope, const Span& end) {
            Guard guard(*this, end);
            for (std::size_t i = 0; i < steps.size(); ++i) {
                const auto& proof = steps[i];
                tick(proof.span);
                switch (proof.kind) {
                case Step::Rewrite:
                case Step::RewriteReverse:
                    goal = rewrite(proof, goal, scope);
                    continue;
                case Step::Local:
                    shape(proof, 1, 0);
                    value(proof.terms.front(), scope);
                    continue;
                case Step::Have: {
                    shape(proof, 1, 1);
                    auto fact = form(proof.terms[0], scope);
                    prove(proof.proofs[0], fact, scope);
                    assume(scope, proof.index, std::move(fact), proof.span);
                    continue;
                }
                case Step::Inject:
                    inject(proof, scope);
                    continue;
                case Step::Witness:
                    shape(proof, 1, 0);
                    if (goal.kind != Term::Exists) ill_typed(proof.span, "witness requires an existential goal");
                    goal = instantiate(goal, value(proof.terms[0], scope), scope, proof.span);
                    continue;
                case Step::Intro: {
                    shape(proof, 0, 0);
                    if (goal.kind == Term::Implies) {
                        assume(scope, proof.index, goal.parts[0], proof.span);
                        auto consequent = goal.parts[1];
                        goal = std::move(consequent);
                    } else if (quantifier(goal.kind)) {
                        const auto arbitrary = open(goal.type, goal.kind == Term::Forall);
                        goal = instantiate(goal, arbitrary, scope, proof.span);
                        bind(scope, proof.index, arbitrary, proof.span);
                    } else {
                        ill_typed(proof.span, "intro requires a universal or implication goal");
                    }
                    continue;
                }
                case Step::Block:
                    shape(proof, 0, proof.proofs.size());
                    prove_sequence(proof.proofs, goal, scope, proof.span);
                    break;
                case Step::Refl:
                    shape(proof, 0, 0);
                    reflexivity(goal, scope, proof.span);
                    break;
                case Step::Match:
                    proof_match(proof, goal, scope);
                    break;
                case Step::If:
                    proof_if(proof, goal, scope);
                    break;
                case Step::Absurd:
                case Step::Contradiction: {
                    shape(proof, 0, 1);
                    const auto fact = evidence(proof.proofs[0], scope);
                    const bool absurd = proof.kind == Step::Absurd;
                    if (absurd ? fact.kind != Term::Empty : !contradicts(fact, scope, proof.span)) {
                        throw Diagnostic(proof.span, std::string(absurd ? "absurd requires Empty" :
                            "contradiction requires equality between distinct constructors") + ", found " +
                            show(fact, scope, proof.span)).with_code("E1408");
                    }
                    break;
                }
                case Step::Split:
                    shape(proof, 0, goal.parts.size());
                    if (goal.kind != Term::All) ill_typed(proof.span, "split requires a conjunction goal");
                    for (std::size_t branch = 0; branch < proof.proofs.size(); ++branch)
                        prove(proof.proofs[branch], goal.parts[branch], scope);
                    break;
                case Step::ChooseLeft:
                case Step::ChooseRight:
                    shape(proof, 0, 1);
                    if (goal.kind != Term::Either) ill_typed(proof.span, "choose requires a disjunction goal");
                    prove(proof.proofs.front(), goal.parts[proof.kind == Step::ChooseLeft ? 0 : 1], scope);
                    break;
                case Step::Decide:
                    shape(proof, 0, 0);
                    decide(goal, scope, 0, proof.span);
                    break;
                case Step::Calc:
                    calc(proof, goal, scope);
                    break;
                default:
                    require_goal(goal, evidence(proof, scope), scope, proof.span);
                    break;
                }
                if (i + 1 != steps.size()) ill_typed(steps[i + 1].span, "proof step follows a completed goal");
                return;
            }
            incomplete(goal, scope, end);
        }

        std::vector<SymbolId> check(const IrLawRecord& record, SymbolId module) {
            const auto& law = session.symbol(record.law_symbol, record.statement.span);
            if (record.status == LawStatus::TrustedAxiom)
                throw Diagnostic(law.span, "trusted axioms are reserved for the Phase 2 standard library")
                    .with_code("E1420");
            allow_abstract = record.status == LawStatus::Abstract;
            frame = {record.law_symbol, module, {}};
            Scope scope{module};
            auto goal = form(record.statement, scope);
            for (std::uint32_t i = 0; i < record.introductions; ++i) {
                if (quantifier(goal.kind)) {
                    const auto binder = goal.binder;
                    const auto input = open(goal.type, goal.kind == Term::Forall);
                    goal = instantiate(goal, input, scope, law.span);
                    bind(scope, binder, input, law.span);
                    frame.arguments.push_back(input);
                } else if (goal.kind == Term::Implies) {
                    assume(scope, goal.binder, goal.parts[0], law.span);
                    auto consequent = goal.parts[1];
                    goal = std::move(consequent);
                } else {
                    ill_typed(law.span, "invalid leading binder count in law declaration");
                }
            }
            for (const auto& abbreviation : record.abbreviations)
                value(abbreviation.value, scope);
            if (allow_abstract) {
                if (record.certificate) ill_typed(law.span, "an abstract law must not carry a certificate");
            } else if (!record.certificate) {
                incomplete(goal, scope, law.span);
            } else {
                prove(*record.certificate, goal, scope);
            }
            return {footprint.begin(), footprint.end()};
        }
    };

    const IrProgram& program;
    std::size_t budget;
    std::unique_ptr<PurityAnalysis> purity;
    std::unique_ptr<DispatchTargets> dispatch;
    std::set<std::string> checked_immutable_layouts;
    std::set<SymbolId> reassigned_fields;
    std::set<SymbolId> exposed_constructors;
    bool inspected_writes = false;
    std::map<SymbolId, std::vector<const IrBody*>> bodies;
    std::map<SymbolId, std::vector<const IrNode*>> initializers;
    std::map<std::pair<SymbolId, std::string>, std::vector<SymbolId>> members;
    using Instance = std::pair<SymbolId, SymbolId>;
    std::vector<Instance> active;
    std::map<Instance, std::vector<SymbolId>> results;

    const IrSymbol& symbol(SymbolId id, const Span& at) const {
        if (id >= program.symbols.size()) ill_typed(at, "proof references an unknown NeuraIR symbol");
        return program.symbols[id];
    }

    std::string name(SymbolId id) const {
        return id < program.symbols.size() ? program.symbols[id].name : "unknown";
    }

    bool is_module(SymbolId id) const {
        return id < program.symbols.size() && program.symbols[id].kind == SymbolKind::Module;
    }

    SymbolId module_of(SymbolId owner) const {
        while (owner < program.symbols.size()) {
            if (is_module(owner)) return owner;
            owner = program.symbols[owner].owner;
        }
        return no_symbol;
    }

    bool derives(SymbolId scope, SymbolId owner) const {
        std::set<SymbolId> seen;
        for (; scope < program.symbols.size() && seen.insert(scope).second; scope = program.symbols[scope].base)
            if (scope == owner) return true;
        return false;
    }

    void inspect_field_writes() {
        inspected_writes = true;
        for (const auto& body : program.bodies) {
            if (body.owner >= program.symbols.size()) continue;
            const auto& owner = program.symbols[body.owner];
            std::vector<const IrNode*> pending;
            for (const auto& node : body.instructions) pending.push_back(&node);
            while (!pending.empty()) {
                const auto& node = *pending.back();
                pending.pop_back();
                if (node.op == IrOp::Store && !node.operands.empty()) {
                    const auto& field = node.operands[0];
                    if (field.op == IrOp::Field && field.symbol < program.symbols.size()) {
                        const auto& item = program.symbols[field.symbol];
                        const bool initializes = deferred_binding(item) && owner.kind == SymbolKind::Function &&
                            owner.name == "init" && owner.owner == item.owner && field.operands.size() == 1 &&
                            field.operands[0].op == IrOp::Receiver && field.operands[0].symbol == item.owner;
                        if (!initializes) reassigned_fields.insert(field.symbol);
                    }
                }
                if ((node.op == IrOp::Call || node.op == IrOp::VirtualCall || node.op == IrOp::MakeClosure ||
                     node.op == IrOp::Reference) && node.symbol < program.symbols.size()) {
                    const auto& target = program.symbols[node.symbol];
                    if (target.kind == SymbolKind::Function && target.name == "init" &&
                        target.owner < program.symbols.size() && program.symbols[target.owner].kind == SymbolKind::Class)
                        exposed_constructors.insert(target.owner);
                }
                for (const auto& nested : node.operands) pending.push_back(&nested);
                for (const auto& nested : node.body) pending.push_back(&nested);
                for (const auto& nested : node.alternative) pending.push_back(&nested);
            }
        }
    }

    bool immutable_layout(const IrType& type, std::set<SymbolId>& active_types, const Span& at) const {
        switch (type.kind) {
        case TypeKind::Bool:
        case TypeKind::String:
        case TypeKind::Integer:
        case TypeKind::Float:
            return true;
        case TypeKind::Tuple:
            return std::ranges::all_of(type.arguments,
                [&](const IrType& part) { return immutable_layout(part, active_types, at); });
        case TypeKind::Enum:
        case TypeKind::Object:
            break;
        default:
            return false;
        }
        if (type.symbol >= program.symbols.size()) return false;
        const auto& nominal = program.symbols[type.symbol];
        const auto kind = type.kind == TypeKind::Enum ? SymbolKind::Enum : SymbolKind::Class;
        if (nominal.kind != kind || nominal.generics.size() != type.arguments.size()) return false;
        for (const auto& argument : type.arguments)
            if (!immutable_layout(argument, active_types, at)) return false;
        if (!active_types.insert(type.symbol).second) return true;
        const auto close = [&] { active_types.erase(type.symbol); };
        if (type.kind == TypeKind::Enum) {
            for (const auto& variant : program.symbols) {
                if (variant.kind != SymbolKind::Variant || variant.owner != type.symbol) continue;
                for (const auto& item : variant.payload_types) {
                    if (!immutable_layout(substitute_type(item, nominal.generics, type.arguments), active_types, at))
                        return false;
                }
            }
        } else {
            for (SymbolId layout = 0; layout < program.symbols.size(); ++layout) {
                const auto& candidate = program.symbols[layout];
                if (candidate.kind != SymbolKind::Class || !derives(layout, type.symbol)) continue;
                if (!has_tag(candidate, TagId::Immutable) || exposed_constructors.contains(layout) ||
                    (layout != type.symbol && !candidate.generics.empty())) return false;
                if (effective_hook(program, layout, HookKind::Get) != no_symbol ||
                    effective_hook(program, layout, HookKind::Set) != no_symbol)
                    forbidden(at, "uses a hooked class reference");
                for (SymbolId id = 0; id < program.symbols.size(); ++id) {
                    const auto& field = program.symbols[id];
                    if (field.kind != SymbolKind::Field || !derives(layout, field.owner)) continue;
                    if (!field.constant || reassigned_fields.contains(id)) return false;
                    const auto field_type = substitute_type(field.type, nominal.generics, type.arguments);
                    if (!immutable_layout(field_type, active_types, at)) return false;
                }
            }
        }
        close();
        return true;
    }

    void check_immutable(const IrType& type, const Span& at) {
        std::string key;
        type_key(key, type);
        if (checked_immutable_layouts.contains(key)) return;
        if (!inspected_writes) inspect_field_writes();
        std::set<SymbolId> active_types;
        if (!immutable_layout(type, active_types, at))
            ill_typed(at, "class " + show_type(type) + " does not have a deeply immutable layout for laws");
        checked_immutable_layouts.insert(std::move(key));
    }

    SymbolId target(SymbolId declared, SymbolId receiver) const {
        // Follow the same member and overload selection as the module
        // receiver at circuit conversion, including same-name replacements.
        const auto& original = program.symbols[declared];
        std::set<SymbolId> visited;
        for (auto module = receiver; is_module(module) && visited.insert(module).second;
             module = program.symbols[module].base) {
            const auto found = members.find({module, original.name});
            if (found == members.end()) continue;
            const auto& choices = found->second;
            if (choices.size() == 1) return choices.front();
            if (std::ranges::find(choices, declared) != choices.end()) return declared;
            for (const auto candidate : choices) {
                std::set<SymbolId> overrides;
                for (auto id = program.symbols[candidate].overrides;
                     id < program.symbols.size() && overrides.insert(id).second;
                     id = program.symbols[id].overrides) {
                    if (id == declared) return candidate;
                }
            }
            for (const auto candidate : choices) {
                const auto& item = program.symbols[candidate];
                if (item.type == original.type && std::ranges::equal(item.parameters, original.parameters,
                        [&](SymbolId a, SymbolId b) { return program.symbols[a].type == program.symbols[b].type; }))
                    return candidate;
            }
            return choices.front();
        }
        return declared;
    }

    const IrLawRecord& find_law(SymbolId law, SymbolId module, const Span& at) const {
        std::set<SymbolId> visited;
        for (auto scope = module; is_module(scope) && visited.insert(scope).second; scope = program.symbols[scope].base) {
            for (const auto& record : program.laws)
                if (record.law_symbol == law && record.owner == scope) return record;
        }
        ill_typed(at, "no proof core record for law '" + name(law) + "'");
    }

    [[noreturn]] void forbidden(const Span& at, const std::string& operation, SymbolId callee = no_symbol) const {
        auto error = Diagnostic(at, "law uses an impure or forbidden operation: " + operation).with_code("E1410");
        if (callee < program.symbols.size() && purity) {
            if (const auto reason = purity->impure(callee)) {
                for (const auto& [called, span] : reason->calls) error.label(span, "calls '" + name(called) + "'");
                error.label(reason->span, reason->operation);
            }
        }
        throw error;
    }

    std::vector<SymbolId> check(const IrLawRecord& law, SymbolId module, const Span& at) {
        const auto& declaration = symbol(law.law_symbol, at);
        if (declaration.kind != SymbolKind::Law || !is_module(law.owner) ||
            !derives(module, law.owner) || !derives(law.owner, declaration.owner))
            ill_typed(at, "law record has an invalid declaring or verifying module");
        const auto key = Instance{law.law_symbol, module};
        if (const auto found = results.find(key); found != results.end()) return found->second;
        const auto cycle = std::ranges::find(active, key);
        if (cycle != active.end()) {
            std::string chain;
            for (auto next = cycle; next != active.end(); ++next) chain += name(next->first) + " -> ";
            throw Diagnostic(at, "cyclic law dependency: " + chain + name(key.first)).with_code("E1414");
        }
        if (active.size() >= max_depth)
            throw Diagnostic(at, "proof normalization fuel exhausted in the law dependency chain").with_code("E1415");
        active.push_back(key);
        struct Pop {
            std::vector<Instance>& active;
            ~Pop() { active.pop_back(); }
        } pop{active};
        const auto checked = Run(*this).check(law, module);
        results.emplace(key, checked);
        return checked;
    }
};

LawCheckSession::LawCheckSession(const IrProgram& program, std::size_t fuel)
    : checker_(std::make_unique<Checker>(program, fuel)) {}

LawCheckSession::~LawCheckSession() = default;

std::vector<SymbolId> LawCheckSession::check(const IrLawRecord& law, SymbolId module) {
    if (module == no_symbol) module = law.owner;
    return checker_->check(law, module, law.statement.span);
}

} // namespace neura
