// Prints Character.codePointOf for the texts of the targeted name tests in character-tests.nra, as
// "[text]<TAB>hex code point" or "[text]<TAB>rejected", and getName of the code points they use:
//
//     java JavaCodePointOf.java
//
// Recorded with OpenJDK 17.0.21. Texts that name code points Unicode 13.0 did not assign, and the Unicode
// names that only Neura's code_point_of accepts, print "rejected" here.
public class JavaCodePointOf {
    public static void main(String[] args) {
        String[] texts = {
            "LATIN CAPITAL LETTER A", "null", "\tnull\n", " latin small letter a ", "line feed (lf)", "BEL", "BELL",
            "PADDING CHARACTER", "latin 1 supplement 84", "zero width no-break space", "Grinning Face",
            "CJK UNIFIED IDEOGRAPHS 4E00", "cjk unified ideographs 4e00", "HANGUL SYLLABLES AC00", "tangut 17000",
            "PRIVATE USE AREA E000 ", "private use area e000", "lat\u0131n small letter a", "\u017Fpace",
            "cro\u00DF mark", "\uFB01gure dash", "LATIN CAPITAL LETTER \u00C9",
            "CJK UNIFIED IDEOGRAPH-4E00", "hangul syllable ga", "HANGUL SYLLABLE GAG", "HANGUL SYLLABLE A",
            "TANGUT IDEOGRAPH-18D00", "TANGUT IDEOGRAPH-17000", "CJK COMPATIBILITY IDEOGRAPH-F900",
            "", "  ", "LATIN 1 SUPPLEMENT 80", "BASIC LATIN 0", "HANGUL SYLLABLES 0AC00", "HANGUL SYLLABLES AC0",
            "PRIVATE USE AREA 0E000", "PRIVATE USE AREA  E000", "CJK UNIFIED IDEOGRAPHS +4E00",
            "CJK UNIFIED IDEOGRAPHS 3400", "CJK UNIFIED IDEOGRAPH-04E00", "CJK UNIFIED IDEOGRAPH-4E0",
            "BASIC LATIN 378", "GREEK AND COPTIC 378", "LINE FEED", "LF", "BYTE ORDER MARK", "latin_small_letter_a",
            "LATIN  SMALL LETTER A", "LATIN SMALL LETTER", "HANGUL SYLLABLE", "HANGUL SYLLABLE GAX",
            "TANGUT IDEOGRAPH SUPPLEMENT-18D00", "TANGUT IDEOGRAPH-18D1F", "NONCHARACTER-FFFF", "TANGUT 18D00",
            "LATIN CAPITAL LETTER NOT A NAME",
        };
        for (String text : texts) {
            String shown = text.replace("\t", "\\t").replace("\n", "\\n");
            try {
                System.out.println("[" + shown + "]\t" + Integer.toHexString(Character.codePointOf(text)).toUpperCase());
            } catch (IllegalArgumentException e) {
                System.out.println("[" + shown + "]\trejected");
            }
        }
        int[] points = {0x0, 0x7, 0xA, 0x41, 0x80, 0x84, 0x378, 0xAC00, 0xD7A3, 0xD800, 0xE000, 0x4E00, 0xF900,
                        0xFEFF, 0xFFFE, 0xFFFF, 0x13460, 0x17000, 0x18D00, 0x1B170, 0x1F514, 0x1F600, 0x10FFFD};
        for (int point : points) {
            System.out.println(Integer.toHexString(point).toUpperCase() + "\t" + Character.getName(point));
        }
    }
}
