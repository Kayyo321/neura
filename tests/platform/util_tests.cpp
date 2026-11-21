// Unit tests of the portable helpers in src/util that the Windows port added:
// the 128-bit integers of util/int128.hpp and the line-ending helpers of
// util/line_endings.hpp.
//
// Usage: neura_util_tests <int128|line-endings>
//
// Int128 is __int128 on GCC and Clang and a struct on MSVC and clang-cl, so
// the expected values are exact constants, not results of another 128-bit
// implementation. Cases that are undefined for the built-in type (the minimum
// divided by -1) run only against the struct.

#include "util/int128.hpp"
#include "util/line_endings.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

using neura::Int128;
using neura::UInt128;

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) return;
    ++failures;
    std::cout << "FAIL " << what << '\n';
}

std::uint64_t high(UInt128 value) {
    return static_cast<std::uint64_t>(value >> 64);
}

std::uint64_t low(UInt128 value) {
    return static_cast<std::uint64_t>(value);
}

UInt128 unsigned_of(std::uint64_t hi, std::uint64_t lo) {
    return (static_cast<UInt128>(hi) << 64) | static_cast<UInt128>(lo);
}

Int128 signed_of(std::uint64_t hi, std::uint64_t lo) {
    return static_cast<Int128>(unsigned_of(hi, lo));
}

std::string words(UInt128 value) {
    char text[48];
    std::snprintf(text, sizeof text, "0x%016llx_%016llx", static_cast<unsigned long long>(high(value)),
                  static_cast<unsigned long long>(low(value)));
    return text;
}

void expect(UInt128 actual, std::uint64_t hi, std::uint64_t lo, const std::string& what) {
    check(high(actual) == hi && low(actual) == lo,
          what + ": got " + words(actual) + ", expected " + words(unsigned_of(hi, lo)));
}

void expect(Int128 actual, std::uint64_t hi, std::uint64_t lo, const std::string& what) {
    expect(static_cast<UInt128>(actual), hi, lo, what);
}

constexpr std::uint64_t ones = ~std::uint64_t{0};

void test_conversions() {
    expect(Int128(std::int8_t{-1}), ones, ones, "int8_t -1");
    expect(Int128(std::uint8_t{255}), 0, 0xff, "uint8_t 255");
    expect(Int128(std::numeric_limits<std::int16_t>::min()), ones, 0xffffffffffff8000, "int16_t min");
    expect(Int128(std::numeric_limits<std::uint16_t>::max()), 0, 0xffff, "uint16_t max");
    expect(Int128(std::numeric_limits<std::int32_t>::min()), ones, 0xffffffff80000000, "int32_t min");
    expect(Int128(std::numeric_limits<std::uint32_t>::max()), 0, 0xffffffff, "uint32_t max");
    expect(Int128(std::numeric_limits<std::int64_t>::min()), ones, 0x8000000000000000, "int64_t min");
    expect(Int128(std::numeric_limits<std::uint64_t>::max()), 0, ones, "uint64_t max");
    expect(UInt128(std::int64_t{-2}), ones, 0xfffffffffffffffe, "int64_t -2 to unsigned");
    expect(UInt128(std::uint32_t{7}), 0, 7, "uint32_t to unsigned");

    // Narrowing keeps the low bits.
    const Int128 wide = signed_of(0x123, 0xfedcba9876543210);
    check(static_cast<std::int32_t>(wide) == 0x76543210, "to int32_t keeps the low bits");
    check(static_cast<std::uint8_t>(wide) == 0x10, "to uint8_t keeps the low bits");
    check(static_cast<std::int64_t>(signed_of(ones, 0xfffffffffffffffb)) == -5, "to int64_t");
    check(static_cast<bool>(signed_of(1, 0)), "high bits alone are true");
    check(!static_cast<bool>(Int128(0)), "zero is false");
}

void test_arithmetic() {
    expect(UInt128(ones) + UInt128(1), 1, 0, "carry into the high word");
    expect(UInt128(ones) - unsigned_of(1, 0), ones, ones, "borrow out of the high word");
    expect(UInt128(ones) * UInt128(ones), 0xfffffffffffffffe, 1, "64 x 64 bit product");
    expect(Int128(-3) * signed_of(0x0000000004000000, 1), 0xfffffffff3ffffff, 0xfffffffffffffffd, "signed product");
    expect(-signed_of(0x8000000000000000, 0), 0x8000000000000000, 0, "minimum negated wraps");
    expect(-Int128(1), ones, ones, "unary minus");
    expect(Int128(std::numeric_limits<std::int64_t>::max()) + std::int64_t{1}, 0, 0x8000000000000000,
           "mixed with int64_t");
    expect(Int128(std::numeric_limits<std::int64_t>::min()) - std::int64_t{1}, ones, 0x7fffffffffffffff,
           "below int64_t min");
}

void test_shifts() {
    const UInt128 bits = unsigned_of(0x8000000000000001, 3);
    const Int128 negative = static_cast<Int128>(bits);
    struct Case {
        int count;
        std::uint64_t left_hi, left_lo;
        std::uint64_t arithmetic_hi, arithmetic_lo;
        std::uint64_t logical_hi, logical_lo;
    };
    const Case cases[] = {
        {0, 0x8000000000000001, 3, 0x8000000000000001, 3, 0x8000000000000001, 3},
        {63, 0x8000000000000001, 0x8000000000000000, ones, 2, 1, 2},
        {64, 3, 0, ones, 0x8000000000000001, 0, 0x8000000000000001},
        {65, 6, 0, ones, 0xc000000000000000, 0, 0x4000000000000000},
        {127, 0x8000000000000000, 0, ones, ones, 0, 1},
    };
    for (const auto& c : cases) {
        const std::string count = std::to_string(c.count);
        expect(bits << c.count, c.left_hi, c.left_lo, "<< " + count);
        expect(negative << c.count, c.left_hi, c.left_lo, "signed << " + count);
        expect(negative >> c.count, c.arithmetic_hi, c.arithmetic_lo, "arithmetic >> " + count);
        expect(bits >> c.count, c.logical_hi, c.logical_lo, "logical >> " + count);
    }
}

void test_division() {
    struct Signed {
        Int128 dividend, divisor;
        std::uint64_t quotient_hi, quotient_lo;
        std::uint64_t remainder_hi, remainder_lo;
        const char* what;
    };
    const Signed signed_cases[] = {
        {Int128(-7), Int128(2), ones, 0xfffffffffffffffd, ones, ones, "-7 / 2"},
        {Int128(7), Int128(-2), ones, 0xfffffffffffffffd, 0, 1, "7 / -2"},
        {Int128(-7), Int128(-2), 0, 3, ones, ones, "-7 / -2"},
        {signed_of(0xffffffefffffffff, 0xfffffffffffffffc), Int128(3), 0xfffffffaaaaaaaaa, 0xaaaaaaaaaaaaaaaa, ones,
         0xfffffffffffffffe, "-(2^100 + 4) / 3"},
        {signed_of(0x0000001000000000, 5), signed_of(0xffffffffffffffbf, 0xfffffffffffffffd), ones,
         0xffffffffc0000001, 0x3f, 0xffffffff40000008, "(2^100 + 5) / -(2^70 + 3)"},
        {signed_of(0x8000000000000000, 0), Int128(3), 0xd555555555555555, 0x5555555555555556, ones,
         0xfffffffffffffffe, "minimum / 3"},
    };
    for (const auto& c : signed_cases) {
        expect(c.dividend / c.divisor, c.quotient_hi, c.quotient_lo, std::string(c.what) + " quotient");
        expect(c.dividend % c.divisor, c.remainder_hi, c.remainder_lo, std::string(c.what) + " remainder");
    }

    struct Unsigned {
        UInt128 dividend, divisor;
        std::uint64_t quotient_hi, quotient_lo;
        std::uint64_t remainder_hi, remainder_lo;
        const char* what;
    };
    const Unsigned unsigned_cases[] = {
        {unsigned_of(ones, ones), unsigned_of(1, 1), 0, ones, 0, 0, "(2^128 - 1) / (2^64 + 1)"},
        {unsigned_of(0x8000000000000000, 0x3039), unsigned_of(2, 7), 0, 0x3fffffffffffffff, 0, 0x4000000000003040,
         "(2^127 + 12345) / (2^65 + 7)"},
        {unsigned_of(ones, ones), UInt128(3), 0x5555555555555555, 0x5555555555555555, 0, 0, "(2^128 - 1) / 3"},
        {unsigned_of(5, 9), unsigned_of(1, 0), 0, 5, 0, 9, "(5 * 2^64 + 9) / 2^64"},
    };
    for (const auto& c : unsigned_cases) {
        expect(c.dividend / c.divisor, c.quotient_hi, c.quotient_lo, std::string(c.what) + " quotient");
        expect(c.dividend % c.divisor, c.remainder_hi, c.remainder_lo, std::string(c.what) + " remainder");
    }

#if !defined(__SIZEOF_INT128__) || defined(_MSC_VER)
    // Undefined for __int128; the struct wraps to the minimum.
    const Int128 minimum = signed_of(0x8000000000000000, 0);
    expect(minimum / Int128(-1), 0x8000000000000000, 0, "minimum / -1");
    expect(minimum % Int128(-1), 0, 0, "minimum % -1");
    static_assert(Int128(-7) / Int128(2) == Int128(-3), "constant evaluation");
    static_assert(((UInt128(1) << 127) >> 127) == UInt128(1), "constant evaluation");
#endif
}

void test_comparisons() {
    check(Int128(-1) < Int128(0), "signed -1 < 0");
    check(signed_of(0x8000000000000000, 0) < signed_of(0x7fffffffffffffff, ones), "signed minimum < maximum");
    check(UInt128(0) < unsigned_of(0x8000000000000000, 0), "unsigned 0 < 2^127");
    check(static_cast<UInt128>(Int128(-1)) > UInt128(ones), "unsigned 2^128 - 1 > 2^64 - 1");
    check(signed_of(0, ones) > signed_of(0, 1), "low word decides");
    check(Int128(5) == std::int64_t{5}, "equal to an int64_t");
    check(std::max(Int128(-9), Int128(4)) == Int128(4), "std::max");
}

void test_line_endings() {
    using neura::util::normalize_crlf;
    using neura::util::only_line_endings_differ;
    check(normalize_crlf("a\r\nb\r\n") == "a\nb\n", "CR LF to LF, a trailing one too");
    check(normalize_crlf("a\rb") == "a\rb", "a lone CR is kept");
    check(normalize_crlf("a\r") == "a\r", "a final CR is kept");
    check(normalize_crlf("a\r\r\nb") == "a\r\nb", "CR CR LF gives CR LF");
    check(normalize_crlf("a\n\rb") == "a\n\rb", "LF CR is not a pair");
    check(normalize_crlf("") == "", "empty text");

    check(!only_line_endings_differ("a\nb", "a\nb"), "equal text");
    check(only_line_endings_differ("a\r\nb\r\n", "a\nb\n"), "CR LF against LF");
    check(only_line_endings_differ("a\nb\n", "a\r\nb\r\n"), "LF against CR LF");
    check(!only_line_endings_differ("x\r", "x"), "a lone CR is text");
    check(!only_line_endings_differ("a\r\nb", "a\nc"), "different text");
    check(!only_line_endings_differ("a\r\n", "a"), "a missing line ending");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: neura_util_tests <int128|line-endings>\n";
        return 2;
    }
    const std::string_view suite = argv[1];
    if (suite == "int128") {
        test_conversions();
        test_arithmetic();
        test_shifts();
        test_division();
        test_comparisons();
    } else if (suite == "line-endings") {
        test_line_endings();
    } else {
        std::cerr << "neura_util_tests: unknown suite '" << suite << "'\n";
        return 2;
    }
    if (failures == 0) std::cout << suite << ": all checks passed\n";
    return failures == 0 ? 0 : 1;
}
