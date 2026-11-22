#include "nil_narrowing.hpp"

#include "attributes.hpp"
#include "class_hooks.hpp"
#include "hook_arguments.hpp"
#include "host_binding.hpp"
#include "module_dispatch.hpp"
#include "nominal.hpp"
#include "reference_storage.hpp"
#include "tags.hpp"
#include "lexer/lexer.hpp"
#include "util/source_map.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <utility>

namespace neura {
namespace {

enum class Nullability { Nil, NonNil };
using SelectorValues = std::map<SymbolId, std::string_view>;

enum class Scan { Flow, Summary, Diagnostics };

struct ValueState {
    NilState state = NilState::Unknown;
    std::string_view cause{};
    const Span* evidence = nullptr;
    SymbolId origin = no_symbol;

    bool operator==(const ValueState& other) const { return state == other.state; }
};

ValueState merge(ValueState first, ValueState second) {
    return first.state >= second.state ? first : second;
}

struct Path {
    // A general match first selects an arm, then dispatches to its body.
    // Keep the known selector values separate so each body keeps the facts
    // established by the corresponding tests and guard.
    SelectorValues selected;
    std::map<SymbolId, Nullability> facts;
    // Copies made for a match subject or a chained comparison.
    std::map<SymbolId, SymbolId> snapshots;
    std::map<SymbolId, ValueState> values;
    ValueState result;

    bool operator==(const Path&) const = default;
};

using Paths = std::vector<Path>;

template <typename Map>
void retain_agreements(Map& first, const Map& second) {
    std::erase_if(first, [&](const auto& item) {
        auto other = second.find(item.first);
        return other == second.end() || item.second != other->second;
    });
}

void add_path(Paths& paths, Path path) {
    const auto prior = std::ranges::find(paths, path.selected, &Path::selected);
    if (prior == paths.end()) {
        paths.push_back(std::move(path));
    } else {
        retain_agreements(prior->facts, path.facts);
        retain_agreements(prior->snapshots, path.snapshots);
        for (auto& [id, state] : prior->values) {
            const auto other = path.values.find(id);
            state = merge(state, other == path.values.end() ? ValueState{} : other->second);
        }
        for (const auto& [id, state] : path.values) {
            if (state.state == NilState::MaybeNil) prior->values.try_emplace(id, state);
        }
        prior->result = merge(prior->result, path.result);
    }
}

Paths unite(Paths first, Paths second) {
    for (auto& item : second) add_path(first, std::move(item));
    return first;
}

struct Predicate {
    Paths yes;
    Paths no;
};

struct Flow {
    Paths next;
    Paths breaks{};
    Paths continues{};
};

struct Use {
    Span span;
    SymbolId binding;

    bool operator<(const Use& other) const {
        return std::tie(span.file, span.offset, span.length, binding) <
               std::tie(other.span.file, other.span.offset, other.span.length, other.binding);
    }
};

bool equality(const IrNode& node) {
    return node.op == IrOp::Binary && (node.text == "==" || node.text == "!=");
}

bool written_source(const Span& span) {
    if (!span.length) return false;
    const auto file = SourceMap::global().find(span.file);
    return file && file->kind() != SourceKind::Generated && span.offset <= file->text().size() &&
        span.length <= file->text().size() - span.offset;
}

bool same_location(const Span& first, const Span& second) {
    return first.file == second.file && first.offset == second.offset && first.length == second.length;
}

bool contains(const Span& expression, const Span& part) {
    return expression.file == part.file && expression.offset <= part.offset &&
        part.offset + part.length <= expression.offset + expression.length;
}

std::vector<Token> written_tokens(const Span& span) {
    if (!written_source(span)) return {};
    const auto text = source_text(span);
    if (!text) return {};
    // Some synthetic spans end partway through a source token. They cannot
    // justify a diagnostic at a new source operation.
    try { return lex(*text, span.file); }
    catch (const Diagnostic&) { return {}; }
}

const IrNode* tested_reference(const IrNode& node) {
    if (node.op == IrOp::ClassIs) return &node.operands.front();
    if (!equality(node)) return nullptr;
    if (node.operands.back().op == IrOp::Nil) return &node.operands.front();
    if (node.operands.front().op == IrOp::Nil) return &node.operands.back();
    return nullptr;
}

const IrNode* checked_resource(const IrNode& branch) {
    // Lowering a using first tests its stored acquisition. The nil arm's
    // ClassHash is a deliberate trap, not a source dereference.
    if (branch.op != IrOp::Branch || branch.body.size() != 1 || !branch.alternative.empty()) return nullptr;
    const auto& discard = branch.body.front();
    if (discard.op != IrOp::Discard || discard.operands.size() != 1) return nullptr;
    const auto& fault = discard.operands.front();
    if (fault.op != IrOp::ClassHash || fault.operands.size() != 1) return nullptr;
    const auto& test = branch.operands.front();
    const auto* acquired = test.text == "==" ? tested_reference(test) : nullptr;
    const auto& receiver = fault.operands.front();
    return acquired && acquired->op == IrOp::Load && receiver.op == IrOp::Load &&
        acquired->symbol == receiver.symbol ? acquired : nullptr;
}

class Analysis {
public:
    Analysis(const IrProgram& program, const HookIndex& hooks, const std::map<SymbolId, IrNilResult>& summaries)
        : program_(program), hooks_(hooks), modules_(program), summaries_(summaries) {
        for (const auto& contract : program.nil_bindings) contracts_.emplace(contract.binding, &contract);
    }

    void run(const IrBody& body, Scan scan) {
        loop_transfers_.clear();
        owner_ = body.owner;
        Paths input(1);
        if (program_.symbols[owner_].kind == SymbolKind::Function) {
            for (const auto id : program_.symbols[owner_].parameters) {
                if (const auto value = parameter_contract(id); value.state != NilState::Unknown)
                    input.front().values.emplace(id, value);
            }
        }
        region(body.instructions, std::move(input), scan);
    }

    IrNilResult summarize(const IrBody& body) {
        returned_.reset();
        nil_return_ = {};
        run(body, Scan::Summary);
        return {body.owner, returned_.value_or(NilState::Unknown), std::move(nil_return_)};
    }

    void append_facts(IrProgram& program) const {
        for (const auto& [use, non_nil] : observed_) {
            if (non_nil) program.non_nil_uses.emplace_back(use.span, use.binding);
        }
    }

private:
    const IrProgram& program_;
    const HookIndex& hooks_;
    ModuleDispatch modules_;
    const std::map<SymbolId, IrNilResult>& summaries_;
    std::map<SymbolId, const IrNilBinding*> contracts_;
    SymbolId owner_ = no_symbol;
    const IrNode* statement_ = nullptr;
    std::optional<NilState> returned_;
    Span nil_return_;
    std::map<Use, bool> observed_;
    std::map<SymbolId, bool> eligible_;

public:
    bool class_reference(const IrType& type) const {
        if (type.kind == TypeKind::Object) return !is_primitive_reference(type);
        if (type.kind != TypeKind::TypeParameter) return false;
        for (const auto& view : nominal_views(program_, type)) {
            if (view.kind == TypeKind::Object && !is_primitive_reference(view)) return true;
        }
        return false;
    }

private:
    bool optional_reference(const IrType& type) const {
        if (type.kind != TypeKind::Enum || type.name != "Optional" || type.arguments.size() != 1 ||
            !class_reference(type.arguments.front())) return false;
        const auto file = source_file(program_, type.symbol);
        return file != no_symbol && program_.symbols[file].library_path == "std/core/optional.nra";
    }

    bool hooked(const IrType& type) const {
        if (type.kind == TypeKind::Object) return !hooks_.targets(type, HookKind::Get).empty();
        for (const auto& view : nominal_views(program_, type)) {
            if (view.kind == TypeKind::Object && !hooks_.targets(view, HookKind::Get).empty()) return true;
        }
        return false;
    }

    bool stable(SymbolId id) {
        if (id >= program_.symbols.size()) return false;
        if (const auto found = eligible_.find(id); found != eligible_.end()) return found->second;
        const auto& binding = program_.symbols[id];
        // Captures are stored in closure fields. A member of an object and
        // a reassignable global can change without a store to this slot.
        const bool binding_kind = binding.kind == SymbolKind::Local ||
            binding.kind == SymbolKind::Parameter || (binding.kind == SymbolKind::Global && binding.constant);
        const bool eligible = binding_kind && !binding.name.starts_with('$') &&
            class_reference(binding.type) && !hooked(binding.type);
        eligible_.emplace(id, eligible);
        return eligible;
    }

    SymbolId binding(const IrNode& node, const Path& path) {
        if (node.op != IrOp::Load) return no_symbol;
        if (stable(node.symbol)) return node.symbol;
        if (const auto copy = path.snapshots.find(node.symbol); copy != path.snapshots.end())
            return copy->second;
        if (node.symbol < program_.symbols.size() && program_.symbols[node.symbol].kind == SymbolKind::Local &&
            program_.symbols[node.symbol].name.starts_with('$') && class_reference(node.type)) return node.symbol;
        return no_symbol;
    }

    bool named_local(SymbolId id, std::string_view name) const {
        return id < program_.symbols.size() && program_.symbols[id].kind == SymbolKind::Local &&
            program_.symbols[id].name == name;
    }

    ValueState parameter_contract(SymbolId id) const {
        if (id >= program_.symbols.size()) return {};
        const auto& parameter = program_.symbols[id];
        if (!class_reference(parameter.type)) return {};
        if (has_nil_parameter_default(program_, id))
            return {NilState::MaybeNil, "nil-default", &parameter.span, id};
        return {NilState::ContractNonNil};
    }

    ValueState field_contract(SymbolId id) const {
        const auto found = contracts_.find(id);
        if (found == contracts_.end()) return {};
        const auto& contract = *found->second;
        if (contract.initialized_nil) return {NilState::MaybeNil, "initialized-nil", &contract.initializer, id};
        return {NilState::ContractNonNil};
    }

    ValueState function_result(SymbolId id) const {
        const auto found = summaries_.find(id);
        if (found == summaries_.end()) return {};
        const auto& summary = found->second;
        if (summary.state == NilState::MaybeNil)
            return {NilState::MaybeNil, "inferred-return", &summary.nil_return, id};
        return {summary.state};
    }

    SymbolId fixed_virtual_target(const IrNode& call) const {
        if (call.operands.empty() || call.symbol >= program_.symbols.size()) return no_symbol;
        const auto& receiver = call.operands.front();
        const auto& declaration = program_.symbols[call.symbol];
        if (receiver.type.kind == TypeKind::Object && receiver.type.symbol < program_.symbols.size()) {
            const auto& type = program_.symbols[receiver.type.symbol];
            if (type.kind == SymbolKind::Class && has_tag(type, TagId::Final)) return call.symbol;
        }
        if (declaration.owner < program_.symbols.size() &&
            program_.symbols[declaration.owner].kind != SymbolKind::Interface &&
            has_tag(declaration, TagId::Final)) return call.symbol;
        // A written module qualifier names an exact module. An implicit
        // receiver may acquire an override when an importing module derives
        // from it, unless the module is closed to inheritance.
        if (receiver.type.kind == TypeKind::Module &&
            (receiver.op == IrOp::Reference ||
             (receiver.symbol < program_.symbols.size() && program_.symbols[receiver.symbol].constant)))
            return modules_.target(call);
        return no_symbol;
    }

    ValueState value_at(const IrNode& node, const Path& path) {
        if (!class_reference(node.type) && !optional_reference(node.type)) return {};
        switch (node.op) {
        case IrOp::Nil: return {NilState::MaybeNil, "literal-nil", &node.span};
        case IrOp::Receiver:
        case IrOp::Allocate: return {NilState::ProvedNonNil};
        case IrOp::Load: {
            const auto subject = binding(node, path);
            if (const auto fact = path.facts.find(subject); fact != path.facts.end()) {
                if (fact->second == Nullability::NonNil) return {NilState::ProvedNonNil};
                return {NilState::MaybeNil, "guarded-nil", &node.span, subject};
            }
            if (const auto found = path.values.find(node.symbol); found != path.values.end()) return found->second;
            return field_contract(node.symbol);
        }
        case IrOp::Field: return field_contract(node.symbol);
        case IrOp::ClassCast:
            // Keep nil evidence for a stable binding through a class cast.
            // A different target type can fail even for a non-nil input.
            return path.result.state == NilState::MaybeNil && binding(node.operands.front(), path) != no_symbol
                ? path.result : ValueState{};
        case IrOp::EnumConstruct:
            // None cannot contribute a nil payload. Some retains the state
            // of its wrapped reference, including Some(nil).
            return node.operands.empty() ? ValueState{NilState::ProvedNonNil} : path.result;
        case IrOp::EnumProject:
            return optional_reference(node.operands.front().type) ? path.result : ValueState{};
        case IrOp::Call: return function_result(node.symbol);
        case IrOp::VirtualCall: return function_result(fixed_virtual_target(node));
        default: return {};
        }
    }

    std::optional<ValueState> possible_nil(const Paths& paths) const {
        const auto found = std::ranges::find_if(paths, [](const Path& path) {
            return path.result.state == NilState::MaybeNil;
        });
        if (found == paths.end()) return std::nullopt;
        return found->result;
    }

    std::pair<std::string_view, std::string> source_receiver(const IrNode& node) const {
        const bool call = node.op == IrOp::Call || node.op == IrOp::VirtualCall;
        std::string_view kind = call ? "result of" : "reference";
        if (node.symbol < program_.symbols.size()) {
            const auto& target = program_.symbols[node.symbol];
            if (const auto* accessor = find_attribute(target, AttributeId::AccessorOf);
                call && accessor && accessor->value.items[1].integer == 0) {
                const auto field = accessor->value.items[0].symbol;
                if (field < program_.symbols.size()) return {"field", program_.symbols[field].name};
            }
            if (target.kind == SymbolKind::Field) kind = "field";
            const auto& name = target.name;
            if (!name.starts_with('$') && !name.empty()) return {kind, name};
        }
        return {kind, source_text(node.span).value_or("reference")};
    }

    std::optional<Span> written_access(const IrNode& use, const Span& receiver, std::string_view token) const {
        if (!contains(use.span, receiver) || same_location(use.span, receiver)) return std::nullopt;
        const auto end = receiver.offset + receiver.length - use.span.offset;
        const auto tokens = written_tokens(use.span);
        const auto next = std::ranges::find_if(tokens, [&](const Token& item) { return item.span.offset >= end; });
        if (next == tokens.end() || next->kind != TokenKind::Symbol || next->text != token) return std::nullopt;
        return source_span(use.span.file, use.span.offset + next->span.offset, next->span.length);
    }

    bool written_transfer(const IrNode& site, SymbolId target, const Span& value, bool assignment) const {
        if (!written_source(value)) return false;
        if (assignment) {
            if (const auto contract = contracts_.find(target);
                contract != contracts_.end() && same_location(contract->second->initializer, value)) return true;
        }
        if (!contains(site.span, value) || same_location(site.span, value)) return false;
        if (!assignment && value.offset == site.span.offset && written_access(site, value, ".")) return true;
        const auto tokens = written_tokens(site.span);
        const auto offset = value.offset - site.span.offset;
        const auto argument = std::ranges::find_if(tokens, [&](const Token& token) { return token.span.offset >= offset; });
        if (argument == tokens.begin() || argument == tokens.end() || argument->span.offset != offset) return false;
        const auto& before = *(argument - 1);
        if (before.kind != TokenKind::Symbol) return false;
        if (assignment) return before.text == "=";
        return before.text == "(" || before.text == "," || before.text == ":";
    }

    void explain_cause(Diagnostic& warning, const ValueState& evidence, const Span& use) const {
        warning.arg("cause", std::string(evidence.cause));
        if (!evidence.evidence || evidence.evidence->file.empty()) return;
        const auto& at = *evidence.evidence;
        if (evidence.cause == "inferred-return") {
            const auto& function = program_.symbols[evidence.origin].name;
            warning.label(at, "'" + function + "' can return nil here").arg("function", function);
        } else if (at.file != use.file || at.offset != use.offset || at.length != use.length) {
            const auto message = evidence.cause == "nil-default" ? "this parameter defaults to nil" :
                evidence.cause == "initialized-nil" ? "initially nil here" :
                evidence.cause == "assigned-nil" ? "assigned nil here" : "nil can come from here";
            warning.label(at, message);
        }
    }

    void warn_access(const IrNode& use, const IrNode& receiver, std::string category, const Paths& paths) {
        if (!written_source(receiver.span)) return;
        if (receiver.symbol < program_.symbols.size()) {
            const auto& symbol = program_.symbols[receiver.symbol];
            // The selected path of safe access has already tested this
            // value. The source operator itself never issues a warning.
            if (symbol.name == "$optional" || symbol.name == "$present") return;
        }
        const auto evidence = possible_nil(paths);
        if (!evidence) return;
        std::optional<Span> dot;
        if (category == "method" || category == "field") {
            dot = written_access(use, receiver.span, ".");
            if (!dot) return;
        } else if (use.op == IrOp::Index && !written_access(use, receiver.span, "[")) {
            return;
        } else if (use.op == IrOp::Destroy) {
            const auto tokens = written_tokens(use.span);
            if (tokens.empty() || tokens.front().kind != TokenKind::Keyword || tokens.front().text != "destroy") return;
        }
        const auto [kind, name] = source_receiver(receiver);
        auto warning = Diagnostic::warning(receiver.span, "nil-access", std::string(kind) + " '" + name + "' may be nil")
            .with_code("W0006").label("may be nil at this access").arg("binding", name).arg("use", std::move(category));
        explain_cause(warning, *evidence, receiver.span);
        // Optional access changes the result type. This edit is safe only when
        // nothing uses that result, including an enclosing access or argument.
        if (dot && statement_ && statement_->op == IrOp::Discard && statement_->operands.size() == 1 &&
            &statement_->operands.front() == &use)
            warning.suggest("use optional access", *dot, "?.", Applicability::MaybeIncorrect);
        const auto text = source_text(receiver.span).value_or(name);
        const bool copy = use.op == IrOp::Branch || receiver.op != IrOp::Load || !stable(receiver.symbol);
        const auto test = copy ? "let guarded_value = " + text + "; guarded_value" : text;
        warning.help("guard the access with 'if (" + test + ") { ... }'");
        // A declaration must keep its binding in the surrounding scope.
        if (statement_ && statement_->op != IrOp::Initialize && contains(statement_->span, receiver.span)) {
            const auto& span = statement_->span;
            if (auto statement = source_text(span); statement && statement->ends_with(';') &&
                (!copy || statement->find("guarded_value") == std::string::npos)) {
                if (copy) statement->replace(receiver.span.offset - span.offset, receiver.span.length, "guarded_value");
                const std::string indent(span.column - 1, ' ');
                const auto replacement = "if (" + test + ") {\n" + indent + "    " + *statement + "\n" + indent +
                    "} else {\n" + indent + "    <handle nil>\n" + indent + "}";
                warning.suggest("narrow the reference with an if condition", span, replacement,
                                Applicability::HasPlaceholders);
            }
        }
        report_scoped_warning(program_, owner_, std::move(warning));
    }

    void warn_argument(const IrNode& site, SymbolId target, const IrNode& input, bool assignment, const Paths& paths) {
        const auto& use = input.op == IrOp::Load && named_local(input.symbol, "$static_rhs")
            ? program_.symbols[input.symbol].span : input.span;
        if (target >= program_.symbols.size() || input.text == default_argument_nil) return;
        const auto contract = assignment ? field_contract(target) : parameter_contract(target);
        if (contract.state != NilState::ContractNonNil) return;
        const auto evidence = possible_nil(paths);
        if (!evidence || !written_transfer(site, target, use, assignment)) return;
        const auto& destination = program_.symbols[target];
        const auto heading = assignment ? "assignment to '" : "argument for parameter '";
        auto warning = Diagnostic::warning(use, "nil-argument", heading + destination.name + "' may be nil")
            .with_code("W0007").label("may supply nil here")
            .label(destination.span, "requires a non-nil reference")
            .arg("binding", destination.name).arg("use", assignment ? "assignment" : "argument");
        explain_cause(warning, *evidence, use);
        warning.help("test that the value is non-nil before passing or storing it");
        if (auto text = source_text(use)) {
            auto replacement = *text == "nil" ? std::string("<non-nil value>") : *text + " ?? <non-nil fallback>";
            warning.suggest("provide a non-nil value", use, std::move(replacement), Applicability::HasPlaceholders);
        }
        report_scoped_warning(program_, owner_, std::move(warning));
    }

    void observe(const IrNode& node, const Paths& paths, Scan scan) {
        if (scan != Scan::Diagnostics || node.span.file.empty() || !node.span.length || !stable(node.symbol)) return;
        const auto proven = std::ranges::all_of(paths, [&](const Path& path) {
            const auto fact = path.facts.find(node.symbol);
            return fact != path.facts.end() && fact->second == Nullability::NonNil;
        });
        if (proven) {
            const auto written = source_text(node.span);
            if (written && written->find(program_.symbols[node.symbol].name) == std::string::npos) return;
        }
        const auto [prior, inserted] = observed_.emplace(Use{node.span, node.symbol}, proven);
        if (!inserted) prior->second &= proven;
    }

    void overwrite(Path& path, SymbolId id, const IrNode* input, ValueState value = {}, bool assignment = false) {
        if (named_local(id, "$arm")) {
            if (input && input->op == IrOp::Integer) path.selected[id] = input->text;
            else path.selected.erase(id);
            return;
        }
        const auto saved = input && (named_local(id, "$match") || named_local(id, "$comparison"))
            ? binding(*input, path) : no_symbol;
        path.facts.erase(id);
        path.values.erase(id);
        std::erase_if(path.snapshots, [&](const auto& item) { return item.first == id || item.second == id; });
        if (saved != no_symbol) path.snapshots[id] = saved;
        if (id >= program_.symbols.size()) return;
        const auto& symbol = program_.symbols[id];
        if (value.state != NilState::Unknown && (symbol.kind == SymbolKind::Local || symbol.kind == SymbolKind::Parameter)) {
            if (value.cause == "literal-nil" && !symbol.name.starts_with('$')) value = {NilState::MaybeNil,
                assignment ? "assigned-nil" : "initialized-nil", value.evidence, id};
            path.values.emplace(id, value);
        }
    }

    void forget_locals(const std::vector<SymbolId>& locals, Paths& paths) {
        if (locals.empty()) return;
        Paths merged;
        for (auto& path : paths) {
            for (const auto id : locals) overwrite(path, id, nullptr);
            add_path(merged, std::move(path));
        }
        paths = std::move(merged);
    }

    // A cleanup executes on exits. A binding that any registered cleanup
    // writes loses its fact on those exits, including loop control transfers.
    void forget_cleanup_writes(const IrNode& cleanup, Paths& paths) {
        if (paths.empty()) return;
        if (cleanup.op == IrOp::Store && cleanup.operands.front().op == IrOp::Load) {
            for (auto& path : paths) overwrite(path, cleanup.operands.front().symbol, nullptr);
        }
        for (const auto* region : {&cleanup.operands, &cleanup.body, &cleanup.alternative}) {
            for (const auto& child : *region) forget_cleanup_writes(child, paths);
        }
    }

    Flow region(std::span<const IrNode> nodes, Paths input, Scan scan, bool close_scope = true) {
        Flow total{std::move(input)};
        std::vector<SymbolId> locals;
        std::vector<const IrNode*> cleanups;
        const auto exiting = [&](Paths paths) {
            for (const auto* cleanup : cleanups) forget_cleanup_writes(*cleanup, paths);
            if (close_scope) forget_locals(locals, paths);
            return paths;
        };
        for (const auto& node : nodes) {
            if (total.next.empty()) break;
            if (node.op == IrOp::Cleanup) {
                // The facts inside a cleanup must hold on every exit after
                // registration, so start its separate source flow unknown.
                if (scan == Scan::Diagnostics) {
                    region(node.body, Paths(1), scan);
                    region(node.alternative, Paths(1), scan);
                }
                cleanups.push_back(&node);
                continue;
            }
            if (node.op == IrOp::Initialize) locals.push_back(node.symbol);
            const auto* outer_statement = statement_;
            if (node.op == IrOp::Return || node.op == IrOp::Discard || node.op == IrOp::Store ||
                node.op == IrOp::Initialize || node.op == IrOp::Destroy) statement_ = &node;
            auto step = visit(node, std::move(total.next), scan);
            statement_ = outer_statement;
            total.next = std::move(step.next);
            total.breaks = unite(std::move(total.breaks), exiting(std::move(step.breaks)));
            total.continues = unite(std::move(total.continues), exiting(std::move(step.continues)));
        }
        total.next = exiting(std::move(total.next));
        return total;
    }

    static bool assume(Path& path, SymbolId id, Nullability value) {
        if (id == no_symbol) return true;
        const auto [prior, inserted] = path.facts.emplace(id, value);
        if (!inserted && prior->second != value) return false;
        // The guard and an ordinary non-nil assignment can meet at a join.
        // The diagnostic value stays NonNil when both paths establish it,
        // even though the written narrowing ends at the assignment.
        if (value == Nullability::NonNil) path.values[id] = {NilState::ProvedNonNil};
        return true;
    }

    Predicate region_condition(const std::vector<IrNode>& nodes, Paths input, Scan scan) {
        const auto prefix = std::span(nodes).first(nodes.size() - 1);
        auto flow = region(prefix, std::move(input), scan, false);
        auto result = condition(nodes.back(), std::move(flow.next), scan);
        std::vector<SymbolId> locals;
        for (const auto& node : prefix) {
            if (node.op == IrOp::Initialize) locals.push_back(node.symbol);
        }
        forget_locals(locals, result.yes);
        forget_locals(locals, result.no);
        return result;
    }

    Predicate condition(const IrNode& test, Paths input, Scan scan) {
        if (input.empty()) return {};
        if (test.op == IrOp::ShortCircuit) {
            auto lhs = condition(test.operands.front(), std::move(input), scan);
            if (test.text == "&&") {
                auto rhs = condition(test.body.front(), std::move(lhs.yes), scan);
                return {std::move(rhs.yes), unite(std::move(lhs.no), std::move(rhs.no))};
            }
            auto rhs = condition(test.body.front(), std::move(lhs.no), scan);
            return {unite(std::move(lhs.yes), std::move(rhs.yes)), std::move(rhs.no)};
        }
        if (test.op == IrOp::Unary && test.text == "!") {
            auto result = condition(test.operands.front(), std::move(input), scan);
            return {std::move(result.no), std::move(result.yes)};
        }
        if (test.op == IrOp::ScopedValue || test.op == IrOp::ComparisonChain)
            return region_condition(test.body, std::move(input), scan);
        if (test.op == IrOp::SelectValue) {
            auto predicate = condition(test.operands.front(), std::move(input), scan);
            auto yes = region_condition(test.body, std::move(predicate.yes), scan);
            auto no = region_condition(test.alternative, std::move(predicate.no), scan);
            return {unite(std::move(yes.yes), std::move(no.yes)), unite(std::move(yes.no), std::move(no.no))};
        }
        if (test.op == IrOp::Boolean) {
            if (test.text == "true") return {std::move(input), {}};
            return {{}, std::move(input)};
        }
        auto evaluated = visit(test, std::move(input), scan);
        Predicate result;
        const auto* subject = tested_reference(test);
        for (auto& path : evaluated.next) {
            if (subject) {
                const auto id = binding(*subject, path);
                const auto positive = test.text == "==" ? Nullability::Nil : Nullability::NonNil;
                const auto negative = positive == Nullability::Nil ? Nullability::NonNil : Nullability::Nil;
                auto yes = path;
                if (assume(yes, id, positive)) add_path(result.yes, std::move(yes));
                if (test.op == IrOp::ClassIs || assume(path, id, negative)) add_path(result.no, std::move(path));
            } else if (equality(test) && test.operands.front().op == IrOp::Load &&
                       test.operands.back().op == IrOp::Integer) {
                const auto selected = path.selected.find(test.operands.front().symbol);
                if (selected == path.selected.end()) {
                    result.yes.push_back(path);
                    result.no.push_back(std::move(path));
                } else {
                    const bool equal = selected->second == test.operands.back().text;
                    auto& branch = equal == (test.text == "==") ? result.yes : result.no;
                    add_path(branch, std::move(path));
                }
            } else {
                result.yes.push_back(path);
                result.no.push_back(std::move(path));
            }
        }
        return result;
    }

    struct LoopTransfer {
        Paths entry;
        Paths head;
        Paths exit;
    };

    std::map<const IrNode*, std::vector<LoopTransfer>> loop_transfers_;

    static bool same_loop_entry(const Path& first, const Path& second) {
        // Fixed-point equality ignores warning provenance. Cache it as well
        // so a reused transfer still points to the same reason for nil.
        return first == second && std::ranges::equal(first.values, second.values, [](const auto& a, const auto& b) {
            const auto& left = a.second;
            const auto& right = b.second;
            return left.cause == right.cause && left.evidence == right.evidence && left.origin == right.origin;
        });
    }

    Flow loop(const IrNode& node, Paths input, Scan scan) {
        const auto iteration = [&](Paths head, Scan body_scan) {
            auto predicate = condition(node.operands.front(), std::move(head), body_scan);
            auto body = region(node.body, std::move(predicate.yes), body_scan);
            auto latch = region(node.alternative, unite(std::move(body.next), std::move(body.continues)), body_scan);
            return std::pair{std::move(latch.next), unite(std::move(predicate.no), std::move(body.breaks))};
        };
        for (auto& path : input) path.result = {};

        // Outer fixpoint iterations can revisit the same inner loop with the
        // same entry. Compute its transfer only once per function scan.
        auto& transfers = loop_transfers_[&node];
        auto prior = std::ranges::find_if(transfers, [&](const LoopTransfer& transfer) {
            return std::ranges::equal(input, transfer.entry, same_loop_entry);
        });
        if (prior == transfers.end()) {
            auto head = input;
            for (;;) {
                auto [latch, exit] = iteration(head, Scan::Flow);
                for (auto& path : latch) path.result = {};
                auto next = unite(input, std::move(latch));
                if (next == head) {
                    transfers.push_back({std::move(input), std::move(head), std::move(exit)});
                    prior = std::prev(transfers.end());
                    break;
                }
                head = std::move(next);
            }
        }
        if (scan == Scan::Flow) return {prior->exit};
        // Record diagnostics and returns once, using the stabilized head.
        return {iteration(prior->head, scan).second};
    }

    void operand_warnings(const IrNode& node, std::size_t index, const Paths& paths) {
        const auto& operand = node.operands[index];
        if (index == 0 && class_reference(operand.type)) {
            if (node.op == IrOp::Field) warn_access(node, operand, "field", paths);
            if (node.op == IrOp::Index) warn_access(node, operand, "index", paths);
            if (node.op == IrOp::VirtualCall) {
                const bool getter = has_attribute(program_.symbols[node.symbol], AttributeId::AccessorOf);
                warn_access(node, operand, getter ? "field" : "method", paths);
            }
            if (node.op == IrOp::Destroy) warn_access(node, operand, "implicit", paths);
        }
        if (node.op == IrOp::Initialize) {
            // The static-preparation body installs nil placeholders before
            // the real static initializers. The latter use ordinary stores.
            if (operand.op != IrOp::Nil || !is_static_scope(program_.symbols[owner_]))
                warn_argument(node, node.symbol, operand, true, paths);
        } else if (node.op == IrOp::Store && index == 1) {
            warn_argument(node, node.operands.front().symbol, operand, true, paths);
        } else if (node.op == IrOp::Call || node.op == IrOp::VirtualCall || node.op == IrOp::ExternalCall ||
                   node.op == IrOp::BaseInitialize || node.op == IrOp::ConformanceCall || node.op == IrOp::Allocate) {
            const std::size_t first = node.op == IrOp::VirtualCall || node.op == IrOp::BaseInitialize ? 1 : 0;
            if (index < first || node.symbol >= program_.symbols.size()) return;
            const auto& function = program_.symbols[node.symbol];
            if (const auto* accessor = find_attribute(function, AttributeId::AccessorOf);
                accessor && accessor->value.items[1].integer == 1 && index == first) {
                warn_argument(node, accessor->value.items[0].symbol, operand, true, paths);
            } else if (index - first < function.parameters.size()) {
                warn_argument(node, function.parameters[index - first], operand, false, paths);
            }
        }
    }

    void return_value(const IrNode& node, const Paths& paths) {
        for (const auto& path : paths) {
            if (path.result.state == NilState::MaybeNil && returned_ != NilState::MaybeNil)
                nil_return_ = node.span;
            returned_ = std::max(returned_.value_or(NilState::ProvedNonNil), path.result.state);
        }
    }

    Flow visit(const IrNode& node, Paths input, Scan scan) {
        if (input.empty()) return {};
        if (node.op == IrOp::Block || node.op == IrOp::TaskScope || node.op == IrOp::ScopedValue ||
            node.op == IrOp::ComparisonChain)
            return region(node.body, std::move(input), scan);
        if (node.op == IrOp::Branch || node.op == IrOp::SelectValue) {
            if (const auto* resource = checked_resource(node)) {
                if (scan == Scan::Diagnostics && DiagnosticSink::current()) {
                    for (auto& path : input) path.result = value_at(*resource, path);
                    warn_access(node, *resource, "implicit", input);
                }
                if (stable(resource->symbol)) return {condition(node.operands.front(), std::move(input), scan).no};
            }
            auto predicate = condition(node.operands.front(), std::move(input), scan);
            auto yes = region(node.body, std::move(predicate.yes), scan);
            auto no = region(node.alternative, std::move(predicate.no), scan);
            return {unite(std::move(yes.next), std::move(no.next)),
                    unite(std::move(yes.breaks), std::move(no.breaks)),
                    unite(std::move(yes.continues), std::move(no.continues))};
        }
        if (node.op == IrOp::Loop) return loop(node, std::move(input), scan);
        if (node.op == IrOp::ShortCircuit) {
            auto result = condition(node, std::move(input), scan);
            return {unite(std::move(result.yes), std::move(result.no))};
        }
        if (node.op == IrOp::Load) observe(node, input, scan);
        Flow evaluated{std::move(input)};
        for (std::size_t i = 0; i < node.operands.size(); ++i) {
            const auto& operand = node.operands[i];
            if (node.op == IrOp::Store && i == 0 && operand.op == IrOp::Load && operand.operands.empty()) continue;
            auto value = visit(operand, std::move(evaluated.next), scan);
            evaluated.next = std::move(value.next);
            evaluated.breaks = unite(std::move(evaluated.breaks), std::move(value.breaks));
            evaluated.continues = unite(std::move(evaluated.continues), std::move(value.continues));
            if (scan == Scan::Diagnostics && DiagnosticSink::current()) operand_warnings(node, i, evaluated.next);
        }
        if (node.op == IrOp::Initialize || (node.op == IrOp::Store && node.operands.front().op == IrOp::Load)) {
            const auto id = node.op == IrOp::Initialize ? node.symbol : node.operands.front().symbol;
            for (auto& path : evaluated.next) {
                if (node.operands.empty()) overwrite(path, id, nullptr);
                else overwrite(path, id, &node.operands.back(), path.result, node.op == IrOp::Store);
            }
        } else if (node.op == IrOp::Break) {
            evaluated.breaks = unite(std::move(evaluated.breaks), std::exchange(evaluated.next, {}));
        } else if (node.op == IrOp::Continue) {
            evaluated.continues = unite(std::move(evaluated.continues), std::exchange(evaluated.next, {}));
        } else if (node.op == IrOp::Return || node.op == IrOp::TrapMessage || node.op == IrOp::Fail) {
            if (node.op == IrOp::Return && scan == Scan::Summary) return_value(node, evaluated.next);
            evaluated.next.clear();
        }
        for (auto& path : evaluated.next) path.result = value_at(node, path);
        return evaluated;
    }
};

} // namespace

void analyze_nil_references(IrProgram& program, std::size_t first_body) {
    const HookIndex hooks(program, true);
    std::map<SymbolId, IrNilResult> summaries;
    for (const auto& summary : program.nil_results) summaries.emplace(summary.function, summary);
    Analysis analysis(program, hooks, summaries);
    std::vector<const IrBody*> functions;
    const auto source_bodies = program.bodies | std::views::drop(first_body);
    for (const auto& body : source_bodies) {
        const auto& symbol = program.symbols[body.owner];
        if (symbol.kind != SymbolKind::Function || !analysis.class_reference(symbol.type) || symbol.external) continue;
        // A recursive call alone is not evidence of a proved result.
        summaries.emplace(body.owner, IrNilResult{body.owner, NilState::Unknown, {}});
        functions.push_back(&body);
    }
    bool changed;
    do {
        changed = false;
        for (const auto* function : functions) {
            auto next = analysis.summarize(*function);
            auto& prior = summaries.at(function->owner);
            if (next.state != prior.state) {
                prior = std::move(next);
                changed = true;
            }
        }
    } while (changed);
    for (const auto* function : functions) program.nil_results.push_back(summaries.at(function->owner));
    for (const auto& body : source_bodies) analysis.run(body, Scan::Diagnostics);
    analysis.append_facts(program);
}

} // namespace neura
