// Same workload as the Java and Neura versions, using List<T> for the generations.
internal static class ObjectChurn
{
    private const int Batch = 256;
    private const int Lifetime = 64;
    private const long Mask = 1073741823L;

    private sealed class Particle(long value, int born)
    {
        public readonly long Value = value;
        public readonly int Born = born;
        public int Visits;
    }

    private sealed class Lcg
    {
        private long seed = 12345;

        public long Next()
        {
            seed = (seed * 1103515245L + 12345) & 2147483647L;
            return seed;
        }
    }

    public static string Run(int n)
    {
        var random = new Lcg();
        var survivors = new List<Particle>();
        long checksum = 0;
        for (int round = 0; round < n; round++)
        {
            var generation = new List<Particle>();
            for (int i = 0; i < survivors.Count; i++)
                generation.Add(survivors[i]);
            int firstNew = generation.Count;
            for (int i = 0; i < Batch; i++)
                generation.Add(new Particle(random.Next() % 100000, round));
            for (int i = firstNew; i < generation.Count; i += 3)
                generation[i] = new Particle(random.Next() % 100000, round);

            var next = new List<Particle>();
            long total = 0;
            for (int i = 0; i < generation.Count; i++)
            {
                Particle particle = generation[i];
                particle.Visits++;
                if (particle.Value % 16 == 0 && round - particle.Born < Lifetime)
                {
                    next.Add(particle);
                    total = (total + particle.Value + particle.Visits) & Mask;
                }
            }
            survivors = next;
            checksum = (checksum * 31 + survivors.Count + total) & Mask;
        }
        return $"object-churn {survivors.Count} {checksum}\n";
    }
}
