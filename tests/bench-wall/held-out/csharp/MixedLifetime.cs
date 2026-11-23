// Row 17. The dense page index, LRU list and rolling audit match mixed-lifetime.nra.
using System;
using System.Globalization;

public static class MixedLifetime
{
    private const int AuditSize = 16;
    private const int MaxPages = 4096;
    private const long Mask = 1073741823L;

    private sealed record Snapshot(int Key, long Value);

    private sealed class Entry(int key, Snapshot page)
    {
        public readonly int Key = key;
        public Snapshot Page = page;
        public Entry Previous = null!;
        public Entry Next = null!;
    }

    private static void Unlink(Entry entry)
    {
        entry.Previous.Next = entry.Next;
        entry.Next.Previous = entry.Previous;
    }

    private static long Fold(long checksum, Snapshot sample)
    {
        return (checksum * 31 + sample.Key + sample.Value) & Mask;
    }

    public static string Workload(int requests)
    {
        int capacity = Math.Clamp(requests / 32, 4, MaxPages);
        var empty = new Snapshot(0, 0);
        var root = new Entry(-1, empty);
        root.Previous = root;
        root.Next = root;
        var directory = new Entry[capacity * 4];
        Array.Fill(directory, root);
        var audit = new Snapshot[AuditSize];
        Array.Fill(audit, empty);
        int resident = 0;
        int hits = 0;
        int evictions = 0;
        long checksum = 0;
        long random = 20261123;
        for (int i = 0; i < requests; i++)
        {
            random = (random * 1103515245L + 12345) & 2147483647L;
            int keyRange = i % 4 == 0 ? directory.Length : capacity / 2;
            int key = (int)((random >> 8) % keyRange);
            Entry entry = directory[key];
            bool missed = entry == root;
            if (missed)
            {
                if (resident == capacity)
                {
                    Entry victim = root.Previous;
                    Unlink(victim);
                    directory[victim.Key] = root;
                    evictions++;
                }
                else
                {
                    resident++;
                }
                entry = new Entry(key, empty);
                directory[key] = entry;
            }
            else
            {
                hits++;
                Unlink(entry);
            }
            entry.Next = root.Next;
            entry.Previous = root;
            root.Next.Previous = entry;
            root.Next = entry;

            bool changed = missed || i % 5 == 0;
            long old = entry.Page.Value;
            long value = changed ? (old * 33 + random + key) & Mask : old;
            var sample = new Snapshot(key, value);
            if (changed)
                entry.Page = sample;
            int slot = i % AuditSize;
            checksum = Fold(checksum, audit[slot]);
            audit[slot] = sample;
        }
        foreach (Snapshot sample in audit)
            checksum = Fold(checksum, sample);
        for (Entry cursor = root.Next; cursor != root; cursor = cursor.Next)
            checksum = Fold(checksum, cursor.Page);
        return FormattableString.Invariant($"mixed-lifetime {requests} {resident} {hits} {evictions} {checksum}\n");
    }

    public static int Main(string[] args)
    {
        if (args.Length != 1 || !int.TryParse(args[0], NumberStyles.AllowLeadingSign,
            CultureInfo.InvariantCulture, out int size) || size < 0)
            return 1;
        Console.Write(Workload(size));
        return 0;
    }
}
