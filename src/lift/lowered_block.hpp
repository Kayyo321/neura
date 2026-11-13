#pragma once

#include "expression.hpp"
#include "nvr/image.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace neura::lift {

// One straight-line operation over 64-bit registers. Registers hold values
// in their slot encoding: the low `width` bits, zero above. Register i < the
// block's input count holds input i; instruction k writes register
// input count + k.
enum class Opcode : std::uint8_t {
    Constant,
    // Integer arithmetic with the failures of docs/lang/Numbers.md:
    // overflow for add, subtract, negate, multiply; a zero divisor or
    // MIN / -1 for divide; a zero divisor for remainder.
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    Negate,
    // Addition and subtraction without an overflow check ("+~", "-~"):
    // the result modulo 2^width.
    AddWrapping,
    SubtractWrapping,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And,
    Or,
    Not,
    // Integer bitwise operations, and shifts by a count that must be below
    // the width; a signed right shift fills with the sign.
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    ShiftLeft,
    ShiftRight,
    // A signed or unsigned index widened to uint64; a negative index fails.
    Index,
    // Integer to integer; a value outside the target range fails.
    ConvertInteger,
    // The value hash of docs/lang/Stdlib.md's hash_code for a Boolean,
    // integer, or float operand, as a uint32 (value_hash in evaluate.hpp).
    // It reads only bits, so it does not depend on the float environment.
    ValueHash,
    // binary32 and binary64 only, by hardware arithmetic.
    // Arithmetic, binary32 and binary64 only, by hardware arithmetic, with
    // every NaN result replaced by the canonical quiet NaN.
    FloatAdd,
    FloatSubtract,
    FloatMultiply,
    FloatDivide,
    // Flips the sign bit, of any format and of a NaN too.
    FloatNegate,
    // IEEE comparisons of any format: a NaN is unordered, -0 equals +0.
    FloatEqual,
    FloatNotEqual,
    FloatLess,
    FloatLessEqual,
    FloatGreater,
    FloatGreaterEqual,
    // To binary32 or binary64, rounded to nearest even.
    FloatFromInteger,
    // From any format, truncated toward zero; NaN, an infinity, or a value
    // outside the target range fails.
    FloatToInteger,
    // Widening, which is exact, or binary64 to binary32, rounded by the
    // hardware; a NaN becomes the target's canonical NaN. A conversion to
    // the same format passes the value through and emits no instruction.
    FloatConvert,
};

inline constexpr std::uint32_t no_exit = 0xffffffffU;

// The float format of an operand or result.
enum class FloatType : std::uint8_t { None, Binary16, BFloat16, Binary32, Binary64 };

struct Instruction {
    Opcode op = Opcode::Constant;
    // Operand width and signedness (for a conversion, the source's).
    std::uint8_t width = 0;
    bool is_signed = false;
    // Conversions: the target width and signedness.
    std::uint8_t to_width = 0;
    bool to_signed = false;
    // Float operations: the operand's format, and a conversion's target.
    FloatType format = FloatType::None;
    FloatType to_format = FloatType::None;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint64_t immediate = 0;
    // For an operation that can fail: the trap reason of its check, and the
    // trap exit that reason selects (no_exit when the block has none).
    std::uint32_t reason = 0;
    std::uint32_t trap_exit = no_exit;
};

// A value an exit reads from the graph: output lanes [first_lane, first_lane
// + lanes), computed into `reg`.
struct LoweredOutput {
    std::uint32_t first_lane = 0;
    std::uint32_t lanes = 0;
    std::uint32_t reg = 0;
};

// An accepted lifted block compiled to native operations. It depends on the
// graph, the block's input types, its exit table (kinds, trap reasons, and
// the lanes each exit reads) and selector, but not on slot numbers, exit
// targets, or the function, so blocks sharing a graph and those can share it.
struct LoweredBlock {
    // Lane count of each input, in input-descriptor order.
    std::vector<unsigned> input_widths;
    std::vector<Instruction> code;
    // Sorted by first lane.
    std::vector<LoweredOutput> outputs;

    // Exit selection once no check failed.
    enum class Selection { Fixed, Condition, Dispatch };
    Selection selection = Selection::Fixed;
    std::uint32_t fixed_exit = 0;
    std::uint32_t condition = 0;
    std::uint32_t exit_if_true = 0;
    std::uint32_t exit_if_false = 0;
    // Dispatch: (tag, exit), sorted by tag; any other tag selects
    // `dispatch_default`. The tag is input 0.
    std::vector<std::pair<std::uint64_t, std::uint32_t>> dispatch;
    std::uint32_t dispatch_default = 0;

    // The graph's output shape, to write the result as output lanes.
    std::uint32_t exit_count = 1;
    std::uint32_t selector_start = 0;
    std::uint32_t output_lanes = 0;
    // Whether execution needs the default floating-point environment.
    bool uses_float = false;
};

// Compiles a well-typed lifted form of `block`. Fails, with the reason, for
// an operation it cannot lower exactly (binary16 and bfloat16 arithmetic, any
// float operation on a platform without bit-exact IEEE hardware arithmetic),
// an exit table whose selection the form does not determine, or a graph
// output lane no exit reads. Compiling does not prove anything: only
// lift_and_prove() pairs it with the proofs.
struct LoweringResult {
    std::optional<LoweredBlock> block;
    std::string failure;
};
LoweringResult lower(const nvr::Image& image, const nvr::Block& block, const LiftedForm& form);

// Executes a lowered block on whole input values, in the block's input
// order (each the slot encoding of its input). Writes one value per entry of
// block.outputs into `outputs` and returns the selected exit. For a check's
// trap exit, the outputs are unspecified: such an exit reads no lane.
// Returns nothing, declining the step, when float operations meet a
// floating-point environment other than the default (round to nearest even,
// subnormals neither flushed nor treated as zero), or a check fails whose
// trap exit the block lacks; the reference evaluator then decides.
std::optional<std::uint32_t> execute(const LoweredBlock& block, std::span<const std::uint64_t> inputs,
                                     std::span<std::uint64_t> outputs);
// The same with the caller's registers: at least one per input and one per
// instruction. The executor's tier steps use it and allocate nothing.
std::optional<std::uint32_t> execute(const LoweredBlock& block, std::span<const std::uint64_t> inputs,
                                     std::span<std::uint64_t> outputs, std::span<std::uint64_t> registers);

// The graph output lanes of an executed block: the selector of `exit` and
// each output value's bits. `lanes` has block.output_lanes entries; lanes no
// exit reads are left as they are (a lowered block has none).
void write_lanes(const LoweredBlock& block, std::uint32_t exit, std::span<const std::uint64_t> outputs,
                 std::span<std::int64_t> lanes);

// Whether this build and the current thread compute binary32 and binary64
// arithmetic bit-exactly as IEEE 754 requires.
bool float_lowering_supported();
bool float_environment_is_default();

} // namespace neura::lift
