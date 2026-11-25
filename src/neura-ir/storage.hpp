#pragma once

#include "ir.hpp"
#include "purity.hpp"

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace neura {

// Storage, owner and transfer facts (docs/design/Representation030.md,
// "Storage, owner and transfer facts") and the escape rules that the storage
// milestone enforces (docs/design/Storage030.md, "Regions").
//
// Nothing here is stored. Every fact is derived from a declaration, its type,
// the tree of a body and the result summaries of PurityAnalysis, and checked
// against them; no attribute, node field or .nir record carries one. Class
// reference policy provenance is reserved for the class-hook milestone and
// has no encoding here.
//
// check_storage() turns every finding into a diagnostic (E0451, E0452,
// E0453 and E0479). The source loader runs it on the lowered program, and
// every driver that specializes runs it again on the specialized, linked
// program, before the optimizer.
//
// Values are tracked by origin: an array that a block creates (it belongs to
// that block, or to the loop iteration that runs it), the parameter, global or
// field it was read from, or a primitive reference to storage in a frame. A
// tuple or enum that contains an array has the array's lifetime and is tracked
// the same way. A local's origins are the union over every store to it, so the
// analysis follows no control flow and may report a finding that a path-
// sensitive one would not; where a rule is open it reports.

// -- Storage classes ---------------------------------------------------------

enum class StorageClass : std::uint8_t {
    Frame,    // lives in the activation: locals, and parameters of other types
    Static,   // module and class-level storage
    InObject, // a field, inside its heap object
    Borrow,   // names storage owned elsewhere: an array parameter, a reference
};

std::string_view describe_storage_class(StorageClass storage);

// The class of a local, parameter, field or global, from its kind, its owner
// and its type. A parameter that holds an array (an array, or a tuple or enum
// containing one) borrows the caller's array. Anything of primitive reference
// type borrows the storage it names. A field is inside its object, except a
// static field, which is static. A local array binding is a frame slot: the
// array it holds belongs to a block (see ArrayCreation). Nothing for any
// other kind of symbol.
std::optional<StorageClass> storage_class(const IrProgram& program, SymbolId symbol);

// -- Owners of fresh arrays --------------------------------------------------

// Where a node sits: Function is the instruction list of the body, and the
// other three are the regions of an enclosing node.
enum class Region : std::uint8_t { Function, Operands, Body, Alternative };

struct OwnerStep {
    Region region = Region::Function;
    std::size_t index = 0;
    bool operator==(const OwnerStep&) const = default;
};

// A lexical scope of a body, identified by the tree alone. `path` leads to
// the node that opens it (empty for the function itself) and `arm` is the
// region of that node the scope is: the body of a Block, either arm of a
// Branch, or the body or latch of a Loop, where `iteration` is set because
// each pass through the loop is its own scope. A ScopedValue and a
// SelectValue open no scope: what they compute moves out to the scope that
// contains them. A task scope's body is a scope.
struct StorageOwner {
    SymbolId function = no_symbol;
    std::vector<OwnerStep> path;
    Region arm = Region::Function;
    bool iteration = false;

    // True when `inner` is a scope strictly inside this one.
    bool encloses(const StorageOwner& inner) const;
    bool operator==(const StorageOwner&) const = default;
    bool operator<(const StorageOwner& other) const;
};

// A node that creates an array, with the scope that owns it: a collection
// literal, a repeat collection, or a call whose result summary says it is
// fresh. A range literal is a repeat collection inside a ScopedValue, which
// opens no scope, so it belongs to the block that holds the range.
struct ArrayCreation {
    SymbolId function = no_symbol;
    IrOp op = IrOp::Collection;
    // The callee of a call that creates the array.
    SymbolId callee = no_symbol;
    Span span;
    StorageOwner owner;
};

enum class StorageStage {
    Lowered,     // as lowering produces it; generic declarations are the only copy
    Specialized, // after specialize(); generic declarations stay but are skipped
};

// The original array allocations retained by a ListPointer. The converter
// uses ordinary managed heap operations for these sites, so every alias
// sees the same storage for the collection's full lifetime.
std::set<const IrNode*> retained_view_arrays(const IrProgram& program);

// Every creation in the bodies of `program`, in body order and, within a body,
// in evaluation order. Closure and reference classes that lowering and
// specialization synthesize are skipped.
std::vector<ArrayCreation> find_array_creations(const IrProgram& program,
                                                StorageStage stage = StorageStage::Lowered);
std::vector<ArrayCreation> find_array_creations(const IrProgram& program, const PurityAnalysis& purity,
                                                StorageStage stage = StorageStage::Lowered);

// -- Result transfer ---------------------------------------------------------

// What a call of a function hands back, for a function whose result is an
// array (or holds one) or a primitive reference.
struct ResultTransfer {
    enum class Kind : std::uint8_t {
        None,     // the result is neither an array nor a reference
        Fresh,    // a value the call created: it moves to the caller
        Borrowed, // storage owned elsewhere, named by `origin`
        Unknown,  // the analysis cannot say
    };
    enum class Origin : std::uint8_t {
        None,
        Parameter, // `slot` of the callee (0 is the receiver, k is parameter k-1)
        Global,    // a global, or storage the summary only knows to be static
        Field,     // a field of an owner: `symbol`, and `slot` when the owner is a slot
        Local,     // frame storage of the callee itself: an error for the region rules
        Unknown,
    };
    Kind kind = Kind::None;
    Origin origin = Origin::None;
    std::size_t slot = static_cast<std::size_t>(-1);
    SymbolId symbol = no_symbol;

    bool operator==(const ResultTransfer&) const = default;
};

std::string describe_result_transfer(const IrProgram& program, const ResultTransfer& transfer);

// What the summary of `function` in `purity` says about its result.
ResultTransfer recorded_result_transfer(const IrProgram& program, const PurityAnalysis& purity, SymbolId function);
// What the bodies of `function` return, from the tree: each return is a fresh
// array, a borrow with an origin, or unknown, and the function is the join of
// its returns. A function with no body is unknown.
ResultTransfer derived_result_transfer(const IrProgram& program, const PurityAnalysis& purity, SymbolId function);

struct TransferMismatch {
    SymbolId function = no_symbol;
    // The first return that decides the derived fact, or the function.
    Span span;
    ResultTransfer recorded;
    ResultTransfer derived;
};

// A mismatch is a contradiction, not a difference in precision: an unknown
// summary, or an unknown derivation, agrees with anything. `recorded` is the
// claim under test, so a caller can check a summary that it has edited.
std::optional<TransferMismatch> check_result_transfer(const IrProgram& program, const PurityAnalysis& purity,
                                                      SymbolId function, const ResultTransfer& recorded);
// Every function with a body whose recorded transfer contradicts its body.
std::vector<TransferMismatch> check_result_transfer(const IrProgram& program, const PurityAnalysis& purity,
                                                    StorageStage stage = StorageStage::Lowered);

// -- Escapes -----------------------------------------------------------------

enum class EscapeKind : std::uint8_t {
    ArrayInField,                // stored in a field, or in a static field
    ArrayInGlobal,               // assigned to a global in a function
    ArrayInListElement,          // appended to a list, stored in one, or written in a list literal
    ArrayInChannelMessage,       // sent on a channel
    ArrayInTaskArgument,         // captured by the function value a task starts
    ArrayCapturedByClosure,      // captured by an escaping closure
    ArrayAssignedToOuterBinding, // created in an inner scope, assigned to a binding of an outer one
    ArrayReturnedBorrowed,       // returned, but borrowed from a parameter, global or field
    ReferenceReturned,           // a reference to a local or parameter, returned
    ReferenceInField,            // a reference to a local or parameter, stored in a field or global
    ReferenceCapturedByClosure,  // a reference to a local or parameter, captured by a closure
    ReferenceRetainedByCall,     // such a reference, passed to a call that retains it
    ArrayInLongerLivedElement,   // stored in an array element with a longer or unknown lifetime
    ArrayBacksManagedView,       // a ListPointer backing site cannot be proved to leave the region
    ArrayRetainedByCall,         // passed to a callee that may retain the value elsewhere
    ClosureNeedsOwnership,      // a capturing function value escapes its activation
    BoundNeedsOwnership,        // a bound receiver escapes its activation
    GeneratorNeedsOwnership,    // a capturing generator escapes its activation (E0479)
};

std::string_view describe_escape_kind(EscapeKind kind);

struct StorageEscape {
    EscapeKind kind = EscapeKind::ArrayInField;
    // The function whose body holds the operation. After specialization this
    // is the instance, so a generic function is flagged where its type
    // parameters are arrays.
    SymbolId function = no_symbol;
    // The offending operation: the store, return, capture or send.
    Span operation;
    // The declaration of the storage that escapes: the binding, parameter,
    // global or field that holds the value, or the expression that creates it
    // when nothing names it.
    Span storage;
    // That storage's symbol, when it has one.
    SymbolId symbol = no_symbol;
    // Where the value goes: the field, global or outer binding.
    SymbolId target = no_symbol;
};

// Every escape in the bodies of `program`, in body order. `stage` says whether
// generic declarations are the program (Lowered) or are skipped in favor of
// their instances (Specialized). Primitive references are recognized by the
// ReferencePlace operation. Run the analysis before materialization replaces
// those operations with boxes and loses the provenance of the borrowed places.
std::vector<StorageEscape> find_storage_escapes(const IrProgram& program,
                                                StorageStage stage = StorageStage::Lowered);
std::vector<StorageEscape> find_storage_escapes(const IrProgram& program, const PurityAnalysis& purity,
                                                StorageStage stage = StorageStage::Lowered);

// Enforce ownership and borrow lifetimes on unmaterialized IR.
void check_storage(const IrProgram& program, StorageStage stage);

} // namespace neura
