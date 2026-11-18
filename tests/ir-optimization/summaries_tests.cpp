// Effect and retention summaries (src/neura-ir/purity.hpp, docs/design/
// Representation030.md "Effect and retention summaries"): one case for each
// fact, present and absent, for recursion, virtual calls, function values,
// kernels, host bindings with and without a contract, retention, result
// ownership and the implicit initializer; the rejection of a bad contract;
// the equality of summaries built from source and from its .nir; and the
// differential test that holds the optimizer's flags to the ones the
// optimizer has always computed.
#include "ir-optimization/effects.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/attributes.hpp"
#include "neura-ir/integer.hpp"
#include "neura-ir/kernel_signature.hpp"
#include "neura-ir/nir_format.hpp"
#include "neura-ir/purity.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/verifier.hpp"
#include "support.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unistd.h>

namespace neura::testing {
namespace {

using Allocation = PurityAnalysis::Allocation;
using Retention = PurityAnalysis::Retention;
using Ownership = PurityAnalysis::Ownership;
using Fact = PurityAnalysis::Fact;
using Summary = PurityAnalysis::Summary;

fs::path summaries(const Environment& environment) { return environment.source_dir / "tests" / "ir-optimization" / "summaries"; }

IrProgram prepared(std::vector<fs::path> files) {
    auto program = lower_files(std::move(files));
    materialize_reference_storage(program);
    specialize(program);
    return program;
}

IrProgram facts_program(const Environment& environment) { return prepared({summaries(environment) / "facts.nra"}); }

const Summary& summary_of(const IrProgram& program, const PurityAnalysis& purity, std::string_view name) {
    const auto* found = purity.summary(find_symbol(program, name));
    check(found != nullptr, std::string(name) + " has a summary");
    return *found;
}

std::size_t line_of(const fs::path& file, std::string_view text) {
    std::ifstream in(file);
    std::string line;
    for (std::size_t number = 1; std::getline(in, line); ++number) {
        if (line.find(text) != std::string::npos) return number;
    }
    fail("no line holds " + std::string(text));
}

// The retention of the argument at `slot`, and a readable name for failures.
Retention retention(const Summary& summary, std::size_t slot) {
    check(slot < summary.retention.size(), "the summary has the slot");
    return summary.retention[slot];
}

// ---- the optimizer's own flags, as they were before the summaries ----------

struct Flags {
    bool allocates = false;
    bool traps = false;
    bool host = false;
    bool scheduler = false;
    bool operator==(const Flags&) const = default;
    void join(const Flags& other) {
        allocates = allocates || other.allocates;
        traps = traps || other.traps;
        host = host || other.host;
        scheduler = scheduler || other.scheduler;
    }
};

constexpr Flags everything{true, true, true, true};

// EffectAnalysis::add of the optimizer, kept to the flags it set and solved
// by plain iteration to a fixed point. It is the reference that the purity
// analysis's `optimizer` flags must equal for every function, so that the
// optimizer reading them cannot change a decision.
class ReferenceFlags {
public:
    explicit ReferenceFlags(const IrProgram& program) : program_(program), modules_(program) {
        std::map<SymbolId, std::vector<SymbolId>> calls;
        std::map<SymbolId, Flags> local;
        for (const auto& body : program.bodies) {
            if (body.owner >= program.symbols.size() || program.symbols[body.owner].kind != SymbolKind::Function ||
                local.contains(body.owner)) {
                continue;
            }
            Flags flags;
            auto& callees = calls[body.owner];
            for (const auto& node : body.instructions) add(node, flags, callees);
            local[body.owner] = flags;
        }
        flags_ = local;
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& [function, own] : local) {
                Flags result = own;
                for (const auto callee : calls[function]) {
                    result.join(local.contains(callee) ? flags_.at(callee) : everything);
                }
                if (!(result == flags_.at(function))) {
                    flags_[function] = result;
                    changed = true;
                }
            }
        }
    }
    const std::map<SymbolId, Flags>& flags() const { return flags_; }

private:
    const IrProgram& program_;
    ModuleDispatch modules_;
    std::map<SymbolId, Flags> flags_;

    static bool comparison(const std::string& op) {
        return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
    }
    bool local(SymbolId id) const {
        return id < program_.symbols.size() && (program_.symbols[id].kind == SymbolKind::Local ||
                                                 program_.symbols[id].kind == SymbolKind::Parameter);
    }
    void children(const IrNode& node, Flags& flags, std::vector<SymbolId>& callees) const {
        for (const auto* region : {&node.operands, &node.body, &node.alternative}) {
            for (const auto& child : *region) add(child, flags, callees);
        }
    }
    void add(const IrNode& node, Flags& flags, std::vector<SymbolId>& callees) const {
        switch (node.op) {
        case IrOp::Boolean: case IrOp::Integer: case IrOp::Float: case IrOp::Nil: case IrOp::Reference:
        case IrOp::Receiver: case IrOp::ReinterpretFloat: case IrOp::ShortCircuit: case IrOp::ComparisonChain:
        case IrOp::ScopedValue: case IrOp::SelectValue: case IrOp::Block: case IrOp::Branch: case IrOp::Loop:
        case IrOp::Break: case IrOp::Continue: case IrOp::Return: case IrOp::Discard:
            break;
        case IrOp::Load:
            if (!local(node.symbol)) flags.traps = true;
            break;
        case IrOp::Store: {
            if (node.operands.size() != 2) {
                flags.join(everything);
                break;
            }
            const auto& destination = node.operands.front();
            if (destination.op != IrOp::Load) flags.traps = true;
            for (const auto& operand : destination.operands) add(operand, flags, callees);
            add(node.operands.back(), flags, callees);
            return;
        }
        case IrOp::Initialize:
            if (!local(node.symbol) && !(node.symbol < program_.symbols.size() &&
                                         program_.symbols[node.symbol].kind == SymbolKind::Global)) {
                flags.traps = true;
            }
            break;
        case IrOp::Unary:
            if (node.text != "!" && node.text != "~") flags.traps = true;
            break;
        case IrOp::Binary: {
            const auto operand = node.operands.empty() ? TypeKind::Void : node.operands.front().type.kind;
            const bool scalar = operand == TypeKind::Bool || operand == TypeKind::Integer;
            if (!(scalar && (comparison(node.text) || (is_bitwise_binary(node.text) && !is_shift(node.text))))) {
                flags.traps = true;
            }
            if (operand == TypeKind::String) flags.allocates = true;
            break;
        }
        case IrOp::Convert: case IrOp::Fail: case IrOp::TrapMessage: case IrOp::Field: case IrOp::Index:
        case IrOp::Length: case IrOp::CollectionVersion: case IrOp::TextLength: case IrOp::TextByteAt:
        case IrOp::TextEqual: case IrOp::Project: case IrOp::EnumTag: case IrOp::EnumProject:
            flags.traps = true;
            break;
        case IrOp::TextSlice: case IrOp::TextFromUtf8: case IrOp::ConcatText: case IrOp::Append: case IrOp::RemoveAt: case IrOp::String:
        case IrOp::Allocate: case IrOp::Collection: case IrOp::RepeatCollection: case IrOp::Tuple:
        case IrOp::EnumConstruct: case IrOp::MakeClosure:
            flags.allocates = flags.traps = true;
            break;
        case IrOp::Call:
            flags.traps = true;
            callees.push_back(node.symbol);
            break;
        case IrOp::VirtualCall:
            if (const auto target = modules_.target(node); target != no_symbol) {
                flags.traps = true;
                callees.push_back(target);
            } else {
                flags.join(everything);
            }
            break;
        case IrOp::ExternalCall:
            if (node.symbol < program_.symbols.size() && kernel_bound(program_.symbols[node.symbol])) {
                flags.allocates = flags.traps = true;
            } else {
                flags.join(everything);
            }
            break;
        case IrOp::TaskScope:
            flags.scheduler = flags.traps = true;
            break;
        default:
            flags.join(everything);
            break;
        }
        children(node, flags, callees);
    }
};

Flags flags_of(const Summary& summary) {
    return {summary.optimizer.allocates, summary.optimizer.traps, summary.optimizer.host, summary.optimizer.scheduler};
}

// Compares the purity analysis's optimizer flags, and the flags that
// EffectAnalysis now reports, with the reference, for every function.
std::size_t compare_flags(const IrProgram& program, const std::string& label) {
    const ReferenceFlags reference(program);
    const PurityAnalysis purity(program);
    const optimization::EffectAnalysis owning(program);
    const optimization::EffectAnalysis sharing(program, purity);
    for (const auto& [function, expected] : reference.flags()) {
        const auto name = label + ": " + qualified_name(program, function);
        const auto* summary = purity.summary(function);
        check(summary != nullptr, name + " has a summary");
        const auto actual = flags_of(*summary);
        const auto text = [](const Flags& flags) {
            return std::string(flags.allocates ? "allocates " : "") + (flags.traps ? "traps " : "") +
                   (flags.host ? "host " : "") + (flags.scheduler ? "scheduler" : "");
        };
        check(actual == expected, name + " differs\n  reference: " + text(expected) + "\n  purity:    " + text(actual));
        for (const auto* analysis : {&owning, &sharing}) {
            const auto& effects = analysis->function(function);
            check((Flags{effects.allocates, effects.may_trap, effects.host, effects.scheduler}) == expected,
                  name + ": EffectAnalysis reports other flags than the reference");
        }
    }
    return reference.flags().size();
}

std::vector<fs::path> files_in(const fs::path& directory, bool recursive = true) {
    std::vector<fs::path> files;
    const auto take = [&](const fs::directory_entry& entry) {
        if (entry.is_regular_file() && entry.path().extension() == ".nra") files.push_back(fs::canonical(entry.path()));
    };
    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) take(entry);
    } else {
        for (const auto& entry : fs::directory_iterator(directory)) take(entry);
    }
    std::ranges::sort(files);
    return files;
}

} // namespace

// Every program of the repository, before and after the optimizer has run:
// the flags that the optimizer reads from the summaries equal the ones it used
// to compute, function by function. The examples are compared as the verifier
// suite takes them, all together and not specialized, and one directory at a
// time and one file at a time, as a program is built.
NEURA_TEST(structure, summary_flags_equal_the_optimizers_own_for_every_program) {
    struct Input {
        std::vector<fs::path> files;
        bool specialized;
    };
    std::vector<Input> inputs;
    for (const auto& file : files_in(environment.fixtures())) inputs.push_back({{file}, true});
    for (const auto& file : files_in(environment.source_dir / "tests" / "backend" / "programs", false)) {
        inputs.push_back({{file}, true});
    }
    for (const auto& file : files_in(summaries(environment))) inputs.push_back({{file}, true});
    inputs.push_back({example_files(environment), false});
    for (const auto& file : files_in(environment.examples(), false)) inputs.push_back({{file}, true});
    for (const auto& directory : fs::directory_iterator(environment.examples())) {
        if (!directory.is_directory()) continue;
        const auto files = files_in(directory.path());
        for (const auto& file : files) inputs.push_back({{file}, true});
        if (files.size() > 1) inputs.push_back({files, true});
    }
    // Inputs that do not lower on their own, with the reason.
    const std::map<std::string, std::string> unlowerable_reason{
        {"math-tests.nra", "a test file: the test runner supplies its Core import"},
        {"greetings.nra", "a file of the multi-file module, lowered with its directory"},
    };
    std::size_t functions = 0;
    std::size_t compared = 0;
    std::vector<std::string> unlowerable;
    for (const auto& [files, specialized] : inputs) {
        const auto label = files.size() == 1 ? files.front().filename().string() : "all examples";
        if (unlowerable_reason.contains(label) || (files.size() > 1 && files.front().filename() == "math-tests.nra")) {
            continue;
        }
        std::optional<IrProgram> lowered;
        try {
            lowered = specialized ? prepared(files) : lower_files(files);
        } catch (const std::exception& error) {
            unlowerable.push_back(files.front().string() + ": " + error.what());
            continue;
        }
        auto& program = *lowered;
        functions += compare_flags(program, label + " before optimization");
        optimize(program);
        functions += compare_flags(program, label + " after optimization");
        ++compared;
    }
    std::string names;
    for (const auto& name : unlowerable) names += "\n  " + name;
    check(unlowerable.empty(), "inputs that did not lower:" + names);
    check(compared > 65, "the comparison covers the repository's programs, not " + std::to_string(compared));
    check(functions > 20000, "and many functions, not " + std::to_string(functions));
}

NEURA_TEST(structure, summaries_report_allocation_traps_host_scheduler_and_class_initialization) {
    const auto program = facts_program(environment);
    const PurityAnalysis purity(program);
    const auto& quiet = summary_of(program, purity, "Facts.quiet");
    check_equal(static_cast<int>(quiet.allocation()), static_cast<int>(Allocation::Free), "quiet is allocation-free");
    check(!quiet.traps && !quiet.host && !quiet.scheduler && !quiet.class_init && !quiet.hooks,
          "quiet has no other fact");

    const auto& adds = summary_of(program, purity, "Facts.adds");
    check(adds.traps.has_value(), "the sum may trap");
    check_equal(static_cast<int>(adds.allocation()), static_cast<int>(Allocation::Free), "the sum does not allocate");

    const auto& fresh = summary_of(program, purity, "Facts.fresh_list");
    check_equal(static_cast<int>(fresh.allocation()), static_cast<int>(Allocation::May), "a list is allocated");
    check(fresh.allocates && fresh.allocates->span.line == line_of(summaries(environment) / "facts.nra", "= new([n]);"),
          "the allocation is attributed to the list literal");

    const auto& prints = summary_of(program, purity, "Facts.prints");
    check(prints.host.has_value(), "printing makes a host request");
    const auto cause = purity.origin(find_symbol(program, "Facts.prints"), Fact::Host);
    check(cause && cause->callee == no_symbol && cause->operation.find("host request") != std::string::npos,
          "the chain ends at the host request");

    const auto& spawns = summary_of(program, purity, "Facts.spawns");
    check(spawns.scheduler.has_value(), "a task scope uses the scheduler");
    check(!summary_of(program, purity, "Facts.adds").scheduler, "adding does not");

    const auto& touches = summary_of(program, purity, "Facts.touches_static");
    check(touches.class_init.has_value(), "reading a static field may run the class's initializer");
    check(touches.class_init->operation.find("static initialization of 'Counter'") != std::string::npos,
          "the cause names the class: " + touches.class_init->operation);
    check_equal(touches.class_init->span.line, line_of(summaries(environment) / "facts.nra", "Counter.created;"),
                "the initializer is attributed to the access that runs it");
    check(!quiet.class_init && !adds.class_init, "functions that touch no class run no initializer");

    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        if (const auto* found = purity.summary(id)) {
            check(!found->hooks, "nothing sets the reserved hook fact: " + program.symbols[id].name);
        }
    }
}

NEURA_TEST(structure, summaries_reach_a_fixed_point_over_mutual_recursion) {
    const auto program = facts_program(environment);
    const PurityAnalysis purity(program);
    for (const auto* name : {"Facts.even_free", "Facts.odd_free"}) {
        const auto& item = summary_of(program, purity, name);
        check_equal(static_cast<int>(item.allocation()), static_cast<int>(Allocation::Free),
                    std::string(name) + " allocates nothing");
        check(item.traps.has_value(), std::string(name) + " may trap (the subtraction, the call)");
    }
    // pong allocates and ping only calls pong, which calls ping back.
    for (const auto* name : {"Facts.ping", "Facts.pong"}) {
        check_equal(static_cast<int>(summary_of(program, purity, name).allocation()), static_cast<int>(Allocation::May),
                    std::string(name) + " allocates through the cycle");
    }
    const auto cause = purity.origin(find_symbol(program, "Facts.ping"), Fact::Allocation);
    check(cause && cause->callee == no_symbol &&
              cause->span.line == line_of(summaries(environment) / "facts.nra", "new([n as int64]);"),
          "ping's allocation is attributed to pong's list literal");
}

NEURA_TEST(structure, summaries_use_known_targets_of_virtual_calls_and_give_up_on_the_rest) {
    const auto program = facts_program(environment);
    const PurityAnalysis purity(program);
    // Blob.area allocates, so a call that may reach it may allocate; the
    // call has known targets, so it is not unknown.
    const auto& known = summary_of(program, purity, "Facts.area_of");
    check_equal(static_cast<int>(known.allocation()), static_cast<int>(Allocation::May),
                "a virtual call with known targets takes their facts");
    check(!known.host && !known.scheduler, "and only theirs");
    const auto& orphan = summary_of(program, purity, "Facts.identity_of");
    check_equal(static_cast<int>(orphan.allocation()), static_cast<int>(Allocation::Unknown),
                "a virtual call without a target is unknown");
    check(orphan.traps && orphan.host && orphan.scheduler && orphan.class_init, "in every fact");
    check_equal(static_cast<int>(retention(orphan, 1)), static_cast<int>(Retention::Unknown), "and the receiver may be kept");
}

NEURA_TEST(structure, summaries_call_through_function_values_is_unknown) {
    const auto program = facts_program(environment);
    const PurityAnalysis purity(program);
    const auto& item = summary_of(program, purity, "Facts.call_value");
    check_equal(static_cast<int>(item.allocation()), static_cast<int>(Allocation::Unknown), "allocation is unknown");
    check(item.traps && item.host && item.scheduler && item.class_init, "so is every other fact");
    check_equal(static_cast<int>(retention(item, 2)), static_cast<int>(Retention::Unknown),
                "what is passed to a function value may be kept anywhere");
    check_equal(static_cast<int>(item.ownership), static_cast<int>(Ownership::Fresh),
                "a number result holds no reference");
    const auto& result = summary_of(program, purity, "Facts.value_result");
    check_equal(static_cast<int>(result.ownership), static_cast<int>(Ownership::Unknown),
                "what a function value returns has an unknown owner");
}

NEURA_TEST(structure, summaries_of_kernels_come_from_their_signature) {
    const auto program = facts_program(environment);
    const PurityAnalysis purity(program);
    SymbolId kernel = no_symbol;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        if (kernel_bound(program.symbols[id]) && program.symbols[id].name == "i64" &&
            kernel_binding(program.symbols[id])->first == "neura.sort.i64") {
            kernel = id;
        }
    }
    check(kernel != no_symbol, "the program binds the i64 sort kernel");
    const auto& item = *purity.summary(kernel);
    check_equal(static_cast<int>(item.allocation()), static_cast<int>(Allocation::May), "a kernel allocates its result");
    check(item.traps.has_value(), "and may trap");
    check(!item.host && !item.scheduler && !item.class_init, "it makes no host request and uses no scheduler");
    check_equal(static_cast<int>(retention(item, 1)), static_cast<int>(Retention::NotRetained), "it keeps no argument");
    check(!item.writes.empty(), "and it writes the list it sorts");
    const auto& user = summary_of(program, purity, "Facts.sorts");
    check(!user.host && !user.scheduler, "a function that calls it has no host or scheduler fact");
    check_equal(static_cast<int>(retention(user, 1)), static_cast<int>(Retention::NotRetained), "and keeps the list no more");
}

namespace {

// A host binding by its service name. The console's byte write takes a list
// of bytes, which a summary tracks; its line write takes a string, an
// immutable reference that it does not.
SymbolId binding_named(const IrProgram& program, std::string_view service) {
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.host_binding && !item.has_body && item.host_binding->name == service) return id;
    }
    fail("the program has no binding for " + std::string(service));
}

SymbolId console_binding(const IrProgram& program) { return binding_named(program, "neura.console.write_line"); }

// A function whose body calls the binding.
SymbolId caller_of(const IrProgram& program, SymbolId binding) {
    std::function<bool(const std::vector<IrNode>&)> calls = [&](const std::vector<IrNode>& nodes) {
        return std::ranges::any_of(nodes, [&](const IrNode& node) {
            return (node.op == IrOp::ExternalCall && node.symbol == binding) || calls(node.operands) ||
                   calls(node.body) || calls(node.alternative);
        });
    };
    for (const auto& body : program.bodies) {
        if (calls(body.instructions)) return body.owner;
    }
    fail("nothing calls the binding");
}

} // namespace

NEURA_TEST(structure, summaries_of_host_bindings_need_a_contract) {
    auto program = facts_program(environment);
    const auto binding = binding_named(program, "neura.console.write");
    const auto caller = caller_of(program, binding);
    {
        const PurityAnalysis purity(program);
        const auto& item = *purity.summary(binding);
        check_equal(static_cast<int>(item.allocation()), static_cast<int>(Allocation::Unknown),
                    "without a contract the allocation is unknown");
        check(item.traps && item.host && item.scheduler && item.class_init, "and so is every other fact");
        check_equal(static_cast<int>(retention(item, 2)), static_cast<int>(Retention::Unknown), "and every argument");
        check_equal(static_cast<int>(item.ownership), static_cast<int>(Ownership::Unknown), "and the result");
        check_equal(static_cast<int>(purity.summary(caller)->allocation()), static_cast<int>(Allocation::Unknown),
                    "a caller inherits the unknown");
    }
    for (const bool retains : {false, true}) {
        set_attribute(program.symbols[binding], AttributeId::HostEffects,
                      host_effects_payload({.allocates = false, .traps = true, .scheduler = false, .retains = retains}));
        check(verify_ir(program).empty(), "a contract on a host binding verifies");
        const PurityAnalysis purity(program);
        const auto& item = *purity.summary(binding);
        check_equal(static_cast<int>(item.allocation()), static_cast<int>(Allocation::Free), "the contract says no allocation");
        check(item.traps.has_value() && item.host.has_value(), "it may trap, and it is a host request");
        check(!item.scheduler && !item.class_init, "it does not use the scheduler");
        const auto expected = static_cast<int>(retains ? Retention::Elsewhere : Retention::NotRetained);
        check_equal(static_cast<int>(retention(item, 2)), expected, "retention follows the contract");
        check(purity.summary(caller)->host.has_value(), "the caller makes a host request");
        // The optimizer still takes a host call as unknown, whatever the contract.
        check(flags_of(*purity.summary(caller)) == everything, "the optimizer's flags ignore the contract");
    }
}

NEURA_TEST(structure, summaries_record_how_each_argument_is_retained) {
    const auto program = facts_program(environment);
    const PurityAnalysis purity(program);
    const auto kind = [&](std::string_view name, std::size_t slot) {
        return retention(summary_of(program, purity, name), slot);
    };
    check_equal(static_cast<int>(kind("Facts.reads_only", 1)), static_cast<int>(Retention::NotRetained), "read only");
    check_equal(static_cast<int>(kind("Facts.quiet", 1)), static_cast<int>(Retention::NotRetained), "a number");
    check_equal(static_cast<int>(kind("Facts.identity", 1)), static_cast<int>(Retention::ByResult), "returned");
    check_equal(static_cast<int>(kind("Facts.store_global", 1)), static_cast<int>(Retention::Elsewhere), "stored in a global");
    check_equal(static_cast<int>(kind("Facts.store_field", 2)), static_cast<int>(Retention::Elsewhere),
                "stored in the field of another argument");
    check_equal(static_cast<int>(kind("Facts.store_field", 1)), static_cast<int>(Retention::NotRetained),
                "the object that receives it is not itself kept");
    check_equal(static_cast<int>(kind("Facts.call_value", 2)), static_cast<int>(Retention::Unknown), "given to a function value");
    check_equal(static_cast<int>(kind("Facts.prints", 1)), static_cast<int>(Retention::Unknown),
                "a string is an immutable reference, which is not tracked");
    check_equal(static_cast<int>(retention(summary_of(program, purity, "Frozen.read"), 0)),
                static_cast<int>(Retention::Unknown), "the receiver of an immutable class is not tracked");
    const auto& cause = summary_of(program, purity, "Facts.store_global").retained_at;
    check(cause.contains(1) && cause.at(1).span.line == line_of(summaries(environment) / "facts.nra", "registry = items;"),
          "the retention is attributed to the store");
}

NEURA_TEST(structure, summaries_record_who_owns_a_result) {
    const auto program = facts_program(environment);
    const PurityAnalysis purity(program);
    const auto& fresh = summary_of(program, purity, "Facts.fresh_list");
    check_equal(static_cast<int>(fresh.ownership), static_cast<int>(Ownership::Fresh), "a new list is fresh");
    const auto& borrowed = summary_of(program, purity, "Facts.identity");
    check_equal(static_cast<int>(borrowed.ownership), static_cast<int>(Ownership::Borrowed), "an argument is borrowed");
    check_equal(borrowed.borrowed_slot, std::size_t{1}, "from parameter 1");
    const auto& global = summary_of(program, purity, "Facts.static_list");
    check_equal(static_cast<int>(global.ownership), static_cast<int>(Ownership::Static), "a global list is static");
    const auto& unknown = summary_of(program, purity, "Facts.value_result");
    check_equal(static_cast<int>(unknown.ownership), static_cast<int>(Ownership::Unknown), "a function value's result is unknown");
    check(unknown.ownership_cause.has_value(), "the unknown has a cause");
}

// A .nir file holds the program as lowered. Both sides are then prepared as a
// build prepares them, so the summaries are those of the linked program.
NEURA_TEST(structure, summaries_from_source_and_from_nir_are_equal) {
    for (const auto& file : {summaries(environment) / "facts.nra", environment.fixtures() / "purity.nra",
                             environment.fixtures() / "inlining.nra"}) {
        auto program = lower_files({file});
        auto reread = read_nir(write_nir(program), "program.nir");
        check(same_symbols(program, reread), "the symbols survive");
        for (auto* item : {&program, &reread}) {
            materialize_reference_storage(*item);
            specialize(*item);
        }
        const PurityAnalysis left(program);
        const PurityAnalysis right(reread);
        std::size_t compared = 0;
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            const auto* a = left.summary(id);
            const auto* b = right.summary(id);
            const auto name = file.filename().string() + ": " + qualified_name(program, id);
            check_equal(a != nullptr, b != nullptr, "both have a summary for " + name);
            if (!a) continue;
            ++compared;
            check(a->same(*b), name + " has equal raw summaries");
            check(a->retention == b->retention && a->ownership == b->ownership && a->borrowed_slot == b->borrowed_slot,
                  name + " has equal retention and ownership");
            check_equal(static_cast<int>(a->allocation()), static_cast<int>(b->allocation()), name + " allocation");
            check(a->traps.has_value() == b->traps.has_value() && a->host.has_value() == b->host.has_value() &&
                      a->scheduler.has_value() == b->scheduler.has_value() &&
                      a->class_init.has_value() == b->class_init.has_value(),
                  name + " has equal facts");
        }
        check(compared > 10, "summaries were compared");
    }
}

// ---- contracts that are rejected --------------------------------------------

namespace {

bool reports(const IrProgram& program, std::string_view text) {
    return std::ranges::any_of(verify_ir(program), [&](const VerificationIssue& issue) {
        return issue.message.find(text) != std::string::npos;
    });
}

bool rejected_by_reader(const IrProgram& program, std::string_view text) {
    try {
        const auto bytes = write_nir(program);
        read_nir(bytes, "contract.nir");
    } catch (const std::exception& error) {
        return std::string(error.what()).find(text) != std::string::npos;
    }
    return false;
}

} // namespace

NEURA_TEST(verifier, rejects_a_host_contract_on_a_function_with_a_body) {
    auto program = facts_program(environment);
    const auto function = find_symbol(program, "Facts.quiet");
    check(program.symbols[function].has_body, "quiet has a body");
    set_attribute(program.symbols[function], AttributeId::HostEffects, host_effects_payload({}));
    check(reports(program, "a host-effects contract belongs to a host binding without a body"),
          "the verifier rejects a contract on a function with a body");
    check(rejected_by_reader(program, "a host-effects contract belongs to a host binding without a body"),
          "the .nir reader rejects it too");
}

NEURA_TEST(verifier, rejects_a_host_contract_on_a_function_that_is_not_a_host_binding) {
    auto program = facts_program(environment);
    SymbolId kernel = no_symbol;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        if (kernel_bound(program.symbols[id])) kernel = id;
    }
    check(kernel != no_symbol, "the program binds a kernel");
    set_attribute(program.symbols[kernel], AttributeId::HostEffects, host_effects_payload({}));
    check(reports(program, "a host-effects contract belongs to a host binding without a body"),
          "a kernel takes its summary from its signature, not a contract");
}

NEURA_TEST(verifier, rejects_a_host_contract_that_breaks_its_schema) {
    auto program = facts_program(environment);
    const auto binding = console_binding(program);
    for (const auto& bad : {attribute_list({attribute_bool(true), attribute_bool(false), attribute_bool(true)}),
                            attribute_list({attribute_bool(true), attribute_bool(false), attribute_bool(true),
                                            attribute_text("yes")}),
                            attribute_bool(true), attribute_list({})}) {
        auto broken = program;
        set_attribute(broken.symbols[binding], AttributeId::HostEffects, bad);
        check(reports(broken, "host.effects"), "the verifier names the attribute whose payload is wrong");
        check(rejected_by_reader(broken, "host.effects"), "the writer or the reader refuses it");
    }
    // A well-formed contract passes, and survives the .nir round trip.
    set_attribute(program.symbols[binding], AttributeId::HostEffects, host_effects_payload({}));
    check(verify_ir(program).empty(), "a well-formed contract verifies");
    const auto reread = read_nir(write_nir(program), "contract.nir");
    check(host_effects(reread.symbols[binding]).has_value(), "the contract survives the .nir file");
}

// The standard library declares the contract with #HostEffects; lowering turns
// the tag into the attribute and the summary reads it. The library is copied
// so that one binding can carry the tag.
NEURA_TEST(structure, host_effects_tag_becomes_the_contract_attribute) {
    const auto copy = fs::temp_directory_path() / ("neura-summaries-" + std::to_string(::getpid()));
    fs::remove_all(copy);
    fs::copy(environment.source_dir / "stdlib", copy, fs::copy_options::recursive);
    {
        const auto path = copy / "std" / "io" / "console.nra";
        std::ifstream in(path);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string anchor = "    let write_line = func(text: string) -> WriteOutcome host(";
        const auto at = text.find(anchor);
        check(at != std::string::npos, "the console declares write_line");
        text.insert(at, "    #HostEffects(allocates: false, retains: false)\n");
        std::ofstream(path) << text;
    }
    const auto saved = library_root();
    library_root() = copy;
    IrProgram program;
    try {
        program = prepared({summaries(environment) / "facts.nra"});
    } catch (...) {
        library_root() = saved;
        fs::remove_all(copy);
        throw;
    }
    library_root() = saved;
    fs::remove_all(copy);
    const auto binding = console_binding(program);
    const auto contract = host_effects(program.symbols[binding]);
    check(contract.has_value(), "the binding carries the contract attribute");
    check(!contract->allocates && contract->traps && contract->scheduler && !contract->retains,
          "an argument that is not written defaults to the side that may");
    check(verify_ir(program).empty(), "the program verifies");
    const PurityAnalysis purity(program);
    check_equal(static_cast<int>(purity.summary(binding)->allocation()), static_cast<int>(Allocation::Free),
                "the summary reads the contract");
}

} // namespace neura::testing
