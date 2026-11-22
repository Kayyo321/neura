internal static class HashMap
{
    private const long Mask = 1073741823L;

    public static string Run(int n)
    {
        // As in LinkedHashMap, retain insertion order and remove entries
        // in constant time without relying on Dictionary enumeration order.
        var entries = new Dictionary<long, LinkedListNode<(long Key, long Value)>>();
        var order = new LinkedList<(long Key, long Value)>();
        for (int i = 0; i < n; i++)
        {
            long key = (long)i * 7919;
            entries.Add(key, order.AddLast((key, i)));
        }
        long total = 0;
        for (int i = 0; i < n; i++)
        {
            long key = (long)i * 7919;
            total += entries.TryGetValue(key, out var entry) ? entry.Value.Value : -1;
        }
        for (int i = 0; i < n; i += 2)
        {
            long key = (long)i * 7919;
            entries[key].Value = (key, 0);
        }
        for (int i = 0; i < n; i += 4)
        {
            long key = (long)i * 7919;
            if (entries.Remove(key, out var entry))
                order.Remove(entry);
        }
        long checksum = 0;
        foreach (var (key, value) in order)
            checksum = (checksum * 31 + key * 3 + value) & Mask;

        return $"hash-map {entries.Count} {total} {checksum}\n";
    }
}
