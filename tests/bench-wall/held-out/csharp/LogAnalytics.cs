// Row 16. Generated request logs, path normalization, grouping, ranking and checksum match log-analytics.nra.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

public static class LogAnalytics
{
    private const int BatchSize = 4000;
    private const int HostCount = 24;
    private const int TopCount = 8;
    private const long Mask = 1073741823;
    private static readonly CultureInfo Culture = CultureInfo.InvariantCulture;
    private static readonly string[] Methods = { "GET", "GET", "GET", "POST", "PUT", "DELETE" };
    private static readonly string[] Paths = {
        "/api/items/#", "/api/items/#/reviews", "/api/users/#", "/api/users/#/orders/#", "/api/search",
        "/static/app.js", "/static/style.css", "/health", "/api/carts/#/lines", "/login", "/api/reports/daily/#",
        "/api/items"
    };
    private static readonly int[] Statuses = { 200, 200, 200, 200, 200, 200, 201, 204, 301, 304, 400, 404, 404, 429, 500, 503 };

    private sealed class RandomSource(long seed)
    {
        private long state = seed;

        public int Next(int bound)
        {
            state = (state * 1103515245 + 12345) & 2147483647;
            return (int)((state >> 4) % bound);
        }
    }

    private sealed record Request(string Host, string Method, string Endpoint, int Status, int Bytes, int Latency);

    private sealed class EndpointStats(string key)
    {
        public readonly string Key = key;
        public int Count;
        public int ClientErrors;
        public int ServerErrors;
        public long Bytes;
        public long LatencyTotal;
        public double LatencySquares;
        public int LatencyMax;

        public void Add(Request request)
        {
            Count++;
            if (request.Status >= 500)
                ServerErrors++;
            else if (request.Status >= 400)
                ClientErrors++;
            Bytes += request.Bytes;
            LatencyTotal += request.Latency;
            double latency = request.Latency;
            LatencySquares += latency * latency;
            if (request.Latency > LatencyMax)
                LatencyMax = request.Latency;
        }

        public double Mean() => (double)LatencyTotal / Count;

        public double Variance()
        {
            double mean = Mean();
            return LatencySquares / Count - mean * mean;
        }
    }

    private sealed class HostStats(string host)
    {
        public readonly string Host = host;
        public int Requests;
        public long Bytes;
        public double SmoothedLatency;

        public void Add(Request request)
        {
            if (Requests == 0)
                SmoothedLatency = request.Latency;
            else
                SmoothedLatency = SmoothedLatency * 0.875 + request.Latency * 0.125;
            Requests++;
            Bytes += request.Bytes;
        }
    }

    private static string GenerateBatch(RandomSource random, int first, int count)
    {
        var lines = new List<string>();
        for (int i = first; i < first + count; i++)
        {
            int seconds = i % 86400;
            var line = new StringBuilder();
            line.Append(Culture, $"2026-11-23T{seconds / 3600:D2}:{seconds / 60 % 60:D2}:{seconds % 60:D2}");
            line.Append(Culture, $" web-{1 + random.Next(HostCount):D2} ");
            line.Append(Methods[random.Next(Methods.Length)]).Append(' ');
            string template = Paths[random.Next(Paths.Length)];
            foreach (var piece in template.Split('#'))
            {
                if (piece.Length == 0)
                    continue;
                line.Append(piece);
                if (piece[^1] == '/')
                    line.Append(Culture, $"{1 + random.Next(99999)}");
            }
            line.Append(Culture, $" {Statuses[random.Next(Statuses.Length)]} {120 + random.Next(48000)}");
            line.Append(Culture, $" {2 + random.Next(40) + random.Next(40) * random.Next(12)}ms req-{i}");
            lines.Add(line.ToString());
        }
        return string.Join("\n", lines);
    }

    private static bool IsNumber(string text)
    {
        if (text.Length == 0)
            return false;
        foreach (char c in text)
            if (c < '0' || c > '9')
                return false;
        return true;
    }

    private static string Normalize(string path)
    {
        var segments = path.Split('/');
        for (int i = 0; i < segments.Length; i++)
            if (IsNumber(segments[i]))
                segments[i] = ":id";
        return string.Join("/", segments);
    }

    private static Request ParseLine(string line)
    {
        var fields = line.Split(' ');
        if (fields.Length != 8)
            throw new InvalidOperationException("log analytics: malformed line");
        string latency = fields[6].EndsWith("ms", StringComparison.Ordinal) ? fields[6][..^2] : "";
        return new Request(fields[1], fields[2], Normalize(fields[3]), int.Parse(fields[4], Culture),
            int.Parse(fields[5], Culture), int.Parse(latency, Culture));
    }

    private static long Scaled(double value) => (long)(value * 1000.0);

    public static string Workload(int lineCount)
    {
        var random = new RandomSource(5150);
        var endpoints = new Dictionary<string, EndpointStats>();
        var hosts = new Dictionary<string, HostStats>();
        var perHour = new int[24];
        long textBytes = 0;
        int first = 0;
        while (first < lineCount)
        {
            int count = Math.Min(BatchSize, lineCount - first);
            string text = GenerateBatch(random, first, count);
            textBytes += Encoding.UTF8.GetByteCount(text);
            foreach (string line in text.Split('\n'))
            {
                var request = ParseLine(line);
                string key = request.Method + " " + request.Endpoint;
                if (!endpoints.TryGetValue(key, out var endpoint))
                {
                    endpoint = new EndpointStats(key);
                    endpoints.Add(key, endpoint);
                }
                endpoint.Add(request);
                if (!hosts.TryGetValue(request.Host, out var host))
                {
                    host = new HostStats(request.Host);
                    hosts.Add(request.Host, host);
                }
                host.Add(request);
                perHour[int.Parse(line.Substring(11, 2), Culture)]++;
            }
            first += count;
        }

        var rankedEndpoints = new List<EndpointStats>(endpoints.Values);
        rankedEndpoints.Sort((a, b) => {
            int order = b.Count.CompareTo(a.Count);
            return order != 0 ? order : string.CompareOrdinal(a.Key, b.Key);
        });
        var rankedHosts = new List<HostStats>(hosts.Values);
        rankedHosts.Sort((a, b) => {
            int order = b.Bytes.CompareTo(a.Bytes);
            return order != 0 ? order : string.CompareOrdinal(a.Host, b.Host);
        });

        var report = new StringBuilder();
        report.Append(Culture, $"lines {lineCount} bytes {textBytes} endpoints {endpoints.Count}\n");
        for (int i = 0; i < TopCount && i < rankedEndpoints.Count; i++)
        {
            var stats = rankedEndpoints[i];
            report.Append(Culture, $"{stats.Key,-32}{stats.Count,8}{stats.ClientErrors,7}{stats.ServerErrors,7}");
            report.Append(Culture, $"{stats.Bytes,12}{Scaled(stats.Mean()),10}{Scaled(stats.Variance()),14}{stats.LatencyMax,6}\n");
        }
        for (int i = 0; i < TopCount && i < rankedHosts.Count; i++)
        {
            var stats = rankedHosts[i];
            report.Append(Culture, $"{stats.Host,-8}{stats.Requests,8}{stats.Bytes,12}{Scaled(stats.SmoothedLatency),10}\n");
        }
        for (int hour = 0; hour < perHour.Length; hour++)
            report.Append(Culture, $"hour {hour:D2} {perHour[hour]}\n");

        byte[] result = Encoding.UTF8.GetBytes(report.ToString());
        long checksum = 0;
        foreach (byte b in result)
            checksum = (checksum * 31 + b) & Mask;
        return FormattableString.Invariant($"log-analytics {lineCount} {endpoints.Count} {result.Length} {checksum}\n");
    }

    public static int Main(string[] args)
    {
        if (args.Length != 1 || !int.TryParse(args[0], NumberStyles.AllowLeadingSign, Culture, out int size) || size < 0)
            return 1;
        Console.Write(Workload(size));
        return 0;
    }
}
