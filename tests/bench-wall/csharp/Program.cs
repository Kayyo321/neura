using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using BenchmarkDotNet.Attributes;
using BenchmarkDotNet.Configs;
using BenchmarkDotNet.Engines;
using BenchmarkDotNet.Jobs;
using BenchmarkDotNet.Running;

internal static class Program
{
    internal static Func<int, string> Workload(string name) => name switch
    {
        "binary-trees" => BinaryTrees.Run,
        "closures" => Closures.Run,
        "collatz" => Collatz.Run,
        "dispatch" => Dispatch.Run,
        "fannkuch" => Fannkuch.Run,
        "hash-map" => HashMap.Run,
        "mandelbrot" => Mandelbrot.Run,
        "matrix-multiply" => MatrixMultiply.Run,
        "nbody" => NBody.Run,
        "object-churn" => ObjectChurn.Run,
        "parse-results" => ParseResults.Run,
        "sha256" => Sha256.Run,
        "sieve" => Sieve.Run,
        "sort" => Sort.Run,
        "spectral-norm" => SpectralNorm.Run,
        "string-building" => StringBuilding.Run,
        "text-split" => TextSplit.Run,
        _ => throw new ArgumentException($"Unknown benchmark {name}"),
    };

    public static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        if (args is ["--verify-text-edges"])
        {
            Console.Write(TextSplit.VerifyEdges());
            return 0;
        }
        if (args is [var name, var size])
        {
            Console.Write(Workload(name)(int.Parse(size, CultureInfo.InvariantCulture)));
            return 0;
        }
        if (args is ["--warmed", var bench, var input, var runs, var warmups, var output, var expected])
            return Warm(bench, input, int.Parse(runs, CultureInfo.InvariantCulture),
                        int.Parse(warmups, CultureInfo.InvariantCulture), output, expected);

        Console.Error.WriteLine("Usage: BenchWall NAME SIZE or BenchWall --warmed NAME SIZE RUNS WARMUPS OUTPUT_DIR EXPECTED_OUTPUT");
        return 2;
    }

    private static int Warm(string name, string size, int iterations, int warmups, string output, string expected)
    {
        if (iterations < 1 || warmups < 0)
        {
            Console.Error.WriteLine("Warmed runs must be positive and warmups must be nonnegative");
            return 2;
        }

        output = Path.GetFullPath(output);
        string scratch = Path.Combine(output, $".benchmarkdotnet-{Guid.NewGuid():N}");
        Directory.CreateDirectory(scratch);
        try
        {
            EmitWarmedSamples(name, size, iterations, warmups, output, expected, scratch);
            return 0;
        }
        catch (InvalidDataException error)
        {
            Console.Error.WriteLine(error.Message);
            return 1;
        }
        finally
        {
            Directory.Delete(scratch, recursive: true);
        }
    }

    private static void EmitWarmedSamples(string name, string size, int iterations, int warmups,
                                          string output, string expected, string scratch)
    {
        string cpuFile = Path.Combine(scratch, "cpu-seconds.json");
        var job = Job.Default
            .WithStrategy(RunStrategy.Monitoring)
            .WithLaunchCount(1)
            .WithWarmupCount(warmups)
            .WithIterationCount(iterations)
            .WithInvocationCount(1)
            .WithUnrollFactor(1)
            .WithGcServer(false)
            .WithGcConcurrent(false)
            .WithEnvironmentVariable("BENCH_WALL_NAME", name)
            .WithEnvironmentVariable("BENCH_WALL_SIZE", size)
            .WithEnvironmentVariable("BENCH_WALL_EXPECTED", Path.GetFullPath(expected))
            .WithEnvironmentVariable("BENCH_WALL_CPU_FILE", cpuFile);
#if CHECKED_ARITHMETIC
        job = job.WithCustomBuildConfiguration("Checked");
#endif
        // Keep only the numeric samples. BenchmarkDotNet's default logs
        // include the local runtime environment and directory names.
        var config = ManualConfig.CreateEmpty()
            .WithArtifactsPath(scratch)
            .WithOptions(ConfigOptions.DisableLogFile)
            .AddJob(job);
        var report = BenchmarkRunner.Run<WarmedWorkload>(config).Reports.Single();
        if (!report.Success)
            throw new InvalidDataException("Warmed benchmark failed to run or verify its output");

        var wall = report.AllMeasurements
            .Where(sample => sample.IterationMode == IterationMode.Workload &&
                             sample.IterationStage == IterationStage.Actual)
            .Select(sample => sample.Nanoseconds / sample.Operations / 1_000_000_000.0)
            .ToArray();
        var cpu = JsonSerializer.Deserialize<double[]>(File.ReadAllText(cpuFile))!;
        // The child collects CPU samples for the warmups as well. The
        // actual workload measurements are the final iterations.
        if (wall.Length != iterations || cpu.Length < iterations)
            throw new InvalidDataException("Incomplete warmed measurement samples");
        File.WriteAllText(Path.Combine(output, "samples.json"), JsonSerializer.Serialize(new
        {
            wall_seconds = wall,
            cpu_seconds = cpu[^iterations..],
        }));
    }
}

public class WarmedWorkload
{
    private readonly Process process = Process.GetCurrentProcess();
    private readonly List<double> cpuSamples = [];
    private Func<int, string> run = null!;
    private string expected = null!;
    private string? result;
    private int size;
    private TimeSpan cpuStart;

    [GlobalSetup]
    public void Prepare()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        run = Program.Workload(Environment.GetEnvironmentVariable("BENCH_WALL_NAME")!);
        size = int.Parse(Environment.GetEnvironmentVariable("BENCH_WALL_SIZE")!, CultureInfo.InvariantCulture);
        expected = File.ReadAllText(Environment.GetEnvironmentVariable("BENCH_WALL_EXPECTED")!);
    }

    [IterationSetup]
    public void Start()
    {
        result = null;
        process.Refresh();
        cpuStart = process.TotalProcessorTime;
    }

    [Benchmark]
    public void Execute()
    {
        result = run(size);
    }

    [IterationCleanup]
    public void Verify()
    {
        process.Refresh();
        double cpu = (process.TotalProcessorTime - cpuStart).TotalSeconds;
        // Overhead iterations do not call the workload.
        if (result is null)
            return;
        if (result != expected)
            throw new InvalidDataException("Warmed benchmark output differs from the expected output");
        cpuSamples.Add(cpu);
    }

    [GlobalCleanup]
    public void Finish()
    {
        File.WriteAllText(Environment.GetEnvironmentVariable("BENCH_WALL_CPU_FILE")!, JsonSerializer.Serialize(cpuSamples));
        process.Dispose();
    }
}
