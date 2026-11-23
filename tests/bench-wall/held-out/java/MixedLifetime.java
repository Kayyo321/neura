// Row 17. The dense page index, LRU list and rolling audit match mixed-lifetime.nra.
import java.util.Arrays;

public final class MixedLifetime {
    private static final int AUDIT_SIZE = 16;
    private static final int MAX_PAGES = 4096;
    private static final long MASK = 1073741823L;

    private record Snapshot(int key, long value) {}

    private static final class Entry {
        final int key;
        Snapshot page;
        Entry previous;
        Entry next;

        Entry(int key, Snapshot page) {
            this.key = key;
            this.page = page;
        }
    }

    private static void unlink(Entry entry) {
        entry.previous.next = entry.next;
        entry.next.previous = entry.previous;
    }

    private static long fold(long checksum, Snapshot sample) {
        return (checksum * 31 + sample.key() + sample.value()) & MASK;
    }

    public static String workload(int requests) {
        int capacity = Math.max(4, Math.min(MAX_PAGES, requests / 32));
        Snapshot empty = new Snapshot(0, 0);
        Entry root = new Entry(-1, empty);
        root.previous = root;
        root.next = root;
        Entry[] directory = new Entry[capacity * 4];
        Arrays.fill(directory, root);
        Snapshot[] audit = new Snapshot[AUDIT_SIZE];
        Arrays.fill(audit, empty);
        int resident = 0;
        int hits = 0;
        int evictions = 0;
        long checksum = 0;
        long random = 20261123L;
        for (int i = 0; i < requests; i++) {
            random = (random * 1103515245L + 12345) & 2147483647L;
            int keyRange = i % 4 == 0 ? directory.length : capacity / 2;
            int key = (int) ((random >> 8) % keyRange);
            Entry entry = directory[key];
            boolean missed = entry == root;
            if (missed) {
                if (resident == capacity) {
                    Entry victim = root.previous;
                    unlink(victim);
                    directory[victim.key] = root;
                    evictions++;
                } else {
                    resident++;
                }
                entry = new Entry(key, empty);
                directory[key] = entry;
            } else {
                hits++;
                unlink(entry);
            }
            entry.next = root.next;
            entry.previous = root;
            root.next.previous = entry;
            root.next = entry;

            boolean changed = missed || i % 5 == 0;
            long old = entry.page.value();
            long value = changed ? (old * 33 + random + key) & MASK : old;
            Snapshot sample = new Snapshot(key, value);
            if (changed) {
                entry.page = sample;
            }
            int slot = i % AUDIT_SIZE;
            checksum = fold(checksum, audit[slot]);
            audit[slot] = sample;
        }
        for (Snapshot sample : audit) {
            checksum = fold(checksum, sample);
        }
        for (Entry cursor = root.next; cursor != root; cursor = cursor.next) {
            checksum = fold(checksum, cursor.page);
        }
        return "mixed-lifetime " + requests + " " + resident + " " + hits + " " + evictions + " " + checksum + "\n";
    }

    public static void main(String[] args) {
        if (args.length == 1 && args[0].matches("[+-]?[0-9]+")) {
            try {
                int size = Integer.parseInt(args[0]);
                if (size >= 0) {
                    System.out.print(workload(size));
                    return;
                }
            } catch (NumberFormatException ignored) {
                // Outside the signed 32-bit range.
            }
        }
        System.exit(1);
    }
}
