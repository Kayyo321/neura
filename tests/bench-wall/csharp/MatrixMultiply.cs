internal static class MatrixMultiply
{
    private const long Mask = 1073741823L;

    public static string Run(int n)
    {
        long[] a = new long[n * n];
        long[] b = new long[n * n];
        long[] c = new long[n * n];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
            {
                a[i * n + j] = (i * 7 + j * 3) % 100;
                b[i * n + j] = (i * 5 + j * 11) % 100;
            }

        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
            {
                long sum = 0;
                for (int k = 0; k < n; k++)
                    sum += a[i * n + k] * b[k * n + j];

                c[i * n + j] = sum;
            }

        long checksum = 0;
        for (int i = 0; i < n * n; i++)
            checksum = (checksum * 31 + c[i]) & Mask;

        return $"matrix-multiply {checksum}\n";
    }
}
