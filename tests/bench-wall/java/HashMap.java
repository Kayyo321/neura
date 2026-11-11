// Same input and output as ../neura/hash-map.nra. Std.Collections.HashMap
// keeps insertion order, so the counterpart is java.util.LinkedHashMap: each
// language's standard insertion-ordered hash map, not one shared algorithm.
public final class HashMap {
    private static final long MASK = 1073741823L;

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        java.util.LinkedHashMap<Integer, Long> map = new java.util.LinkedHashMap<>();
        for (int i = 0; i < n; i = i + 1) {
            map.put(i * 7919, (long) i);
        }
        long total = 0;
        for (int i = 0; i < n; i = i + 1) {
            total = total + map.getOrDefault(i * 7919, -1L);
        }
        for (int i = 0; i < n; i = i + 2) {
            map.put(i * 7919, 0L);
        }
        for (int i = 0; i < n; i = i + 4) {
            map.remove(i * 7919);
        }
        long checksum = 0;
        for (java.util.Map.Entry<Integer, Long> entry : map.entrySet()) {
            checksum = (checksum * 31 + (long) entry.getKey() * 3 + entry.getValue()) & MASK;
        }
        System.out.println("hash-map " + map.size() + " " + total + " " + checksum);
    }
}
