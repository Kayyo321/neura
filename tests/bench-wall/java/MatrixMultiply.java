// Same algorithm, input, and output as ../neura/matrix-multiply.nra.
public final class MatrixMultiply {
    private static final long MASK = 1073741823L;

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        long[] a = new long[n * n];
        long[] b = new long[n * n];
        long[] c = new long[n * n];
        for (int i = 0; i < n; i = i + 1) {
            for (int j = 0; j < n; j = j + 1) {
                a[i * n + j] = (i * 7 + j * 3) % 100;
                b[i * n + j] = (i * 5 + j * 11) % 100;
            }
        }
        for (int i = 0; i < n; i = i + 1) {
            for (int j = 0; j < n; j = j + 1) {
                long sum = 0;
                for (int k = 0; k < n; k = k + 1) {
                    sum = sum + a[i * n + k] * b[k * n + j];
                }
                c[i * n + j] = sum;
            }
        }
        long checksum = 0;
        for (int i = 0; i < n * n; i = i + 1) {
            checksum = (checksum * 31 + c[i]) & MASK;
        }
        System.out.println("matrix-multiply " + checksum);
    }
}
