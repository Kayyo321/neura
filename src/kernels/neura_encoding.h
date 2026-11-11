/* Encoding kernels: UTF-8 validation and counting, Base64 (RFC 4648), and
 * hexadecimal. They follow stdlib/std/encoding/utf8.nra, base64.nra, and
 * hex.nra, and Strings.from_bytes and Strings.scalar_count in
 * stdlib/std/text/strings.nra, including the error kinds and offsets. */
#ifndef NEURA_ENCODING_H
#define NEURA_ENCODING_H

#include "neura_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Utf8.validate: -1 when the bytes are valid UTF-8, otherwise the offset of
 * the first ill-formed sequence (the length of the longest valid prefix). */
NEURA_KERNEL("neura.encoding.utf8_validate", 1, "1 + bytes / 64")
neura_i64 neura_k_encoding_utf8_validate(neura_bytes bytes);

/* Strings.from_bytes without the copy: NEURA_OK for valid UTF-8, otherwise
 * NEURA_ERR_INVALID_START_BYTE, NEURA_ERR_INVALID_CONTINUATION_BYTE, or
 * NEURA_ERR_UNEXPECTED_END_OF_DATA with the offset that Utf8.validate
 * returns. */
NEURA_KERNEL("neura.encoding.utf8_check", 1, "1 + bytes / 64")
neura_status neura_k_encoding_utf8_check(neura_bytes bytes, neura_i64* error_offset);

/* Strings.scalar_count: the number of bytes that are not continuation
 * bytes. */
NEURA_KERNEL("neura.encoding.utf8_scalar_count", 1, "1 + text / 64")
neura_i64 neura_k_encoding_utf8_scalar_count(neura_text text);

/* Base64.encode, encode_unpadded, encode_url, encode_url_unpadded: `url`
 * selects the URL- and filename-safe alphabet, `padded` the final `=`. */
NEURA_KERNEL("neura.encoding.base64_encode", 1, "1 + bytes / 48")
NEURA_KERNEL_BUFFER("out", "4 * ((bytes + 2) / 3)")
neura_status neura_k_encoding_base64_encode(neura_bytes bytes, neura_bool url, neura_bool padded,
                                            neura_out_text* out);

/* Base64.decode and its three variants, strict as documented there. Errors:
 * NEURA_ERR_INVALID_CHARACTER, NEURA_ERR_INVALID_PADDING,
 * NEURA_ERR_UNEXPECTED_PADDING, NEURA_ERR_UNEXPECTED_END_OF_DATA, and
 * NEURA_ERR_NONZERO_TRAILING_BITS, each with its offset. */
NEURA_KERNEL("neura.encoding.base64_decode", 1, "1 + text / 64")
NEURA_KERNEL_BUFFER("out", "3 * (text / 4) + 2")
neura_status neura_k_encoding_base64_decode(neura_text text, neura_bool url, neura_bool padded,
                                            neura_out_bytes* out, neura_i64* error_offset);

/* Hex.encode: two lowercase digits per byte. */
NEURA_KERNEL("neura.encoding.hex_encode", 1, "1 + bytes / 32")
NEURA_KERNEL_BUFFER("out", "2 * bytes")
neura_status neura_k_encoding_hex_encode(neura_bytes bytes, neura_out_text* out);

/* Hex.decode: either case. Errors: NEURA_ERR_INVALID_HEX_DIGIT at the
 * digit, NEURA_ERR_ODD_HEX_DIGITS at the text's length. */
NEURA_KERNEL("neura.encoding.hex_decode", 1, "1 + text / 64")
NEURA_KERNEL_BUFFER("out", "text / 2")
neura_status neura_k_encoding_hex_decode(neura_text text, neura_out_bytes* out, neura_i64* error_offset);

#ifdef __cplusplus
}
#endif

#endif
