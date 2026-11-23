// Same algorithm, input, and output as ../../fixtures/log-analytics.nra (row 16):
// generated log text split into lines and fields, parsed, normalized and
// grouped in hash maps, then ranked. Floats are printed only as scaled
// integers. String.split drops trailing empty fields; no generated text has
// any, so it splits as Std.Text.Strings.split does.
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public final class LogAnalytics {
    private static final int BATCH_SIZE = 4000;
    private static final int HOST_COUNT = 24;
    private static final int TOP_COUNT = 8;
    private static final long MASK = 1073741823L;

    private static final String[] METHODS = {"GET", "GET", "GET", "POST", "PUT", "DELETE"};
    private static final String[] PATHS = {
        "/api/items/#", "/api/items/#/reviews", "/api/users/#", "/api/users/#/orders/#", "/api/search",
        "/static/app.js", "/static/style.css", "/health", "/api/carts/#/lines", "/login", "/api/reports/daily/#",
        "/api/items"
    };
    private static final int[] STATUSES = {200, 200, 200, 200, 200, 200, 201, 204, 301, 304, 400, 404, 404, 429, 500, 503};

    private static final class Random {
        private long state;

        Random(long seed) {
            state = seed;
        }

        int next(int bound) {
            state = (state * 1103515245L + 12345L) & 2147483647L;
            return (int) ((state >> 4) % bound);
        }
    }

    private record Request(String host, String method, String endpoint, int status, int bytes, int latency) {}

    private static final class EndpointStats {
        final String key;
        int count;
        int clientErrors;
        int serverErrors;
        long bytes;
        long latencyTotal;
        double latencySquares;
        int latencyMax;

        EndpointStats(String key) {
            this.key = key;
        }

        void add(Request request) {
            count += 1;
            if (request.status() >= 500) {
                serverErrors += 1;
            } else if (request.status() >= 400) {
                clientErrors += 1;
            }
            bytes += request.bytes();
            latencyTotal += request.latency();
            double latency = request.latency();
            latencySquares += latency * latency;
            if (request.latency() > latencyMax) {
                latencyMax = request.latency();
            }
        }

        double mean() {
            return (double) latencyTotal / (double) count;
        }

        double variance() {
            double average = mean();
            return latencySquares / (double) count - average * average;
        }
    }

    private static final class HostStats {
        final String host;
        int requests;
        long bytes;
        double smoothedLatency;

        HostStats(String host) {
            this.host = host;
        }

        void add(Request request) {
            if (requests == 0) {
                smoothedLatency = request.latency();
            } else {
                smoothedLatency = smoothedLatency * 0.875 + (double) request.latency() * 0.125;
            }
            requests += 1;
            bytes += request.bytes();
        }
    }

    private static String twoDigits(int value) {
        return value < 10 ? "0" + value : Integer.toString(value);
    }

    private static String generateBatch(Random random, int first, int count) {
        List<String> lines = new ArrayList<>();
        for (int i = first; i < first + count; i++) {
            int seconds = i % 86400;
            StringBuilder line = new StringBuilder();
            line.append("2026-11-23T");
            line.append(twoDigits(seconds / 3600));
            line.append(":");
            line.append(twoDigits(seconds / 60 % 60));
            line.append(":");
            line.append(twoDigits(seconds % 60));
            line.append(" web-");
            line.append(twoDigits(1 + random.next(HOST_COUNT)));
            line.append(" ");
            line.append(METHODS[random.next(METHODS.length)]);
            line.append(" ");
            String template = PATHS[random.next(PATHS.length)];
            for (String piece : template.split("#")) {
                if (piece.isEmpty()) {
                    continue;
                }
                line.append(piece);
                if (piece.charAt(piece.length() - 1) == '/') {
                    line.append(1 + random.next(99999));
                }
            }
            line.append(" ");
            line.append(STATUSES[random.next(STATUSES.length)]);
            line.append(" ");
            line.append(120 + random.next(48000));
            line.append(" ");
            line.append(2 + random.next(40) + random.next(40) * random.next(12));
            line.append("ms req-");
            line.append(i);
            lines.add(line.toString());
        }
        return String.join("\n", lines);
    }

    private static boolean isNumber(String text) {
        if (text.isEmpty()) {
            return false;
        }
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c < '0' || c > '9') {
                return false;
            }
        }
        return true;
    }

    private static String normalize(String path) {
        String[] segments = path.split("/");
        for (int i = 0; i < segments.length; i++) {
            if (isNumber(segments[i])) {
                segments[i] = ":id";
            }
        }
        return String.join("/", segments);
    }

    private static Request parseLine(String line) {
        String[] fields = line.split(" ");
        if (fields.length != 8) {
            throw new IllegalStateException("log analytics: malformed line");
        }
        String latencyText = fields[6].endsWith("ms") ? fields[6].substring(0, fields[6].length() - 2) : "";
        return new Request(fields[1], fields[2], normalize(fields[3]), Integer.parseInt(fields[4]),
            Integer.parseInt(fields[5]), Integer.parseInt(latencyText));
    }

    private static long scaled(double value) {
        return (long) (value * 1000.0);
    }

    public static void main(String[] args) {
        if (args.length != 1 || !args[0].matches("[+-]?[0-9]+")) {
            System.exit(1);
        }
        int size;
        try {
            size = Integer.parseInt(args[0]);
        } catch (NumberFormatException error) {
            System.exit(1);
            return;
        }
        if (size < 0) {
            System.exit(1);
        }
        System.out.print(workload(size));
    }

    public static String workload(int lineCount) {
        Random random = new Random(5150);
        Map<String, EndpointStats> endpoints = new HashMap<>();
        Map<String, HostStats> hosts = new HashMap<>();
        int[] perHour = new int[24];
        long textBytes = 0;
        int first = 0;
        while (first < lineCount) {
            int count = Math.min(BATCH_SIZE, lineCount - first);
            String text = generateBatch(random, first, count);
            textBytes += text.getBytes(StandardCharsets.UTF_8).length;
            for (String line : text.split("\n")) {
                Request request = parseLine(line);
                String key = request.method() + " " + request.endpoint();
                endpoints.computeIfAbsent(key, EndpointStats::new).add(request);
                hosts.computeIfAbsent(request.host(), HostStats::new).add(request);
                perHour[Integer.parseInt(line.substring(11, 13))] += 1;
            }
            first += count;
        }

        List<EndpointStats> rankedEndpoints = new ArrayList<>(endpoints.values());
        rankedEndpoints.sort(Comparator.comparingInt((EndpointStats s) -> s.count).reversed()
            .thenComparing(s -> s.key));
        List<HostStats> rankedHosts = new ArrayList<>(hosts.values());
        rankedHosts.sort(Comparator.comparingLong((HostStats s) -> s.bytes).reversed()
            .thenComparing(s -> s.host));
        StringBuilder report = new StringBuilder();
        report.append("lines ").append(lineCount).append(" bytes ").append(textBytes).append(" endpoints ")
            .append(endpoints.size()).append("\n");
        for (int i = 0; i < TOP_COUNT && i < rankedEndpoints.size(); i++) {
            EndpointStats stats = rankedEndpoints.get(i);
            report.append(String.format(Locale.ROOT, "%-32s%8d%7d%7d%12d%10d%14d%6d\n", stats.key, stats.count,
                stats.clientErrors, stats.serverErrors, stats.bytes, scaled(stats.mean()),
                scaled(stats.variance()), stats.latencyMax));
        }
        for (int i = 0; i < TOP_COUNT && i < rankedHosts.size(); i++) {
            HostStats stats = rankedHosts.get(i);
            report.append(String.format(Locale.ROOT, "%-8s%8d%12d%10d\n", stats.host, stats.requests, stats.bytes,
                scaled(stats.smoothedLatency)));
        }
        for (int hour = 0; hour < perHour.length; hour++) {
            report.append(String.format(Locale.ROOT, "hour %02d %d\n", hour, perHour[hour]));
        }
        byte[] text = report.toString().getBytes(StandardCharsets.UTF_8);
        long checksum = 0;
        for (byte b : text) {
            checksum = (checksum * 31 + (b & 0xff)) & MASK;
        }
        return "log-analytics " + lineCount + " " + endpoints.size() + " " + text.length + " " + checksum + "\n";
    }
}
