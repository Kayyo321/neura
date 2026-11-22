#pragma once

// 128-bit integers for arithmetic that must not overflow 64 bits (the range
// analysis of tier 3 computes its interval bounds in them). GCC and Clang
// provide __int128 and neura::Int128 is that type. MSVC has none, and clang-cl
// has the type but its division calls __divti3, which the MSVC libraries do
// not provide, so both get a struct that gives the same results: two's
// complement with wraparound modulo 2^128, division that truncates toward
// zero, arithmetic right shift of signed values, and integer conversions that
// keep the low bits. Shift counts must be below 128 and divisors nonzero, as
// for the built-in type.

#include <compare>
#include <concepts>
#include <cstdint>

namespace neura {

#if defined(__SIZEOF_INT128__) && !defined(_MSC_VER)

__extension__ typedef __int128 Int128;
__extension__ typedef unsigned __int128 UInt128;

#else

namespace int128_detail {

// The 128 bits of either signedness, as two 64-bit words.
struct Bits {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
};

constexpr Bits add(Bits a, Bits b) {
    const std::uint64_t lo = a.lo + b.lo;
    return {a.hi + b.hi + (lo < a.lo ? 1U : 0U), lo};
}

constexpr Bits subtract(Bits a, Bits b) {
    return {a.hi - b.hi - (a.lo < b.lo ? 1U : 0U), a.lo - b.lo};
}

constexpr Bits negate(Bits a) { return subtract({}, a); }

// The full product of two 64-bit words, from 32-bit halves so that it is the
// same in constant evaluation and at run time.
constexpr Bits multiply_words(std::uint64_t a, std::uint64_t b) {
    const std::uint64_t a0 = a & 0xffffffffU;
    const std::uint64_t a1 = a >> 32;
    const std::uint64_t b0 = b & 0xffffffffU;
    const std::uint64_t b1 = b >> 32;
    const std::uint64_t p00 = a0 * b0;
    const std::uint64_t p01 = a0 * b1;
    const std::uint64_t p10 = a1 * b0;
    const std::uint64_t p11 = a1 * b1;
    const std::uint64_t middle = (p00 >> 32) + (p01 & 0xffffffffU) + (p10 & 0xffffffffU);
    return {p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32), (middle << 32) | (p00 & 0xffffffffU)};
}

// The product modulo 2^128, which is the same for both signednesses.
constexpr Bits multiply(Bits a, Bits b) {
    Bits r = multiply_words(a.lo, b.lo);
    r.hi += a.lo * b.hi + a.hi * b.lo;
    return r;
}

constexpr Bits shift_left(Bits a, unsigned n) {
    if (n == 0) return a;
    if (n >= 64) return {a.lo << (n - 64), 0};
    return {(a.hi << n) | (a.lo >> (64 - n)), a.lo << n};
}

// `fill` is all ones for an arithmetic shift of a negative value, else zero.
constexpr Bits shift_right(Bits a, unsigned n, std::uint64_t fill) {
    if (n == 0) return a;
    if (n >= 64) return {fill, n == 64 ? a.hi : (a.hi >> (n - 64)) | (fill << (128 - n))};
    return {(a.hi >> n) | (fill << (64 - n)), (a.lo >> n) | (a.hi << (64 - n))};
}

constexpr bool less(Bits a, Bits b) { return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo; }

struct Division {
    Bits quotient;
    Bits remainder;
};

// Unsigned long division. A zero divisor divides in 64 bits, so that it
// faults as the built-in division does.
constexpr Division divide_unsigned(Bits n, Bits d) {
    if (d.hi == 0 && (n.hi == 0 || d.lo == 0)) return {{0, n.lo / d.lo}, {0, n.lo % d.lo}};
    Division r;
    for (unsigned bit = 128; bit-- > 0;) {
        r.remainder = shift_left(r.remainder, 1);
        r.remainder.lo |= shift_right(n, bit, 0).lo & 1U;
        if (!less(r.remainder, d)) {
            r.remainder = subtract(r.remainder, d);
            if (bit >= 64) {
                r.quotient.hi |= std::uint64_t{1} << (bit - 64);
            } else {
                r.quotient.lo |= std::uint64_t{1} << bit;
            }
        }
    }
    return r;
}

template <bool Signed>
class Wide {
public:
    constexpr Wide() = default;

    // Every integer converts as it does to the built-in type: a signed value
    // is sign-extended and an unsigned one zero-extended.
    template <std::integral T>
    constexpr Wide(T value) : bits_{high_word(value), static_cast<std::uint64_t>(value)} {}

    // Between signednesses the bits are kept, as static_cast does.
    template <bool Other>
        requires(Other != Signed)
    explicit constexpr Wide(Wide<Other> value) : bits_{value.bits()} {}

    // To a narrower integer the low bits are kept.
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    explicit constexpr operator T() const {
        return static_cast<T>(bits_.lo);
    }
    explicit constexpr operator bool() const { return (bits_.hi | bits_.lo) != 0; }

    constexpr Bits bits() const { return bits_; }

    friend constexpr bool operator==(const Wide& a, const Wide& b) {
        return a.bits_.hi == b.bits_.hi && a.bits_.lo == b.bits_.lo;
    }
    friend constexpr std::strong_ordering operator<=>(const Wide& a, const Wide& b) {
        if (a.bits_.hi != b.bits_.hi) {
            if constexpr (Signed) {
                return static_cast<std::int64_t>(a.bits_.hi) <=> static_cast<std::int64_t>(b.bits_.hi);
            } else {
                return a.bits_.hi <=> b.bits_.hi;
            }
        }
        return a.bits_.lo <=> b.bits_.lo;
    }

    friend constexpr Wide operator+(const Wide& a) { return a; }
    friend constexpr Wide operator-(const Wide& a) { return from(negate(a.bits_)); }
    friend constexpr Wide operator~(const Wide& a) { return from({~a.bits_.hi, ~a.bits_.lo}); }
    friend constexpr Wide operator+(const Wide& a, const Wide& b) { return from(add(a.bits_, b.bits_)); }
    friend constexpr Wide operator-(const Wide& a, const Wide& b) { return from(subtract(a.bits_, b.bits_)); }
    friend constexpr Wide operator*(const Wide& a, const Wide& b) { return from(multiply(a.bits_, b.bits_)); }
    friend constexpr Wide operator/(const Wide& a, const Wide& b) { return from(divide(a.bits_, b.bits_).quotient); }
    friend constexpr Wide operator%(const Wide& a, const Wide& b) { return from(divide(a.bits_, b.bits_).remainder); }
    friend constexpr Wide operator&(const Wide& a, const Wide& b) {
        return from({a.bits_.hi & b.bits_.hi, a.bits_.lo & b.bits_.lo});
    }
    friend constexpr Wide operator|(const Wide& a, const Wide& b) {
        return from({a.bits_.hi | b.bits_.hi, a.bits_.lo | b.bits_.lo});
    }
    friend constexpr Wide operator^(const Wide& a, const Wide& b) {
        return from({a.bits_.hi ^ b.bits_.hi, a.bits_.lo ^ b.bits_.lo});
    }

    // The shift count is an integer of any type, as for the built-in shifts.
    template <std::integral N>
    constexpr Wide operator<<(N n) const {
        return from(shift_left(bits_, static_cast<unsigned>(n)));
    }
    template <std::integral N>
    constexpr Wide operator>>(N n) const {
        return from(shift_right(bits_, static_cast<unsigned>(n), negative() ? ~std::uint64_t{0} : 0));
    }

    constexpr Wide& operator+=(const Wide& b) { return *this = *this + b; }
    constexpr Wide& operator-=(const Wide& b) { return *this = *this - b; }
    constexpr Wide& operator*=(const Wide& b) { return *this = *this * b; }
    constexpr Wide& operator/=(const Wide& b) { return *this = *this / b; }
    constexpr Wide& operator%=(const Wide& b) { return *this = *this % b; }
    constexpr Wide& operator&=(const Wide& b) { return *this = *this & b; }
    constexpr Wide& operator|=(const Wide& b) { return *this = *this | b; }
    constexpr Wide& operator^=(const Wide& b) { return *this = *this ^ b; }
    template <std::integral N>
    constexpr Wide& operator<<=(N n) {
        return *this = *this << n;
    }
    template <std::integral N>
    constexpr Wide& operator>>=(N n) {
        return *this = *this >> n;
    }
    constexpr Wide& operator++() { return *this += 1; }
    constexpr Wide& operator--() { return *this -= 1; }
    constexpr Wide operator++(int) {
        const Wide old = *this;
        ++*this;
        return old;
    }
    constexpr Wide operator--(int) {
        const Wide old = *this;
        --*this;
        return old;
    }

private:
    template <std::integral T>
    static constexpr std::uint64_t high_word([[maybe_unused]] T value) {
        if constexpr (std::signed_integral<T>) {
            return value < 0 ? ~std::uint64_t{0} : 0;
        } else {
            return 0;
        }
    }

    static constexpr Wide from(Bits bits) {
        Wide w;
        w.bits_ = bits;
        return w;
    }

    constexpr bool negative() const { return Signed && (bits_.hi >> 63) != 0; }

    // Signed division truncates toward zero and the remainder takes the sign
    // of the dividend; the minimum divided by -1 wraps to itself.
    static constexpr Division divide(Bits a, Bits b) {
        if constexpr (!Signed) {
            return divide_unsigned(a, b);
        } else {
            const bool a_negative = (a.hi >> 63) != 0;
            const bool b_negative = (b.hi >> 63) != 0;
            Division r = divide_unsigned(a_negative ? negate(a) : a, b_negative ? negate(b) : b);
            if (a_negative != b_negative) r.quotient = negate(r.quotient);
            if (a_negative) r.remainder = negate(r.remainder);
            return r;
        }
    }

    Bits bits_;
};

}  // namespace int128_detail

using Int128 = int128_detail::Wide<true>;
using UInt128 = int128_detail::Wide<false>;

#endif

}  // namespace neura
