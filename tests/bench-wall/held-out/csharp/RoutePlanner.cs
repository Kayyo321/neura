// Row 15. Grid roads and highway links share mutable closure state with the
// road directory. Dijkstra's heap breaks ties in the same order in each twin.
using System;
using System.Collections.Generic;
using System.Globalization;

public static class RoutePlanner
{
    private const int Width = 110;
    private const int Height = 90;
    private const int Highways = 900;
    private const int ClosureInterval = 64;
    private const long Mask = 1073741823L;

    private sealed class Generator(long seed)
    {
        private long state = seed;

        public int Next(int bound)
        {
            state = (state * 1103515245L + 12345L) & 2147483647L;
            return (int)((state >> 4) % bound);
        }
    }

    private sealed class Road(int target, int minutes)
    {
        public int Target { get; } = target;
        public int Minutes { get; } = minutes;
        public bool Open { get; set; } = true;
    }

    private sealed class Intersection(int id)
    {
        public int Id { get; } = id;
        public List<Road> Roads { get; } = new();
    }

    private sealed class RoadNetwork
    {
        public List<Intersection> Intersections { get; } = new();
        public List<Road> AllRoads { get; } = new();

        public RoadNetwork(Generator random)
        {
            for (int i = 0; i < Width * Height; i++)
                Intersections.Add(new Intersection(i));

            for (int y = 0; y < Height; y++)
            {
                for (int x = 0; x < Width; x++)
                {
                    int here = y * Width + x;
                    if (x + 1 < Width && random.Next(10) != 0)
                        Connect(here, here + 1, 5 + random.Next(40));
                    if (y + 1 < Height && random.Next(10) != 0)
                        Connect(here, here + Width, 5 + random.Next(40));
                }
            }

            for (int i = 0; i < Highways; i++)
            {
                int from = random.Next(Intersections.Count);
                int to = random.Next(Intersections.Count);
                if (from != to)
                    Connect(from, to, 60 + random.Next(400));
            }
        }

        private void Connect(int from, int to, int minutes)
        {
            var forward = new Road(to, minutes);
            var backward = new Road(from, minutes);
            Intersections[from].Roads.Add(forward);
            Intersections[to].Roads.Add(backward);
            AllRoads.Add(forward);
            AllRoads.Add(backward);
        }
    }

    private sealed class MinHeap
    {
        private long[] keys = new long[64];
        private int[] values = new int[64];
        public int Count { get; private set; }
        public long TopKey => keys[0];

        public void Clear() => Count = 0;

        public void Push(long key, int value)
        {
            if (Count == keys.Length)
            {
                Array.Resize(ref keys, Count * 2);
                Array.Resize(ref values, Count * 2);
            }
            int child = Count++;
            keys[child] = key;
            values[child] = value;
            while (child > 0)
            {
                int parent = (child - 1) / 2;
                if (keys[parent] <= keys[child])
                    break;
                Swap(parent, child);
                child = parent;
            }
        }

        public int Pop()
        {
            int top = values[0];
            Count--;
            keys[0] = keys[Count];
            values[0] = values[Count];
            int parent = 0;
            while (true)
            {
                int left = parent * 2 + 1;
                if (left >= Count)
                    break;
                int smallest = left;
                if (left + 1 < Count && keys[left + 1] < keys[left])
                    smallest = left + 1;
                if (keys[parent] <= keys[smallest])
                    break;
                Swap(parent, smallest);
                parent = smallest;
            }
            return top;
        }

        private void Swap(int a, int b)
        {
            (keys[a], keys[b]) = (keys[b], keys[a]);
            (values[a], values[b]) = (values[b], values[a]);
        }
    }

    private sealed record Route(long Minutes, List<int> Path, int Settled);

    private sealed class Planner
    {
        private readonly RoadNetwork network;
        private readonly long[] distance;
        private readonly int[] previous;
        private readonly int[] stamp;
        private readonly MinHeap heap = new();
        private int query;

        public Planner(RoadNetwork roads)
        {
            network = roads;
            int count = roads.Intersections.Count;
            distance = new long[count];
            previous = new int[count];
            stamp = new int[count];
            Array.Fill(distance, long.MaxValue);
            Array.Fill(previous, -1);
            Array.Fill(stamp, -1);
        }

        private long DistanceOf(int node) => stamp[node] == query ? distance[node] : long.MaxValue;

        private void Reach(int node, long minutes, int from)
        {
            stamp[node] = query;
            distance[node] = minutes;
            previous[node] = from;
            heap.Push(minutes, node);
        }

        public Route Plan(int source, int target)
        {
            query++;
            heap.Clear();
            Reach(source, 0, -1);
            int settled = 0;
            while (heap.Count > 0)
            {
                long minutes = heap.TopKey;
                int node = heap.Pop();
                if (minutes > DistanceOf(node))
                    continue;
                settled++;
                if (node == target)
                    break;
                foreach (var road in network.Intersections[node].Roads)
                {
                    if (!road.Open)
                        continue;
                    long candidate = minutes + road.Minutes;
                    if (candidate < DistanceOf(road.Target))
                        Reach(road.Target, candidate, node);
                }
            }

            var path = new List<int>();
            long total = DistanceOf(target);
            if (total != long.MaxValue)
            {
                for (int at = target; at != -1; at = previous[at])
                    path.Add(at);
                path.Reverse();
            }
            return new Route(total, path, settled);
        }
    }

    public static string Workload(int queries)
    {
        var random = new Generator(424242);
        var network = new RoadNetwork(random);
        var planner = new Planner(network);
        var closed = network.AllRoads[0];
        int reached = 0;
        long hops = 0, settled = 0, checksum = 0;
        for (int i = 0; i < queries; i++)
        {
            if (i % ClosureInterval == 0)
            {
                closed.Open = true;
                closed = network.AllRoads[random.Next(network.AllRoads.Count)];
                closed.Open = false;
            }
            var route = planner.Plan(random.Next(network.Intersections.Count),
                                     random.Next(network.Intersections.Count));
            settled += route.Settled;
            if (route.Path.Count == 0)
            {
                checksum = (checksum * 31 + 7) & Mask;
                continue;
            }
            reached++;
            hops += route.Path.Count - 1;
            checksum = (checksum * 31 + route.Minutes) & Mask;
            foreach (int node in route.Path)
                checksum = (checksum * 17 + node) & Mask;
        }
        return FormattableString.Invariant($"route-planner {queries} {network.AllRoads.Count} {reached} {hops} {settled} {checksum}\n");
    }

    public static int Main(string[] args)
    {
        if (args.Length != 1 || !int.TryParse(args[0], NumberStyles.AllowLeadingSign,
                CultureInfo.InvariantCulture, out int queries) || queries < 0)
            return 1;
        Console.Write(Workload(queries));
        return 0;
    }
}
