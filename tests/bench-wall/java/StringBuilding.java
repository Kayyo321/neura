// Same algorithm, input, and output as ../neura/string-building.nra.
import java.nio.charset.StandardCharsets;

public final class StringBuilding {
    private static final long MASK = 1073741823L;

    public static void main(String[] args) {
        System.out.print(workload(Integer.parseInt(args[0])));
    }

    public static String workload(int n) {
        StringBuilder builder = new StringBuilder();
        for (int i = 0; i < n; i = i + 1) {
            builder.append("item ").append((long) i).append(";");
        }
        byte[] text = builder.toString().getBytes(StandardCharsets.UTF_8);
        long checksum = 0;
        for (int i = 0; i < text.length; i = i + 1) {
            checksum = (checksum * 31 + (text[i] & 0xff)) & MASK;
        }
        return "string-building " + text.length + " " + checksum + System.lineSeparator();
    }
}
