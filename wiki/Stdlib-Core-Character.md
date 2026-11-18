# `Std.Core.Character`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Character`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Character`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Character** · [`export const MinCodePoint: uint32 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L26)
  The smallest code point, U+0000.
- **Character** · [`export const MaxCodePoint: uint32 = 1114111`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L28)
  The largest code point, U+10FFFF.
- **Character** · [`export const MinSupplementaryCodePoint: uint32 = 65536`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L30)
  The first supplementary code point, U+10000.
- **Character** · [`export const MinHighSurrogate: uint32 = 55296`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L32)
  The first high (leading) surrogate, U+D800.
- **Character** · [`export const MaxHighSurrogate: uint32 = 56319`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L34)
  The last high (leading) surrogate, U+DBFF.
- **Character** · [`export const MinLowSurrogate: uint32 = 56320`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L36)
  The first low (trailing) surrogate, U+DC00.
- **Character** · [`export const MaxLowSurrogate: uint32 = 57343`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L38)
  The last low (trailing) surrogate, U+DFFF.
- **Character** · [`export const MinSurrogate: uint32 = 55296`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L40)
  The first surrogate, U+D800.
- **Character** · [`export const MaxSurrogate: uint32 = 57343`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L42)
  The last surrogate, U+DFFF.
- **Character** · [`export const MinRadix: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L44)
  The smallest radix of digit and for_digit.
- **Character** · [`export const MaxRadix: int = 36`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L46)
  The largest radix of digit and for_digit.
- **Character** · [`export const UnicodeVersion: string = "17.0.0"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L48)
  The version of the Unicode Character Database behind every property.
- **Character** · [`export let GeneralCategory = enum ( UppercaseLetter, LowercaseLetter, TitlecaseLetter, ModifierLetter, OtherLetter, NonspacingMark, SpacingMark, EnclosingMark, DecimalNumber, LetterNumber, OtherNumber, ConnectorPunctuation, DashPunctuation, OpenPunctuation, ClosePunctuation, InitialPunctuation, FinalPunctuation, OtherPunctuation, MathSymbol, CurrencySymbol, ModifierSymbol, OtherSymbol, SpaceSeparator, LineSeparator, ParagraphSeparator, Control, Format, Surrogate, PrivateUse, Unassigned )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L53)
  The General_Category values in Unicode's order. Java's getType constants map one to one: OpenPunctuation is START_PUNCTUATION, ClosePunctuation END_PUNCTUATION, SpacingMark COMBINING_SPACING_MARK, DecimalNumber DECIMAL_DIGIT_NUMBER, and NonspacingMark NON_SPACING_MARK.
- **Character** · [`export let BidiClass = enum ( LeftToRight, RightToLeft, ArabicLetter, EuropeanNumber, EuropeanSeparator, EuropeanTerminator, ArabicNumber, CommonSeparator, NonspacingMark, BoundaryNeutral, ParagraphSeparator, SegmentSeparator, WhiteSpace, OtherNeutral, LeftToRightEmbedding, LeftToRightOverride, RightToLeftEmbedding, RightToLeftOverride, PopDirectionalFormat, LeftToRightIsolate, RightToLeftIsolate, FirstStrongIsolate, PopDirectionalIsolate, Undefined )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L63)
  The Bidi_Class values, in the order of Java's DIRECTIONALITY constants, and Undefined, which get_directionality returns for code points that are not defined.
- **Character** · [`export let Character = class implements Comparable<Character>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L75)
  One Unicode scalar value with value semantics: equal when the code points are equal, hashed as the code point (Java's hashCode is the char value), ordered by code point, and printed as the character itself. Tagged `#Final`, `#NoDiscard`, `#Immutable`, `#ThreadSafe`.
- **Character.Character** · [`export last code_point: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L77)
  The Unicode scalar value.
- **Character.Character** · [`export let init = func(value: uint32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L81)
  The character of a Unicode scalar value. Traps with "invalid argument: not a Unicode scalar value" for a surrogate or a value above 0x10FFFF.
- **Character.Character** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L88)
  The one-character string. Tagged `#Pure`.
- **Character.Character** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L91)
  Tests whether another Character holds the same code point.
- **Character.Character** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L100)
  The code point, as for a uint32 (Stdlib.md section 5.2). Tagged `#Pure`.
- **Character.Character** · [`export let compare_to = func(other: Character) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L104)
  Orders by code point. Tagged `#Pure`.
- **Character.Character** · [`export let get_type = func -> GeneralCategory`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L108)
  The general category. Tagged `#Pure`.
- **Character.Character** · [`export let get_directionality = func -> BidiClass`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L112)
  The directionality (Bidi_Class), Undefined for an unassigned character. Tagged `#Pure`.
- **Character.Character** · [`export let is_upper = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L116)
  Tests the Uppercase property. Tagged `#Pure`.
- **Character.Character** · [`export let is_lower = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L120)
  Tests the Lowercase property. Tagged `#Pure`.
- **Character.Character** · [`export let is_title_case = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L124)
  Tests for general category Lt. Tagged `#Pure`.
- **Character.Character** · [`export let is_letter = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L128)
  Tests for a letter (general category L). Tagged `#Pure`.
- **Character.Character** · [`export let is_digit = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L132)
  Tests for a decimal digit (general category Nd). Tagged `#Pure`.
- **Character.Character** · [`export let is_letter_or_digit = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L136)
  Tests for a letter or a decimal digit. Tagged `#Pure`.
- **Character.Character** · [`export let is_alphabetic = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L140)
  Tests the Alphabetic property. Tagged `#Pure`.
- **Character.Character** · [`export let is_alpha = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L144)
  Tests for a letter, as is_letter. Tagged `#Pure`.
- **Character.Character** · [`export let is_numeric = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L148)
  Tests for a Unicode numeric value. Tagged `#Pure`.
- **Character.Character** · [`export let is_alpha_numeric = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L152)
  Tests for a letter or a character with a numeric value. Tagged `#Pure`.
- **Character.Character** · [`export let is_whitespace = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L157)
  Tests Java's white space set. Tagged `#Pure`.
- **Character.Character** · [`export let is_space_char = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L161)
  Tests for general category Zs, Zl, or Zp. Tagged `#Pure`.
- **Character.Character** · [`export let is_space = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L165)
  Tests the White_Space property. Tagged `#Pure`.
- **Character.Character** · [`export let is_iso_control = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L169)
  Tests for an ISO control character. Tagged `#Pure`.
- **Character.Character** · [`export let is_defined = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L173)
  Tests whether the character is assigned (its category is not Unassigned). Tagged `#Pure`.
- **Character.Character** · [`export let is_ideographic = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L177)
  Tests the Ideographic property. Tagged `#Pure`.
- **Character.Character** · [`export let is_mirrored = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L181)
  Tests the Bidi_Mirrored property. Tagged `#Pure`.
- **Character.Character** · [`export let is_bmp = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L185)
  Tests whether the character is in the Basic Multilingual Plane. Tagged `#Pure`.
- **Character.Character** · [`export let is_supplementary = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L189)
  Tests whether the character is outside the Basic Multilingual Plane. Tagged `#Pure`.
- **Character.Character** · [`export let to_upper = func -> Character`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L193)
  The simple uppercase mapping. Tagged `#Pure`.
- **Character.Character** · [`export let to_lower = func -> Character`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L197)
  The simple lowercase mapping. Tagged `#Pure`.
- **Character.Character** · [`export let to_title = func -> Character`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L201)
  The simple titlecase mapping. Tagged `#Pure`.
- **Character.Character** · [`export let to_upper_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L205)
  The full uppercase mapping, as String.to_upper_case gives it. Tagged `#Pure`.
- **Character.Character** · [`export let to_lower_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L209)
  The full lowercase mapping, as String.to_lower_case gives it. Tagged `#Pure`.
- **Character.Character** · [`export let digit = func(radix: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L213)
  The digit value in the radix, or -1; see the module function digit. Tagged `#Pure`.
- **Character.Character** · [`export let numeric_value = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L217)
  The numeric value; see the module function numeric_value. Tagged `#Pure`.
- **Character.Character** · [`export let char_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L221)
  The number of UTF-16 code units, 1 or 2. Tagged `#Pure`.
- **Character.Character** · [`export let utf8_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L225)
  The number of UTF-8 bytes, 1 through 4. Tagged `#Pure`.
- **Character** · [`export let of = func(code_point: uint32) -> Character`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L409)
  The Character of a Unicode scalar value. Traps with "invalid argument: not a Unicode scalar value" for a surrogate or a value above 0x10FFFF. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Character** · [`export let try_of = func(code_point: uint32) -> Optional<Character>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L413)
  The Character of a Unicode scalar value, or None for a surrogate or a value above 0x10FFFF. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Character** · [`export let from_text = func(text: string) -> Optional<Character>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L418)
  The Character of a string that holds exactly one scalar value, or None. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Character** · [`export let to_string = func(code_point: uint32) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L431)
  The one-character string of a scalar value (Java's toString(int)). Traps as `of` does. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let hash = func(code_point: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L435)
  The code point as its hash, as for a uint32 (Java's hashCode(char)). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let compare = func(left: uint32, right: uint32) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L439)
  Orders two code points numerically. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let get_type = func(code_point: uint32) -> GeneralCategory`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L445)
  The General_Category; Surrogate for U+D800 through U+DFFF and Unassigned above 0x10FFFF. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let category_code = func(category: GeneralCategory) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L449)
  The two-letter abbreviation of a category, such as "Lu". Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let get_directionality = func(code_point: uint32) -> BidiClass`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L455)
  The Bidi_Class of a defined code point (Java's getDirectionality); Undefined for a code point whose category is Unassigned, including noncharacters, and above 0x10FFFF. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let bidi_class = func(code_point: uint32) -> BidiClass`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L461)
  The Bidi_Class, including the default the UCD gives unassigned code points (for example RightToLeft in the Hebrew block and BoundaryNeutral for noncharacters); Undefined above 0x10FFFF. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let bidi_code = func(bidi: BidiClass) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L465)
  The abbreviation of a bidi class, such as "AL"; an empty string for Undefined. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_upper = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L469)
  Tests the Uppercase property: category Lu or Other_Uppercase (Java's isUpperCase). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_lower = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L473)
  Tests the Lowercase property: category Ll or Other_Lowercase (Java's isLowerCase). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_title_case = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L477)
  Tests for category Lt, such as U+01C5 (Java's isTitleCase). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_letter = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L481)
  Tests for category Lu, Ll, Lt, Lm, or Lo (Java's isLetter). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_digit = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L485)
  Tests for category Nd (Java's isDigit). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_letter_or_digit = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L489)
  Tests for a letter or category Nd (Java's isLetterOrDigit). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_alphabetic = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L493)
  Tests the Alphabetic property: letters, category Nl, and Other_Alphabetic (Java's isAlphabetic). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_alpha = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L497)
  Tests for a letter, the same set as is_letter (Python's str.isalpha). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_numeric = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L503)
  Tests for a Unicode Numeric_Value, any Numeric_Type other than None (Python's str.isnumeric). Includes decimal digits, other digits, fractions, Roman numerals, and the CJK ideographs with numeric values in Unihan. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_alpha_numeric = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L507)
  Tests is_alpha or is_numeric (Python's str.isalnum). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_whitespace = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L514)
  Tests Java's white space: category Zs, Zl, or Zp except the no-break spaces U+00A0, U+2007, and U+202F, and U+0009 through U+000D and U+001C through U+001F (Java's isWhitespace). The String companion's strip and indentation use the same set. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_space_char = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L518)
  Tests for category Zs, Zl, or Zp (Java's isSpaceChar). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_space = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L524)
  Tests the Unicode White_Space property: U+0009 through U+000D, U+0020, U+0085, U+00A0, and the separators of categories Zs, Zl, and Zp. Unlike is_whitespace it includes the no-break spaces and NEL and excludes U+001C through U+001F. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_iso_control = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L528)
  Tests for U+0000 through U+001F and U+007F through U+009F (Java's isISOControl). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_defined = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L533)
  Tests whether the category is not Unassigned (Java's isDefined). Surrogates and private use code points are defined; noncharacters are not. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_ideographic = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L537)
  Tests the Ideographic property (Java's isIdeographic). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_mirrored = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L541)
  Tests the Bidi_Mirrored property (Java's isMirrored). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_identifier_start = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L546)
  Tests whether a code point may start a Unicode identifier: ID_Start or U+2E2F VERTICAL TILDE (Java's isUnicodeIdentifierStart, its profile of UAX #31). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_identifier_part = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L551)
  Tests whether a code point may continue a Unicode identifier: ID_Continue, U+2E2F, or is_identifier_ignorable (Java's isUnicodeIdentifierPart). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_identifier_ignorable = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L556)
  Tests U+0000 through U+0008, U+000E through U+001B, U+007F through U+009F, and category Cf (Java's isIdentifierIgnorable). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_java_identifier_start = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L562)
  Tests whether a code point may start a Java identifier: a letter or category Nl, Sc, or Pc (Java's isJavaIdentifierStart). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_java_identifier_part = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L568)
  Tests whether a code point may continue a Java identifier: a letter, category Sc, Pc, Nd, Nl, Mc, or Mn, or is_identifier_ignorable (Java's isJavaIdentifierPart). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_emoji = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L572)
  Tests the Emoji property of UTS #51 (Java's isEmoji). Digits, `#`, and `*` have it. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_emoji_presentation = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L576)
  Tests the Emoji_Presentation property (Java's isEmojiPresentation). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_emoji_modifier = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L581)
  Tests the Emoji_Modifier property, the skin tone modifiers (Java's isEmojiModifier). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_emoji_modifier_base = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L585)
  Tests the Emoji_Modifier_Base property (Java's isEmojiModifierBase). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_emoji_component = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L590)
  Tests the Emoji_Component property (Java's isEmojiComponent). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_extended_pictographic = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L594)
  Tests the Extended_Pictographic property (Java's isExtendedPictographic). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let to_upper = func(code_point: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L602)
  The simple uppercase mapping of UnicodeData.txt; the code point itself when it has none (Java's toUpperCase(int)). U+00DF maps to itself; to_upper_string gives "SS". Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let to_lower = func(code_point: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L606)
  The simple lowercase mapping; the code point itself when it has none (Java's toLowerCase(int)). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let to_title = func(code_point: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L611)
  The simple titlecase mapping, which is the uppercase mapping where UnicodeData.txt gives none (Java's toTitleCase(int)). U+01C6 maps to U+01C5. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let to_upper_string = func(code_point: uint32) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L616)
  The full uppercase mapping with the root rules of String.to_upper_case, which can be several characters: U+00DF gives "SS". Traps as `of` does. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let to_lower_string = func(code_point: uint32) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L626)
  The full lowercase mapping with the root rules of String.to_lower_case: U+0130 gives "i" followed by U+0307. A lone U+03A3 has no letter before it, so it maps to U+03C3, not the final sigma. Traps as `of` does. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let digit = func(code_point: uint32, radix: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L639)
  The value of a digit in a radix (Java's digit(int, int)): the decimal digit value of a category Nd code point, or 10 through 35 for A through Z and a through z in ASCII or fullwidth form, when the value is below the radix; otherwise -1. Unlike Java, a radix outside 2 through 36 traps with "invalid argument: radix outside 2 through 36" (Stdlib.md section 5.1). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let for_digit = func(value: int, radix: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L645)
  The lowercase ASCII digit for a value from 0 through radix - 1 (Java's forDigit). Unlike Java, which returns U+0000, a radix outside 2 through 36 traps as in digit and a value outside the radix traps with "invalid argument: digit out of range", as Scalars.from_digit does. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let numeric_value = func(code_point: uint32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L652)
  Java's getNumericValue over the Unicode Numeric_Value property: 10 through 35 for A through Z and a through z in ASCII or fullwidth form; the value when it is an integer from 0 through 2^31 - 1, such as 50 for U+216C ROMAN NUMERAL FIFTY; -2 for a fraction, a negative value, or a larger value; and -1 when there is none. The CJK ideographs with Unihan numeric values have them, such as 5 for U+4E94. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let numeric_value_text = func(code_point: uint32) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L657)
  The exact Unicode Numeric_Value as text, an integer or a fraction such as "1/2" or "-1/2", or None when there is none. Latin letters have none. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let get_name = func(code_point: uint32) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L668)
  The Unicode Name property of a code point, with Java's getName fallback for defined code points that have none. Tagged `#ThreadSafe`.
- **Character** · [`export let code_point_of = func(name: string) -> Optional<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L677)
  The code point with a Unicode name, matched without regard to case (Java's codePointOf). Tagged `#ThreadSafe`.
- **Character** · [`export let is_valid_code_point = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L687)
  Tests whether a value is at most 0x10FFFF (Java's isValidCodePoint). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_scalar_value = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L691)
  Tests for a Unicode scalar value: at most 0x10FFFF and not a surrogate. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_bmp = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L695)
  Tests for a code point below U+10000 (Java's isBmpCodePoint). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_supplementary = func(code_point: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L699)
  Tests for U+10000 through U+10FFFF (Java's isSupplementaryCodePoint). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_surrogate = func(unit: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L704)
  Tests for U+D800 through U+DFFF (Java's isSurrogate). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_high_surrogate = func(unit: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L708)
  Tests for U+D800 through U+DBFF (Java's isHighSurrogate). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_low_surrogate = func(unit: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L712)
  Tests for U+DC00 through U+DFFF (Java's isLowSurrogate). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let is_surrogate_pair = func(high: uint32, low: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L716)
  Tests a high surrogate followed by a low surrogate (Java's isSurrogatePair). Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let to_code_point = func(high: uint32, low: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L722)
  The supplementary code point of a surrogate pair (Java's toCodePoint). Unlike Java, which does not validate, a pair that is not one traps with "invalid argument: not a surrogate pair". Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let high_surrogate = func(code_point: uint32) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L732)
  The high surrogate of a supplementary code point (Java's highSurrogate). Unlike Java, which returns an unspecified unit, another value traps with "invalid argument: not a supplementary code point". Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let low_surrogate = func(code_point: uint32) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L739)
  The low surrogate of a supplementary code point (Java's lowSurrogate). Traps as high_surrogate. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let char_count = func(code_point: uint32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L747)
  The number of UTF-16 code units of a code point: 2 from U+10000, otherwise 1. Like Java's charCount it does not validate the value. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let to_chars = func(code_point: uint32) -> uint16[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L753)
  The UTF-16 code units of a code point, one for a BMP code point (a surrogate code point gives itself) and a surrogate pair from U+10000 (Java's toChars). A value above 0x10FFFF traps with "invalid argument: not a Unicode code point". Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Character** · [`export let utf8_length = func(code_point: uint32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L769)
  The number of UTF-8 bytes of a scalar value, 1 through 4. Traps as `of` does. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let utf16_length = func(code_point: uint32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L776)
  The number of UTF-16 code units of a scalar value, 1 or 2. Traps as `of` does. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let to_utf8 = func(code_point: uint32) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L783)
  The UTF-8 bytes of a scalar value. Traps as `of` does. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Character** · [`export let code_point_at = func(units: uint16[list], index: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L789)
  The code point at a UTF-16 index: a surrogate pair's code point when a high surrogate there is followed by a low surrogate, otherwise the unit itself (Java's codePointAt). An index outside the list traps as a list read does. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let code_point_before = func(units: uint16[list], index: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L803)
  The code point that ends before a UTF-16 index (Java's codePointBefore): a surrogate pair's code point when the units at index - 2 and index - 1 form one, otherwise the unit at index - 1. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let code_point_count = func(units: uint16[list], start: int, end: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L818)
  The number of code points in units start through end - 1, a surrogate pair counting once and an unpaired surrogate counting as one code point (Java's codePointCount). Traps with "invalid argument: range outside the list" unless 0 <= start <= end <= the length. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let offset_by_code_points = func(units: uint16[list], index: int, offset: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L840)
  The UTF-16 index `offset` code points away from `index`, forward for a positive offset and backward for a negative one (Java's offsetByCodePoints). Traps with "invalid argument: offset outside the list" when the move leaves the list. Tagged `#Pure`, `#ThreadSafe`.
- **Character** · [`export let reverse_bytes = func(unit: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/character.nra#L865)
  The unit with its two bytes swapped (Java's reverseBytes(char)). Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 8.5 `Character`

`Std.Core.Character` is Java's `Character` over Unicode 17.0.0, the version of the `String` case tables, in the library's snake_case names. It has the two shapes Java has:

- **Module functions on code points.** Each takes a code point as a `uint32`, as Java's static methods take an `int`, and is total over `uint32`: a value above 0x10FFFF has the properties of an unassigned code point, and only the members that build text, UTF-8, or UTF-16 trap for it. The surrogate code points U+D800 through U+DFFF have the category `Surrogate`. There is still no `char` type ([section 8.3](Stdlib-Design#83-scalars)).
- **The class `Character`**, an `#Immutable` value holding one Unicode scalar value, the get-only field `code_point`. `of(code_point)` and `new Character(code_point)` trap with `invalid argument: not a Unicode scalar value` for a surrogate or a value above 0x10FFFF, because a `Character`'s text must be one character; `try_of` returns `None` instead, and `from_text(text)` gives the character of a string that holds exactly one scalar value. `equals` compares the code points, `hash_code` is the code point (Java's `hashCode` is the `char` value, and a `uint32` hashes to itself, [section 5.2](Stdlib-Design#52-equality-hashing-and-text)), `compare_to` orders by code point, and `to_string` is the character. Its methods apply the common module functions to the value: the classification predicates through `is_mirrored`, `get_type`, `get_directionality`, the case mappings (which return `Character`), `digit`, `numeric_value`, `char_count`, and `utf8_length`. The identifier, emoji, and `bidi_class` functions have no method; call them with `code_point`.

| Member | Behavior | Java |
| --- | --- | --- |
| `MinCodePoint`, `MaxCodePoint`, `MinSupplementaryCodePoint`, `Min`/`MaxHighSurrogate`, `Min`/`MaxLowSurrogate`, `Min`/`MaxSurrogate`, `MinRadix`, `MaxRadix`, `UnicodeVersion` | 0, 0x10FFFF, 0x10000, the surrogate bounds, 2, 36, and `"17.0.0"`. | The constants of the same names |
| `get_type(cp) → GeneralCategory`, `category_code(c)` | The General_Category, an enum in Unicode's order (`UppercaseLetter` … `Unassigned`), and its two-letter code. | `getType` |
| `get_directionality(cp) → BidiClass` | The Bidi_Class of a defined code point; `Undefined` when the category is `Unassigned`, including noncharacters. | `getDirectionality` |
| `bidi_class(cp)`, `bidi_code(b)` | The Bidi_Class with the UCD's defaults for unassigned code points (`RightToLeft` in the Hebrew block, `BoundaryNeutral` for noncharacters), and its abbreviation. | |
| `is_upper`, `is_lower` | The Uppercase and Lowercase properties: category Lu or Ll, or Other_Uppercase or Other_Lowercase. | `isUpperCase`, `isLowerCase` |
| `is_title_case`, `is_letter`, `is_digit`, `is_letter_or_digit` | Category Lt; L; Nd; L or Nd. | `isTitleCase`, `isLetter`, `isDigit`, `isLetterOrDigit` |
| `is_alphabetic`, `is_ideographic`, `is_mirrored` | The Alphabetic, Ideographic, and Bidi_Mirrored properties. | `isAlphabetic`, `isIdeographic`, `isMirrored` |
| `is_alpha`, `is_numeric`, `is_alpha_numeric` | A letter; a code point with a Unicode Numeric_Value (any Numeric_Type but None, including fractions, Roman numerals, and CJK ideographs with Unihan values); either. Python's `str.isalpha`, `isnumeric`, and `isalnum` for one character. | |
| `is_whitespace` | Java's set: category Zs, Zl, or Zp except U+00A0, U+2007, and U+202F, and U+0009 through U+000D and U+001C through U+001F. The `String` companion's strip and indentation use it. | `isWhitespace` |
| `is_space_char` | Category Zs, Zl, or Zp. | `isSpaceChar` |
| `is_space` | The White_Space property: U+0009 through U+000D, U+0020, U+0085, U+00A0, and the separators. | Not Java's deprecated `isSpace`, which tests five ASCII characters |
| `is_iso_control`, `is_defined` | U+0000 through U+001F and U+007F through U+009F; a category other than `Unassigned` (surrogates and private use are defined, noncharacters are not). | `isISOControl`, `isDefined` |
| `is_identifier_start`, `is_identifier_part`, `is_identifier_ignorable` | Java's profile of UAX #31: ID_Start and U+2E2F; ID_Continue, U+2E2F, and the ignorable set; U+0000 through U+0008, U+000E through U+001B, U+007F through U+009F, and category Cf. | `isUnicodeIdentifierStart`, `isUnicodeIdentifierPart`, `isIdentifierIgnorable` |
| `is_java_identifier_start`, `is_java_identifier_part` | A letter or category Nl, Sc, or Pc; a letter, category Sc, Pc, Nd, Nl, Mc, or Mn, or the ignorable set. | `isJavaIdentifierStart`, `isJavaIdentifierPart` |
| `is_emoji`, `is_emoji_presentation`, `is_emoji_modifier`, `is_emoji_modifier_base`, `is_emoji_component`, `is_extended_pictographic` | The UTS #51 properties. | `isEmoji` … `isExtendedPictographic` |
| `to_upper`, `to_lower`, `to_title` `→ uint32` | The simple one-to-one mappings of UnicodeData.txt; the code point itself when there is none. The titlecase mapping is the uppercase one where UnicodeData.txt gives none. | `toUpperCase`, `toLowerCase`, `toTitleCase` |
| `to_upper_string(cp)`, `to_lower_string(cp)`, each also with a language tag | The full mappings of `String.to_upper_case` and `to_lower_case` ([String](StandardLibrary#unicode-case-and-locale)): U+00DF gives `"SS"`, U+0130 gives `i` and U+0307, and `"tr"` maps `i` to U+0130. A lone U+03A3 has no cased letter before it and maps to U+03C3. | `String.toUpperCase` on one character |
| `digit(cp, radix) → int` | The decimal digit value of a category Nd code point, or 10 through 35 for A through Z and a through z in ASCII or fullwidth form, when below the radix; otherwise -1. | `digit`; Java returns -1 for a bad radix, this traps |
| `for_digit(value, radix) → uint32` | The lowercase ASCII digit. | `forDigit`; Java returns U+0000 for bad input, this traps |
| `numeric_value(cp) → int` | 10 through 35 for the Latin letters as in `digit`; the Numeric_Value when it is an integer from 0 through 2^31 - 1; -2 for a fraction, a negative value, or a larger value; -1 for none. | `getNumericValue`, over the full Numeric_Value property |
| `numeric_value_text(cp) → Optional<string>` | The exact Numeric_Value, such as `"1/2"`, `"-1/2"`, or `"1000000000000"`. | |
| `is_valid_code_point`, `is_scalar_value`, `is_bmp`, `is_supplementary` | At most 0x10FFFF; and not a surrogate; below 0x10000; 0x10000 through 0x10FFFF. | `isValidCodePoint`, `isBmpCodePoint`, `isSupplementaryCodePoint` |
| `is_surrogate`, `is_high_surrogate`, `is_low_surrogate`, `is_surrogate_pair(high, low)` | Range tests on a `uint32` unit. | The methods of the same names |
| `to_code_point(high, low)`, `high_surrogate(cp)`, `low_surrogate(cp)` | Surrogate pair conversion. A pair that is not one, or a code point that is not supplementary, traps. | `toCodePoint`, `highSurrogate`, `lowSurrogate`, which do not validate |
| `char_count(cp)`, `to_chars(cp) → uint16[list]` | The UTF-16 units: one below 0x10000, where a surrogate code point gives itself, and a pair above. `char_count` does not validate; `to_chars` traps above 0x10FFFF. | `charCount`, `toChars` |
| `code_point_at(units, i)`, `code_point_before(units, i)`, `code_point_count(units, start, end)`, `offset_by_code_points(units, i, n)` | The `uint16[list]` forms. An unpaired surrogate counts as one code point. A range or a move outside the list traps. | The `char[]` forms |
| `utf8_length(cp)`, `utf16_length(cp)`, `to_utf8(cp)` | Byte and unit counts and the UTF-8 bytes of a scalar value. | |
| `to_string(cp)`, `hash(cp)`, `compare(a, b) → Ordering`, `reverse_bytes(unit: uint16)` | The one-character string (traps unless a scalar value), the code point, numeric order, and the unit with its bytes swapped. | `toString(int)`, `hashCode(char)`, `compare`, `reverseBytes` |
| `get_name(cp)`, `code_point_of(name)` | Stubs: they trap with `unsupported: Std.Core.Character.get_name is not implemented in Neura 0.3.0 (the Unicode name table is not bundled)` and the same for `code_point_of`. The name table is about 0.9 MB of text for 35,000 ranges. | `getName`, `codePointOf` |

**Traps.** Each message follows `invalid argument: `: `not a Unicode scalar value` (`of`, `new Character`, `to_string`, `to_upper_string`, `to_lower_string`, `utf8_length`, `utf16_length`, `to_utf8`), `not a Unicode code point` (`to_chars`), `not a surrogate pair` (`to_code_point`), `not a supplementary code point` (`high_surrogate`, `low_surrogate`), `radix outside 2 through 36` (`digit`, `for_digit`), `digit out of range` (`for_digit`), `range outside the list` (`code_point_count`), `offset outside the list` (`offset_by_code_points`), and `NoBlock has no range` (`UnicodeBlock.first` and `last`). A UTF-16 index outside the list in `code_point_at` and `code_point_before` is the list read's own trap.

**Not provided.** Java members that have no counterpart, and why: `MIN_VALUE`, `MAX_VALUE`, `SIZE`, `BYTES`, `charValue`, `valueOf(char)`, and `hashCode(char)` concern the 16-bit `char`, which Neura does not have (`code_point`, `of`, and `hash` take their place); the deprecated `isJavaLetter`, `isJavaLetterOrDigit`, and `isSpace`; `toChars(int, char[], int)` and the `char[]` overloads with an offset, count, start, or limit, since a list slice serves; `codePointAt(CharSequence, int)` and its relatives, which are `String.code_point_at` and its relatives on strings; `describeConstable`; and the deprecated constructor.

`Std.Core.UnicodeScript` and `Std.Core.UnicodeBlock` are Java's `Character.UnicodeScript` and `Character.UnicodeBlock`. Each exports a generated enum, `UnicodeScript` with one variant per Script value sorted by long name, and `UnicodeBlock` with `NoBlock` and the blocks of Blocks.txt in code point order, and the module functions `of(cp)` (`Unknown` or `NoBlock` for a code point outside every script or block and above 0x10FFFF), `name(v)` (`Old_Italic`, `Latin-1 Supplement`), `for_name(text) → Optional` (long names, codes, and aliases under the loose matching of UAX #44: ASCII case, spaces, underscores, and hyphens are ignored), and `all()`. `UnicodeScript.code(v)` is the ISO 15924 code, and `UnicodeBlock.first(v)` and `last(v)` are a block's bounds, which trap for `NoBlock`.

<!-- example: check -->
```neura
let character = import:Std.Core.Character;
let unicode_script = import:Std.Core.UnicodeScript;

let sigma = character.of(931);
let lower = sigma.to_lower();                        // σ, U+03C3
let category = character.get_type(453);             // GeneralCategory.TitlecaseLetter
let title = character.to_title(454);                 // U+01C5
let sharp_s = character.to_upper_string(223);        // "SS"
let fifty = character.numeric_value(8556);           // 50, ROMAN NUMERAL FIFTY
let script = unicode_script.of(1488);                // UnicodeScript.Hebrew
```

**Data and lookup.** `scripts/generate-unicode-tables.py` writes the tables from the Unicode Character Database 17.0.0 as Perl's `Unicode::UCD` exposes it; it checks the version and the SHA-256 of the data it reads, and `--check` compares the checked-in files. Each table is a string of printable bytes, six bits each: a range table holds the first code point of each range with its value, and a boundary table the boundaries of a property's inversion list. A lookup bisects the table, so it reads about log2(ranges) entries: 4,144 General_Category ranges, 1,268 Bidi_Class ranges, 1,510 case ranges, 2,215 numeric ranges, and between 2 and 1,598 boundaries for each binary property. The tables come to about 88 KB of source; a program that does not call a member does not carry its table.

**Verification.** `tests/stdlib/character` checks every code point from 0 through 0x10FFFF, for each of 38 module functions (every predicate, `get_type`, `get_directionality`, `bidi_class`, the three simple case mappings, `digit` with radix 36, `numeric_value`, and the script and block), against digests that its generator computes per code point from the unpacked UCD data; it adds targeted cases (ASCII, Latin-1, final sigma, dotless i, titlecase digraphs, surrogates, private use, noncharacters, unassigned code points, and the largest code point) and spot checks of `numeric_value_text`, the full-case strings, and `digit` at other radices. This proves the packing and the lookups against the Perl 17.0.0 data. Before writing the test, the generator checks that data against three other sources and stops on any difference it does not list: Python's `unicodedata` (Unicode 15.1.0) for every code point assigned by 15.1, for the category, bidi class, mirroring, decimal and numeric values, `isalpha`, `isnumeric`, `isalnum`, `isupper`, `islower`, and the one-to-one upper, lower, and title mappings (the listed differences are 16.0 and 17.0 changes); Perl's regular expression engine for every code point and the binary properties (Alphabetic, Uppercase, Lowercase, Ideographic, ID_Start, ID_Continue, Bidi_Mirrored, White_Space, the six emoji properties) and the categories L, Lt, Nd, and Cn; and the simple case mappings against the 17.0.0 case tables of `String`, built from the unmodified UCD files. The script and block tables and the Unicode 16.0 and 17.0 additions to the Bidi_Class and numeric tables have no source other than the Perl data. Parity with Java is by definition, taken from the Java SE 25 documentation: no Java runtime is part of the tests.
