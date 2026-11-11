// Tests of the C kernels (src/kernels, docs/runtime/Kernels.md) against
// published vectors, boundary cases, and the behaviour of the Neura
// implementations they replace. The math suite checks every case of
// tests/stdlib/math/support/math-vectors.nra bit for bit, the vectors that
// Std.Math.Math is tested against.
//
// Usage: neura_kernel_tests <suite> <source-dir>
// Suites: abi digest encoding text integer hash sort math table

#include "kernel_table.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& what, int line) {
    if (condition) return;
    ++failures;
    std::cout << "FAIL line " << line << ": " << what << '\n';
}

#define CHECK(condition) check((condition), #condition, __LINE__)
#define CHECK_EQ(actual, expected) \
    check((actual) == (expected), std::string(#actual " == " #expected " (got ") + show(actual) + ")", __LINE__)

std::string show(const std::string& s) { return '"' + s + '"'; }
template <typename T>
std::string show(const T& value) {
    std::ostringstream out;
    out << +value;
    return out.str();
}

neura_text text(const std::string& s) {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), static_cast<std::uint64_t>(s.size())};
}
neura_bytes bytes(const std::string& s) {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), static_cast<std::uint64_t>(s.size())};
}

// An output buffer that grows on NEURA_NEED_CAPACITY, as a runtime would.
struct Out {
    std::vector<std::uint8_t> storage;
    neura_out_bytes bytes_view{};
    neura_out_text text_view{};
    std::string str() const { return std::string(storage.begin(), storage.end()); }
};

// Calls `kernel` with a buffer of `initial` bytes, grows it once if asked,
// and returns the status. The result is in out.storage.
template <typename View, typename Call>
neura_status with_output(Out& out, std::uint64_t initial, Call call) {
    out.storage.assign(initial, 0xAA);
    View view{out.storage.data(), 0, initial};
    neura_status status = call(&view);
    if (status == NEURA_NEED_CAPACITY) {
        std::uint64_t needed = view.len;
        if (needed <= initial) return NEURA_NEED_CAPACITY;  // a kernel must ask for more
        out.storage.assign(needed, 0xAA);
        view = View{out.storage.data(), 0, needed};
        status = call(&view);
    }
    out.storage.resize(status == NEURA_OK ? view.len : 0);
    return status;
}

std::string hex_of(const std::vector<std::uint8_t>& v) {
    std::string s;
    for (auto b : v) {
        s += "0123456789abcdef"[b >> 4];
        s += "0123456789abcdef"[b & 15];
    }
    return s;
}

std::uint64_t bits_of(double x) {
    std::uint64_t b;
    std::memcpy(&b, &x, 8);
    return b;
}
double from_bits(std::uint64_t b) {
    double x;
    std::memcpy(&x, &b, 8);
    return x;
}

// ---------------------------------------------------------------- abi

int abi_suite() {
    CHECK_EQ(std::string(neura_kernel_status_message(NEURA_OK)), std::string("ok"));
    CHECK_EQ(std::string(neura_kernel_status_message(NEURA_ERR_INVALID_START_BYTE)),
             std::string("invalid start byte"));
    CHECK_EQ(std::string(neura_kernel_status_message(NEURA_ERR_ODD_HEX_DIGITS)),
             std::string("odd number of hex digits"));
    CHECK_EQ(std::string(neura_kernel_status_message(NEURA_TRAP_RADIX_OUT_OF_RANGE)),
             std::string("invalid argument: radix outside 2 through 36"));
    CHECK_EQ(std::string(neura_kernel_status_message(NEURA_TRAP_DIGEST_FINISHED)),
             std::string("invalid state: digest finished"));
    CHECK_EQ(std::string(neura_kernel_status_message(static_cast<neura_status>(999))),
             std::string("unknown kernel status"));
    CHECK_EQ(std::string(neura_kernel_status_error_kind(NEURA_ERR_INVALID_PADDING)), std::string("Encoding"));
    CHECK_EQ(std::string(neura_kernel_status_error_kind(NEURA_ERR_PARSE_EXPECTED_DIGIT)),
             std::string("Parse.UnexpectedEnd"));
    CHECK(neura_kernel_status_error_kind(NEURA_TRAP_EMPTY_TARGET) == nullptr);
    CHECK(neura_kernel_status_error_kind(NEURA_OK) == nullptr);
    // Every status has a message.
    for (int s : {0, 1, 16, 17, 18, 19, 20, 21, 22, 23, 24, 32, 33, 34, 35, 64, 65, 66, 67, 68, 69, 70, 71, 72}) {
        CHECK(std::string(neura_kernel_status_message(static_cast<neura_status>(s))) != "unknown kernel status");
    }
    // The ABI types have fixed shapes.
    static_assert(sizeof(neura_text) == 16 && sizeof(neura_bytes) == 16);
    static_assert(sizeof(neura_out_text) == 24 && sizeof(neura_out_bytes) == 24);
    static_assert(sizeof(neura_view_f64) == 16 && sizeof(neura_mut_i64) == 16);
    static_assert(sizeof(neura_bool) == 1);
    return failures;
}

// ---------------------------------------------------------------- digest

std::string sha256_hex(const std::string& message) {
    Out out;
    neura_status status = with_output<neura_out_bytes>(out, 0, [&](neura_out_bytes* o) {
        return neura_k_digest_sha256(bytes(message), o);
    });
    if (status != NEURA_OK) return "status " + std::to_string(status);
    return hex_of(out.storage);
}

int digest_suite() {
    // FIPS 180-4 examples (also RFC 6234 section 8.5).
    CHECK_EQ(sha256_hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256_hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK_EQ(sha256_hex("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopq"
                        "rsmnopqrstnopqrstu"),
             std::string("cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
    CHECK_EQ(sha256_hex(std::string(1000000, 'a')),
             std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    // Padding boundaries: 55, 56, 63, 64 bytes.
    CHECK_EQ(sha256_hex(std::string(55, 'a')),
             std::string("9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
    CHECK_EQ(sha256_hex(std::string(56, 'a')),
             std::string("b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"));
    CHECK_EQ(sha256_hex(std::string(64, 'a')),
             std::string("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));

    // The running digest: every split of a 200-byte message, in up to three updates.
    std::string message;
    for (int i = 0; i < 200; ++i) message += static_cast<char>(i * 7 + 3);
    const std::string whole = sha256_hex(message);
    std::vector<std::uint8_t> state(NEURA_SHA256_STATE_SIZE);
    neura_state st{state.data(), state.size()};
    for (std::size_t cut = 0; cut <= message.size(); ++cut) {
        for (std::size_t cut2 : {cut, (cut + message.size()) / 2, message.size()}) {
            CHECK(neura_k_digest_sha256_init(&st) == NEURA_OK);
            CHECK(neura_k_digest_sha256_update(&st, bytes(message.substr(0, cut))) == NEURA_OK);
            CHECK(neura_k_digest_sha256_update(&st, bytes(message.substr(cut, cut2 - cut))) == NEURA_OK);
            CHECK(neura_k_digest_sha256_update(&st, bytes(message.substr(cut2))) == NEURA_OK);
            Out out;
            CHECK(with_output<neura_out_bytes>(out, 32, [&](neura_out_bytes* o) {
                      return neura_k_digest_sha256_finish(&st, o);
                  }) == NEURA_OK);
            if (hex_of(out.storage) != whole) {
                CHECK(hex_of(out.storage) == whole);
                break;
            }
        }
    }
    // Equal messages give equal states, however they were split.
    std::vector<std::uint8_t> other(NEURA_SHA256_STATE_SIZE);
    neura_state st2{other.data(), other.size()};
    neura_k_digest_sha256_init(&st);
    neura_k_digest_sha256_update(&st, bytes(message.substr(0, 70)));
    neura_k_digest_sha256_init(&st2);
    neura_k_digest_sha256_update(&st2, bytes(message.substr(0, 3)));
    neura_k_digest_sha256_update(&st2, bytes(message.substr(3, 67)));
    CHECK(state == other);
    // A finished digest traps on update and finish; the state stays finished.
    neura_k_digest_sha256_init(&st);
    std::uint8_t digest[32];
    neura_out_bytes out32{digest, 0, 32};
    CHECK(neura_k_digest_sha256_finish(&st, &out32) == NEURA_OK);
    CHECK_EQ(hex_of(std::vector<std::uint8_t>(digest, digest + 32)),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(neura_k_digest_sha256_update(&st, bytes("x")) == NEURA_TRAP_DIGEST_FINISHED);
    CHECK(neura_k_digest_sha256_finish(&st, &out32) == NEURA_TRAP_DIGEST_FINISHED);
    // A small buffer asks for 32 bytes and leaves the state unfinished.
    neura_k_digest_sha256_init(&st);
    neura_out_bytes small{digest, 0, 31};
    CHECK(neura_k_digest_sha256_finish(&st, &small) == NEURA_NEED_CAPACITY);
    CHECK_EQ(small.len, 32u);
    CHECK(neura_k_digest_sha256_finish(&st, &out32) == NEURA_OK);
    // Invalid states.
    neura_state short_state{state.data(), 111};
    CHECK(neura_k_digest_sha256_init(&short_state) == NEURA_TRAP_INVALID_STATE);
    CHECK(neura_k_digest_sha256_update(&short_state, bytes("")) == NEURA_TRAP_INVALID_STATE);
    neura_k_digest_sha256_init(&st);
    state[104] = 64;
    CHECK(neura_k_digest_sha256_update(&st, bytes("")) == NEURA_TRAP_INVALID_STATE);
    neura_k_digest_sha256_init(&st);
    state[105] = 2;
    CHECK(neura_k_digest_sha256_update(&st, bytes("")) == NEURA_TRAP_INVALID_STATE);
    neura_k_digest_sha256_init(&st);
    state[111] = 1;
    CHECK(neura_k_digest_sha256_update(&st, bytes("")) == NEURA_TRAP_INVALID_STATE);
    neura_k_digest_sha256_init(&st);
    state[50] = 1;  // a pending byte past the count
    CHECK(neura_k_digest_sha256_update(&st, bytes("")) == NEURA_TRAP_INVALID_STATE);

    // CRC-32: the check value, and other published values.
    CHECK_EQ(neura_k_digest_crc32(bytes("123456789")), 0xCBF43926u);
    CHECK_EQ(neura_k_digest_crc32(bytes("")), 0u);
    CHECK_EQ(neura_k_digest_crc32(bytes("The quick brown fox jumps over the lazy dog")), 0x414FA339u);
    CHECK_EQ(neura_k_digest_crc32(bytes("a")), 0xE8B7BE43u);
    CHECK_EQ(neura_k_digest_crc32(bytes(std::string(32, '\0'))), 0x190A55ADu);
    CHECK_EQ(neura_k_digest_crc32(bytes(std::string(32, '\xff'))), 0xFF6CAB0Bu);
    // The running register: Crc32.update in pieces equals Crc32.of.
    for (std::size_t cut = 0; cut <= 9; ++cut) {
        const std::string s = "123456789";
        std::uint32_t reg = 0xFFFFFFFFu;
        reg = neura_k_digest_crc32_update(reg, bytes(s.substr(0, cut)));
        reg = neura_k_digest_crc32_update(reg, bytes(s.substr(cut)));
        CHECK_EQ(reg ^ 0xFFFFFFFFu, 0xCBF43926u);
    }
    return failures;
}

// ---------------------------------------------------------------- encoding

std::string b64(const std::string& in, bool url, bool padded) {
    Out out;
    neura_status status = with_output<neura_out_text>(out, 0, [&](neura_out_text* o) {
        return neura_k_encoding_base64_encode(bytes(in), url, padded, o);
    });
    return status == NEURA_OK ? out.str() : "status";
}

struct Decoded {
    neura_status status;
    std::string value;
    std::int64_t offset;
};

Decoded b64_decode(const std::string& in, bool url, bool padded) {
    Out out;
    std::int64_t offset = -7;
    neura_status status = with_output<neura_out_bytes>(out, 0, [&](neura_out_bytes* o) {
        return neura_k_encoding_base64_decode(text(in), url, padded, o, &offset);
    });
    return {status, out.str(), offset};
}

Decoded hex_decode(const std::string& in) {
    Out out;
    std::int64_t offset = -7;
    neura_status status = with_output<neura_out_bytes>(out, 0, [&](neura_out_bytes* o) {
        return neura_k_encoding_hex_decode(text(in), o, &offset);
    });
    return {status, out.str(), offset};
}

void expect_decoded(const Decoded& d, neura_status status, const std::string& value, std::int64_t offset, int line) {
    check(d.status == status, "status " + std::to_string(d.status) + " expected " + std::to_string(status), line);
    if (status == NEURA_OK) {
        check(d.value == value, "value " + show(d.value) + " expected " + show(value), line);
    } else {
        check(d.offset == offset, "offset " + std::to_string(d.offset) + " expected " + std::to_string(offset), line);
    }
}

#define EXPECT_DECODED(d, status, value, offset) expect_decoded((d), (status), (value), (offset), __LINE__)

Decoded utf8(const std::string& in) {
    std::int64_t offset = -7;
    neura_status status = neura_k_encoding_utf8_check(bytes(in), &offset);
    return {status, in, offset};
}

int encoding_suite() {
    // RFC 4648 section 10.
    const std::vector<std::pair<std::string, std::string>> rfc = {
        {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"}};
    for (const auto& [plain, encoded] : rfc) {
        CHECK_EQ(b64(plain, false, true), encoded);
        std::string unpadded = encoded.substr(0, encoded.find('='));
        CHECK_EQ(b64(plain, false, false), unpadded);
        EXPECT_DECODED(b64_decode(encoded, false, true), NEURA_OK, plain, 0);
        EXPECT_DECODED(b64_decode(unpadded, false, false), NEURA_OK, plain, 0);
        EXPECT_DECODED(b64_decode(encoded, true, true), NEURA_OK, plain, 0);
    }
    // The two alphabets differ only in 62 and 63.
    const std::string high("\xfb\xff\xbf", 3);
    CHECK_EQ(b64(high, false, true), std::string("+/+/"));
    CHECK_EQ(b64(high, true, true), std::string("-_-_"));
    CHECK_EQ(b64(std::string("\xfb\xff", 2), true, true), std::string("-_8="));
    CHECK_EQ(b64(std::string("\xfb\xff", 2), true, false), std::string("-_8"));
    EXPECT_DECODED(b64_decode("-_-_", true, true), NEURA_OK, high, 0);
    EXPECT_DECODED(b64_decode("+/+/", true, true), NEURA_ERR_INVALID_CHARACTER, "", 0);
    EXPECT_DECODED(b64_decode("-_-_", false, true), NEURA_ERR_INVALID_CHARACTER, "", 0);
    // Every byte round-trips.
    std::string all;
    for (int b = 0; b < 256; ++b) all += static_cast<char>(b);
    for (bool url : {false, true}) {
        for (bool padded : {false, true}) {
            for (std::size_t n = 0; n <= all.size(); n += 17) {
                const std::string part = all.substr(n % 7, n);
                EXPECT_DECODED(b64_decode(b64(part, url, padded), url, padded), NEURA_OK, part, 0);
            }
        }
    }
    // Errors, with the offsets of Base64.decode_with.
    EXPECT_DECODED(b64_decode("Zm9v!", false, true), NEURA_ERR_INVALID_CHARACTER, "", 4);
    EXPECT_DECODED(b64_decode("Zm 9v", false, true), NEURA_ERR_INVALID_CHARACTER, "", 2);
    EXPECT_DECODED(b64_decode("Zm9v\n", false, true), NEURA_ERR_INVALID_CHARACTER, "", 4);
    EXPECT_DECODED(b64_decode("Zg=", false, true), NEURA_ERR_UNEXPECTED_END_OF_DATA, "", 3);
    EXPECT_DECODED(b64_decode("Zg", false, true), NEURA_ERR_UNEXPECTED_END_OF_DATA, "", 2);
    EXPECT_DECODED(b64_decode("Zm9", false, true), NEURA_ERR_UNEXPECTED_END_OF_DATA, "", 3);
    EXPECT_DECODED(b64_decode("Z", false, false), NEURA_ERR_UNEXPECTED_END_OF_DATA, "", 1);
    EXPECT_DECODED(b64_decode("Zm9vZ", false, false), NEURA_ERR_UNEXPECTED_END_OF_DATA, "", 5);
    EXPECT_DECODED(b64_decode("Zg==", false, false), NEURA_ERR_UNEXPECTED_PADDING, "", 2);
    EXPECT_DECODED(b64_decode("Z===", false, true), NEURA_ERR_INVALID_PADDING, "", 1);
    EXPECT_DECODED(b64_decode("====", false, true), NEURA_ERR_INVALID_PADDING, "", 0);
    EXPECT_DECODED(b64_decode("Zm9v====", false, true), NEURA_ERR_INVALID_PADDING, "", 4);
    EXPECT_DECODED(b64_decode("Zg=A", false, true), NEURA_ERR_INVALID_PADDING, "", 3);
    EXPECT_DECODED(b64_decode("Zm8==", false, true), NEURA_ERR_INVALID_PADDING, "", 4);
    EXPECT_DECODED(b64_decode("Zg==Zg==", false, true), NEURA_ERR_INVALID_PADDING, "", 4);
    EXPECT_DECODED(b64_decode("Zh==", false, true), NEURA_ERR_NONZERO_TRAILING_BITS, "", 1);
    EXPECT_DECODED(b64_decode("Zm9=", false, true), NEURA_ERR_NONZERO_TRAILING_BITS, "", 2);
    EXPECT_DECODED(b64_decode("Zh", false, false), NEURA_ERR_NONZERO_TRAILING_BITS, "", 1);
    // A structural error is reported before trailing bits.
    EXPECT_DECODED(b64_decode("Zh=", false, true), NEURA_ERR_UNEXPECTED_END_OF_DATA, "", 3);

    // Hex.
    {
        Out out;
        CHECK(with_output<neura_out_text>(out, 0, [&](neura_out_text* o) {
                  return neura_k_encoding_hex_encode(bytes("foobar"), o);
              }) == NEURA_OK);
        CHECK_EQ(out.str(), std::string("666f6f626172"));
        CHECK(with_output<neura_out_text>(out, 0, [&](neura_out_text* o) {
                  return neura_k_encoding_hex_encode(bytes(all), o);
              }) == NEURA_OK);
        EXPECT_DECODED(hex_decode(out.str()), NEURA_OK, all, 0);
        CHECK_EQ(out.str().substr(0, 8), std::string("00010203"));
        CHECK_EQ(out.str().substr(out.str().size() - 4), std::string("feff"));
    }
    EXPECT_DECODED(hex_decode(""), NEURA_OK, "", 0);
    EXPECT_DECODED(hex_decode("DEADbeef"), NEURA_OK, std::string("\xde\xad\xbe\xef", 4), 0);
    EXPECT_DECODED(hex_decode("0g"), NEURA_ERR_INVALID_HEX_DIGIT, "", 1);
    EXPECT_DECODED(hex_decode("g0"), NEURA_ERR_INVALID_HEX_DIGIT, "", 0);
    EXPECT_DECODED(hex_decode("gg"), NEURA_ERR_INVALID_HEX_DIGIT, "", 0);
    EXPECT_DECODED(hex_decode("abc"), NEURA_ERR_ODD_HEX_DIGITS, "", 3);
    EXPECT_DECODED(hex_decode("abz"), NEURA_ERR_INVALID_HEX_DIGIT, "", 2);
    EXPECT_DECODED(hex_decode("ab z"), NEURA_ERR_INVALID_HEX_DIGIT, "", 2);

    // UTF-8: Unicode Table 3-7 boundaries and the three error kinds.
    EXPECT_DECODED(utf8(""), NEURA_OK, "", 0);
    EXPECT_DECODED(utf8("a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"), NEURA_OK, "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80", 0);
    EXPECT_DECODED(utf8("\xed\x9f\xbf\xee\x80\x80\xf4\x8f\xbf\xbf"), NEURA_OK, "\xed\x9f\xbf\xee\x80\x80\xf4\x8f\xbf\xbf", 0);
    EXPECT_DECODED(utf8("ab\x80"), NEURA_ERR_INVALID_START_BYTE, "", 2);
    EXPECT_DECODED(utf8("\xc0\xaf"), NEURA_ERR_INVALID_START_BYTE, "", 0);
    EXPECT_DECODED(utf8("\xc1\xbf"), NEURA_ERR_INVALID_START_BYTE, "", 0);
    EXPECT_DECODED(utf8("x\xf5\x80\x80\x80"), NEURA_ERR_INVALID_START_BYTE, "", 1);
    EXPECT_DECODED(utf8("\xff"), NEURA_ERR_INVALID_START_BYTE, "", 0);
    EXPECT_DECODED(utf8("\xe0\x80\x80"), NEURA_ERR_INVALID_CONTINUATION_BYTE, "", 0);  // overlong
    EXPECT_DECODED(utf8("\xed\xa0\x80"), NEURA_ERR_INVALID_CONTINUATION_BYTE, "", 0);  // surrogate
    EXPECT_DECODED(utf8("\xf0\x8f\xbf\xbf"), NEURA_ERR_INVALID_CONTINUATION_BYTE, "", 0);
    EXPECT_DECODED(utf8("\xf4\x90\x80\x80"), NEURA_ERR_INVALID_CONTINUATION_BYTE, "", 0);  // above U+10FFFF
    EXPECT_DECODED(utf8("ok\xe2\x82z"), NEURA_ERR_INVALID_CONTINUATION_BYTE, "", 2);
    EXPECT_DECODED(utf8("ok\xe2\x82"), NEURA_ERR_UNEXPECTED_END_OF_DATA, "", 2);
    EXPECT_DECODED(utf8("\xf0\x9f\x98"), NEURA_ERR_UNEXPECTED_END_OF_DATA, "", 0);
    EXPECT_DECODED(utf8("\xc3"), NEURA_ERR_UNEXPECTED_END_OF_DATA, "", 0);
    // An ill-formed byte at the very end after a valid prefix of the sequence.
    EXPECT_DECODED(utf8("\xe2\x28"), NEURA_ERR_INVALID_CONTINUATION_BYTE, "", 0);
    CHECK_EQ(neura_k_encoding_utf8_validate(bytes("abc")), -1);
    CHECK_EQ(neura_k_encoding_utf8_validate(bytes("a\xc3\xa9\xed\xa0\x80")), 3);
    CHECK_EQ(neura_k_encoding_utf8_validate(bytes("\xc3\xa9\xc3")), 2);
    // validate and check agree on every two-byte input and a sample of three-byte inputs.
    for (int a = 0; a < 256; ++a) {
        for (int b = 0; b < 256; ++b) {
            std::string s{static_cast<char>(a), static_cast<char>(b)};
            for (int c : {-1, 0x41, 0x80, 0xbf}) {
                std::string t = c < 0 ? s : s + static_cast<char>(c);
                std::int64_t offset = -1;
                neura_status status = neura_k_encoding_utf8_check(bytes(t), &offset);
                std::int64_t validate = neura_k_encoding_utf8_validate(bytes(t));
                if ((status == NEURA_OK) != (validate == -1) || (status != NEURA_OK && offset != validate)) {
                    CHECK(false);
                }
            }
        }
    }
    CHECK_EQ(neura_k_encoding_utf8_scalar_count(text("")), 0);
    CHECK_EQ(neura_k_encoding_utf8_scalar_count(text("a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80")), 4);
    return failures;
}

// ---------------------------------------------------------------- text

std::string replaced(const std::string& s, const std::string& target, const std::string& replacement,
                     std::int64_t limit = 2147483647) {
    Out out;
    neura_status status = with_output<neura_out_text>(out, 0, [&](neura_out_text* o) {
        return neura_k_text_replace(text(s), text(target), text(replacement), limit, o);
    });
    return status == NEURA_OK ? out.str() : "status " + std::to_string(status);
}

// One window of neura.text.index_of: the raw result, or -1000 - status.
std::int64_t window_of(const std::string& s, const std::string& needle, std::int64_t from, std::int64_t limit) {
    std::int64_t result = -99;
    neura_status status = neura_k_text_index_of(text(s), text(needle), from, limit, &result);
    return status == NEURA_OK ? result : -1000 - status;
}

// Strings.search: windows that start at `first` and double, as the wrapper
// calls them, or -1000 - status.
std::int64_t index_of(const std::string& s, const std::string& needle, std::int64_t from = 0,
                      std::int64_t first = 128) {
    std::int64_t limit = first;
    for (;;) {
        std::int64_t found = -99;
        neura_status status = neura_k_text_index_of(text(s), text(needle), from, limit, &found);
        if (status != NEURA_OK) return -1000 - status;
        if (found >= -1) return found;
        std::int64_t end = -2 - found;
        if (end <= from || end > static_cast<std::int64_t>(s.size())) return -5000;  // no progress
        from = end;
        limit *= 2;
    }
}

std::int64_t count_of(const std::string& s, const std::string& target) {
    std::int64_t result = -99;
    neura_status status = neura_k_text_count(text(s), text(target), &result);
    return status == NEURA_OK ? result : -1000 - status;
}

// Strings.separators: count with an empty list, then fill a list of that length.
std::vector<std::int64_t> separators(const std::string& s, const std::string& separator, std::int64_t limit,
                                     neura_status* status) {
    std::int64_t count = -99;
    *status = neura_k_text_split(text(s), text(separator), limit, neura_mut_i64{nullptr, 0}, &count);
    if (*status != NEURA_OK) return {};
    std::vector<std::int64_t> offsets(static_cast<std::size_t>(count), -7);
    std::int64_t again = -99;
    *status = neura_k_text_split(text(s), text(separator), count, neura_mut_i64{offsets.data(), offsets.size()},
                                 &again);
    if (again != count) *status = NEURA_NEED_CAPACITY;  // a mismatch the tests report
    return offsets;
}

// Strings.split_pieces with the split kernel.
std::vector<std::string> split(const std::string& s, const std::string& separator,
                               std::int64_t limit = 2147483647) {
    neura_status status = NEURA_OK;
    std::vector<std::int64_t> offsets = separators(s, separator, limit - 1, &status);
    if (status != NEURA_OK) return {"status " + std::to_string(status)};
    std::vector<std::string> pieces;
    std::int64_t start = 0;
    for (std::int64_t found : offsets) {
        pieces.push_back(s.substr(start, found - start));
        start = found + static_cast<std::int64_t>(separator.size());
    }
    pieces.push_back(s.substr(start));
    return pieces;
}

// Strings.split_pieces as the Neura code wrote it, on std::string::find.
std::vector<std::string> split_reference(const std::string& s, const std::string& separator,
                                         std::int64_t limit = 2147483647) {
    std::vector<std::string> pieces;
    std::size_t start = 0;
    for (std::int64_t remaining = limit - 1; remaining > 0; --remaining) {
        std::size_t found = s.find(separator, start);
        if (found == std::string::npos) break;
        pieces.push_back(s.substr(start, found - start));
        start = found + separator.size();
    }
    pieces.push_back(s.substr(start));
    return pieces;
}

std::string mapped(neura_status (*kernel)(neura_text, neura_out_text*), const std::string& s) {
    Out out;
    neura_status status = with_output<neura_out_text>(out, 0, [&](neura_out_text* o) { return kernel(text(s), o); });
    return status == NEURA_OK ? out.str() : "status";
}

std::string trim(const std::string& s) {
    std::int64_t start = neura_k_text_space_prefix_length(text(s));
    std::int64_t end = neura_k_text_space_suffix_start(text(s));
    start = std::min(start, end);
    return s.substr(start, end - start);
}

std::string blank_trim(const std::string& s) {
    std::int64_t start = neura_k_text_blank_prefix_length(text(s));
    std::int64_t end = std::max(neura_k_text_blank_suffix_start(text(s)), start);
    return s.substr(start, end - start);
}

int text_suite() {
    const std::string euro = "\xe2\x82\xac";  // U+20AC
    const std::string s = "a" + euro + "b" + euro + "ab";
    CHECK_EQ(index_of(s, "b"), 4);
    CHECK_EQ(index_of(s, euro), 1);
    CHECK_EQ(index_of(s, euro, 2 + 3), 5);  // from on a boundary
    CHECK_EQ(index_of(s, "ab"), 8);
    CHECK_EQ(index_of(s, ""), 0);
    CHECK_EQ(index_of(s, "", 4), 4);
    CHECK_EQ(index_of(s, "", 10), 10);
    CHECK_EQ(index_of(s, "zz"), -1);
    CHECK_EQ(index_of(s, s + "x"), -1);
    CHECK_EQ(index_of("", ""), 0);
    CHECK_EQ(index_of("", "a"), -1);
    CHECK_EQ(index_of(s, "a", 11), -1000 - NEURA_TRAP_OFFSET_OUT_OF_RANGE);
    CHECK_EQ(index_of(s, "a", -1), -1000 - NEURA_TRAP_OFFSET_OUT_OF_RANGE);
    CHECK_EQ(index_of(s, "a", 2), -1000 - NEURA_TRAP_NOT_CHARACTER_BOUNDARY);
    // A byte match that would start or end inside a character is no match.
    // Strings.find skips the boundary test for a one-byte needle; a valid one
    // is ASCII, which only matches on a boundary.
    CHECK_EQ(index_of(euro, "\x82"), 1);
    CHECK_EQ(index_of(euro + "x", "\x82x"), -1);
    CHECK_EQ(index_of(euro + "x", "\x82\xac"), -1);
    CHECK_EQ(neura_k_text_last_index_of(text(s), text("a")), 8);
    CHECK_EQ(neura_k_text_last_index_of(text(s), text(euro)), 5);
    CHECK_EQ(neura_k_text_last_index_of(text(s), text("")), 10);
    CHECK_EQ(neura_k_text_last_index_of(text(s), text("q")), -1);
    CHECK_EQ(neura_k_text_last_index_of(text("ab"), text("abc")), -1);
    CHECK_EQ(neura_k_text_last_index_of(text("aaa"), text("aa")), 1);
    CHECK_EQ(neura_k_text_last_index_of(text(euro), text("\xac")), -1);
    CHECK(neura_k_text_contains(text(s), text("b" + euro)) == 1);
    CHECK(neura_k_text_contains(text(s), text("ba")) == 0);
    CHECK(neura_k_text_contains(text(""), text("")) == 1);
    CHECK(neura_k_text_starts_with(text(s), text("a" + euro)) == 1);
    CHECK(neura_k_text_starts_with(text(s), text("")) == 1);
    CHECK(neura_k_text_starts_with(text("a"), text("ab")) == 0);
    CHECK(neura_k_text_starts_with(text(euro), text("\xe2\x82")) == 0);  // ends inside a character
    CHECK(neura_k_text_ends_with(text(s), text("ab")) == 1);
    CHECK(neura_k_text_ends_with(text(s), text("")) == 1);
    CHECK(neura_k_text_ends_with(text("b"), text("ab")) == 0);
    CHECK(neura_k_text_ends_with(text(euro), text("\x82\xac")) == 0);  // starts inside a character
    CHECK_EQ(neura_k_text_compare(text("abc"), text("abc")), 0);
    CHECK_EQ(neura_k_text_compare(text("abc"), text("abd")), -1);
    CHECK_EQ(neura_k_text_compare(text("abd"), text("abc")), 1);
    CHECK_EQ(neura_k_text_compare(text("ab"), text("abc")), -1);
    CHECK_EQ(neura_k_text_compare(text("abc"), text("ab")), 1);
    CHECK_EQ(neura_k_text_compare(text(""), text("")), 0);
    CHECK_EQ(neura_k_text_compare(text("\xc3\xa9"), text("z")), 1);  // bytes compare unsigned
    CHECK_EQ(count_of("aaaa", "aa"), 2);
    CHECK_EQ(count_of("aaaaa", "aa"), 2);
    CHECK_EQ(count_of(s, euro), 2);
    CHECK_EQ(count_of("", "a"), 0);
    CHECK_EQ(count_of("abc", ""), -1000 - NEURA_TRAP_EMPTY_TARGET);
    CHECK_EQ(mapped(neura_k_text_ascii_lower, "Hello, W\xc3\x89RLD @[`{"), std::string("hello, w\xc3\x89rld @[`{"));
    CHECK_EQ(mapped(neura_k_text_ascii_upper, "Hello, w\xc3\xa9rld @[`{"), std::string("HELLO, W\xc3\xa9RLD @[`{"));
    CHECK_EQ(mapped(neura_k_text_ascii_lower, ""), std::string(""));
    // Strings.trim and String.trim.
    CHECK_EQ(trim(" \t\n\v\f\r x y \r\n"), std::string("x y"));
    CHECK_EQ(trim(" \t "), std::string(""));
    CHECK_EQ(trim(""), std::string(""));
    CHECK_EQ(trim(std::string("\0x\0", 3)), std::string("\0x\0", 3));  // NUL is not space here
    CHECK_EQ(trim("\x1c" "a"), std::string("\x1c" "a"));
    CHECK_EQ(neura_k_text_space_prefix_length(text("   ")), 3);  // trim_start leaves ""
    CHECK_EQ(neura_k_text_space_suffix_start(text("   ")), 0);
    CHECK_EQ(blank_trim(std::string("\0\x1f x \x01", 7)), std::string("x"));
    CHECK_EQ(blank_trim(std::string("\0 \x7f", 3)), std::string("\x7f"));
    CHECK_EQ(blank_trim("  "), std::string(""));
    CHECK_EQ(blank_trim(""), std::string(""));
    // Strings.replace and replace_first.
    CHECK_EQ(replaced("a.b.c", ".", "--"), std::string("a--b--c"));
    CHECK_EQ(replaced("a.b.c", ".", "--", 1), std::string("a--b.c"));
    CHECK_EQ(replaced("a.b.c", ".", "--", 0), std::string("a.b.c"));
    CHECK_EQ(replaced("a.b.c", ".", "--", -5), std::string("a.b.c"));
    CHECK_EQ(replaced("aaaa", "aa", "b"), std::string("bb"));
    CHECK_EQ(replaced("aaa", "aa", "b"), std::string("ba"));
    CHECK_EQ(replaced("abc", "x", "y"), std::string("abc"));
    CHECK_EQ(replaced("", "x", "y"), std::string(""));
    CHECK_EQ(replaced("xx", "x", ""), std::string(""));
    CHECK_EQ(replaced(s, euro, "E"), std::string("aEbEab"));
    CHECK_EQ(replaced("abc", "", "y"), "status " + std::to_string(NEURA_TRAP_EMPTY_TARGET));
    // Strings.split.
    CHECK(split("a,b,,c", ",") == (std::vector<std::string>{"a", "b", "", "c"}));
    CHECK(split("", ",") == (std::vector<std::string>{""}));
    CHECK(split(",", ",") == (std::vector<std::string>{"", ""}));
    CHECK(split("a::b:::c", "::") == (std::vector<std::string>{"a", "b", ":c"}));
    CHECK(split("abc", "") == (std::vector<std::string>{"status " + std::to_string(NEURA_TRAP_EMPTY_SEPARATOR)}));
    CHECK(split("abc", "", 1) == (std::vector<std::string>{"status " + std::to_string(NEURA_TRAP_EMPTY_SEPARATOR)}));
    CHECK(split("a,b,c", ",", 1) == (std::vector<std::string>{"a,b,c"}));
    CHECK(split("a,b,c", ",", 2) == (std::vector<std::string>{"a", "b,c"}));
    CHECK(split("a,b,c", ",", 3) == (std::vector<std::string>{"a", "b", "c"}));
    CHECK(split("a,b,c", ",", 9) == (std::vector<std::string>{"a", "b", "c"}));
    CHECK(split("aaa", "aa") == (std::vector<std::string>{"", "a"}));
    CHECK(split(euro + "x" + euro, euro) == (std::vector<std::string>{"", "x", ""}));
    {
        // The count stops at the limit; a short list receives the first offsets only.
        std::int64_t count = -1;
        CHECK(neura_k_text_split(text("a,b,c,d"), text(","), 2, neura_mut_i64{nullptr, 0}, &count) == NEURA_OK);
        CHECK_EQ(count, 2);
        CHECK(neura_k_text_split(text("a,b,c,d"), text(","), 0, neura_mut_i64{nullptr, 0}, &count) == NEURA_OK);
        CHECK_EQ(count, 0);
        CHECK(neura_k_text_split(text("a,b,c,d"), text(","), -3, neura_mut_i64{nullptr, 0}, &count) == NEURA_OK);
        CHECK_EQ(count, 0);
        std::int64_t one[1] = {-7};
        CHECK(neura_k_text_split(text("a,b,c,d"), text(","), 9, neura_mut_i64{one, 1}, &count) == NEURA_OK);
        CHECK_EQ(count, 3);
        CHECK_EQ(one[0], 1);
        CHECK(neura_k_text_split(text(","), text(",,"), 9, neura_mut_i64{one, 1}, &count) == NEURA_OK);
        CHECK_EQ(count, 0);
    }

    // The bounded window of index_of. Candidates start in [from, from + limit), with the end moved past
    // continuation bytes; -2 - end means "none in the window, search on from end".
    const std::string padded = "ab" + euro + "cd" + euro + "ef";  // euro at 2..4 and 7..9
    CHECK_EQ(window_of(padded, "ef", 0, 3), -2 - 5);    // end 3 is inside the first euro: moved to 5
    CHECK_EQ(window_of(padded, "ef", 5, 3), -2 - 10);   // end 8 moved to 10
    CHECK_EQ(window_of(padded, "ef", 10, 1), 10);
    CHECK_EQ(window_of(padded, "zz", 0, 3), -2 - 5);
    CHECK_EQ(window_of(padded, "zz", 5, 100), -1);      // the window reaches the last candidate
    CHECK_EQ(window_of(padded, "zz", 10, 1), -1);       // the only candidate left was in the window
    CHECK_EQ(window_of(padded, "cd", 0, 6), 5);         // an occurrence starting at end - 1 is found
    CHECK_EQ(window_of(padded, "cd", 0, 4), -2 - 5);    // and one starting at the end is not
    CHECK_EQ(window_of(padded, "cd", 5, 0), 5);         // a limit below 1 counts as 1
    CHECK_EQ(window_of(padded, "d", 5, -9), -2 - 6);
    CHECK_EQ(window_of(padded, "", 8, 1), -1000 - NEURA_TRAP_NOT_CHARACTER_BOUNDARY);
    CHECK_EQ(window_of(padded, "", 5, 1), 5);
    CHECK_EQ(window_of(padded, padded, 0, 1), 0);
    CHECK_EQ(window_of(padded, padded + "x", 0, 1), -1);
    CHECK_EQ(window_of(padded, "a", 13, 1), -1000 - NEURA_TRAP_OFFSET_OUT_OF_RANGE);
    CHECK_EQ(window_of(padded, "a", 12, 9223372036854775807), -1);  // from + limit does not overflow
    for (std::int64_t first : {1, 2, 3, 128}) {
        CHECK_EQ(index_of(padded, "ef", 0, first), 10);
        CHECK_EQ(index_of(padded, euro + "e", 0, first), 7);
        CHECK_EQ(index_of(padded, "q", 0, first), -1);
    }

    // Against a plain byte search on random ASCII text, where the boundary rule never applies. Two- and
    // three-letter alphabets and needles up to 12 bytes reach both the periodic and the non-periodic paths of
    // the two-way scan; the first window of 1 to 4 bytes makes the search cross many window ends.
    std::mt19937_64 random(20261111);
    for (int round = 0; round < 6000; ++round) {
        std::string hay, needle;
        const char* letters = round % 3 == 0 ? "abc" : "ab";
        std::size_t alphabet = round % 3 == 0 ? 3 : 2;
        std::size_t n = random() % 60, m = random() % (round % 2 == 0 ? 4 : 12) + 1;
        for (std::size_t i = 0; i < n; ++i) hay += letters[random() % alphabet];
        for (std::size_t i = 0; i < m; ++i) needle += letters[random() % alphabet];
        if (round % 5 == 0 && n > m) needle = hay.substr(random() % (n - m), m);  // one that occurs
        std::size_t from = random() % (n + 1);
        std::size_t expected = hay.find(needle, from);
        CHECK_EQ(index_of(hay, needle, static_cast<std::int64_t>(from), static_cast<std::int64_t>(random() % 4 + 1)),
                 expected == std::string::npos ? -1 : static_cast<std::int64_t>(expected));
        std::int64_t limit = static_cast<std::int64_t>(random() % 6) + 1;
        CHECK(split(hay, needle, limit) == split_reference(hay, needle, limit));
        CHECK(split(hay, needle) == split_reference(hay, needle));
        std::size_t last = hay.rfind(needle);
        CHECK_EQ(neura_k_text_last_index_of(text(hay), text(needle)),
                 last == std::string::npos ? -1 : static_cast<std::int64_t>(last));
        std::int64_t occurrences = 0;
        std::string rebuilt;
        std::size_t at = 0;
        for (std::size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + m)) {
            ++occurrences;
            rebuilt += hay.substr(at, p - at) + "<>";
            at = p + m;
        }
        rebuilt += hay.substr(at);
        CHECK_EQ(count_of(hay, needle), occurrences);
        CHECK_EQ(replaced(hay, needle, "<>"), rebuilt);
        CHECK_EQ(neura_k_text_compare(text(hay), text(needle)), hay < needle ? -1 : (hay == needle ? 0 : 1));
        if (failures > 20) break;
    }

    // The inputs on which a naive search takes n * m steps: a run of one byte searched for a run of it with
    // one other byte at an end. The two-way scan reads each byte a bounded number of times, so these take
    // milliseconds; a quadratic search would take seconds and the test's timeout would catch it.
    const std::string run(1u << 20, 'a');
    const std::string tail_needle = std::string(1u << 14, 'a') + "b";
    const std::string head_needle = "b" + std::string(1u << 14, 'a');
    CHECK_EQ(index_of(run, tail_needle), -1);
    CHECK_EQ(index_of(run + "b", tail_needle), static_cast<std::int64_t>(run.size() + 1 - tail_needle.size()));
    CHECK_EQ(neura_k_text_last_index_of(text(run), text(head_needle)), -1);
    CHECK_EQ(neura_k_text_last_index_of(text("b" + run), text(head_needle)), 0);
    CHECK(neura_k_text_contains(text(run), text(tail_needle)) == 0);
    CHECK_EQ(count_of(run, tail_needle), 0);
    CHECK_EQ(count_of(run, std::string(1u << 14, 'a')), static_cast<std::int64_t>(run.size() >> 14));
    CHECK_EQ(replaced(run, tail_needle, "x"), run);
    CHECK_EQ(split(run, tail_needle).size(), 1u);
    CHECK_EQ(split(run, "a").size(), run.size() + 1);
    return failures;
}

// ---------------------------------------------------------------- integer

std::string format_i64(std::int64_t v, std::int64_t radix) {
    Out out;
    neura_status status = with_output<neura_out_text>(out, 0, [&](neura_out_text* o) {
        return neura_k_integer_format_i64(v, radix, o);
    });
    return status == NEURA_OK ? out.str() : "status " + std::to_string(status);
}

std::string format_u64(std::uint64_t v, std::int64_t radix) {
    Out out;
    neura_status status = with_output<neura_out_text>(out, 0, [&](neura_out_text* o) {
        return neura_k_integer_format_u64(v, radix, o);
    });
    return status == NEURA_OK ? out.str() : "status " + std::to_string(status);
}

struct Parsed {
    neura_status status;
    std::int64_t value;
    std::uint64_t unsigned_value;
    std::int64_t offset;
};

Parsed parse_i64(const std::string& s, std::int64_t radix = 10) {
    Parsed p{NEURA_OK, -99, 0, -99};
    p.status = neura_k_integer_parse_i64(text(s), radix, &p.value, &p.offset);
    return p;
}

Parsed parse_u64(const std::string& s, std::int64_t radix = 10) {
    Parsed p{NEURA_OK, -99, 0, -99};
    p.status = neura_k_integer_parse_u64(text(s), radix, &p.unsigned_value, &p.offset);
    return p;
}

void expect_parse_error(const Parsed& p, neura_status status, std::int64_t offset, int line) {
    check(p.status == status && p.offset == offset,
          "status " + std::to_string(p.status) + " offset " + std::to_string(p.offset) + ", expected " +
              std::to_string(status) + " at " + std::to_string(offset),
          line);
}

#define EXPECT_PARSE_ERROR(p, status, offset) expect_parse_error((p), (status), (offset), __LINE__)

int integer_suite() {
    const std::int64_t min = std::numeric_limits<std::int64_t>::min();
    const std::int64_t max = std::numeric_limits<std::int64_t>::max();
    const std::uint64_t umax = std::numeric_limits<std::uint64_t>::max();
    CHECK_EQ(format_i64(0, 10), std::string("0"));
    CHECK_EQ(format_i64(-1, 10), std::string("-1"));
    CHECK_EQ(format_i64(min, 10), std::string("-9223372036854775808"));
    CHECK_EQ(format_i64(max, 10), std::string("9223372036854775807"));
    CHECK_EQ(format_i64(min, 2), "-1" + std::string(63, '0'));
    CHECK_EQ(format_i64(max, 16), std::string("7fffffffffffffff"));
    CHECK_EQ(format_i64(min, 16), std::string("-8000000000000000"));
    CHECK_EQ(format_i64(min, 36), std::string("-1y2p0ij32e8e8"));
    CHECK_EQ(format_i64(-255, 16), std::string("-ff"));
    CHECK_EQ(format_i64(35, 36), std::string("z"));
    CHECK_EQ(format_i64(5, 1), "status " + std::to_string(NEURA_TRAP_RADIX_OUT_OF_RANGE));
    CHECK_EQ(format_i64(5, 37), "status " + std::to_string(NEURA_TRAP_RADIX_OUT_OF_RANGE));
    CHECK_EQ(format_u64(0, 2), std::string("0"));
    CHECK_EQ(format_u64(umax, 10), std::string("18446744073709551615"));
    CHECK_EQ(format_u64(umax, 2), std::string(64, '1'));
    CHECK_EQ(format_u64(umax, 36), std::string("3w5e11264sgsf"));
    CHECK_EQ(format_u64(1, 0), "status " + std::to_string(NEURA_TRAP_RADIX_OUT_OF_RANGE));
    // A fixed buffer of the declared capacity is always enough.
    {
        std::uint8_t buffer[65];
        neura_out_text out{buffer, 0, 65};
        CHECK(neura_k_integer_format_i64(min, 2, &out) == NEURA_OK);
        CHECK_EQ(out.len, 65u);
        neura_out_text tight{buffer, 0, 64};
        CHECK(neura_k_integer_format_i64(min, 2, &tight) == NEURA_NEED_CAPACITY);
        CHECK_EQ(tight.len, 65u);
    }

    Parsed p = parse_i64("-9223372036854775808");
    CHECK(p.status == NEURA_OK && p.value == min);
    p = parse_i64("+9223372036854775807");
    CHECK(p.status == NEURA_OK && p.value == max);
    p = parse_i64("-0");
    CHECK(p.status == NEURA_OK && p.value == 0);
    p = parse_i64("007");
    CHECK(p.status == NEURA_OK && p.value == 7);
    p = parse_i64("-zZ", 36);
    CHECK(p.status == NEURA_OK && p.value == -(35 * 36 + 35));
    p = parse_i64("-1" + std::string(63, '0'), 2);
    CHECK(p.status == NEURA_OK && p.value == min);
    EXPECT_PARSE_ERROR(parse_i64("9223372036854775808"), NEURA_ERR_PARSE_OUT_OF_RANGE, 0);
    EXPECT_PARSE_ERROR(parse_i64("-9223372036854775809"), NEURA_ERR_PARSE_OUT_OF_RANGE, 0);
    EXPECT_PARSE_ERROR(parse_i64("1" + std::string(63, '0'), 2), NEURA_ERR_PARSE_OUT_OF_RANGE, 0);
    EXPECT_PARSE_ERROR(parse_i64("99999999999999999999999"), NEURA_ERR_PARSE_OUT_OF_RANGE, 0);
    EXPECT_PARSE_ERROR(parse_i64(""), NEURA_ERR_PARSE_EMPTY, 0);
    EXPECT_PARSE_ERROR(parse_i64("-"), NEURA_ERR_PARSE_EXPECTED_DIGIT, 1);
    EXPECT_PARSE_ERROR(parse_i64("+"), NEURA_ERR_PARSE_EXPECTED_DIGIT, 1);
    EXPECT_PARSE_ERROR(parse_i64("12a"), NEURA_ERR_PARSE_INVALID_CHARACTER, 2);
    EXPECT_PARSE_ERROR(parse_i64(" 1"), NEURA_ERR_PARSE_INVALID_CHARACTER, 0);
    EXPECT_PARSE_ERROR(parse_i64("1 "), NEURA_ERR_PARSE_INVALID_CHARACTER, 1);
    EXPECT_PARSE_ERROR(parse_i64("--1"), NEURA_ERR_PARSE_INVALID_CHARACTER, 1);
    EXPECT_PARSE_ERROR(parse_i64("0x10", 16), NEURA_ERR_PARSE_INVALID_CHARACTER, 1);
    EXPECT_PARSE_ERROR(parse_i64("102", 2), NEURA_ERR_PARSE_INVALID_CHARACTER, 2);
    // The grammar error comes before overflow.
    EXPECT_PARSE_ERROR(parse_i64("99999999999999999999x"), NEURA_ERR_PARSE_INVALID_CHARACTER, 20);
    CHECK(parse_i64("1", 1).status == NEURA_TRAP_RADIX_OUT_OF_RANGE);
    CHECK(parse_i64("", 37).status == NEURA_TRAP_RADIX_OUT_OF_RANGE);

    p = parse_u64("18446744073709551615");
    CHECK(p.status == NEURA_OK && p.unsigned_value == umax);
    p = parse_u64("+0");
    CHECK(p.status == NEURA_OK && p.unsigned_value == 0);
    p = parse_u64("-0");
    CHECK(p.status == NEURA_OK && p.unsigned_value == 0);
    p = parse_u64("-000");
    CHECK(p.status == NEURA_OK && p.unsigned_value == 0);
    p = parse_u64("FFFFFFFFFFFFFFFF", 16);
    CHECK(p.status == NEURA_OK && p.unsigned_value == umax);
    EXPECT_PARSE_ERROR(parse_u64("-1"), NEURA_ERR_PARSE_OUT_OF_RANGE, 0);
    EXPECT_PARSE_ERROR(parse_u64("18446744073709551616"), NEURA_ERR_PARSE_OUT_OF_RANGE, 0);
    EXPECT_PARSE_ERROR(parse_u64("10000000000000000", 16), NEURA_ERR_PARSE_OUT_OF_RANGE, 0);
    EXPECT_PARSE_ERROR(parse_u64(""), NEURA_ERR_PARSE_EMPTY, 0);
    EXPECT_PARSE_ERROR(parse_u64("-"), NEURA_ERR_PARSE_EXPECTED_DIGIT, 1);
    EXPECT_PARSE_ERROR(parse_u64("1_000"), NEURA_ERR_PARSE_INVALID_CHARACTER, 1);
    CHECK(parse_u64("1", 37).status == NEURA_TRAP_RADIX_OUT_OF_RANGE);

    // Round trips in every radix, including the boundary values.
    std::mt19937_64 random(7);
    for (std::int64_t radix = 2; radix <= 36; ++radix) {
        std::vector<std::int64_t> values = {0, 1, -1, min, max, min + 1, max - 1};
        for (int i = 0; i < 40; ++i) values.push_back(static_cast<std::int64_t>(random()) >> (random() % 64));
        for (std::int64_t v : values) {
            Parsed back = parse_i64(format_i64(v, radix), radix);
            CHECK(back.status == NEURA_OK && back.value == v);
            std::uint64_t u = static_cast<std::uint64_t>(v);
            Parsed uback = parse_u64(format_u64(u, radix), radix);
            CHECK(uback.status == NEURA_OK && uback.unsigned_value == u);
        }
        if (failures > 20) break;
    }
    // Decimal text equals the C library's.
    for (int i = 0; i < 1000; ++i) {
        std::int64_t v = static_cast<std::int64_t>(random());
        CHECK_EQ(format_i64(v, 10), std::to_string(v));
    }
    return failures;
}

// ---------------------------------------------------------------- hash

int hash_suite() {
    // Values observed from `neura run` with the Neura implementations.
    CHECK_EQ(neura_k_hash_string(text("abc")), 96354u);
    CHECK_EQ(neura_k_hash_string(text("")), 0u);
    CHECK_EQ(neura_k_hash_i64(-2), 1u);
    CHECK_EQ(neura_k_hash_f64(from_bits(0x7FF8000000000000ull)), 2146959360u);
    CHECK_EQ(neura_k_hash_f64(from_bits(0xFFF8000000000000ull)), 2146959360u);  // -NaN
    CHECK_EQ(neura_k_hash_f64(from_bits(0x7FF0000000000001ull)), 2146959360u);  // payload NaN
    CHECK_EQ(neura_k_hash_f64(-0.0), 2147483648u);
    CHECK_EQ(neura_k_hash_f64(0.0), 0u);
    CHECK_EQ(neura_k_hash_f64(1.5), 1073217536u);
    const std::int64_t list[] = {1, 2, 3};
    CHECK_EQ(neura_k_hash_list_i64(neura_view_i64{list, 3}), 30817u);
    CHECK_EQ(neura_k_hash_list_i64(neura_view_i64{list, 0}), 1u);
    const std::uint8_t small[] = {1, 2, 3};
    CHECK_EQ(neura_k_hash_list_u8(neura_view_u8{small, 3}), 30817u);
    const std::uint64_t ulist[] = {1, 2, 3};
    CHECK_EQ(neura_k_hash_list_u64(neura_view_u64{ulist, 3}), 30817u);
    const double flist[] = {1.5, -0.0};
    CHECK_EQ(neura_k_hash_list_f64(neura_view_f64{flist, 2}), (31u + 1073217536u) * 31u + 2147483648u);
    // String hash: 31 * h + byte, wrapping, bytes unsigned.
    std::uint32_t h = 0;
    std::string long_text;
    for (int i = 0; i < 1000; ++i) {
        long_text += static_cast<char>(i * 37);
        h = h * 31u + static_cast<std::uint8_t>(long_text.back());
    }
    CHECK_EQ(neura_k_hash_string(text(long_text)), h);
    CHECK_EQ(neura_k_hash_combine(0xFFFFFFFFu, 0xFFFFFFFFu), static_cast<std::uint32_t>(0xFFFFFFFFull * 32));
    CHECK_EQ(neura_k_hash_combine(17u, 5u), 532u);
    CHECK_EQ(neura_k_hash_spread(0x12345678u), 0x12345678u ^ 0x1234u);
    CHECK_EQ(neura_k_hash_u64(0x0123456789ABCDEFull), 0x89ABCDEFu ^ 0x01234567u);
    CHECK_EQ(neura_k_hash_i64(std::numeric_limits<std::int64_t>::min()), 0x80000000u);
    CHECK_EQ(neura_k_hash_u64(5), 5u);
    return failures;
}

// ---------------------------------------------------------------- sort

int total_compare(double a, double b) {
    bool an = a != a, bn = b != b;
    if (an || bn) return an && bn ? 0 : (an ? 1 : -1);
    if (a < b) return -1;
    if (a > b) return 1;
    bool as = std::signbit(a), bs = std::signbit(b);
    return as == bs ? 0 : (as ? -1 : 1);
}

int sort_suite() {
    std::mt19937_64 random(99);
    const std::vector<std::size_t> sizes = {0, 1, 2, 15, 16, 17, 31, 32, 33, 47, 64, 100, 257, 1000, 4097};
    for (std::size_t n : sizes) {
        for (int round = 0; round < 3; ++round) {
            std::vector<std::int64_t> items(n), scratch(n);
            std::vector<std::uint64_t> uitems(n), uscratch(n);
            for (std::size_t i = 0; i < n; ++i) {
                std::uint64_t r = random();
                items[i] = round == 0 ? static_cast<std::int64_t>(r % 7) - 3 : static_cast<std::int64_t>(r);
                uitems[i] = round == 2 ? n - i : r;
            }
            std::vector<std::int64_t> expected = items;
            std::stable_sort(expected.begin(), expected.end());
            CHECK(neura_k_sort_i64(neura_mut_i64{items.data(), n}, neura_mut_i64{scratch.data(), n}) == NEURA_OK);
            CHECK(items == expected);
            std::vector<std::uint64_t> uexpected = uitems;
            std::stable_sort(uexpected.begin(), uexpected.end());
            CHECK(neura_k_sort_u64(neura_mut_u64{uitems.data(), n}, neura_mut_u64{uscratch.data(), n}) == NEURA_OK);
            CHECK(uitems == uexpected);
        }
    }
    // float64: total order, NaNs last in their original order with their payloads, -0.0 before 0.0.
    const std::vector<std::uint64_t> specials = {
        0x7FF8000000000000ull, 0xFFF8000000000000ull, 0x7FF0000000000001ull, 0xFFF0000000000000ull,
        0x7FF0000000000000ull, 0x8000000000000000ull, 0x0000000000000000ull, 0x0000000000000001ull,
        0x8000000000000001ull, 0x7FEFFFFFFFFFFFFFull, 0x3FF0000000000000ull, 0xBFF0000000000000ull};
    for (std::size_t n : sizes) {
        std::vector<double> items(n), scratch(n + 5);
        for (std::size_t i = 0; i < n; ++i) {
            std::uint64_t r = random();
            if (r % 3 == 0) {
                items[i] = from_bits(specials[r % specials.size()]);
                if ((r >> 20) % 5 == 0) items[i] = from_bits(0x7FF8000000000000ull | (r >> 30));  // NaN payloads
            } else {
                items[i] = static_cast<double>(static_cast<std::int64_t>(r % 21) - 10) / 4.0;
            }
        }
        std::vector<double> expected = items;
        std::stable_sort(expected.begin(), expected.end(),
                         [](double a, double b) { return total_compare(a, b) < 0; });
        CHECK(neura_k_sort_f64(neura_mut_f64{items.data(), n}, neura_mut_f64{scratch.data(), n + 5}) == NEURA_OK);
        bool same = true;
        for (std::size_t i = 0; i < n; ++i) same = same && bits_of(items[i]) == bits_of(expected[i]);
        CHECK(same);
    }
    // A short scratch buffer leaves the items unchanged.
    std::vector<std::int64_t> items = {3, 2, 1}, scratch(2);
    CHECK(neura_k_sort_i64(neura_mut_i64{items.data(), 3}, neura_mut_i64{scratch.data(), 2}) ==
          NEURA_TRAP_SCRATCH_TOO_SMALL);
    CHECK((items == std::vector<std::int64_t>{3, 2, 1}));
    return failures;
}

// ---------------------------------------------------------------- math

// Reads `export let <name>_cases` lists of math-vectors.nra.
std::map<std::string, std::vector<std::uint64_t>> read_math_vectors(const std::string& source_dir) {
    std::ifstream file(source_dir + "/tests/stdlib/math/support/math-vectors.nra");
    std::map<std::string, std::vector<std::uint64_t>> cases;
    std::string line, current;
    while (std::getline(file, line)) {
        auto at = line.find("export let ");
        auto end = line.find("_cases = func");
        if (at != std::string::npos && end != std::string::npos) {
            current = line.substr(at + 11, end - at - 11);
            continue;
        }
        if (current.empty()) continue;
        if (line.find("];") != std::string::npos) {
            current.clear();
            continue;
        }
        // Only the rows of numbers; skip `let cases: uint64[list] = [`.
        if (line.find_first_not_of("0123456789, ") != std::string::npos) continue;
        std::string digits;
        for (char c : line + ",") {
            if (c >= '0' && c <= '9') {
                digits += c;
            } else if (!digits.empty()) {
                cases[current].push_back(std::stoull(digits));
                digits.clear();
            }
        }
    }
    return cases;
}

int math_suite(const std::string& source_dir) {
    using Unary = neura_f64 (*)(neura_f64);
    using Binary = neura_f64 (*)(neura_f64, neura_f64);
    const std::map<std::string, Unary> unary = {
        {"sqrt", neura_k_math_sqrt}, {"cbrt", neura_k_math_cbrt}, {"exp", neura_k_math_exp},
        {"expm1", neura_k_math_expm1}, {"log", neura_k_math_log}, {"log1p", neura_k_math_log1p},
        {"log10", neura_k_math_log10}, {"log2", neura_k_math_log2}, {"sin", neura_k_math_sin},
        {"cos", neura_k_math_cos}, {"tan", neura_k_math_tan}, {"asin", neura_k_math_asin},
        {"acos", neura_k_math_acos}, {"atan", neura_k_math_atan}, {"sinh", neura_k_math_sinh},
        {"cosh", neura_k_math_cosh}, {"tanh", neura_k_math_tanh}, {"to_radians", neura_k_math_to_radians},
        {"to_degrees", neura_k_math_to_degrees}};
    const std::map<std::string, Binary> binary = {
        {"pow", neura_k_math_pow}, {"atan2", neura_k_math_atan2}, {"hypot", neura_k_math_hypot}};
    auto cases = read_math_vectors(source_dir);
    CHECK_EQ(cases.size(), unary.size() + binary.size());
    std::size_t checked = 0;
    for (const auto& [name, values] : cases) {
        int reported = 0;
        bool is_unary = unary.count(name) != 0;
        CHECK(is_unary || binary.count(name) != 0);
        std::size_t width = is_unary ? 3 : 4;
        CHECK(!values.empty() && values.size() % width == 0);
        for (std::size_t i = 0; i + width <= values.size(); i += width) {
            std::uint64_t got = is_unary ? bits_of(unary.at(name)(from_bits(values[i])))
                                         : bits_of(binary.at(name)(from_bits(values[i]), from_bits(values[i + 1])));
            std::uint64_t expected = values[i + width - 2];
            ++checked;
            if (got != expected && reported++ < 5) {
                ++failures;
                std::cout << "FAIL " << name << " of bits " << values[i] << (is_unary ? "" : ", ")
                          << (is_unary ? "" : std::to_string(values[i + 1])) << ": expected " << expected << ", got "
                          << got << '\n';
            }
        }
    }
    std::cout << checked << " math vectors checked\n";
    CHECK(checked > 8000);
    // Canonical NaN and signed zeros directly.
    const std::uint64_t nan = 0x7FF8000000000000ull;
    CHECK_EQ(bits_of(neura_k_math_sqrt(-1.0)), nan);
    CHECK_EQ(bits_of(neura_k_math_sqrt(from_bits(0xFFF8000000000123ull))), nan);
    CHECK_EQ(bits_of(neura_k_math_sqrt(-0.0)), 0x8000000000000000ull);
    CHECK_EQ(bits_of(neura_k_math_log(-1.0)), nan);
    CHECK_EQ(bits_of(neura_k_math_sin(from_bits(0x7FF0000000000000ull))), nan);
    CHECK_EQ(bits_of(neura_k_math_to_radians(from_bits(0xFFF8000000000001ull))), nan);
    CHECK_EQ(bits_of(neura_k_math_sin(-0.0)), 0x8000000000000000ull);
    CHECK_EQ(bits_of(neura_k_math_atan2(-0.0, -0.0)), bits_of(-3.141592653589793));
    CHECK_EQ(bits_of(neura_k_math_pow(from_bits(nan), 0.0)), bits_of(1.0));
    CHECK_EQ(bits_of(neura_k_math_log2(1024.0)), bits_of(10.0));
    return failures;
}

// ---------------------------------------------------------------- table

int table_suite() {
    using namespace neura::kernels;
    CHECK_EQ(kKernelCount, sizeof(kKernels) / sizeof(kKernels[0]));
    CHECK(kKernelCount >= 60);
    std::set<std::string> names;
    for (const KernelInfo& k : kKernels) {
        CHECK(names.insert(k.name).second);
        CHECK(std::string(k.name).rfind("neura.", 0) == 0);
        CHECK(k.version >= 1);
        CHECK(k.thunk != nullptr);
        CHECK(k.cost.step_count > 0);
        std::vector<std::uint64_t> args(k.param_count, 1000);
        CHECK(evaluate(k.cost, args.data()) >= 1);  // every kernel charges at least one step
        for (std::size_t i = 0; i < k.param_count; ++i) {
            if (k.params[i] == Kind::OutText || k.params[i] == Kind::OutBytes || k.params[i] == Kind::State) {
                bool sized = false;
                for (std::size_t b = 0; b < k.buffer_count; ++b) sized = sized || k.buffers[b].param == i;
                CHECK(sized);
            }
        }
    }
    // Formula evaluation: saturation, x / 0, log2.
    const Step saturate[] = {{Op::Const, UINT64_MAX}, {Op::Const, 2}, {Op::Mul, 0}, {Op::Const, 1}, {Op::Add, 0}};
    CHECK_EQ(evaluate(Formula{"", saturate, 5}, nullptr), UINT64_MAX);
    const Step divide[] = {{Op::Const, 7}, {Op::Const, 0}, {Op::Div, 0}, {Op::Const, 3}, {Op::Const, 5},
                           {Op::Sub, 0}, {Op::Add, 0}};
    CHECK_EQ(evaluate(Formula{"", divide, 7}, nullptr), 0u);
    const Step logs[] = {{Op::Arg, 0}, {Op::Log2, 0}, {Op::Arg, 1}, {Op::Log2, 0}, {Op::Max, 0}};
    const std::uint64_t log_args[] = {0, 1025};
    CHECK_EQ(evaluate(Formula{"", logs, 5}, log_args), 10u);

    auto find = [&](const std::string& name) -> const KernelInfo* {
        for (const KernelInfo& k : kKernels) {
            if (name == k.name) return &k;
        }
        return nullptr;
    };
    // The sort charge: 1 + n * (1 + log2(n)) / 8.
    const KernelInfo* sort = find("neura.sort.f64");
    CHECK(sort != nullptr && sort->param_count == 2 && sort->params[0] == Kind::MutF64);
    if (sort) {
        const std::uint64_t n[] = {1024, 1024};
        CHECK_EQ(evaluate(sort->cost, n), 1u + 1024u * 11u / 8u);
    }
    // Charges against work: every charge is at least 1 + touched / 64 steps for the elements the call can
    // touch (no unbounded work for a few steps), and at most 1 + touched / 8 (no charge for work the call
    // cannot do). Arguments are in parameter order: a length for a view or output, a value for a scalar.
    struct Charge {
        const char* name;
        std::vector<std::uint64_t> args;
        std::uint64_t touched;
    };
    const std::uint64_t mega = 1u << 20;
    const std::vector<Charge> charges = {
        {"neura.text.index_of", {mega, 4, 0, 128, 0}, 132},           // the first window
        {"neura.text.index_of", {mega, 4, mega - 1000, 128, 0}, 132},  // a window far into the text
        {"neura.text.index_of", {mega, 4, mega - 100, 128, 0}, 100},   // the rest is shorter than the window
        {"neura.text.index_of", {mega, 4, 0, mega, 0}, mega},
        {"neura.text.index_of", {mega, mega / 2, 0, 128, 0}, mega / 2 + 128},  // a long needle is read once
        {"neura.text.last_index_of", {mega, 4}, mega},
        {"neura.text.contains", {mega, mega / 2}, mega},
        {"neura.text.count", {mega, 1, 0}, mega},
        {"neura.text.split", {mega, 1, 2147483646, 0, 0}, mega},
        {"neura.text.split", {mega, 1, 2147483646, mega, 0}, 2 * mega},  // the offsets are copied too
        {"neura.text.replace", {mega, 1, 1, 2147483647, 0}, 2 * mega},
        // replace_first of a one-byte target with a long replacement copies the text and one replacement.
        {"neura.text.replace", {mega, 1, 4096, 1, 0}, mega + 4096},
        {"neura.text.replace", {mega, 1, 4096, 0, 0}, mega},
        {"neura.text.starts_with", {mega, 64}, 64},
        {"neura.text.compare", {mega, 64}, 64},
        {"neura.text.ascii_lower", {mega, 0}, 2 * mega},
        {"neura.text.space_prefix_length", {mega}, mega},
        {"neura.hash.string", {mega}, mega},
        {"neura.encoding.utf8_validate", {mega}, mega},
        {"neura.encoding.base64_decode", {mega, 0, 0, 0, 0}, mega},
        {"neura.integer.parse_i64", {64, 10, 0, 0}, 64},
        {"neura.digest.sha256_update", {112, mega}, mega},
    };
    for (const Charge& c : charges) {
        const KernelInfo* k = find(c.name);
        CHECK(k != nullptr);
        if (k == nullptr) continue;
        CHECK_EQ(c.args.size(), k->param_count);
        if (c.args.size() != k->param_count) continue;
        std::uint64_t charge = evaluate(k->cost, c.args.data());
        if (charge < 1 + c.touched / 64 || charge > 1 + c.touched / 8) {
            check(false, std::string(c.name) + " charges " + std::to_string(charge) + " steps for " +
                             std::to_string(c.touched) + " elements", __LINE__);
        }
    }
    // A sort is charged by its comparisons and moves, about n * log2(n).
    if (sort) {
        for (std::uint64_t n : {std::uint64_t{1}, std::uint64_t{16}, std::uint64_t{1000}, mega}) {
            const std::uint64_t items[] = {n, n};
            std::uint64_t work = n * (1 + static_cast<std::uint64_t>(std::log2(static_cast<double>(n))));
            std::uint64_t charge = evaluate(sort->cost, items);
            CHECK(charge >= 1 + work / 64 && charge <= 1 + work / 4);
        }
    }
    // The replace capacity is bounded by the limit too: replace_first never asks for more than one copy.
    const KernelInfo* replace_kernel = find("neura.text.replace");
    if (replace_kernel) {
        const std::uint64_t first[] = {mega, 1, 4096, 1, 0};
        CHECK_EQ(evaluate(replace_kernel->buffers[0].size, first), mega + 4096);
    }

    // Calls through the thunks.
    const KernelInfo* sha = find("neura.digest.sha256");
    CHECK(sha != nullptr && sha->result == Kind::Status && sha->params[0] == Kind::Bytes);
    if (sha) {
        const std::string abc = "abc";
        neura_bytes input = bytes(abc);
        std::uint8_t digest[32];
        neura_out_bytes out{digest, 0, 32};
        neura_out_bytes* out_pointer = &out;
        void* args[] = {&input, &out_pointer};
        neura_status status = NEURA_NEED_CAPACITY;
        sha->thunk(args, &status);
        CHECK(status == NEURA_OK);
        CHECK_EQ(hex_of(std::vector<std::uint8_t>(digest, digest + 32)),
                 std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        const std::uint64_t lengths[] = {640, 32};
        CHECK_EQ(evaluate(sha->cost, lengths), 11u);
    }
    const KernelInfo* pow = find("neura.math.pow");
    CHECK(pow != nullptr && pow->result == Kind::F64 && pow->param_count == 2);
    if (pow) {
        double x = 2.0, y = 10.0, r = 0.0;
        void* args[] = {&x, &y};
        pow->thunk(args, &r);
        CHECK_EQ(r, 1024.0);
    }
    const KernelInfo* parse = find("neura.integer.parse_u64");
    CHECK(parse != nullptr && parse->params[2] == Kind::ResultU64 && parse->params[3] == Kind::ResultI64);
    if (parse) {
        const std::string ff = "ff";
        neura_text input = text(ff);
        std::int64_t radix = 16;
        std::uint64_t value = 0;
        std::int64_t offset = -1;
        std::uint64_t* value_pointer = &value;
        std::int64_t* offset_pointer = &offset;
        void* args[] = {&input, &radix, &value_pointer, &offset_pointer};
        neura_status status = NEURA_NEED_CAPACITY;
        parse->thunk(args, &status);
        CHECK(status == NEURA_OK && value == 255);
    }

    // The declared capacities are enough: run the output kernels with exactly that capacity.
    std::mt19937_64 random(5);
    auto capacity = [&](const KernelInfo* k, const std::vector<std::uint64_t>& args) {
        for (std::size_t b = 0; b < k->buffer_count; ++b) {
            const auto& p = k->params[k->buffers[b].param];
            if (p == Kind::OutText || p == Kind::OutBytes) return evaluate(k->buffers[b].size, args.data());
        }
        return std::uint64_t{0};
    };
    const KernelInfo* b64e = find("neura.encoding.base64_encode");
    const KernelInfo* b64d = find("neura.encoding.base64_decode");
    const KernelInfo* hexe = find("neura.encoding.hex_encode");
    const KernelInfo* hexd = find("neura.encoding.hex_decode");
    const KernelInfo* repl = find("neura.text.replace");
    CHECK(b64e && b64d && hexe && hexd && repl);
    if (!(b64e && b64d && hexe && hexd && repl)) return failures;
    for (int round = 0; round < 300; ++round) {
        std::string data;
        std::size_t n = random() % 70;
        for (std::size_t i = 0; i < n; ++i) data += static_cast<char>(random());
        for (bool padded : {false, true}) {
            std::vector<std::uint8_t> buffer(capacity(b64e, {n, 0, 0, 0}) + 1);
            neura_out_text out{buffer.data(), 0, buffer.size() - 1};
            CHECK(neura_k_encoding_base64_encode(bytes(data), 0, padded, &out) == NEURA_OK);
            std::string encoded(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(out.len));
            std::vector<std::uint8_t> decoded(capacity(b64d, {encoded.size(), 0, 0, 0, 0}) + 1);
            neura_out_bytes dout{decoded.data(), 0, decoded.size() - 1};
            std::int64_t offset = 0;
            CHECK(neura_k_encoding_base64_decode(text(encoded), 0, padded, &dout, &offset) == NEURA_OK);
        }
        std::vector<std::uint8_t> hex(capacity(hexe, {n, 0}));
        neura_out_text hout{hex.data(), 0, hex.size()};
        CHECK(neura_k_encoding_hex_encode(bytes(data), &hout) == NEURA_OK);
        std::vector<std::uint8_t> back(capacity(hexd, {hout.len, 0, 0}));
        neura_out_bytes bout{back.data(), 0, back.size()};
        std::int64_t offset = 0;
        CHECK(neura_k_encoding_hex_decode(neura_text{hex.data(), hout.len}, &bout, &offset) == NEURA_OK);
        std::string hay, target, replacement;
        for (std::size_t i = 0; i < n; ++i) hay += "ab"[random() % 2];
        for (std::size_t i = 0, m = random() % 3 + 1; i < m; ++i) target += "ab"[random() % 2];
        replacement = std::string(random() % 9, 'r');
        std::int64_t rlimit = round % 4 == 0 ? static_cast<std::int64_t>(random() % 3) - 1 : 2147483647;
        // The runtime passes a negative scalar to a formula as 0.
        const std::uint64_t formula_limit = static_cast<std::uint64_t>(std::max<std::int64_t>(rlimit, 0));
        std::vector<std::uint8_t> rbuffer(
            capacity(repl, {hay.size(), target.size(), replacement.size(), formula_limit, 0}));
        neura_out_text rout{rbuffer.data(), 0, rbuffer.size()};
        CHECK(neura_k_text_replace(text(hay), text(target), text(replacement), rlimit, &rout) == NEURA_OK);
        if (failures > 20) break;
    }
    return failures;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: neura_kernel_tests <suite> <source-dir>\n";
        return 2;
    }
    const std::string suite = argv[1];
    const std::map<std::string, std::function<int()>> suites = {
        {"abi", abi_suite},       {"digest", digest_suite}, {"encoding", encoding_suite},
        {"text", text_suite},     {"integer", integer_suite}, {"hash", hash_suite},
        {"sort", sort_suite},     {"math", [&] { return math_suite(argv[2]); }}, {"table", table_suite}};
    auto it = suites.find(suite);
    if (it == suites.end()) {
        std::cerr << "unknown suite " << suite << '\n';
        return 2;
    }
    int result = it->second();
    if (result == 0) std::cout << "kernels-" << suite << ": ok\n";
    return result == 0 ? 0 : 1;
}
