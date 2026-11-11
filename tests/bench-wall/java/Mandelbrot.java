// Same algorithm, input, and output as ../neura/mandelbrot.nra.
public final class Mandelbrot {
    private static final int ITERATIONS = 50;
    private static final long MASK = 1073741823L;

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        double size = n;
        int inside = 0;
        long checksum = 0;
        for (int y = 0; y < n; y = y + 1) {
            double ci = 2.0 * (double) y / size - 1.0;
            for (int x = 0; x < n; x = x + 1) {
                double cr = 2.0 * (double) x / size - 1.5;
                double zr = 0.0;
                double zi = 0.0;
                double tr = 0.0;
                double ti = 0.0;
                int i = 0;
                while (i < ITERATIONS && tr + ti <= 4.0) {
                    zi = 2.0 * zr * zi + ci;
                    zr = tr - ti + cr;
                    tr = zr * zr;
                    ti = zi * zi;
                    i = i + 1;
                }
                long bit = 0;
                if (tr + ti <= 4.0) {
                    bit = 1;
                    inside = inside + 1;
                }
                checksum = (checksum * 31 + bit) & MASK;
            }
        }
        System.out.println("mandelbrot " + inside + " " + checksum);
    }
}
