// Same operation order as the Java and Neura versions.
internal static class SpectralNorm
{
    private static double Entry(int i, int j) => 1.0 / ((i + j) * (i + j + 1) / 2 + i + 1);

    private static void Multiply(int n, double[] vector, double[] destination)
    {
        for (int i = 0; i < n; i++)
        {
            double sum = 0.0;
            for (int j = 0; j < n; j++)
                sum += Entry(i, j) * vector[j];
            destination[i] = sum;
        }
    }

    private static void MultiplyTransposed(int n, double[] vector, double[] destination)
    {
        for (int i = 0; i < n; i++)
        {
            double sum = 0.0;
            for (int j = 0; j < n; j++)
                sum += Entry(j, i) * vector[j];
            destination[i] = sum;
        }
    }

    public static string Run(int n)
    {
        var u = new double[n];
        var v = new double[n];
        var scratch = new double[n];
        Array.Fill(u, 1.0);
        for (int round = 0; round < 10; round++)
        {
            Multiply(n, u, scratch);
            MultiplyTransposed(n, scratch, v);
            Multiply(n, v, scratch);
            MultiplyTransposed(n, scratch, u);
        }

        double vbv = 0.0;
        double vv = 0.0;
        for (int i = 0; i < n; i++)
        {
            vbv += u[i] * v[i];
            vv += v[i] * v[i];
        }
        ulong bits = BitConverter.DoubleToUInt64Bits(Math.Sqrt(vbv / vv));
        return $"spectral-norm {bits:x}\n";
    }
}
