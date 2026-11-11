// Same algorithm, input, and output as ../neura/fannkuch.nra.
public final class Fannkuch {
    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        int[] perm = new int[n];
        int[] perm1 = new int[n];
        int[] count = new int[n];
        for (int i = 0; i < n; i = i + 1) {
            perm1[i] = i;
        }
        int maxFlips = 0;
        int checksum = 0;
        int permutation = 0;
        int r = n;
        while (true) {
            while (r != 1) {
                count[r - 1] = r;
                r = r - 1;
            }
            for (int i = 0; i < n; i = i + 1) {
                perm[i] = perm1[i];
            }
            int flips = 0;
            int k = perm[0];
            while (k != 0) {
                int half = (k + 1) / 2;
                for (int i = 0; i < half; i = i + 1) {
                    int swapped = perm[i];
                    perm[i] = perm[k - i];
                    perm[k - i] = swapped;
                }
                flips = flips + 1;
                k = perm[0];
            }
            if (flips > maxFlips) {
                maxFlips = flips;
            }
            if (permutation % 2 == 0) {
                checksum = checksum + flips;
            } else {
                checksum = checksum - flips;
            }
            // Rotate prefixes of perm1 to reach the next permutation.
            while (true) {
                if (r == n) {
                    System.out.println("fannkuch " + checksum + " " + maxFlips);
                    return;
                }
                int first = perm1[0];
                for (int i = 0; i < r; i = i + 1) {
                    perm1[i] = perm1[i + 1];
                }
                perm1[r] = first;
                count[r] = count[r] - 1;
                if (count[r] > 0) {
                    break;
                }
                r = r + 1;
            }
            permutation = permutation + 1;
        }
    }
}
