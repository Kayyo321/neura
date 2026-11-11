#pragma once

#include "nvr/image.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace neura::lift {

// A typed operation behind a block's graph. Leaves are block inputs and
// immediates; inner nodes are the operations of docs/lang/Numbers.md. A
// subexpression can be shared, which makes the expressions of a block a DAG;
// regeneration builds a shared subexpression once per use, as the converter
// does for an expression it evaluates twice.
struct Expression {
    enum class Kind { Input, Immediate, Unary, Binary, Convert };

    Kind kind = Kind::Input;
    // Image type of the result.
    std::uint32_t type = 0;
    // "+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=", "&&", "||",
    // "!", "neg", "index" (a signed index widened to uint64 after a bounds
    // check), and "+~", an addition whose overflow is not checked, as in
    // generated code that proves it cannot overflow. Integers also have the
    // bitwise "&", "|", "^", and "~", and the shifts "<<" and ">>" (a count
    // of at least the width fails; ">>" of a signed value fills with its
    // sign), as docs/lang/Numbers.md defines them.
    std::string op;
    std::vector<std::shared_ptr<const Expression>> operands;
    // Input: position in the block's input list.
    std::size_t input = 0;
    // Immediate: the value's slot encoding.
    std::uint64_t bits = 0;
};

using ExpressionPtr = std::shared_ptr<const Expression>;

ExpressionPtr input(std::uint32_t type, std::size_t index);
ExpressionPtr immediate(std::uint32_t type, std::uint64_t bits);
ExpressionPtr unary(std::string op, std::uint32_t type, ExpressionPtr operand);
ExpressionPtr convert(std::uint32_t type, ExpressionPtr operand);
ExpressionPtr binary(std::string op, std::uint32_t type, ExpressionPtr left, ExpressionPtr right);

// The lifted form of one block: what its graph computes, stated as typed
// operations over the block's inputs. A form is a claim; only the structural
// proof (proof.hpp) makes it trustworthy.
struct LiftedForm {
    // The operation behind each value an exit reads from the graph, keyed by
    // the value's first output lane.
    std::map<std::uint32_t, ExpressionPtr> values;
    // The same values in the order the block computes them, which fixes the
    // order of its checks. Empty means lane order.
    std::vector<std::pair<std::uint32_t, ExpressionPtr>> order;
    // For a block that selects among exits by a condition: the exit taken
    // when the condition holds, and the condition. Exits not covered, such
    // as traps, are taken when their checks fail.
    std::optional<std::size_t> exit_when_true;
    ExpressionPtr condition;
    // Checks whose failure selects a trap exit: (reason, operation whose
    // failure it is), in evaluation order.
    std::vector<std::pair<std::uint32_t, ExpressionPtr>> checks;
    // Dispatch blocks: the layouts each Call exit is taken for.
    std::map<std::size_t, std::vector<std::uint32_t>> dispatch;
    // A shared form, as the converter records one (a LIFT hint): each
    // expression node stands for one computation, however many times it is
    // used, so a node reachable twice is built and computed once. The
    // scheduled nodes come first, in the order the block computes them;
    // they include operations whose only effect is a check, such as a
    // statement whose value is discarded, and fix the order of every check.
    // A form that is not shared builds a node once per use and has no
    // schedule.
    bool shared = false;
    std::vector<ExpressionPtr> schedule;

    // The values in computation order: `order` when given, else lane order.
    std::vector<std::pair<std::uint32_t, ExpressionPtr>> computation_order() const;
};

// Why a form cannot describe `block` of `image`, or nullptr when it is well
// typed: every expression names an existing input with that input's type,
// uses an operation the circuit library builds for its operand types, and
// yields its declared type; the values are those of the lanes the exits
// read; and nesting is bounded. A form from an untrusted source must pass
// this before it is replayed, because the circuit gadgets assume operands of
// matching widths.
const char* form_type_error(const nvr::Image& image, const nvr::Block& block, const LiftedForm& form);

// Type helpers shared by the proof, the recognizer, and the lowering.
std::uint64_t width_mask(unsigned width);
bool is_integer(const nvr::Type& type);
bool is_floating(const nvr::Type& type);
bool is_comparison(const std::string& op);

} // namespace neura::lift
