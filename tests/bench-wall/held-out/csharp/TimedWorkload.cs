using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

public static class TimedWorkload
{
    // Linux CLOCK_PROCESS_CPUTIME_ID, which includes runtime compiler and GC threads.
    private const int ProcessCpuClock = 2;

    [DllImport("libc", EntryPoint = "clock_gettime")]
    private static extern int ClockGetTime(int clock, out Timespec value);

    [StructLayout(LayoutKind.Sequential)]
    private struct Timespec
    {
        public nint Seconds;
        public nint Nanoseconds;
    }

    private static long ProcessCpuNanos()
    {
        if (ClockGetTime(ProcessCpuClock, out Timespec value) != 0)
            throw new InvalidOperationException("process CPU clock failed");
        return (long)value.Seconds * 1_000_000_000 + (long)value.Nanoseconds;
    }

    public static int Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--runtime-info")
        {
            Console.WriteLine(RuntimeEnvironment.GetRuntimeDirectory());
            return 0;
        }
        Type type = Assembly.GetExecutingAssembly().GetType(args[0], throwOnError: true)!;
        Func<int, string> workload = type.GetMethod("Workload", BindingFlags.Public | BindingFlags.Static)!
            .CreateDelegate<Func<int, string>>();
        int size = int.Parse(args[1], CultureInfo.InvariantCulture);
        int warmups = int.Parse(args[2], CultureInfo.InvariantCulture);
        int samples = int.Parse(args[3], CultureInfo.InvariantCulture);
        byte[] expected = File.ReadAllBytes(args[4]);
        string? stop = Environment.GetEnvironmentVariable("NEURA_HELD_OUT_STOP");
        long[] walls = new long[warmups + samples];
        long[] cpus = new long[walls.Length];
        for (int i = 0; i < walls.Length; i++)
        {
            if (File.Exists(stop)) throw new InvalidOperationException("benchmark STOP requested");
            long cpuStart = ProcessCpuNanos();
            long start = Stopwatch.GetTimestamp();
            string result = workload(size);
            long elapsed = Stopwatch.GetTimestamp() - start;
            long cpuEnd = ProcessCpuNanos();
            if (!Encoding.UTF8.GetBytes(result).AsSpan().SequenceEqual(expected))
                throw new InvalidDataException($"output differs in invocation {i}");
            walls[i] = (long)(elapsed * (1e9 / Stopwatch.Frequency));
            cpus[i] = cpuEnd - cpuStart;
        }
        for (int i = 0; i < walls.Length; i++)
            Console.Error.WriteLine(FormattableString.Invariant($"sample {i} {walls[i]} {cpus[i]}"));
        Console.OpenStandardOutput().Write(expected);
        return 0;
    }
}
