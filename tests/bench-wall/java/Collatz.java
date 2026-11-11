// Same algorithm, input, and output as ../neura/collatz.nra.
public final class Collatz {
    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        int bestStart = 0;
        long bestLength = 0;
        long total = 0;
        for (int start = 1; start <= n; start = start + 1) {
            long value = start;
            long length = 1;
            while (value != 1) {
                if (value % 2 == 0) {
                    value = value / 2;
                } else {
                    value = 3 * value + 1;
                }
                length = length + 1;
            }
            total = total + length;
            if (length > bestLength) {
                bestLength = length;
                bestStart = start;
            }
        }
        System.out.println("collatz " + bestStart + " " + bestLength + " " + total);
    }
}
