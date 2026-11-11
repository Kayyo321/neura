/* Integer text kernels: Int64 and UInt64 to_string_radix and parse_radix
 * (stdlib/std/core/int64.nra, u-int64.nra, and the shared grammar of
 * stdlib/std/core/internal/integer-text.nra).
 *
 * A radix outside 2 through 36 gives NEURA_TRAP_RADIX_OUT_OF_RANGE. Digits
 * are written in lowercase and read in either case. The parse grammar is an
 * optional `+` or `-` and then one or more digits of the radix. Its errors,
 * first error first, with the offset stored through error_offset:
 *   NEURA_ERR_PARSE_EMPTY at 0 for empty text,
 *   NEURA_ERR_PARSE_EXPECTED_DIGIT at the length for a lone sign,
 *   NEURA_ERR_PARSE_INVALID_CHARACTER at the first byte that is not a digit,
 *   NEURA_ERR_PARSE_OUT_OF_RANGE at 0 for a value outside the type; the
 *   wrapper appends "int64" or "uint64" to the message.
 * UInt64 accepts "-0" (and "-00", ...) as 0 and rejects any other negative
 * value as out of range. */
#ifndef NEURA_INTEGER_TEXT_H
#define NEURA_INTEGER_TEXT_H

#include "neura_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Int64.to_string_radix. */
NEURA_KERNEL("neura.integer.format_i64", 1, "2")
NEURA_KERNEL_BUFFER("out", "65")
neura_status neura_k_integer_format_i64(neura_i64 value, neura_i64 radix, neura_out_text* out);

/* UInt64.to_string_radix. */
NEURA_KERNEL("neura.integer.format_u64", 1, "2")
NEURA_KERNEL_BUFFER("out", "64")
neura_status neura_k_integer_format_u64(neura_u64 value, neura_i64 radix, neura_out_text* out);

/* Int64.parse_radix. */
NEURA_KERNEL("neura.integer.parse_i64", 1, "1 + text / 16")
neura_status neura_k_integer_parse_i64(neura_text text, neura_i64 radix, neura_i64* result, neura_i64* error_offset);

/* UInt64.parse_radix. */
NEURA_KERNEL("neura.integer.parse_u64", 1, "1 + text / 16")
neura_status neura_k_integer_parse_u64(neura_text text, neura_i64 radix, neura_u64* result, neura_i64* error_offset);

#ifdef __cplusplus
}
#endif

#endif
