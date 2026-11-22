// Same algorithm, input, and output as the Java and Neura versions.
internal static class Sieve
{
    public static string Run(int n)
    {
        var composite = new bool[n + 1];
        int count = 0;
        long sum = 0;
        for (int i = 2; i <= n; i++)
        {
            if (composite[i])
                continue;
            count++;
            sum += i;
            for (long multiple = (long)i * i; multiple <= n; multiple += i)
                composite[(int)multiple] = true;
        }
        return $"sieve {count} {sum}\n";
    }
}
