<!-- Generated from docs/lang/Numbers.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Numbers

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This page specifies Neura's integer and floating-point types: their representation, literals, arithmetic, comparisons, conversions, and failures. The compiler, the optimizer's constant folding, the neuron circuits, and NVM implement these rules, and the [release checklist](Release) lists the tests that check each one.

## Integer types

| Type | Representation |
| --- | --- |
| `int8`, `int16`, `int32`, `int64` | Two's complement signed integers of 8, 16, 32, and 64 bits |
| `uint8`, `uint16`, `uint32`, `uint64` | Unsigned integers of 8, 16, 32, and 64 bits |
| `int` | Another name for `int32` |
| `byte` | Another name for `uint8` |

`int` and `byte` are aliases, not separate types: `int` and `int32` are interchangeable everywhere, including as type arguments, so `Box<int>` and `Box<int32>` are one type. Collection `length` has type `int`.

### Integer literals

A literal is a sequence of decimal digits. It takes the integer type that its context expects. With no expected type it is `int32`. A literal outside its type's range is a compile-time error at the literal. A leading `-` belongs to the literal, so `-128` is a valid `int8` and `-9223372036854775808` a valid `int64`. A minus applied to an unsigned literal other than zero is out of range.

### Integer arithmetic

`+`, `-`, `*`, `/`, `%`, and unary `-` take two operands of the same integer type and produce that type. There are no implicit conversions between integer types. A literal operand takes the other operand's type.

| Operation | Result | Failure |
| --- | --- | --- |
| `a + b`, `a - b`, `a * b`, `-a` | The exact result | Traps when the exact result is outside the type's range (overflow) |
| `a / b` | The quotient rounded toward zero | Traps when `b` is zero, and for `MIN / -1` of a signed type |
| `a % b` | `a - (a / b) * b`: the remainder has the sign of `a`, or is zero | Traps when `b` is zero. `MIN % -1` is 0 |

Nothing wraps or saturates: an operation either produces its exact mathematical result or traps. A trap ends the run with trap reason 1 (arithmetic failure) and process status 136, `ExitCode.ArithmeticError` ([Errors](Errors)). For example, `7 / 2` is 3, `-7 / 2` is -3, `-7 % 2` is -1, and `7 % -2` is 1.

**Compatibility.** Before this release, a division whose remainder was not zero trapped, because the rounding rule was open. It now truncates toward zero. A program that relied on that trap now receives the truncated quotient. Overflow and division by zero trap as before.

### Integer comparisons

`==`, `!=`, `<`, `<=`, `>`, and `>=` compare two operands of the same integer type by their mathematical values and produce `bool`.

## Bitwise operations

The unary operator `~` complements every bit. The operators `&`, `|`, and `^` compute per-bit AND, OR, and XOR on two integers of the same type. All four produce a value of the operand type.

The operators `<<` and `>>` shift the fixed-width bit pattern of the left operand. The count can have any integer type. A negative count or a count greater than or equal to the left operand's width traps. A left shift shifts in zeros and discards bits beyond the width. A right shift of a signed integer extends its sign bit, and a right shift of an unsigned integer shifts in zeros. The result has the left operand's type.

## Floating-point types

`float` is an alias of `float32`. An unconstrained decimal floating-point literal still defaults to `float64`.

| Type | Format | Precision (significand bits) | Exponent bits | Largest finite value |
| --- | --- | --- | --- | --- |
| `float16` | IEEE 754 binary16 | 11 | 5 | 65504 |
| `bfloat16` | binary32's exponent with a 7-bit fraction | 8 | 8 | about 3.39e38 |
| `float32` | IEEE 754 binary32 | 24 | 8 | about 3.40e38 |
| `float64` | IEEE 754 binary64 | 53 | 11 | about 1.80e308 |

Each value is encoded with a sign bit, a biased exponent, and a fraction, exactly as in IEEE 754, so every bit pattern is a value. Subnormal numbers, signed zeros, and infinities are supported in every format. Hardware support is not required: the circuits compute every operation bit by bit.

### Rounding and exceptions

Every operation that cannot represent its exact result rounds to the nearest representable value, ties to the value with an even fraction. A result too large for the format rounds to an infinity of the same sign; a result too small rounds to a subnormal value or to zero of the same sign. Floating-point operations never trap and set no flags: invalid operations produce NaN, division of a nonzero value by zero produces an infinity, and overflow produces an infinity, as IEEE 754 specifies by default.

### NaN

Every NaN that an arithmetic operation or a conversion produces is the canonical quiet NaN: sign 0, all exponent bits set, and only the most significant fraction bit set. NaN payloads are therefore never propagated, and a program's results do not depend on which NaN an input held. Unary `-` is the one exception: it flips the sign bit of any value, including a NaN, so `-x` of the canonical NaN has sign 1.

### Float literals

A float literal has digits with a fraction (`1.5`), an exponent (`1e3`, `2.5e-4`), or both. With no expected type it is `float64`; in a float context it takes the expected format. The compiler converts the decimal value directly to the target format by exact rational arithmetic and rounds once, ties to even, so a `float16` literal is not rounded through `float64` first. A literal whose rounded value would be infinite, such as `65520.0` as `float16`, is a compile-time error. So is a nonzero literal that rounds to zero, such as `1e-50` as `float32`. `-0.0` is negative zero. There are no literals for infinities or NaN; `1.0 / 0.0` and `0.0 / 0.0` produce them.

### Float arithmetic and comparisons

`+`, `-`, `*`, and `/` take two operands of the same float type, compute the exact result, and round it once. There is no `%` on floats. Unary `-` flips the sign bit.

Comparisons follow IEEE 754. A NaN operand is unordered: `==`, `<`, `<=`, `>`, and `>=` are false and `!=` is true, so `x != x` holds exactly when `x` is NaN. `-0.0 == 0.0` is true. Ordered comparisons of other values compare their mathematical values, with negative infinity below every finite value and positive infinity above.

Float arithmetic is not reassociated or contracted: `a * b + c` rounds the product and then the sum.

### Bit reinterpretation

`value.to_bits()` returns the float's raw encoding as a same-width unsigned integer. Call `float16.from_bits`, `bfloat16.from_bits`, `float32.from_bits`, or `float64.from_bits` with the corresponding unsigned integer to reinterpret it in the other direction. Both directions preserve every bit, including NaN payloads and signed zero. `float` is interchangeable with `float32`.

## Conversions

`expression as Type` converts a numeric value explicitly. It binds tighter than `*` and looser than unary operators, so `-x as int64` converts `-x` and `a * b as int64` converts only `b`. Converting a value to its own type returns it.

| From | To | Result | Failure |
| --- | --- | --- | --- |
| Integer | Integer | The same value | Traps when the value is outside the target range, including a negative value converted to an unsigned type |
| Integer | Float | The nearest value, ties to even; an infinity if beyond the format (only possible for `float16`) | None |
| Float | Integer | The value truncated toward zero | Traps for a NaN, an infinity, or a truncated value outside the target range |
| Float | Float | Exact when the target holds the value; otherwise rounded to nearest, ties to even, with infinities on overflow | None; a NaN becomes the target's canonical NaN |
| Enum | Integer | The variant's ordinal, in declaration order from 0 | Traps when the ordinal is outside the target range |

A conversion trap uses trap reason 1. There are no implicit conversions, no conversions to or from `bool`, and no conversion from an integer to an enum. Conversions between enums are not provided.

## Where the rules are enforced

The frontend checks literal ranges and conversion types. The optimizer folds integer arithmetic, comparisons, and conversions only when the folded result is defined, so every trapping operation stays in the program. It does not fold floating-point arithmetic. The circuits compute every operation with ReLU and identity neurons over the operands' bit lanes ([Backend](Backend)), and NVM evaluates them under graph profile 1.
