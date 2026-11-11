/* Text kernels (neura_text.h). Lengths are below 2^63, so offsets are
 * computed in int64_t as Neura's int arithmetic computes them. */
#include "neura_text.h"

static int continuation(uint8_t b) { return (b & 192) == 128; }

static int bytes_equal(const uint8_t* a, const uint8_t* b, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* Strings.matches_at. */
static int matches_at(neura_text s, neura_text needle, int64_t offset) {
    int64_t length = (int64_t)s.len;
    int64_t end = offset + (int64_t)needle.len;
    if (end > length) {
        return 0;
    }
    if (end < length && continuation(s.data[end])) {
        return 0;
    }
    if (offset < length && continuation(s.data[offset])) {
        return 0;
    }
    return bytes_equal(s.data + offset, needle.data, needle.len);
}

/* Searches use the two-way algorithm of Crochemore and Perrin ("Two-way
 * string-matching", J. ACM 38(3), 1991): a critical factorization of the
 * needle by maximal suffixes, then a scan that matches the right part left
 * to right and the left part right to left, remembering a matched period
 * for a periodic needle. It needs no allocation and reads each byte of the
 * text a bounded number of times, so a search of n bytes for a needle of m bytes
 * takes O(n + m) work, never O(n * m). That bound is what lets a search be
 * charged by the length of the text it can read (docs/runtime/Kernels.md).
 *
 * A sequence is read forwards or, for last_index_of, backwards. */
typedef struct sequence {
    const uint8_t* data;
    uint64_t len;
    int reversed;
} sequence;

static uint8_t at(sequence x, uint64_t i) { return x.reversed ? x.data[x.len - 1 - i] : x.data[i]; }

static sequence forwards(const uint8_t* data, uint64_t len) {
    sequence x = {data, len, 0};
    return x;
}

static sequence backwards(const uint8_t* data, uint64_t len) {
    sequence x = {data, len, 1};
    return x;
}

/* A needle prepared for the two-way scan: its critical factorization
 * (needle = left right, with right starting at `suffix`) and the shift after
 * a match or a mismatch in the left part. */
typedef struct two_way {
    sequence needle;
    uint64_t suffix;
    uint64_t period;
    int periodic;
} two_way;

/* The critical factorization of a needle of at least one byte: the later of
 * the maximal suffixes under the byte order and under its reverse, with the
 * period found for it. Wrapping arithmetic on the suffix starts (UINT64_MAX
 * stands for -1) is intended. */
static uint64_t critical_factorization(sequence needle, uint64_t* period) {
    uint64_t n = needle.len;
    if (n < 3) {
        *period = 1;
        return n - 1;
    }
    uint64_t max_suffix = UINT64_MAX;
    uint64_t j = 0;
    uint64_t k = 1;
    uint64_t p = 1;
    while (j + k < n) {
        uint8_t a = at(needle, j + k);
        uint8_t b = at(needle, max_suffix + k);
        if (a < b) {
            j += k;
            k = 1;
            p = j - max_suffix;
        } else if (a == b) {
            if (k != p) {
                k++;
            } else {
                j += p;
                k = 1;
            }
        } else {
            max_suffix = j++;
            k = p = 1;
        }
    }
    *period = p;
    uint64_t max_suffix_rev = UINT64_MAX;
    j = 0;
    k = p = 1;
    while (j + k < n) {
        uint8_t a = at(needle, j + k);
        uint8_t b = at(needle, max_suffix_rev + k);
        if (b < a) {
            j += k;
            k = 1;
            p = j - max_suffix_rev;
        } else if (a == b) {
            if (k != p) {
                k++;
            } else {
                j += p;
                k = 1;
            }
        } else {
            max_suffix_rev = j++;
            k = p = 1;
        }
    }
    if (max_suffix_rev + 1 < max_suffix + 1) {
        return max_suffix + 1;
    }
    *period = p;
    return max_suffix_rev + 1;
}

/* Prepares a needle of at least one byte. */
static two_way prepare(sequence needle) {
    two_way w;
    w.needle = needle;
    w.suffix = critical_factorization(needle, &w.period);
    w.periodic = 1;
    for (uint64_t i = 0; i < w.suffix; i++) {
        if (at(needle, i) != at(needle, i + w.period)) {
            w.periodic = 0;
            break;
        }
    }
    if (!w.periodic) {
        /* No two occurrences are closer than this. */
        uint64_t right = needle.len - w.suffix;
        w.period = (w.suffix > right ? w.suffix : right) + 1;
    }
    return w;
}

/* Which byte matches are occurrences, after Strings.matches_at: a candidate
 * whose first byte or end byte is a continuation byte is no match.
 * Strings.find skips that test for a one-byte needle (FIND_RULE);
 * Strings.last_index_of never does (LAST_RULE). For a valid needle in valid
 * text the test never rejects a byte match. */
enum { FIND_RULE, LAST_RULE };

static int accepted(neura_text s, int64_t offset, uint64_t n, int rule) {
    if (rule == FIND_RULE && n == 1) {
        return 1;
    }
    int64_t length = (int64_t)s.len;
    int64_t end = offset + (int64_t)n;
    if (end < length && continuation(s.data[end])) {
        return 0;
    }
    if (offset < length && continuation(s.data[offset])) {
        return 0;
    }
    return 1;
}

/* The first occurrence in `hay` that the rule accepts, as an offset of s, or
 * -1. `hay` is s[base, base + hay.len) read forwards, or all of s read
 * backwards, where position j is the occurrence that ends at s.len - j. A
 * rejected match shifts the window as a mismatch in the left part does. */
static int64_t scan(const two_way* w, sequence hay, neura_text s, int64_t base, int rule) {
    sequence needle = w->needle;
    uint64_t n = needle.len;
    if (hay.len < n) {
        return -1;
    }
    uint64_t last = hay.len - n;
    uint64_t suffix = w->suffix;
    uint64_t period = w->period;
    uint64_t j = 0;
    if (w->periodic) {
        /* The first `memory` bytes of the window are known to match. */
        uint64_t memory = 0;
        while (j <= last) {
            uint64_t i = suffix > memory ? suffix : memory;
            while (i < n && at(needle, i) == at(hay, i + j)) {
                i++;
            }
            if (i >= n) {
                i = suffix - 1;
                while (memory < i + 1 && at(needle, i) == at(hay, i + j)) {
                    i--;
                }
                if (i + 1 < memory + 1) {
                    int64_t offset = hay.reversed ? (int64_t)(hay.len - j - n) : base + (int64_t)j;
                    if (accepted(s, offset, n, rule)) {
                        return offset;
                    }
                }
                j += period;
                memory = n - period;
            } else {
                j += i - suffix + 1;
                memory = 0;
            }
        }
    } else {
        while (j <= last) {
            uint64_t i = suffix;
            while (i < n && at(needle, i) == at(hay, i + j)) {
                i++;
            }
            if (i >= n) {
                i = suffix - 1;
                while (i != UINT64_MAX && at(needle, i) == at(hay, i + j)) {
                    i--;
                }
                if (i == UINT64_MAX) {
                    int64_t offset = hay.reversed ? (int64_t)(hay.len - j - n) : base + (int64_t)j;
                    if (accepted(s, offset, n, rule)) {
                        return offset;
                    }
                }
                j += period;
            } else {
                j += i - suffix + 1;
            }
        }
    }
    return -1;
}

/* Strings.find with a prepared needle: from is 0 through the length. */
static int64_t find_prepared(const two_way* w, neura_text s, int64_t from) {
    return scan(w, forwards(s.data + from, s.len - (uint64_t)from), s, from, FIND_RULE);
}

/* Strings.find: from is 0 through the length. */
static int64_t find(neura_text s, neura_text needle, int64_t from) {
    if (needle.len == 0) {
        return from;
    }
    if (needle.len > s.len - (uint64_t)from) {
        return -1;
    }
    two_way w = prepare(forwards(needle.data, needle.len));
    return find_prepared(&w, s, from);
}

neura_status neura_k_text_index_of(neura_text s, neura_text needle, neura_i64 from, neura_i64 limit,
                                   neura_i64* result) {
    /* Strings.check_offset. */
    if (from < 0 || from > (int64_t)s.len) {
        return NEURA_TRAP_OFFSET_OUT_OF_RANGE;
    }
    if (from < (int64_t)s.len && continuation(s.data[from])) {
        return NEURA_TRAP_NOT_CHARACTER_BOUNDARY;
    }
    uint64_t n = needle.len;
    uint64_t start = (uint64_t)from;
    if (n == 0) {
        *result = from;
        return NEURA_OK;
    }
    if (n > s.len - start) {
        *result = -1;
        return NEURA_OK;
    }
    /* Candidates start in [from, end): `limit` of them (at least one), with
     * `end` moved past continuation bytes to a boundary, or every remaining
     * candidate when that reaches the last one. The sum stays below 2^64
     * because both terms are below 2^63. */
    uint64_t final_end = s.len - n + 1;
    uint64_t end = start + (limit < 1 ? 1u : (uint64_t)limit);
    while (end < final_end && continuation(s.data[end])) {
        end++;
    }
    int whole = end >= final_end;
    if (whole) {
        end = final_end;
    }
    two_way w = prepare(forwards(needle.data, n));
    int64_t found = scan(&w, forwards(s.data + start, end - start + n - 1), s, from, FIND_RULE);
    *result = (found >= 0 || whole) ? found : -2 - (int64_t)end;
    return NEURA_OK;
}

neura_i64 neura_k_text_last_index_of(neura_text s, neura_text needle) {
    if (needle.len == 0) {
        return (neura_i64)s.len;
    }
    if (needle.len > s.len) {
        return -1;
    }
    two_way w = prepare(backwards(needle.data, needle.len));
    return scan(&w, backwards(s.data, s.len), s, 0, LAST_RULE);
}

neura_bool neura_k_text_contains(neura_text s, neura_text needle) {
    return find(s, needle, 0) >= 0;
}

neura_bool neura_k_text_starts_with(neura_text s, neura_text prefix) {
    return (neura_bool)matches_at(s, prefix, 0);
}

neura_bool neura_k_text_ends_with(neura_text s, neura_text suffix) {
    int64_t start = (int64_t)s.len - (int64_t)suffix.len;
    if (start < 0) {
        return 0;
    }
    return (neura_bool)matches_at(s, suffix, start);
}

neura_i32 neura_k_text_compare(neura_text a, neura_text b) {
    uint64_t shorter = a.len < b.len ? a.len : b.len;
    for (uint64_t i = 0; i < shorter; i++) {
        if (a.data[i] != b.data[i]) {
            return a.data[i] < b.data[i] ? -1 : 1;
        }
    }
    if (a.len == b.len) {
        return 0;
    }
    return a.len < b.len ? -1 : 1;
}

/* The occurrences Strings.replace_up_to replaces: up to `limit` of them,
 * none for a limit below 1. When `offsets` is not null, the offsets of the
 * first `capacity` of them are stored there. The target is not empty. */
static int64_t occurrences(neura_text s, neura_text target, int64_t limit, int64_t* offsets, uint64_t capacity) {
    if (limit < 1 || target.len > s.len) {
        return 0;
    }
    int64_t n = (int64_t)target.len;
    int64_t count = 0;
    two_way w = prepare(forwards(target.data, target.len));
    int64_t found = find_prepared(&w, s, 0);
    while (found >= 0 && count < limit) {
        if (offsets != 0 && (uint64_t)count < capacity) {
            offsets[count] = found;
        }
        count++;
        if (count < limit) {
            found = find_prepared(&w, s, found + n);
        }
    }
    return count;
}

neura_status neura_k_text_count(neura_text s, neura_text target, neura_i64* result) {
    if (target.len == 0) {
        return NEURA_TRAP_EMPTY_TARGET;
    }
    *result = occurrences(s, target, INT64_MAX, 0, 0);
    return NEURA_OK;
}

/* Strings.map_ascii_letters. */
static neura_status map_ascii(neura_text s, uint8_t low, uint8_t high, neura_out_text* out) {
    if (s.len > out->cap) {
        out->len = s.len;
        return NEURA_NEED_CAPACITY;
    }
    for (uint64_t i = 0; i < s.len; i++) {
        uint8_t b = s.data[i];
        out->data[i] = (b >= low && b <= high) ? (uint8_t)(b ^ 32) : b;
    }
    out->len = s.len;
    return NEURA_OK;
}

neura_status neura_k_text_ascii_lower(neura_text s, neura_out_text* out) { return map_ascii(s, 'A', 'Z', out); }

neura_status neura_k_text_ascii_upper(neura_text s, neura_out_text* out) { return map_ascii(s, 'a', 'z', out); }

/* Strings.is_space. */
static int is_space(uint8_t b) { return b == 32 || (b >= 9 && b <= 13); }

neura_i64 neura_k_text_space_prefix_length(neura_text s) {
    uint64_t start = 0;
    while (start < s.len && is_space(s.data[start])) {
        start++;
    }
    return (neura_i64)start;
}

neura_i64 neura_k_text_space_suffix_start(neura_text s) {
    uint64_t end = s.len;
    while (end > 0 && is_space(s.data[end - 1])) {
        end--;
    }
    return (neura_i64)end;
}

neura_i64 neura_k_text_blank_prefix_length(neura_text s) {
    uint64_t start = 0;
    while (start < s.len && s.data[start] <= 32) {
        start++;
    }
    return (neura_i64)start;
}

neura_i64 neura_k_text_blank_suffix_start(neura_text s) {
    uint64_t end = s.len;
    while (end > 0 && s.data[end - 1] <= 32) {
        end--;
    }
    return (neura_i64)end;
}

static void copy_bytes(uint8_t* to, const uint8_t* from, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) {
        to[i] = from[i];
    }
}

neura_status neura_k_text_replace(neura_text s, neura_text target, neura_text replacement, neura_i64 limit,
                                  neura_out_text* out) {
    if (target.len == 0) {
        return NEURA_TRAP_EMPTY_TARGET;
    }
    int64_t n = (int64_t)target.len;
    int64_t count = occurrences(s, target, limit, 0, 0);
    /* Each replacement removes n >= 1 bytes, so the text without them and
     * count copies of the replacement fit unless the product overflows. */
    uint64_t kept = s.len - (uint64_t)count * target.len;
    uint64_t needed;
    if (replacement.len != 0 && (uint64_t)count > (UINT64_MAX - kept) / replacement.len) {
        needed = UINT64_MAX;
    } else {
        needed = kept + (uint64_t)count * replacement.len;
    }
    if (needed > out->cap) {
        out->len = needed;
        return NEURA_NEED_CAPACITY;
    }
    uint8_t* to = out->data;
    uint64_t k = 0;
    int64_t start = 0;
    if (count > 0) {
        /* Prepared only when the target occurs, so no longer than the text. */
        two_way w = prepare(forwards(target.data, target.len));
        int64_t found = find_prepared(&w, s, 0);
        for (int64_t r = 0; r < count; r++) {
            copy_bytes(to + k, s.data + start, (uint64_t)(found - start));
            k += (uint64_t)(found - start);
            copy_bytes(to + k, replacement.data, replacement.len);
            k += replacement.len;
            start = found + n;
            if (r + 1 < count) {
                found = find_prepared(&w, s, start);
            }
        }
    }
    copy_bytes(to + k, s.data + start, s.len - (uint64_t)start);
    k += s.len - (uint64_t)start;
    out->len = k;
    return NEURA_OK;
}

neura_status neura_k_text_split(neura_text s, neura_text separator, neura_i64 limit, neura_mut_i64 offsets,
                                neura_i64* result) {
    if (separator.len == 0) {
        return NEURA_TRAP_EMPTY_SEPARATOR;
    }
    *result = occurrences(s, separator, limit, offsets.data, offsets.len);
    return NEURA_OK;
}
