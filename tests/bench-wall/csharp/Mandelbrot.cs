internal static class Mandelbrot
{
    private const int Iterations = 50;
    private const long Mask = 1073741823L;

    public static string Run(int n)
    {
        double size = n;
        int inside = 0;
        long checksum = 0;
        for (int y = 0; y < n; y++)
        {
            double ci = 2.0 * y / size - 1.0;
            for (int x = 0; x < n; x++)
            {
                double cr = 2.0 * x / size - 1.5;
                double zr = 0.0;
                double zi = 0.0;
                double tr = 0.0;
                double ti = 0.0;
                int i = 0;
                while (i < Iterations && tr + ti <= 4.0)
                {
                    zi = 2.0 * zr * zi + ci;
                    zr = tr - ti + cr;
                    tr = zr * zr;
                    ti = zi * zi;
                    i++;
                }
                long bit = 0;
                if (tr + ti <= 4.0)
                {
                    bit = 1;
                    inside++;
                }
                checksum = (checksum * 31 + bit) & Mask;
            }
        }
        return $"mandelbrot {inside} {checksum}\n";
    }
}
