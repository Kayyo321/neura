// Same input and output as ../neura/hash-map.nra. Std.Collections.HashMap
// keeps insertion order, so the counterpart is java.util.LinkedHashMap: each
// language's standard insertion-ordered hash map, not one shared algorithm.
public final class HashMap {
    private static final long MASK = 1073741823L;

    public static void main(String[] args) {
        System.out.print(workload(Integer.parseInt(args[0])));
    }

    public static String workload(int n) {
        java.util.LinkedHashMap<Long, Long> map = new java.util.LinkedHashMap<>();
        for (int i = 0; i < n; i = i + 1) {
            map.put((long) i * 7919, (long) i);
        }
        long total = 0;
        for (int i = 0; i < n; i = i + 1) {
            total = total + map.getOrDefault((long) i * 7919, -1L);
        }
        for (int i = 0; i < n; i = i + 2) {
            map.put((long) i * 7919, 0L);
        }
        for (int i = 0; i < n; i = i + 4) {
            map.remove((long) i * 7919);
        }
        long checksum = 0;
        for (java.util.Map.Entry<Long, Long> entry : map.entrySet()) {
            checksum = (checksum * 31 + entry.getKey() * 3 + entry.getValue()) & MASK;
        }
        return "hash-map " + map.size() + " " + total + " " + checksum + System.lineSeparator();
    }
}
