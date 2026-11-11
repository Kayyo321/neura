// Same algorithm, input, and output as ../neura/spectral-norm.nra.
public final class SpectralNorm {
    private static double entry(int i, int j) {
        return 1.0 / (double) ((i + j) * (i + j + 1) / 2 + i + 1);
    }

    // out = A v
    private static void multiply(int n, double[] v, double[] out) {
        for (int i = 0; i < n; i = i + 1) {
            double sum = 0.0;
            for (int j = 0; j < n; j = j + 1) {
                sum = sum + entry(i, j) * v[j];
            }
            out[i] = sum;
        }
    }

    // out = A^T v
    private static void multiplyTransposed(int n, double[] v, double[] out) {
        for (int i = 0; i < n; i = i + 1) {
            double sum = 0.0;
            for (int j = 0; j < n; j = j + 1) {
                sum = sum + entry(j, i) * v[j];
            }
            out[i] = sum;
        }
    }

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        double[] u = new double[n];
        double[] v = new double[n];
        double[] scratch = new double[n];
        java.util.Arrays.fill(u, 1.0);
        for (int round = 0; round < 10; round = round + 1) {
            multiply(n, u, scratch);
            multiplyTransposed(n, scratch, v);
            multiply(n, v, scratch);
            multiplyTransposed(n, scratch, u);
        }
        double vbv = 0.0;
        double vv = 0.0;
        for (int i = 0; i < n; i = i + 1) {
            vbv = vbv + u[i] * v[i];
            vv = vv + v[i] * v[i];
        }
        double norm = Math.sqrt(vbv / vv);
        System.out.println("spectral-norm " + Long.toHexString(Double.doubleToRawLongBits(norm)));
    }
}
