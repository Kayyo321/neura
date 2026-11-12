// Same algorithm, input, and output as ../neura/text-split.nra. The text is
// ASCII, so a char is a byte. String.split(",", -1) is the counterpart of
// Std.Text.Strings.split: with a one-character separator that is not a
// regular expression metacharacter it searches with indexOf and takes
// substrings, as the Neura library searches and slices, and the limit -1
// keeps trailing empty pieces, as split does. trim removes the spaces that
// Strings.trim removes, and String.join joins as Strings.join does.
import java.util.ArrayList;

public final class TextSplit {
    private static final int FIELDS = 8;
    private static final long MASK = 1073741823L;
    private static final String[] WORDS = {"alpha", "beta", "gamma", "delta"};

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        long seed = 12345;
        int matches = 0;
        long bytes = 0;
        long checksum = 0;
        for (int i = 0; i < n; i = i + 1) {
            StringBuilder line = new StringBuilder();
            for (int field = 0; field < FIELDS; field = field + 1) {
                seed = (seed * 1103515245L + 12345) & 2147483647L;
                if (field > 0) {
                    line.append(",");
                }
                for (long pad = 0; pad < seed % 3; pad = pad + 1) {
                    line.append(" ");
                }
                if (seed % 4 == 0) {
                    line.append(WORDS[(int) ((seed >> 8) % 4)]);
                } else {
                    line.append((seed >> 8) % 200000 - 100000);
                }
                if ((seed >> 4) % 2 == 0) {
                    line.append("  ");
                }
            }
            String[] parts = line.toString().split(",", -1);
            ArrayList<String> fields = new ArrayList<>();
            for (int k = 0; k < parts.length; k = k + 1) {
                String trimmed = parts[k].trim();
                if (trimmed.equals("beta")) {
                    matches = matches + 1;
                }
                fields.add(trimmed);
            }
            String joined = String.join("|", fields);
            bytes = bytes + (long) joined.length();
            for (int k = 0; k < joined.length(); k = k + 1) {
                checksum = (checksum * 31 + (long) joined.charAt(k)) & MASK;
            }
        }
        System.out.println("text-split " + matches + " " + bytes + " " + checksum);
    }
}
