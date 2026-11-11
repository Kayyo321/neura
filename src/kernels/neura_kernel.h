/* The Neura kernel ABI: deterministic, pure C functions that the runtime
 * calls in place of standard library code written in Neura.
 *
 * Every kernel obeys these rules, and scripts/check-kernels.sh checks the
 * ones a tool can see:
 *
 *  - Pure. A kernel reads only its arguments and writes only the buffers,
 *    state, and result pointers it is given. It has no global mutable state,
 *    never allocates, never calls back into the runtime, and never reads the
 *    clock, the environment, the locale, or a file.
 *  - Self-contained. A kernel calls no libm function and no locale-dependent
 *    libc function. Floating point uses only C's + - * / and comparisons on
 *    IEEE 754 binary64 with round-to-nearest-even, compiled with
 *    -ffp-contract=off (FLT_EVAL_METHOD must be 0). Every NaN a kernel
 *    returns from a float function is the canonical NaN 0x7FF8000000000000,
 *    as Neura's own float operations produce.
 *  - Deterministic. The same arguments give the same results and statuses on
 *    every platform. State a kernel keeps between calls (neura_state) is a
 *    byte array with a fixed little-endian layout, never a C struct.
 *  - Charged in advance. Each kernel declares a step charge, a formula over
 *    its arguments (below) that the runtime evaluates and charges before the
 *    call, so a step limit fires at the same point in every execution mode.
 *    The charge counts the most work the arguments allow (the bytes or
 *    elements the call can read or write, not the whole of a text it cannot
 *    reach), and the kernel's worst-case work is within a constant factor of
 *    its charge, so no call does unbounded work for a bounded charge.
 *    A kernel whose work depends on the data beyond its argument sizes also
 *    takes a neura_work and reports the extra work it consumed (below).
 *
 * Header dialect. scripts/gen-kernels.py reads every other header in
 * src/kernels with regular expressions, so a kernel declaration has exactly
 * this shape:
 *
 *     NEURA_KERNEL("neura.area.name", 1, "cost formula")
 *     NEURA_KERNEL_BUFFER("param", "size formula")      (zero or more)
 *     result_type neura_k_area_name(type name, type* name, ...);
 *
 * - Each annotation is one line starting in the first column, and nothing
 *   but further NEURA_KERNEL_BUFFER lines comes between it and the
 *   prototype. The prototype may wrap; it ends at the first ';'. A comment
 *   that ends on the line before the annotation documents the kernel in the
 *   generated reference.
 * - The kernel name is "neura." then lowercase words joined by '.' and '_'.
 *   The C function is neura_k_ and the name after "neura." with '.' turned
 *   into '_'. The second argument is the ABI version of that kernel, a
 *   positive integer that changes whenever its signature or behaviour does.
 * - The result type is neura_status or one of the scalar types below.
 * - Every parameter is one of the types below, by value or (for the result
 *   pointers, output buffers, state, and work) as a single pointer, with a
 *   name. No other types, qualifiers, arrays, or default values appear.
 * - Formulas use decimal integers, parameter names, + - * / ( ), and the
 *   functions log2(x) (floor, with log2(0) = 0), min(a, b), and max(a, b).
 *   A view, output, or state parameter stands for its length in elements; a
 *   scalar parameter for its value, with a negative value taken as 0. The
 *   arithmetic is unsigned 64-bit and saturating: a - b below 0 is 0, a
 *   result above 2^64 - 1 is 2^64 - 1, and x / 0 is 0.
 * - NEURA_KERNEL_BUFFER names an output, state, or writable view parameter;
 *   every output and state parameter needs one. For an output buffer the
 *   formula is a capacity that is always enough, so a runtime that allocates
 *   it never sees NEURA_NEED_CAPACITY. For a state it is the exact size in
 *   bytes. For a writable view used as scratch space it is the minimum
 *   length.
 *
 * Output buffers. The caller owns the memory: `data` with room for `cap`
 * elements. A kernel first works out the exact result length. If it exceeds
 * `cap`, the kernel stores that length in `len`, leaves `data` in an
 * unspecified state, and returns NEURA_NEED_CAPACITY; the caller grows the
 * buffer and calls again (a retry is not charged again). Otherwise the
 * kernel writes the result and its length.
 *
 * Statuses. NEURA_OK is success. A NEURA_ERR_* status is a failure that the
 * Neura wrapper returns as an Err value of Std.Core.Result, with the offset
 * the kernel stores through its `error_offset` parameter. A NEURA_TRAP_*
 * status is a failure that the wrapper turns into the trap whose message
 * neura_kernel_status_message returns. neura_kernel_status_error_kind names
 * the Error variant (and for Parse the ParseErrorKind) of an ERR status.
 *
 * Work budget. A kernel whose work is not bounded by its declared charge
 * takes `neura_work* work`. It reads work->budget, the units it may still
 * spend, adds every unit it spends to work->used, and returns
 * NEURA_TRAP_WORK_EXHAUSTED instead of spending more than the budget. The
 * runtime charges work->used as steps after the call, so the total charge is
 * the formula plus the reported work, identical in every execution mode. No
 * kernel of ABI generation 1 needs this yet; regular expressions will.
 */
#ifndef NEURA_KERNEL_H
#define NEURA_KERNEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The annotations expand to nothing; scripts/gen-kernels.py reads them. */
#define NEURA_KERNEL(name, version, cost)
#define NEURA_KERNEL_BUFFER(param, size)

/* Scalars. */
typedef int8_t neura_i8;
typedef int16_t neura_i16;
typedef int32_t neura_i32;
typedef int64_t neura_i64;
typedef uint8_t neura_u8;
typedef uint16_t neura_u16;
typedef uint32_t neura_u32;
typedef uint64_t neura_u64;
typedef float neura_f32;
typedef double neura_f64;
/* A Neura bool: 0 is false, any other value true. Kernels return 0 or 1. */
typedef uint8_t neura_bool;

/* Read-only views. neura_text holds a Neura string, which is valid UTF-8;
 * neura_bytes holds a uint8[list] or uint8[] and may hold any bytes. */
typedef struct neura_text {
    const uint8_t* data;
    uint64_t len;
} neura_text;
typedef struct neura_bytes {
    const uint8_t* data;
    uint64_t len;
} neura_bytes;

#define NEURA_VIEW_TYPES(X) \
    X(i8, int8_t) X(i16, int16_t) X(i32, int32_t) X(i64, int64_t) \
    X(u8, uint8_t) X(u16, uint16_t) X(u32, uint32_t) X(u64, uint64_t) \
    X(f32, float) X(f64, double)

/* neura_view_T: a read-only array of T. neura_mut_T: the same, writable in
 * place, for kernels such as the sorts. */
#define NEURA_DECLARE_VIEW(suffix, type) \
    typedef struct neura_view_##suffix { const type* data; uint64_t len; } neura_view_##suffix; \
    typedef struct neura_mut_##suffix { type* data; uint64_t len; } neura_mut_##suffix;
NEURA_VIEW_TYPES(NEURA_DECLARE_VIEW)
#undef NEURA_DECLARE_VIEW

/* Output buffers: neura_out_text receives UTF-8 text that becomes a Neura
 * string, neura_out_bytes receives a uint8[list]. */
typedef struct neura_out_text {
    uint8_t* data;
    uint64_t len;
    uint64_t cap;
} neura_out_text;
typedef struct neura_out_bytes {
    uint8_t* data;
    uint64_t len;
    uint64_t cap;
} neura_out_bytes;

/* Caller-held state between calls: `len` bytes with the layout the kernel
 * family documents. A state of the wrong length or with an impossible
 * content gives NEURA_TRAP_INVALID_STATE. */
typedef struct neura_state {
    uint8_t* data;
    uint64_t len;
} neura_state;

/* The work budget of a data-dependent kernel (see above). */
typedef struct neura_work {
    uint64_t budget;
    uint64_t used;
} neura_work;

/* Statuses. The numbers are part of the ABI; new ones are only appended. */
typedef enum neura_status {
    NEURA_OK = 0,
    NEURA_NEED_CAPACITY = 1,

    /* Results: Error.Encoding. */
    NEURA_ERR_INVALID_START_BYTE = 16,
    NEURA_ERR_INVALID_CONTINUATION_BYTE = 17,
    NEURA_ERR_UNEXPECTED_END_OF_DATA = 18,
    NEURA_ERR_INVALID_CHARACTER = 19,
    NEURA_ERR_INVALID_PADDING = 20,
    NEURA_ERR_UNEXPECTED_PADDING = 21,
    NEURA_ERR_NONZERO_TRAILING_BITS = 22,
    NEURA_ERR_INVALID_HEX_DIGIT = 23,
    NEURA_ERR_ODD_HEX_DIGITS = 24,

    /* Results: Error.Parse. */
    NEURA_ERR_PARSE_EMPTY = 32,
    NEURA_ERR_PARSE_EXPECTED_DIGIT = 33,
    NEURA_ERR_PARSE_INVALID_CHARACTER = 34,
    NEURA_ERR_PARSE_OUT_OF_RANGE = 35,

    /* Traps. */
    NEURA_TRAP_OFFSET_OUT_OF_RANGE = 64,
    NEURA_TRAP_NOT_CHARACTER_BOUNDARY = 65,
    NEURA_TRAP_EMPTY_SEPARATOR = 66,
    NEURA_TRAP_EMPTY_TARGET = 67,
    NEURA_TRAP_RADIX_OUT_OF_RANGE = 68,
    NEURA_TRAP_DIGEST_FINISHED = 69,
    NEURA_TRAP_INVALID_STATE = 70,
    NEURA_TRAP_SCRATCH_TOO_SMALL = 71,
    NEURA_TRAP_WORK_EXHAUSTED = 72
} neura_status;

/* The text of a status: the Error message for an ERR status ("value out of
 * range for " is followed by the type name, which the wrapper appends), the
 * trap message for a TRAP status, and "ok" or "output buffer too small"
 * otherwise. Never NULL; an unknown status gives "unknown kernel status". */
const char* neura_kernel_status_message(neura_status status);

/* For an ERR status, the Error variant and, for Parse, the ParseErrorKind,
 * as "Encoding", "Parse.Empty", "Parse.UnexpectedEnd",
 * "Parse.InvalidCharacter", or "Parse.OutOfRange". NULL for other statuses. */
const char* neura_kernel_status_error_kind(neura_status status);

/* The generic call shape of the generated table (kernel_table.inc): args[i]
 * points to the i-th argument's value (for a pointer parameter, to the
 * pointer), and result points to storage for the result type. */
typedef void (*neura_kernel_thunk)(void* const* args, void* result);

#ifdef __cplusplus
}
#endif

#endif
