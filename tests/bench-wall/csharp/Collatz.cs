internal static class Collatz
{
    public static string Run(int n)
    {
        int bestStart = 0;
        long bestLength = 0;
        long total = 0;
        for (int start = 1; start <= n; start++)
        {
            long value = start;
            long length = 1;
            while (value != 1)
            {
                value = value % 2 == 0 ? value / 2 : 3 * value + 1;
                length++;
            }
            total += length;
            if (length > bestLength)
            {
                bestLength = length;
                bestStart = start;
            }
        }
        return $"collatz {bestStart} {bestLength} {total}\n";
    }
}
