#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace neura::jit {

// The operations the region code generator (compiler.cpp) needs from a
// processor, each with one meaning on every processor. Values are 64-bit.
// The generator computes in four scratch registers; a region register lives
// either in the region's register array or, when the backend pins it, in a
// machine register for the whole program (load and store hide which).
//
// Conventions every backend keeps:
// - Only the named scratch registers and the condition flags change, except
//   where an operation says otherwise.
// - T3 is the generator's immediate register: operations that need an
//   internal temporary use one of their own, never T0 to T3.
// - shift() takes its count in T1 (x86-64 shifts by CL).
// - multiply_or_branch(Unsigned) needs its destination in T0 and its source
//   in T1, and clobbers T2 (x86-64 multiplies into RDX:RAX).
// - divide() works on T0 and T1 only (x86-64 divides RDX:RAX) and clobbers
//   T2.
// - call() takes `a` in T0 and `b` in T1 and returns in T0. It clobbers T1
//   to T3. Pinned registers keep their values across it.
enum class Scratch : std::uint8_t { T0, T1, T2, T3 };

enum class Cond : std::uint8_t { Eq, Ne, ULt, ULe, UGt, UGe, SLt, SLe, SGt, SGe };
enum class Alu : std::uint8_t { Add, Sub, And, Or, Xor, Mul };
enum class Shift : std::uint8_t { Left, LogicalRight, ArithmeticRight };
enum class Overflow : std::uint8_t { Unsigned, Signed };

struct Label {
    std::uint32_t id = 0;
};

// Thrown when code cannot be generated (a branch out of range, too many
// registers); the region then runs on the interpreter.
struct GenerationError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class MacroAssembler {
public:
    virtual ~MacroAssembler() = default;

    Label label() { return Label{next_label_++}; }
    virtual void bind(Label label) = 0;
    virtual void jump(Label label) = 0;

    // Pins region registers `pinned` (at most pin_capacity()) to machine
    // registers for the whole program. Call before prologue().
    virtual std::size_t pin_capacity() const = 0;
    virtual void pin(const std::vector<std::uint32_t>& pinned) = 0;

    // Entry: saves what the platform's calling convention requires, takes
    // the context from the first argument, and loads the pinned registers.
    virtual void prologue() = 0;
    // Exit: stores the pinned registers and the completed-block count into
    // the context, restores, and returns.
    virtual void epilogue() = 0;

    virtual void load(Scratch to, std::uint32_t reg) = 0;
    virtual void store(std::uint32_t reg, Scratch from) = 0;
    virtual void set_initialized(std::uint32_t reg) = 0;
    // Stores every pinned register into the register array.
    virtual void flush_pinned() = 0;

    virtual void move_immediate(Scratch to, std::uint64_t value) = 0;
    virtual void move(Scratch to, Scratch from) = 0;
    // to = to op from.
    virtual void alu(Alu op, Scratch to, Scratch from) = 0;
    // to = to shifted by T1 (below 64).
    virtual void shift(Shift op, Scratch to) = 0;
    // to = to shifted by `count`, 1 to 63.
    virtual void shift_immediate(Shift op, Scratch to, unsigned count) = 0;
    virtual void negate(Scratch value) = 0;
    virtual void bit_not(Scratch value) = 0;
    // to = (a cond b) ? 1 : 0; `to` may be a or b.
    virtual void set_if(Cond cond, Scratch to, Scratch a, Scratch b) = 0;
    virtual void branch_if(Cond cond, Scratch a, Scratch b, Label target) = 0;
    virtual void branch_if_zero(Scratch value, Label target) = 0;
    virtual void branch_if_nonzero(Scratch value, Label target) = 0;
    // Branches when bit `bit` (0 to 63) of `value` is set.
    virtual void branch_if_bit(Scratch value, unsigned bit, Label target) = 0;
    // to += from; branches when the sum does not fit 64 bits as unsigned
    // (carry) or as signed (overflow). `to` is unspecified after a branch.
    virtual void add_or_branch(Overflow kind, Scratch to, Scratch from, Label target) = 0;
    // to -= from; branches on signed overflow.
    virtual void subtract_or_branch(Scratch to, Scratch from, Label target) = 0;
    // to = the low 64 bits of to * from; branches when the exact product
    // does not fit 64 bits as unsigned or as signed.
    virtual void multiply_or_branch(Overflow kind, Scratch to, Scratch from, Label target) = 0;
    // T0 = T0 / T1, or T0 % T1 with `remainder`, as unsigned or signed
    // 64-bit values. T1 must not be zero, and a signed division must not
    // be of the smallest value by -1. Clobbers T2.
    virtual void divide(bool is_signed, bool remainder) = 0;

    virtual void load_context(Scratch to, std::uint32_t offset) = 0;
    virtual void load_context_byte(Scratch to, std::uint32_t offset) = 0;
    virtual void store_context32(std::uint32_t offset, std::uint32_t value) = 0;
    // to = the 64-bit value at address `base`.
    virtual void load_indirect(Scratch to, Scratch base) = 0;
    virtual void done_to(Scratch to) = 0;
    virtual void add_done(std::uint32_t count) = 0;

    // Calls the helper at context offset `helper` with (context, immediate,
    // T0, T1); its result is in T0. Pinned registers the call could clobber
    // are stored before and reloaded after it.
    virtual void call(std::uint32_t helper, std::uint32_t immediate) = 0;

    // The finished code with every label resolved.
    virtual std::vector<std::uint8_t> finish() = 0;

private:
    std::uint32_t next_label_ = 0;
};

std::unique_ptr<MacroAssembler> make_x86_64();
std::unique_ptr<MacroAssembler> make_aarch64();

} // namespace neura::jit
