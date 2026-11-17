// Storage, owner and transfer facts (src/neura-ir/storage.hpp,
// docs/design/Representation030.md "Storage, owner and transfer facts",
// docs/design/Storage030.md "Regions"). Each case lowers a program from
// tests/ir-optimization/storage and checks what the analyses derive from the
// declarations, the body trees and the summaries. No stage enforces an escape
// yet, so these cases are the evidence for the rules, and the survey at the end
// lists what the repository's own programs would have to migrate.
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/storage.hpp"
#include "support.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace neura::testing {
namespace {

using Kind = ResultTransfer::Kind;
using Origin = ResultTransfer::Origin;

fs::path storage_dir(const Environment& environment) {
    return environment.source_dir / "tests" / "ir-optimization" / "storage";
}

IrProgram lowered(const Environment& environment, std::string_view file) {
    return lower_files({storage_dir(environment) / file});
}

// Lowered, then specialized without materializing references: the form in
// which the storage milestone will check escapes the second time.
IrProgram linked(const Environment& environment, std::string_view file) {
    auto program = lowered(environment, file);
    specialize(program);
    return program;
}

std::size_t line_of(const fs::path& file, std::string_view text) {
    std::ifstream in(file);
    std::string line;
    for (std::size_t number = 1; std::getline(in, line); ++number) {
        if (line.find(text) != std::string::npos) return number;
    }
    fail("no line holds " + std::string(text));
}

// The first line at or after the line that holds `anchor` that holds `text`.
std::size_t line_after(const fs::path& file, std::string_view anchor, std::string_view text) {
    std::ifstream in(file);
    std::string line;
    bool anchored = false;
    for (std::size_t number = 1; std::getline(in, line); ++number) {
        anchored = anchored || line.find(anchor) != std::string::npos;
        if (anchored && line.find(text) != std::string::npos) return number;
    }
    fail("no line holds " + std::string(text) + " after " + std::string(anchor));
}

std::string joined_lines(const std::set<std::string>& lines) {
    std::string text;
    for (const auto& line : lines) text += line + "\n";
    return text;
}

// The one local or parameter of `function` with this name.
SymbolId variable(const IrProgram& program, std::string_view function, std::string_view name) {
    const auto owner = find_symbol(program, function);
    SymbolId found = no_symbol;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if ((item.kind == SymbolKind::Local || item.kind == SymbolKind::Parameter) && item.owner == owner &&
            item.name == name) {
            check(found == no_symbol, "more than one variable is named " + std::string(name));
            found = id;
        }
    }
    check(found != no_symbol, "no variable " + std::string(name) + " in " + std::string(function));
    return found;
}

std::string_view name_of(StorageClass storage) { return describe_storage_class(storage); }

void expect_class(const IrProgram& program, SymbolId symbol, StorageClass expected, std::string_view what) {
    const auto found = storage_class(program, symbol);
    check(found.has_value(), std::string(what) + " has a storage class");
    check_equal(std::string(name_of(*found)), std::string(name_of(expected)), std::string(what));
}

NEURA_TEST(structure, storage_class_of_each_kind_of_declaration) {
    const auto program = lowered(environment, "classes.nra");
    expect_class(program, find_symbol(program, "Classes.counter"), StorageClass::Static, "a global scalar");
    expect_class(program, find_symbol(program, "Classes.table"), StorageClass::Static, "a global array");
    expect_class(program, find_symbol(program, "Classes.Holder.size"), StorageClass::InObject, "a scalar field");
    expect_class(program, find_symbol(program, "Classes.Holder.items"), StorageClass::InObject, "an array field");
    expect_class(program, find_symbol(program, "Classes.Holder.alias"), StorageClass::Borrow, "a reference field");
    expect_class(program, find_symbol(program, "Classes.Holder.$static.shared"), StorageClass::Static, "a static field");
    expect_class(program, variable(program, "Classes.sample", "values"), StorageClass::Borrow, "an array parameter");
    expect_class(program, variable(program, "Classes.sample", "count"), StorageClass::Frame, "a scalar parameter");
    expect_class(program, variable(program, "Classes.sample", "view"), StorageClass::Borrow, "a reference parameter");
    expect_class(program, variable(program, "Classes.sample", "pair"), StorageClass::Borrow,
                 "a tuple parameter that holds an array");
    expect_class(program, variable(program, "Classes.sample", "total"), StorageClass::Frame, "a scalar local");
    expect_class(program, variable(program, "Classes.sample", "cells"), StorageClass::Frame, "an array local");
    expect_class(program, variable(program, "Classes.sample", "alias"), StorageClass::Borrow, "a reference local");
    check(!storage_class(program, find_symbol(program, "Classes.sample")).has_value(), "a function has no storage class");
    check(!storage_class(program, no_symbol).has_value(), "no symbol has no storage class");
}

// The creations in the function `name`, by the line that holds them.
std::map<std::size_t, ArrayCreation> creations_in(const IrProgram& program, std::string_view name, StorageStage stage) {
    const auto function = find_symbol(program, name);
    std::map<std::size_t, ArrayCreation> result;
    for (const auto& creation : find_array_creations(program, stage)) {
        if (creation.function != function) continue;
        check(result.emplace(creation.span.line, creation).second, "two creations on one line of " + std::string(name));
    }
    return result;
}

NEURA_TEST(structure, array_owners_follow_blocks_loops_and_branches) {
    const auto file = storage_dir(environment) / "owners.nra";
    for (const auto stage : {StorageStage::Lowered, StorageStage::Specialized}) {
        auto program = lowered(environment, "owners.nra");
        if (stage == StorageStage::Specialized) specialize(program);
        const auto found = creations_in(program, "Owners.shape", stage);
        const auto at = [&](std::string_view text) -> const ArrayCreation& {
            const auto line = line_of(file, text);
            const auto it = found.find(line);
            check(it != found.end(), "a creation on the line of " + std::string(text));
            return it->second;
        };
        const auto& top = at("let top").owner;
        const auto& block = at("let in_block").owner;
        const auto& deeper = at("let deeper").owner;
        const auto& pass = at("let per_pass").owner;
        const auto& then_arm = at("let in_then").owner;
        const auto& else_arm = at("let in_else").owner;
        check(top.path.empty() && top.arm == Region::Function, "the function owns its top-level arrays");
        check(top.encloses(block) && block.encloses(deeper) && top.encloses(deeper), "blocks nest");
        check(!block.encloses(top) && !deeper.encloses(block), "an inner block does not enclose an outer one");
        check(!block.encloses(block), "a scope does not enclose itself");
        check(pass.iteration && !block.iteration, "a loop body is an iteration, a block is not");
        check(top.encloses(pass) && !block.encloses(pass), "a loop is inside its own scope only");
        check(top.encloses(then_arm) && top.encloses(else_arm), "branch arms are inside the function");
        check(!then_arm.encloses(else_arm) && !else_arm.encloses(then_arm) && !(then_arm == else_arm),
              "the two arms of a branch are separate scopes");
        // A range's array is created in a ScopedValue, which opens no scope.
        check(at("let ranged").owner == top, "a range literal belongs to the block that holds it");
        check(at("let by_call").owner == top, "the fresh result of a call belongs to the block that holds it");
        check_equal(at("let by_call").callee, find_symbol(program, "Owners.make"), "the call that creates it");
        check(at("let by_call").op == IrOp::Call || at("let by_call").op == IrOp::VirtualCall, "a call creates it");
        check(at("let top").op == IrOp::Collection, "a literal creates it");
        check(at("let ranged").op == IrOp::RepeatCollection, "a range repeats a value");
        check_equal(creations_in(program, "Owners.make", stage).size(), std::size_t{1}, "the callee's own creation");
    }
}

NEURA_TEST(structure, array_owner_identity_comes_from_the_tree_alone) {
    // Two programs built from the same source give equal owners, and a second
    // array of the same iteration shares it.
    const auto first = creations_in(lowered(environment, "owners.nra"), "Owners.shape", StorageStage::Lowered);
    const auto second = creations_in(lowered(environment, "owners.nra"), "Owners.shape", StorageStage::Lowered);
    check_equal(first.size(), second.size(), "the same creations");
    for (const auto& [line, creation] : first) check(creation.owner == second.at(line).owner, "the same owner");
    auto program = lowered(environment, "escapes.nra");
    const auto found = creations_in(program, "Escapes.outer_literal", StorageStage::Lowered);
    check_equal(found.size(), std::size_t{2}, "two arrays");
    const auto& outer = found.begin()->second.owner;
    const auto& inner = found.rbegin()->second.owner;
    check(outer.encloses(inner) && inner.iteration, "the array built on each pass belongs to that pass");
}

// ---- result transfer ---------------------------------------------------------

struct Transfers {
    IrProgram program;
    PurityAnalysis purity;
    explicit Transfers(IrProgram value) : program(std::move(value)), purity(program) {}
    ResultTransfer recorded(std::string_view name) const {
        return recorded_result_transfer(program, purity, find_symbol(program, name));
    }
    ResultTransfer derived(std::string_view name) const {
        return derived_result_transfer(program, purity, find_symbol(program, name));
    }
};

void expect_transfer(const ResultTransfer& transfer, Kind kind, Origin origin, std::size_t slot, std::string_view what) {
    check(transfer.kind == kind, std::string(what) + ": kind");
    check(transfer.origin == origin, std::string(what) + ": origin");
    if (slot != static_cast<std::size_t>(-1)) check_equal(transfer.slot, slot, std::string(what) + ": slot");
}

NEURA_TEST(structure, result_transfer_is_fresh_or_a_borrow_with_an_origin) {
    for (const auto stage : {StorageStage::Lowered, StorageStage::Specialized}) {
        auto program = lowered(environment, "transfer.nra");
        if (stage == StorageStage::Specialized) specialize(program);
        const Transfers t(std::move(program));
        const auto none = static_cast<std::size_t>(-1);
        for (const auto* name : {"Transfer.fresh"}) {
            expect_transfer(t.recorded(name), Kind::Fresh, Origin::None, none, std::string("recorded ") + name);
            expect_transfer(t.derived(name), Kind::Fresh, Origin::None, none, std::string("derived ") + name);
        }
        for (const auto* name : {"Transfer.from_parameter", "Transfer.forwarded"}) {
            expect_transfer(t.recorded(name), Kind::Borrowed, Origin::Parameter, 1, std::string("recorded ") + name);
            expect_transfer(t.derived(name), Kind::Borrowed, Origin::Parameter, 1, std::string("derived ") + name);
        }
        expect_transfer(t.recorded("Transfer.from_global"), Kind::Borrowed, Origin::Global, none, "recorded global");
        expect_transfer(t.derived("Transfer.from_global"), Kind::Borrowed, Origin::Global, none, "derived global");
        // The receiver's field: the owner is slot 0.
        expect_transfer(t.recorded("Transfer.Box.get"), Kind::Borrowed, Origin::Parameter, 0, "recorded field");
        expect_transfer(t.derived("Transfer.Box.get"), Kind::Borrowed, Origin::Field, 0, "derived field");
        check_equal(t.derived("Transfer.Box.get").symbol, find_symbol(t.program, "Transfer.Box.items"), "the field");
        // Two sources: neither a fresh value nor one borrow.
        expect_transfer(t.recorded("Transfer.from_either"), Kind::Unknown, Origin::Unknown, none, "recorded either");
        expect_transfer(t.derived("Transfer.from_either"), Kind::Unknown, Origin::Unknown, none, "derived either");
        expect_transfer(t.recorded("Transfer.scalar"), Kind::None, Origin::None, none, "a scalar result");
        expect_transfer(t.derived("Transfer.scalar"), Kind::None, Origin::None, none, "a scalar result");
        check(describe_result_transfer(t.program, t.derived("Transfer.Box.get")) == "borrowed from field items of slot 0",
              "the description names the field");
        check(check_result_transfer(t.program, t.purity, stage).empty(), "every result agrees with its body");
    }
}

NEURA_TEST(structure, result_transfer_check_reports_a_summary_that_contradicts_the_body) {
    const Transfers t(linked(environment, "transfer.nra"));
    const auto from_parameter = find_symbol(t.program, "Transfer.from_parameter");
    const auto fresh = find_symbol(t.program, "Transfer.fresh");
    // A summary edited by hand: the claim under test is an argument.
    ResultTransfer claim;
    claim.kind = Kind::Fresh;
    auto mismatch = check_result_transfer(t.program, t.purity, from_parameter, claim);
    check(mismatch.has_value(), "a fresh claim for a borrowed result");
    check_equal(mismatch->function, from_parameter, "the function");
    check(mismatch->derived.kind == Kind::Borrowed && mismatch->recorded.kind == Kind::Fresh, "both sides");
    check_equal(mismatch->span.line, line_of(storage_dir(environment) / "transfer.nra", "return source;"),
                "the return that decides it");
    claim.kind = Kind::Borrowed;
    claim.origin = Origin::Parameter;
    claim.slot = 2;
    check(check_result_transfer(t.program, t.purity, from_parameter, claim).has_value(), "the wrong parameter");
    claim.origin = Origin::Global;
    check(check_result_transfer(t.program, t.purity, from_parameter, claim).has_value(), "the wrong origin");
    claim.origin = Origin::Parameter;
    claim.slot = 1;
    check(!check_result_transfer(t.program, t.purity, from_parameter, claim).has_value(), "the right claim");
    claim.kind = Kind::Unknown;
    claim.origin = Origin::Unknown;
    check(!check_result_transfer(t.program, t.purity, from_parameter, claim).has_value(),
          "an unknown claim contradicts nothing");
    claim.kind = Kind::Borrowed;
    claim.origin = Origin::Parameter;
    claim.slot = 1;
    check(check_result_transfer(t.program, t.purity, fresh, claim).has_value(), "a borrow claim for a fresh result");
}

NEURA_TEST(structure, result_transfer_check_reports_a_body_edited_after_its_summary) {
    // The summaries are computed once; editing the tree afterward makes the
    // body contradict them, as a corrupted .nir would.
    auto program = linked(environment, "transfer.nra");
    const PurityAnalysis purity(program);
    check(check_result_transfer(program, purity, StorageStage::Specialized).empty(), "no mismatch before the edit");
    auto& body = body_of(program, "Transfer.from_parameter");
    check_equal(body.size(), std::size_t{1}, "a single return");
    auto& value = body.front().operands.front();
    IrNode literal;
    literal.op = IrOp::Collection;
    literal.type = value.type;
    literal.span = value.span;
    IrNode element;
    element.op = IrOp::Integer;
    element.type = value.type.arguments.front();
    element.text = "1";
    element.span = value.span;
    literal.operands.push_back(element);
    value = literal;
    const auto mismatches = check_result_transfer(program, purity, StorageStage::Specialized);
    check_equal(mismatches.size(), std::size_t{1}, "one function now contradicts its summary");
    check_equal(mismatches.front().function, find_symbol(program, "Transfer.from_parameter"), "the edited function");
    check(mismatches.front().recorded.kind == Kind::Borrowed && mismatches.front().derived.kind == Kind::Fresh,
          "recorded borrowed, derived fresh");
}

// ---- escapes -----------------------------------------------------------------

// The escapes of function `name`: kind, operation line, storage line.
struct Found {
    EscapeKind kind;
    std::size_t operation;
    std::size_t storage;
    bool operator==(const Found&) const = default;
};

std::ostream& operator<<(std::ostream& out, const Found& found) {
    return out << describe_escape_kind(found.kind) << " @" << found.operation << " of " << found.storage;
}

std::string joined(const std::vector<Found>& items) {
    std::ostringstream out;
    for (const auto& item : items) out << item << "; ";
    return out.str();
}

std::vector<Found> escapes_of(const IrProgram& program, StorageStage stage, std::string_view name) {
    const auto function = find_symbol(program, name);
    std::vector<Found> result;
    for (const auto& escape : find_storage_escapes(program, stage)) {
        if (escape.function == function) result.push_back({escape.kind, escape.operation.line, escape.storage.line});
    }
    return result;
}

// Checks one function at both stages: the lowering and the linked program
// find the same escapes ("checked once on lowering and again on the
// specialized, linked program").
void expect_escapes(const Environment& environment, std::string_view file, std::string_view function,
                    std::vector<Found> expected) {
    const auto lowered_program = lowered(environment, file);
    check_equal(joined(escapes_of(lowered_program, StorageStage::Lowered, function)), joined(expected),
                std::string(function) + " on lowering");
    const auto linked_program = linked(environment, file);
    check_equal(joined(escapes_of(linked_program, StorageStage::Specialized, function)), joined(expected),
                std::string(function) + " on the linked program");
}

struct Lines {
    fs::path file;
    std::size_t of(std::string_view text) const { return line_of(file, text); }
};

NEURA_TEST(structure, escape_of_an_array_into_a_field_a_global_or_a_list) {
    const Lines at{storage_dir(environment) / "escapes.nra"};
    using E = EscapeKind;
    expect_escapes(environment, "escapes.nra", "Escapes.Holder.init",
                   {{E::ArrayInField, at.of("instance.items = source"), at.of("export let init = func(source")}});
    expect_escapes(environment, "escapes.nra", "Escapes.to_global",
                   {{E::ArrayInGlobal, at.of("table = source"), at.of("export let to_global")}});
    expect_escapes(environment, "escapes.nra", "Escapes.to_static",
                   {{E::ArrayInGlobal, at.of("Cache.latest = source"), at.of("export let to_static")}});
    expect_escapes(environment, "escapes.nra", "Escapes.stored_in_list",
                   {{E::ArrayInListElement, at.of("rows[0] = source"), at.of("export let stored_in_list")}});
    expect_escapes(environment, "escapes.nra", "Escapes.appended",
                   {{E::ArrayInListElement, at.of("rows.append(source)"), at.of("export let appended")}});
    expect_escapes(environment, "escapes.nra", "Escapes.listed",
                   {{E::ArrayInListElement, at.of("= [source]"), at.of("export let listed")}});
    // The declaration of the storage is the parameter that holds the array.
    const auto program = lowered(environment, "escapes.nra");
    for (const auto& escape : find_storage_escapes(program)) {
        if (escape.function != find_symbol(program, "Escapes.Holder.init")) continue;
        check_equal(program.symbols[escape.target].name, std::string("items"), "the target is the field");
        check_equal(program.symbols[escape.symbol].name, std::string("source"), "the storage is the parameter");
    }
}

NEURA_TEST(structure, escape_of_an_array_captured_by_a_closure_or_a_task) {
    const Lines at{storage_dir(environment) / "escapes.nra"};
    using E = EscapeKind;
    expect_escapes(environment, "escapes.nra", "Escapes.captured",
                   {{E::ArrayCapturedByClosure, line_after(at.file, "export let captured", "let read = func"),
                    at.of("export let captured")}});
}

NEURA_TEST(structure, escape_of_an_array_given_to_a_task) {
    // Specialization already refuses the program (an array is not transferable,
    // Concurrency.md section 5.1), so only lowering is checked.
    const Lines at{storage_dir(environment) / "concurrency.nra"};
    const auto program = lowered(environment, "concurrency.nra");
    const auto found = escapes_of(program, StorageStage::Lowered, "Tasks.spawned");
    check_equal(found.size(), std::size_t{1}, "one escape");
    check(found.front().kind == EscapeKind::ArrayInTaskArgument, "the array is a task argument");
    check_equal(found.front().operation, at.of("spawn work(source)"), "at the spawn");
    check_equal(found.front().storage, at.of("export let spawned"), "of the parameter");
    bool refused = false;
    try {
        auto again = lowered(environment, "concurrency.nra");
        specialize(again);
    } catch (const Diagnostic&) {
        refused = true;
    }
    check(refused, "specialization refuses an array that crosses to a task");
}

NEURA_TEST(structure, escape_of_an_array_sent_on_a_channel) {
    // An array cannot be a message in a program that specializes (it is not
    // transferable), so the instance is built by hand: the send of the
    // generic channel, with its value typed as an array.
    auto program = lowered(environment, "concurrency.nra");
    const auto sends = [&] {
        std::size_t count = 0;
        for (const auto& escape : find_storage_escapes(program)) {
            if (escape.kind == EscapeKind::ArrayInChannelMessage) ++count;
        }
        return count;
    };
    check_equal(sends(), std::size_t{0}, "the generic send holds a type parameter, not an array");
    const auto array = variable(program, "Tasks.sent", "source");
    const auto array_type = program.symbols[array].type;
    check_equal(static_cast<int>(array_type.kind), static_cast<int>(TypeKind::Array), "an array type to send");
    std::size_t edited = 0;
    SymbolId sender = no_symbol;
    std::function<void(IrNode&, SymbolId)> edit = [&](IrNode& node, SymbolId owner) {
        if (node.op == IrOp::SchedulerCall && node.text == "channel.send" && node.operands.size() > 1) {
            node.operands[1].type = array_type;
            ++edited;
            sender = owner;
            return;
        }
        for (auto* region : {&node.operands, &node.body, &node.alternative}) {
            for (auto& child : *region) edit(child, owner);
        }
    };
    for (auto& body : program.bodies) {
        for (auto& node : body.instructions) edit(node, body.owner);
    }
    check(edited > 0, "the standard library sends on a channel");
    check_equal(sends(), std::size_t{1}, "an array sent on a channel");
    for (const auto& escape : find_storage_escapes(program)) {
        if (escape.kind == EscapeKind::ArrayInChannelMessage) check_equal(escape.function, sender, "in the send");
    }
}

NEURA_TEST(structure, escape_of_an_inner_array_assigned_to_an_outer_binding) {
    const Lines at{storage_dir(environment) / "escapes.nra"};
    using E = EscapeKind;
    expect_escapes(environment, "escapes.nra", "Escapes.outer_binding",
                   {{E::ArrayAssignedToOuterBinding, at.of("keep = inner"), at.of("let inner")}});
    expect_escapes(environment, "escapes.nra", "Escapes.outer_literal",
                   {{E::ArrayAssignedToOuterBinding, at.of("keep = [i]"), at.of("keep = [i]")}});
    const auto program = lowered(environment, "escapes.nra");
    for (const auto& escape : find_storage_escapes(program)) {
        if (escape.function != find_symbol(program, "Escapes.outer_binding")) continue;
        check_equal(program.symbols[escape.target].name, std::string("keep"), "the outer binding");
    }
}

NEURA_TEST(structure, escape_of_a_returned_array_that_was_not_created) {
    const Lines at{storage_dir(environment) / "escapes.nra"};
    using E = EscapeKind;
    expect_escapes(environment, "escapes.nra", "Escapes.returns_parameter",
                   {{E::ArrayReturnedBorrowed, at.of("return source;"), at.of("export let returns_parameter")}});
    expect_escapes(environment, "escapes.nra", "Escapes.returns_global",
                   {{E::ArrayReturnedBorrowed, at.of("return table;"), at.of("let table")}});
    expect_escapes(environment, "escapes.nra", "Escapes.returns_field",
                   {{E::ArrayReturnedBorrowed, at.of("return holder.items;"), at.of("export let items")}});
}

NEURA_TEST(structure, escape_of_a_reference_to_a_local_or_parameter) {
    const Lines at{storage_dir(environment) / "escapes.nra"};
    using E = EscapeKind;
    expect_escapes(environment, "escapes.nra", "Escapes.returns_reference",
                   {{E::ReferenceReturned, at.of("return cell;"), at.of("let cell: int = start")}});
    expect_escapes(environment, "escapes.nra", "Escapes.View.init",
                   {{E::ReferenceInField, at.of("instance.total = local"), at.of("let local: int = 1")}});
    expect_escapes(environment, "escapes.nra", "Escapes.captures_reference",
                   {{E::ReferenceCapturedByClosure, line_after(at.file, "export let captures_reference", "let read = func"),
                    at.of("let alias: &int = cell")}});
    // The reference is passed to a constructor that keeps it in a field.
    expect_escapes(environment, "escapes.nra", "Escapes.retained_reference",
                   {{E::ReferenceRetainedByCall, at.of("new(cell)"),
                    line_after(at.file, "export let retained_reference", "let cell: int = 1")}});
}

NEURA_TEST(structure, nothing_escapes_where_the_storage_outlives_its_use) {
    // An inner array used only inside its block, an outer array borrowed in an
    // inner block, an array per loop pass, a returned fresh array, a range, a
    // reference that is only forwarded or used, and a copy of a parameter.
    for (const auto stage : {StorageStage::Lowered, StorageStage::Specialized}) {
        auto program = lowered(environment, "clean.nra");
        if (stage == StorageStage::Specialized) specialize(program);
        for (const auto& escape : find_storage_escapes(program, stage)) {
            if (escape.operation.file.ends_with("clean.nra")) {
                fail("clean.nra has a finding: " + std::string(describe_escape_kind(escape.kind)) + " at " +
                     describe(escape.operation));
            }
        }
        check_equal(creations_in(program, "Clean.per_pass", stage).size(), std::size_t{1}, "the loop's array exists");
    }
}

NEURA_TEST(structure, generic_function_returning_its_argument_is_flagged_in_the_instance) {
    const Lines at{storage_dir(environment) / "generic.nra"};
    const auto generic = lowered(environment, "generic.nra");
    for (const auto& escape : find_storage_escapes(generic)) {
        check(!escape.operation.file.ends_with("generic.nra"), "a type parameter is not an array on lowering");
    }
    auto program = generic;
    specialize(program);
    std::vector<std::string> flagged;
    for (const auto& escape : find_storage_escapes(program, StorageStage::Specialized)) {
        if (!escape.operation.file.ends_with("generic.nra")) continue;
        check(escape.kind == EscapeKind::ArrayReturnedBorrowed, "an array returned but not created");
        check_equal(escape.operation.line, at.of("return value;"), "at the return");
        check_equal(escape.storage.line, at.of("export let echo"), "of the parameter");
        flagged.push_back(program.symbols[escape.function].name);
    }
    check_equal(flagged.size(), std::size_t{1}, "one instance is flagged");
    check_equal(flagged.front(), std::string("echo<int32[]>"), "the array instance, not the int one");
    check(!program.symbols[find_symbol(program, "Generic.echo")].generics.empty(), "the template is still there");
    // The template is the program's only copy on lowering and is skipped after.
    check(find_storage_escapes(program, StorageStage::Lowered).size() >= 1, "both copies are visible without the stage");
}

NEURA_TEST(structure, escape_findings_say_where_they_are_and_leave_the_program_alone) {
    auto program = linked(environment, "escapes.nra");
    const auto before = printed(program);
    const auto escapes = find_storage_escapes(program, StorageStage::Specialized);
    find_array_creations(program, StorageStage::Specialized);
    PurityAnalysis purity(program);
    check_result_transfer(program, purity, StorageStage::Specialized);
    check(before == printed(program), "the analyses do not change the program");
    check(!escapes.empty(), "there are findings");
    std::size_t own = 0;
    for (const auto& escape : escapes) {
        if (!escape.operation.file.ends_with("escapes.nra")) continue;
        ++own;
        check(escape.function != no_symbol, "a function");
        check(escape.operation.line > 0 && escape.storage.line > 0, "both spans");
        check(escape.operation.file.ends_with("escapes.nra") && escape.storage.file.ends_with("escapes.nra"),
              "the spans name this file");
    }
    check_equal(own, std::size_t{16}, "the findings of escapes.nra");
}

// ---- the repository's programs ---------------------------------------------------

std::vector<fs::path> backend_programs(const Environment& environment) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(environment.source_dir / "tests" / "backend" / "programs")) {
        if (entry.is_regular_file() && entry.path().extension() == ".nra") files.push_back(entry.path());
    }
    std::ranges::sort(files);
    check(files.size() > 20, "the backend programs are found");
    return files;
}

std::string where(const fs::path& root, const Span& span) {
    return fs::relative(span.file, root).generic_string() + ":" + std::to_string(span.line);
}

NEURA_TEST(structure, storage_escapes_of_the_backend_programs) {
    // Every program in tests/backend/programs, each lowered on its own with
    // the library it uses, checked on lowering and on the specialized program.
    // This is what the change that enforces the region rules has to migrate,
    // in the programs and in the standard library. Each finding was checked
    // against the rules; the reasons are in the list below.
    //
    // primitive-references.nra:17  `retained` returns a reference to its by-value
    //     parameter `value`, which dies with the frame.
    // primitive-references.nra:70, 71, 98  `new Counter(local, ...)` passes a
    //     reference to the local `local` to a constructor that keeps it in the
    //     field `alias`; the reference outlives the argument's block.
    // ranges-and-variadics.nra:13  `inspect` returns its variadic pack, an
    //     array that the caller created, so it returns an array it did not create.
    // stdlib/std/io/internal/formatting.nra:56  the formatter keeps the array
    //     of arguments it was given in a field.
    const std::vector<std::string> expected{
        "lowered formatting.nra:56 array stored in a field (stdlib/std/io/internal/formatting.nra:56)",
        "lowered primitive-references.nra:17 reference to a local returned",
        "lowered primitive-references.nra:70 reference to a local retained by a call",
        "lowered primitive-references.nra:71 reference to a local retained by a call",
        "lowered primitive-references.nra:98 reference to a local retained by a call",
        "lowered ranges-and-variadics.nra:13 array returned but not created",
        "specialized formatting.nra:56 array stored in a field (stdlib/std/io/internal/formatting.nra:56)",
        "specialized primitive-references.nra:17 reference to a local returned",
        "specialized primitive-references.nra:70 reference to a local retained by a call",
        "specialized primitive-references.nra:71 reference to a local retained by a call",
        "specialized primitive-references.nra:98 reference to a local retained by a call",
        "specialized ranges-and-variadics.nra:13 array returned but not created",
    };
    std::set<std::string> found;
    const auto collect = [&](const IrProgram& program, StorageStage stage, std::string_view label, const fs::path& file) {
        for (const auto& escape : find_storage_escapes(program, stage)) {
            const bool own = fs::path(escape.operation.file) == file;
            auto text = std::string(label) + " " + fs::path(escape.operation.file).filename().string() + ":" +
                        std::to_string(escape.operation.line) + " " + std::string(describe_escape_kind(escape.kind));
            if (!own) text += " (" + where(environment.source_dir, escape.operation) + ")";
            found.insert(std::move(text));
        }
    };
    for (const auto& file : backend_programs(environment)) {
        auto program = lower_files({file});
        collect(program, StorageStage::Lowered, "lowered", file);
        specialize(program);
        collect(program, StorageStage::Specialized, "specialized", file);
    }
    check_equal(joined_lines(found), joined_lines({expected.begin(), expected.end()}),
                "escapes of the backend programs");
}

NEURA_TEST(structure, result_transfers_agree_with_summaries_in_the_backend_programs) {
    // The recomputation from the body and the summary of PurityAnalysis are two
    // derivations of one fact; over every function of every program, with the
    // standard library, they must never contradict each other.
    std::size_t compared = 0;
    auto files = backend_programs(environment);
    for (const auto* fixture : {"classes", "owners", "transfer", "escapes", "clean", "generic"}) {
        files.push_back(storage_dir(environment) / (std::string(fixture) + ".nra"));
    }
    for (const auto& file : files) {
        auto program = lower_files({file});
        for (const auto stage : {0, 1, 2}) {
            auto copy = program;
            if (stage >= 1) materialize_reference_storage(copy);
            if (stage >= 1) specialize(copy);
            const PurityAnalysis purity(copy);
            for (const auto& body : copy.bodies) {
                const auto recorded = recorded_result_transfer(copy, purity, body.owner);
                if (recorded.kind == Kind::Fresh || recorded.kind == Kind::Borrowed) {
                    const auto derived = derived_result_transfer(copy, purity, body.owner);
                    if (derived.kind == Kind::Fresh || derived.kind == Kind::Borrowed) ++compared;
                }
            }
            const auto mismatches =
                check_result_transfer(copy, purity, stage >= 1 ? StorageStage::Specialized : StorageStage::Lowered);
            for (const auto& mismatch : mismatches) {
                fail(file.filename().string() + " stage " + std::to_string(stage) + ": " +
                     qualified_name(copy, mismatch.function) + " records " +
                     describe_result_transfer(copy, mismatch.recorded) + " but returns " +
                     describe_result_transfer(copy, mismatch.derived));
            }
        }
    }
    check(compared > 40, "the sweep compares functions that return an array or a reference");
}

} // namespace
} // namespace neura::testing
