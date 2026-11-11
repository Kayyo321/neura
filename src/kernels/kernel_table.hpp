// The kernel table for C++ callers: one KernelInfo per kernel, generated
// from the annotated headers into kernel_table.inc by scripts/gen-kernels.py
// (docs/runtime/Kernels.md), which also includes every kernel header. A runtime finds a kernel by name and ABI
// version, evaluates its step charge and buffer sizes from the argument
// values, and calls it through its thunk.
#pragma once

#include <cstddef>
#include <cstdint>

#include "neura_kernel.h"

namespace neura::kernels {

// What a parameter or result is. The comment gives the C type.
enum class Kind : std::uint8_t {
    Status,  // neura_status (results only)
    I8, I16, I32, I64, U8, U16, U32, U64, F32, F64, Bool,  // the scalar typedefs
    Text,   // neura_text
    Bytes,  // neura_bytes
    ViewI8, ViewI16, ViewI32, ViewI64, ViewU8, ViewU16, ViewU32, ViewU64, ViewF32, ViewF64,  // neura_view_*
    MutI8, MutI16, MutI32, MutI64, MutU8, MutU16, MutU32, MutU64, MutF32, MutF64,  // neura_mut_*
    OutText,   // neura_out_text*
    OutBytes,  // neura_out_bytes*
    State,     // neura_state*
    Work,      // neura_work*
    ResultI8, ResultI16, ResultI32, ResultI64, ResultU8, ResultU16, ResultU32, ResultU64, ResultF32, ResultF64,
    ResultBool,  // a pointer to the scalar the kernel stores
};

// A formula in postfix order. Const pushes `value`; Arg pushes the value of
// parameter `value` (a length for a view, output, or state, otherwise the
// scalar with a negative value taken as 0); the others pop their operands
// and push the result. The arithmetic is unsigned and saturating, x / 0 is
// 0, and Log2 is the floor with log2(0) = 0.
enum class Op : std::uint8_t { Const, Arg, Add, Sub, Mul, Div, Log2, Min, Max };

struct Step {
    Op op;
    std::uint64_t value;
};

struct Formula {
    const char* text;
    const Step* steps;
    std::size_t step_count;
};

// The capacity of an output buffer, the scratch length of a writable view,
// or the exact size of a state, for the parameter at `param`.
struct BufferSize {
    std::size_t param;
    Formula size;
};

struct KernelInfo {
    const char* name;
    std::uint32_t version;
    Formula cost;
    const Kind* params;
    const char* const* param_names;
    std::size_t param_count;
    Kind result;
    const BufferSize* buffers;
    std::size_t buffer_count;
    neura_kernel_thunk thunk;
};

// Evaluates a formula over the argument values. The stack never holds more
// entries than the formula has steps; the generator rejects formulas deeper
// than 16.
inline std::uint64_t evaluate(const Formula& formula, const std::uint64_t* args) {
    std::uint64_t stack[16];
    std::size_t depth = 0;
    for (std::size_t i = 0; i < formula.step_count; ++i) {
        const Step& step = formula.steps[i];
        if (step.op == Op::Const) {
            stack[depth++] = step.value;
            continue;
        }
        if (step.op == Op::Arg) {
            stack[depth++] = args[step.value];
            continue;
        }
        if (step.op == Op::Log2) {
            std::uint64_t x = stack[depth - 1];
            std::uint64_t log = 0;
            while (x > 1) {
                x >>= 1;
                ++log;
            }
            stack[depth - 1] = log;
            continue;
        }
        std::uint64_t b = stack[--depth];
        std::uint64_t a = stack[depth - 1];
        std::uint64_t r = 0;
        switch (step.op) {
        case Op::Add: r = a + b < a ? UINT64_MAX : a + b; break;
        case Op::Sub: r = a < b ? 0 : a - b; break;
        case Op::Mul: r = (a != 0 && b > UINT64_MAX / a) ? UINT64_MAX : a * b; break;
        case Op::Div: r = b == 0 ? 0 : a / b; break;
        case Op::Min: r = a < b ? a : b; break;
        case Op::Max: r = a < b ? b : a; break;
        default: break;
        }
        stack[depth - 1] = r;
    }
    return depth == 1 ? stack[0] : 0;
}

}  // namespace neura::kernels

#include "kernel_table.inc"
