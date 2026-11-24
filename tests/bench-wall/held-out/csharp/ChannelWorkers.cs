using System;
using System.Collections.Concurrent;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;

public static class ChannelWorkers
{
    private const int Workers = 3;
    private const int Capacity = 16;

    private static int Value(int job)
    {
        long v = job;
        for (int i = 0; i < 3; ++i)
        {
            v = (v * 1103515245 + 12345) & 1073741823;
        }
        return (int)(v & 65535);
    }

    public static string Workload(int count)
    {
        using var jobs = new BlockingCollection<int>(Capacity);
        using var results = new BlockingCollection<int>(Capacity);
        var collector = Task.Factory.StartNew(() =>
        {
            long total = 0;
            for (int i = 0; i < count; ++i)
            {
                total += results.Take();
            }
            return total;
        }, CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default);

        var workers = new Task[Workers];
        for (int i = 0; i < Workers; ++i)
        {
            workers[i] = Task.Factory.StartNew(() =>
            {
                foreach (int job in jobs.GetConsumingEnumerable())
                {
                    results.Add(Value(job));
                }
            }, CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default);
        }
        for (int job = 0; job < count; ++job)
        {
            jobs.Add(job);
        }
        jobs.CompleteAdding();
        Task.WaitAll(workers);
        results.CompleteAdding();
        return FormattableString.Invariant($"channel-workers {count} {collector.Result}\n");
    }

    public static int Main(string[] args)
    {
        if (args.Length != 1 || !int.TryParse(args[0], NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture,
                                              out int count) || count < 0)
        {
            return 1;
        }
        Console.Write(Workload(count));
        return 0;
    }
}
