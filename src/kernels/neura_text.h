/* Text kernels over UTF-8 strings, following stdlib/std/text/strings.nra
 * (Std.Text.Strings) and stdlib/std/core/string.nra (Std.Core.String).
 *
 * Offsets are byte offsets. Every search keeps the rule of Strings.matches_at:
 * a candidate whose first or end byte is a continuation byte does not match.
 * For a valid needle in valid text this never rejects a byte match, so the
 * results are those of a plain byte search. */
#ifndef NEURA_TEXT_H
#define NEURA_TEXT_H

#include "neura_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Strings.index_of_from (and index_of with from = 0): the first occurrence
 * at or after `from`, or -1; an empty needle matches at `from`. `from`
 * outside 0 through the length gives NEURA_TRAP_OFFSET_OUT_OF_RANGE and one
 * on a continuation byte NEURA_TRAP_NOT_CHARACTER_BOUNDARY. */
NEURA_KERNEL("neura.text.index_of", 1, "1 + s / 16")
neura_status neura_k_text_index_of(neura_text s, neura_text needle, neura_i64 from, neura_i64* result);

/* Strings.last_index_of: the last occurrence, or -1; an empty needle
 * matches at the length. */
NEURA_KERNEL("neura.text.last_index_of", 1, "1 + s / 16")
neura_i64 neura_k_text_last_index_of(neura_text s, neura_text needle);

/* Strings.contains. */
NEURA_KERNEL("neura.text.contains", 1, "1 + s / 16")
neura_bool neura_k_text_contains(neura_text s, neura_text needle);

/* Strings.starts_with. */
NEURA_KERNEL("neura.text.starts_with", 1, "1 + prefix / 64")
neura_bool neura_k_text_starts_with(neura_text s, neura_text prefix);

/* Strings.ends_with. */
NEURA_KERNEL("neura.text.ends_with", 1, "1 + suffix / 64")
neura_bool neura_k_text_ends_with(neura_text s, neura_text suffix);

/* Strings.compare as String.compare_to returns it: -1, 0, or 1 by byte-wise
 * lexicographic order, a proper prefix first. */
NEURA_KERNEL("neura.text.compare", 1, "1 + min(a, b) / 64")
neura_i32 neura_k_text_compare(neura_text a, neura_text b);

/* The number of non-overlapping occurrences, left to right: the number of
 * replacements Strings.replace makes. An empty target gives
 * NEURA_TRAP_EMPTY_TARGET, as Strings.replace traps. */
NEURA_KERNEL("neura.text.count", 1, "1 + s / 16")
neura_status neura_k_text_count(neura_text s, neura_text target, neura_i64* result);

/* Strings.to_ascii_lower: A through Z become a through z. */
NEURA_KERNEL("neura.text.ascii_lower", 1, "1 + s / 32")
NEURA_KERNEL_BUFFER("out", "s")
neura_status neura_k_text_ascii_lower(neura_text s, neura_out_text* out);

/* Strings.to_ascii_upper: a through z become A through Z. */
NEURA_KERNEL("neura.text.ascii_upper", 1, "1 + s / 32")
NEURA_KERNEL_BUFFER("out", "s")
neura_status neura_k_text_ascii_upper(neura_text s, neura_out_text* out);

/* The number of leading bytes of Strings.is_space (space, and 9 through 13).
 * With e = space_suffix_start: Strings.trim is [min(this, e), e),
 * trim_start is [this, length), and trim_end is [0, e). */
NEURA_KERNEL("neura.text.space_prefix_length", 1, "1 + s / 64")
neura_i64 neura_k_text_space_prefix_length(neura_text s);

/* Strings.trimmed_end: the offset after the last byte that is not a
 * Strings.is_space byte, or 0. */
NEURA_KERNEL("neura.text.space_suffix_start", 1, "1 + s / 64")
neura_i64 neura_k_text_space_suffix_start(neura_text s);

/* The number of leading bytes up to 32 (String.trim's set: every code point
 * through U+0020). With e = blank_suffix_start: String.trim is
 * [this, max(e, this)). */
NEURA_KERNEL("neura.text.blank_prefix_length", 1, "1 + s / 64")
neura_i64 neura_k_text_blank_prefix_length(neura_text s);

/* The offset after the last byte above 32, or 0. */
NEURA_KERNEL("neura.text.blank_suffix_start", 1, "1 + s / 64")
neura_i64 neura_k_text_blank_suffix_start(neura_text s);

/* Strings.replace (limit 2147483647) and replace_first (limit 1): replaces
 * up to `limit` non-overlapping occurrences, left to right; a limit below 1
 * replaces nothing, as Strings.replace_up_to does. An empty target gives
 * NEURA_TRAP_EMPTY_TARGET. */
NEURA_KERNEL("neura.text.replace", 1, "1 + s / 16 + s / target * replacement / 64")
NEURA_KERNEL_BUFFER("out", "s + s / target * replacement")
neura_status neura_k_text_replace(neura_text s, neura_text target, neura_text replacement, neura_i64 limit,
                                  neura_out_text* out);

/* The scan step of Strings.split and split_limit: the offset of the first
 * separator at or after `start`, or -1 (Strings.find, unchecked: a `start`
 * past the end finds nothing). An empty separator gives
 * NEURA_TRAP_EMPTY_SEPARATOR. Strings never passes a negative `start`; the
 * kernel answers one with NEURA_TRAP_OFFSET_OUT_OF_RANGE instead of reading
 * outside the text. */
NEURA_KERNEL("neura.text.split_next", 1, "1 + (s - start) / 16")
neura_status neura_k_text_split_next(neura_text s, neura_text separator, neura_i64 start, neura_i64* result);

#ifdef __cplusplus
}
#endif

#endif
