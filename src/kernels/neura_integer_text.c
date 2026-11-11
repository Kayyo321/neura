/* Integer text kernels (neura_integer_text.h). */
#include "neura_integer_text.h"

/* IntegerText.digit_byte. */
static uint8_t digit_byte(unsigned value) { return (uint8_t)(value < 10 ? value + 48 : value + 87); }

/* IntegerText.digit_value: 36 for a byte that is a digit of no radix. */
static unsigned digit_value(uint8_t b) {
    if (b >= '0' && b <= '9') {
        return (unsigned)(b - '0');
    }
    if (b >= 'a' && b <= 'z') {
        return (unsigned)(b - 87);
    }
    if (b >= 'A' && b <= 'Z') {
        return (unsigned)(b - 55);
    }
    return 36;
}

/* Writes the reversed digits after an optional '-'. */
static neura_status write_digits(const uint8_t* reversed, unsigned count, int negative, neura_out_text* out) {
    uint64_t needed = count + (negative ? 1u : 0u);
    if (needed > out->cap) {
        out->len = needed;
        return NEURA_NEED_CAPACITY;
    }
    uint64_t k = 0;
    if (negative) {
        out->data[k++] = '-';
    }
    for (unsigned i = count; i > 0; i--) {
        out->data[k++] = reversed[i - 1];
    }
    out->len = k;
    return NEURA_OK;
}

static int radix_valid(int64_t radix) { return radix >= 2 && radix <= 36; }

/* Int64.to_string_radix: digits of the value's negation, so Min needs no
 * special case. Radix 10 uses the decimal to_string, which gives the same
 * text. */
neura_status neura_k_integer_format_i64(neura_i64 value, neura_i64 radix, neura_out_text* out) {
    if (!radix_valid(radix)) {
        return NEURA_TRAP_RADIX_OUT_OF_RANGE;
    }
    int64_t base = radix;
    int64_t rest = value > 0 ? -value : value;
    uint8_t digits[64];
    unsigned count = 0;
    do {
        digits[count++] = digit_byte((unsigned)-(rest % base));
        rest /= base;
    } while (rest != 0);
    return write_digits(digits, count, value < 0, out);
}

neura_status neura_k_integer_format_u64(neura_u64 value, neura_i64 radix, neura_out_text* out) {
    if (!radix_valid(radix)) {
        return NEURA_TRAP_RADIX_OUT_OF_RANGE;
    }
    uint64_t base = (uint64_t)radix;
    uint64_t rest = value;
    uint8_t digits[64];
    unsigned count = 0;
    do {
        digits[count++] = digit_byte((unsigned)(rest % base));
        rest /= base;
    } while (rest != 0);
    return write_digits(digits, count, 0, out);
}

/* IntegerText.syntax_error; on success stores the sign length. */
static neura_status syntax_check(neura_text text, int64_t radix, uint64_t* start, neura_i64* error_offset) {
    if (text.len == 0) {
        *error_offset = 0;
        return NEURA_ERR_PARSE_EMPTY;
    }
    uint64_t sign = (text.data[0] == '+' || text.data[0] == '-') ? 1 : 0;
    if (sign == text.len) {
        *error_offset = (neura_i64)text.len;
        return NEURA_ERR_PARSE_EXPECTED_DIGIT;
    }
    for (uint64_t i = sign; i < text.len; i++) {
        if (digit_value(text.data[i]) >= (unsigned)radix) {
            *error_offset = (neura_i64)i;
            return NEURA_ERR_PARSE_INVALID_CHARACTER;
        }
    }
    *start = sign;
    return NEURA_OK;
}

/* Int64.parse_radix: accumulates the negated value toward Min or -Max. */
neura_status neura_k_integer_parse_i64(neura_text text, neura_i64 radix, neura_i64* result, neura_i64* error_offset) {
    if (!radix_valid(radix)) {
        return NEURA_TRAP_RADIX_OUT_OF_RANGE;
    }
    uint64_t start = 0;
    neura_status status = syntax_check(text, radix, &start, error_offset);
    if (status != NEURA_OK) {
        return status;
    }
    int negative = start == 1 && text.data[0] == '-';
    int64_t limit = negative ? INT64_MIN : -INT64_MAX;
    int64_t base = radix;
    int64_t multiple_limit = limit / base;
    int64_t value = 0;
    for (uint64_t i = start; i < text.len; i++) {
        int64_t digit = (int64_t)digit_value(text.data[i]);
        if (value < multiple_limit) {
            *error_offset = 0;
            return NEURA_ERR_PARSE_OUT_OF_RANGE;
        }
        value *= base;
        if (value < limit + digit) {
            *error_offset = 0;
            return NEURA_ERR_PARSE_OUT_OF_RANGE;
        }
        value -= digit;
    }
    *result = negative ? value : -value;
    return NEURA_OK;
}

neura_status neura_k_integer_parse_u64(neura_text text, neura_i64 radix, neura_u64* result, neura_i64* error_offset) {
    if (!radix_valid(radix)) {
        return NEURA_TRAP_RADIX_OUT_OF_RANGE;
    }
    uint64_t start = 0;
    neura_status status = syntax_check(text, radix, &start, error_offset);
    if (status != NEURA_OK) {
        return status;
    }
    int negative = start == 1 && text.data[0] == '-';
    uint64_t base = (uint64_t)radix;
    uint64_t value = 0;
    for (uint64_t i = start; i < text.len; i++) {
        uint64_t digit = digit_value(text.data[i]);
        if (value > (UINT64_MAX - digit) / base) {
            *error_offset = 0;
            return NEURA_ERR_PARSE_OUT_OF_RANGE;
        }
        value = value * base + digit;
    }
    if (negative && value != 0) {
        *error_offset = 0;
        return NEURA_ERR_PARSE_OUT_OF_RANGE;
    }
    *result = value;
    return NEURA_OK;
}
