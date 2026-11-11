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

/* Strings.find: from must not be negative. */
static int64_t find(neura_text s, neura_text needle, int64_t from) {
    int64_t n = (int64_t)needle.len;
    if (n == 0) {
        return from;
    }
    int64_t last = (int64_t)s.len - n;
    uint8_t first = needle.data[0];
    for (int64_t i = from; i <= last; i++) {
        if (s.data[i] == first && (n == 1 || matches_at(s, needle, i))) {
            return i;
        }
    }
    return -1;
}

neura_status neura_k_text_index_of(neura_text s, neura_text needle, neura_i64 from, neura_i64* result) {
    /* Strings.check_offset. */
    if (from < 0 || from > (int64_t)s.len) {
        return NEURA_TRAP_OFFSET_OUT_OF_RANGE;
    }
    if (from < (int64_t)s.len && continuation(s.data[from])) {
        return NEURA_TRAP_NOT_CHARACTER_BOUNDARY;
    }
    *result = find(s, needle, from);
    return NEURA_OK;
}

neura_i64 neura_k_text_last_index_of(neura_text s, neura_text needle) {
    int64_t i = (int64_t)s.len - (int64_t)needle.len;
    if (needle.len == 0) {
        return i;
    }
    uint8_t first = needle.data[0];
    for (; i >= 0; i--) {
        if (s.data[i] == first && matches_at(s, needle, i)) {
            return i;
        }
    }
    return -1;
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

/* The occurrences Strings.replace_up_to replaces: up to `limit` of them. */
static int64_t count_up_to(neura_text s, neura_text target, int64_t limit) {
    int64_t n = (int64_t)target.len;
    int64_t count = 0;
    int64_t found = find(s, target, 0);
    while (found >= 0 && count < limit) {
        count++;
        if (count < limit) {
            found = find(s, target, found + n);
        }
    }
    return count;
}

neura_status neura_k_text_count(neura_text s, neura_text target, neura_i64* result) {
    if (target.len == 0) {
        return NEURA_TRAP_EMPTY_TARGET;
    }
    *result = count_up_to(s, target, INT64_MAX);
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
    int64_t count = limit < 1 ? 0 : count_up_to(s, target, limit);
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
    int64_t found = count > 0 ? find(s, target, 0) : -1;
    for (int64_t r = 0; r < count; r++) {
        copy_bytes(to + k, s.data + start, (uint64_t)(found - start));
        k += (uint64_t)(found - start);
        copy_bytes(to + k, replacement.data, replacement.len);
        k += replacement.len;
        start = found + n;
        if (r + 1 < count) {
            found = find(s, target, start);
        }
    }
    copy_bytes(to + k, s.data + start, s.len - (uint64_t)start);
    k += s.len - (uint64_t)start;
    out->len = k;
    return NEURA_OK;
}

neura_status neura_k_text_split_next(neura_text s, neura_text separator, neura_i64 start, neura_i64* result) {
    if (separator.len == 0) {
        return NEURA_TRAP_EMPTY_SEPARATOR;
    }
    if (start < 0) {
        return NEURA_TRAP_OFFSET_OUT_OF_RANGE;
    }
    *result = find(s, separator, start);
    return NEURA_OK;
}
