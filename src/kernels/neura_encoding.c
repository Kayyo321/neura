/* Encoding kernels (neura_encoding.h). */
#include "neura_encoding.h"

/* Strings.valid_length: the length of the well-formed sequence at i, or the
 * negated length of the maximal subpart of an ill-formed one (Unicode
 * Table 3-7). */
static int64_t utf8_valid_length(const uint8_t* bytes, uint64_t length, uint64_t i) {
    uint8_t lead = bytes[i];
    if (lead < 128) {
        return 1;
    }
    int64_t size;
    uint8_t low = 128;
    uint8_t high = 191;
    if (lead >= 194 && lead <= 223) {
        size = 2;
    } else if (lead >= 224 && lead <= 239) {
        size = 3;
        if (lead == 224) {
            low = 160;
        } else if (lead == 237) {
            high = 159;
        }
    } else if (lead >= 240 && lead <= 244) {
        size = 4;
        if (lead == 240) {
            low = 144;
        } else if (lead == 244) {
            high = 143;
        }
    } else {
        return -1;
    }
    for (int64_t k = 1; k < size; k++) {
        if (i + (uint64_t)k >= length) {
            return -k;
        }
        uint8_t next = bytes[i + (uint64_t)k];
        if (next < low || next > high) {
            return -k;
        }
        low = 128;
        high = 191;
    }
    return size;
}

/* The first ill-formed sequence: its offset and the size of its maximal
 * subpart, or 0 when the bytes are valid. */
static int64_t utf8_first_error(const uint8_t* bytes, uint64_t length, uint64_t* offset) {
    uint64_t i = 0;
    while (i < length) {
        int64_t size = utf8_valid_length(bytes, length, i);
        if (size <= 0) {
            *offset = i;
            return -size;
        }
        i += (uint64_t)size;
    }
    return 0;
}

neura_i64 neura_k_encoding_utf8_validate(neura_bytes bytes) {
    uint64_t offset = 0;
    if (utf8_first_error(bytes.data, bytes.len, &offset) == 0) {
        return -1;
    }
    return (neura_i64)offset;
}

neura_status neura_k_encoding_utf8_check(neura_bytes bytes, neura_i64* error_offset) {
    uint64_t offset = 0;
    int64_t size = utf8_first_error(bytes.data, bytes.len, &offset);
    if (size == 0) {
        return NEURA_OK;
    }
    *error_offset = (neura_i64)offset;
    /* Strings.invalid_message. */
    uint8_t lead = bytes.data[offset];
    if (lead < 194 || lead > 244) {
        return NEURA_ERR_INVALID_START_BYTE;
    }
    if (offset + (uint64_t)size >= bytes.len) {
        return NEURA_ERR_UNEXPECTED_END_OF_DATA;
    }
    return NEURA_ERR_INVALID_CONTINUATION_BYTE;
}

neura_i64 neura_k_encoding_utf8_scalar_count(neura_text text) {
    int64_t count = 0;
    for (uint64_t i = 0; i < text.len; i++) {
        if ((text.data[i] & 192) != 128) {
            count++;
        }
    }
    return count;
}

static const uint8_t base64_standard[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/',
};

static const uint8_t base64_url[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_',
};

/* Base64.values_of: the digit value of a byte, 255 outside the alphabet. */
static uint8_t base64_value(uint8_t b, int url) {
    if (b >= 'A' && b <= 'Z') {
        return (uint8_t)(b - 'A');
    }
    if (b >= 'a' && b <= 'z') {
        return (uint8_t)(b - 'a' + 26);
    }
    if (b >= '0' && b <= '9') {
        return (uint8_t)(b - '0' + 52);
    }
    if (b == (url ? '-' : '+')) {
        return 62;
    }
    if (b == (url ? '_' : '/')) {
        return 63;
    }
    return 255;
}

neura_status neura_k_encoding_base64_encode(neura_bytes bytes, neura_bool url, neura_bool padded,
                                            neura_out_text* out) {
    uint64_t count = bytes.len;
    uint64_t whole = count / 3;
    uint64_t rest = count - whole * 3;
    uint64_t needed = whole * 4;
    if (rest != 0) {
        needed += padded ? 4 : rest + 1;
    }
    if (needed > out->cap) {
        out->len = needed;
        return NEURA_NEED_CAPACITY;
    }
    const uint8_t* alphabet = url ? base64_url : base64_standard;
    const uint8_t* in = bytes.data;
    uint8_t* chars = out->data;
    uint64_t k = 0;
    uint64_t i = 0;
    while (i + 3 <= count) {
        uint8_t b0 = in[i], b1 = in[i + 1], b2 = in[i + 2];
        chars[k++] = alphabet[b0 >> 2];
        chars[k++] = alphabet[((b0 & 3) << 4) | (b1 >> 4)];
        chars[k++] = alphabet[((b1 & 15) << 2) | (b2 >> 6)];
        chars[k++] = alphabet[b2 & 63];
        i += 3;
    }
    if (rest == 1) {
        uint8_t b0 = in[i];
        chars[k++] = alphabet[b0 >> 2];
        chars[k++] = alphabet[(b0 & 3) << 4];
        if (padded) {
            chars[k++] = '=';
            chars[k++] = '=';
        }
    } else if (rest == 2) {
        uint8_t b0 = in[i], b1 = in[i + 1];
        chars[k++] = alphabet[b0 >> 2];
        chars[k++] = alphabet[((b0 & 3) << 4) | (b1 >> 4)];
        chars[k++] = alphabet[(b1 & 15) << 2];
        if (padded) {
            chars[k++] = '=';
        }
    }
    out->len = k;
    return NEURA_OK;
}

static neura_status fail_at(neura_i64* error_offset, uint64_t offset, neura_status status) {
    *error_offset = (neura_i64)offset;
    return status;
}

/* Base64.decode_with. The structure is checked first, then the capacity, so
 * an error never depends on the output buffer. */
neura_status neura_k_encoding_base64_decode(neura_text text, neura_bool url, neura_bool padded,
                                            neura_out_bytes* out, neura_i64* error_offset) {
    const int use_url = url != 0;
    const uint8_t* t = text.data;
    uint64_t count = text.len;
    /* Whole groups of four alphabet characters. */
    uint64_t i = 0;
    while (i + 4 <= count) {
        uint8_t v0 = base64_value(t[i], use_url), v1 = base64_value(t[i + 1], use_url);
        uint8_t v2 = base64_value(t[i + 2], use_url), v3 = base64_value(t[i + 3], use_url);
        if ((v0 | v1 | v2 | v3) > 63) {
            break;
        }
        i += 4;
    }
    uint64_t digits = 0;
    if (i != count) {
        /* The group at i is the last one, or it holds a byte outside the
         * alphabet. Its alphabet characters number at most three. */
        while (i + digits < count && base64_value(t[i + digits], use_url) < 64) {
            digits++;
        }
        uint64_t end = i + digits;
        if (end == count) {
            if (digits == 1 || padded) {
                return fail_at(error_offset, count, NEURA_ERR_UNEXPECTED_END_OF_DATA);
            }
        } else {
            if (t[end] != '=') {
                return fail_at(error_offset, end, NEURA_ERR_INVALID_CHARACTER);
            }
            if (!padded) {
                return fail_at(error_offset, end, NEURA_ERR_UNEXPECTED_PADDING);
            }
            if (digits < 2) {
                return fail_at(error_offset, end, NEURA_ERR_INVALID_PADDING);
            }
            uint64_t needed = 4 - digits;
            for (uint64_t k = 0; k < needed; k++) {
                if (end + k == count) {
                    return fail_at(error_offset, count, NEURA_ERR_UNEXPECTED_END_OF_DATA);
                }
                if (t[end + k] != '=') {
                    return fail_at(error_offset, end + k, NEURA_ERR_INVALID_PADDING);
                }
            }
            if (end + needed < count) {
                return fail_at(error_offset, end + needed, NEURA_ERR_INVALID_PADDING);
            }
        }
        /* A final group of two or three characters. */
        if (digits == 2) {
            if ((base64_value(t[i + 1], use_url) & 15) != 0) {
                return fail_at(error_offset, i + 1, NEURA_ERR_NONZERO_TRAILING_BITS);
            }
        } else if ((base64_value(t[i + 2], use_url) & 3) != 0) {
            return fail_at(error_offset, i + 2, NEURA_ERR_NONZERO_TRAILING_BITS);
        }
    }
    uint64_t length = (i / 4) * 3 + (digits == 0 ? 0 : digits - 1);
    if (length > out->cap) {
        out->len = length;
        return NEURA_NEED_CAPACITY;
    }
    uint8_t* bytes = out->data;
    uint64_t k = 0;
    for (uint64_t g = 0; g < i; g += 4) {
        uint8_t v0 = base64_value(t[g], use_url), v1 = base64_value(t[g + 1], use_url);
        uint8_t v2 = base64_value(t[g + 2], use_url), v3 = base64_value(t[g + 3], use_url);
        bytes[k++] = (uint8_t)((v0 << 2) | (v1 >> 4));
        bytes[k++] = (uint8_t)(((v1 & 15) << 4) | (v2 >> 2));
        bytes[k++] = (uint8_t)(((v2 & 3) << 6) | v3);
    }
    if (digits >= 2) {
        uint8_t v0 = base64_value(t[i], use_url), v1 = base64_value(t[i + 1], use_url);
        bytes[k++] = (uint8_t)((v0 << 2) | (v1 >> 4));
        if (digits == 3) {
            uint8_t v2 = base64_value(t[i + 2], use_url);
            bytes[k++] = (uint8_t)(((v1 & 15) << 4) | (v2 >> 2));
        }
    }
    out->len = k;
    return NEURA_OK;
}

neura_status neura_k_encoding_hex_encode(neura_bytes bytes, neura_out_text* out) {
    uint64_t needed = bytes.len * 2;
    if (bytes.len > UINT64_MAX / 2 || needed > out->cap) {
        out->len = needed;
        return NEURA_NEED_CAPACITY;
    }
    static const uint8_t digits[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                       '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (uint64_t i = 0; i < bytes.len; i++) {
        out->data[2 * i] = digits[bytes.data[i] >> 4];
        out->data[2 * i + 1] = digits[bytes.data[i] & 15];
    }
    out->len = needed;
    return NEURA_OK;
}

/* Hex.ready_values: the value of a hex digit, 255 for any other byte. */
static uint8_t hex_value(uint8_t b) {
    if (b >= '0' && b <= '9') {
        return (uint8_t)(b - '0');
    }
    if (b >= 'a' && b <= 'f') {
        return (uint8_t)(b - 'a' + 10);
    }
    if (b >= 'A' && b <= 'F') {
        return (uint8_t)(b - 'A' + 10);
    }
    return 255;
}

neura_status neura_k_encoding_hex_decode(neura_text text, neura_out_bytes* out, neura_i64* error_offset) {
    const uint8_t* t = text.data;
    uint64_t count = text.len;
    uint64_t i = 0;
    while (i + 1 < count) {
        uint8_t high = hex_value(t[i]);
        uint8_t low = hex_value(t[i + 1]);
        if ((high | low) > 15) {
            return fail_at(error_offset, high > 15 ? i : i + 1, NEURA_ERR_INVALID_HEX_DIGIT);
        }
        i += 2;
    }
    if (i < count) {
        if (hex_value(t[i]) > 15) {
            return fail_at(error_offset, i, NEURA_ERR_INVALID_HEX_DIGIT);
        }
        return fail_at(error_offset, count, NEURA_ERR_ODD_HEX_DIGITS);
    }
    uint64_t length = count / 2;
    if (length > out->cap) {
        out->len = length;
        return NEURA_NEED_CAPACITY;
    }
    for (uint64_t k = 0; k < length; k++) {
        out->data[k] = (uint8_t)((hex_value(t[2 * k]) << 4) | hex_value(t[2 * k + 1]));
    }
    out->len = length;
    return NEURA_OK;
}
