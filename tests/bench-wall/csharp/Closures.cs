internal static class Closures
{
    private const int Values = 1000;
    private const long Mask = 1073741823L;

    private static List<long> MapValues(List<long> values, Func<long, long> transform)
    {
        var mapped = new List<long>(8);
        for (int i = 0; i < values.Count; i++)
            mapped.Add(transform(values[i]));

        return mapped;
    }

    private static List<long> FilterValues(List<long> values, Predicate<long> keep)
    {
        var kept = new List<long>(8);
        for (int i = 0; i < values.Count; i++)
            if (keep(values[i]))
                kept.Add(values[i]);

        return kept;
    }

    private static long FoldValues(List<long> values, long initial, Func<long, long, long> combine)
    {
        long total = initial;
        for (int i = 0; i < values.Count; i++)
            total = combine(total, values[i]);

        return total;
    }

    private static Func<long, long> MakeStep(long factor, long offset)
    {
        return value => (value * factor + offset) & Mask;
    }

    public static string Run(int n)
    {
        var values = new List<long>(8);
        long seed = 12345;
        for (int i = 0; i < Values; i++)
        {
            seed = (seed * 1103515245L + 12345) & 2147483647L;
            values.Add(seed % 100000);
        }
        long checksum = 0;
        for (int round = 0; round < n; round++)
        {
            long offset = round;
            long threshold = round % 10;
            long weight = round % 7 + 1;
            List<long> shifted = MapValues(values, value => value + offset);
            List<long> kept = FilterValues(shifted, value => value % 10 != threshold);
            long total = FoldValues(kept, 0, (sum, value) => (sum + value * weight) & Mask);
            Func<long, long> step = MakeStep(weight + 2, offset);
            long walked = total;
            for (int i = 0; i < 100; i++)
                walked = step(walked);

            checksum = (checksum * 31 + kept.Count + total + walked) & Mask;
        }
        return $"closures {checksum}\n";
    }
}
