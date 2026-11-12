// Same algorithm, input, and output as ../neura/parse-results.nra. Neura's
// Optional<int> and Optional<int64> become OptionalInt and OptionalLong, and
// Result<int32> becomes a sealed interface with an Ok and an Err record
// matched by a switch; the error carries a kind, an offset, and a message,
// as Std.Core.Error.Parse does.
import java.util.ArrayList;
import java.util.OptionalInt;
import java.util.OptionalLong;

public final class ParseResults {
    private static final int RECORDS = 1000;
    private static final long MASK = 1073741823L;
    private static final String[] KEYS = {"id", "size", "count", "weight"};

    private enum ParseErrorKind { EMPTY, INVALID_CHARACTER, OUT_OF_RANGE }

    private record ParseError(ParseErrorKind kind, int offset, String message) {}

    private sealed interface IntResult permits Ok, Err {}

    private record Ok(int value) implements IntResult {}

    private record Err(ParseError error) implements IntResult {}

    // The offset of the `=` after a non-empty key, or empty.
    private static OptionalInt separatorOf(String line) {
        for (int i = 0; i < line.length(); i = i + 1) {
            if (line.charAt(i) == 61) {
                if (i == 0) {
                    return OptionalInt.empty();
                }
                return OptionalInt.of(i);
            }
        }
        return OptionalInt.empty();
    }

    private static IntResult parseError(ParseErrorKind kind, int offset, String message) {
        return new Err(new ParseError(kind, offset, message));
    }

    // The decimal int in characters start to end of the line: an optional
    // minus sign and one to nine digits.
    private static IntResult parseInt(String line, int start, int end) {
        int at = start;
        boolean negative = false;
        if (at < end && line.charAt(at) == 45) {
            negative = true;
            at = at + 1;
        }
        if (at == end) {
            return parseError(ParseErrorKind.EMPTY, at, "empty number");
        }
        if (end - at > 9) {
            return parseError(ParseErrorKind.OUT_OF_RANGE, at, "too many digits");
        }
        int value = 0;
        while (at < end) {
            char digit = line.charAt(at);
            if (digit < 48 || digit > 57) {
                return parseError(ParseErrorKind.INVALID_CHARACTER, at, "invalid digit");
            }
            value = value * 10 + (digit - 48);
            at = at + 1;
        }
        if (negative) {
            return new Ok(-value);
        }
        return new Ok(value);
    }

    // The weight of a known key, or empty.
    private static OptionalLong keyWeight(String key) {
        if (key.equals("id")) {
            return OptionalLong.of(1);
        }
        if (key.equals("size")) {
            return OptionalLong.of(3);
        }
        if (key.equals("count")) {
            return OptionalLong.of(5);
        }
        if (key.equals("weight")) {
            return OptionalLong.of(7);
        }
        return OptionalLong.empty();
    }

    private static long errorOffset(ParseError failure) {
        return failure.offset();
    }

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        ArrayList<String> lines = new ArrayList<>();
        long seed = 12345;
        for (int i = 0; i < RECORDS; i = i + 1) {
            seed = (seed * 1103515245L + 12345) & 2147483647L;
            String key = KEYS[(int) (seed % 4)];
            long number = (seed >> 4) % 1000000;
            long kind = (seed >> 24) % 10;
            if (kind == 0) {
                lines.add(key + number);
            } else if (kind == 1) {
                lines.add(key + "=" + number + "x");
            } else if (kind == 2) {
                lines.add(key + "=");
            } else if (kind == 3) {
                lines.add("colour=" + number);
            } else if (kind == 4) {
                lines.add(key + "=" + number + "0000000000");
            } else if (kind == 5) {
                lines.add(key + "=-" + number);
            } else {
                lines.add(key + "=" + number);
            }
        }
        int parsed = 0;
        int missing = 0;
        int invalid = 0;
        int unknown = 0;
        long checksum = 0;
        for (int round = 0; round < n; round = round + 1) {
            for (int i = 0; i < lines.size(); i = i + 1) {
                String line = lines.get(i);
                OptionalInt separator = separatorOf(line);
                if (separator.isEmpty()) {
                    missing = missing + 1;
                } else {
                    int at = separator.getAsInt();
                    switch (parseInt(line, at + 1, line.length())) {
                        case Ok ok -> {
                            OptionalLong weight = keyWeight(line.substring(0, at));
                            if (weight.isPresent()) {
                                parsed = parsed + 1;
                                checksum = (checksum * 31 + (long) ok.value() * weight.getAsLong() + (long) round)
                                        & MASK;
                            } else {
                                unknown = unknown + 1;
                            }
                        }
                        case Err err -> {
                            invalid = invalid + 1;
                            checksum = (checksum * 31 + errorOffset(err.error())) & MASK;
                        }
                    }
                }
            }
        }
        System.out.println("parse-results " + parsed + " " + missing + " " + invalid + " " + unknown + " " + checksum);
    }
}
