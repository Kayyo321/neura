// Same input and output as ../neura/sort.nra, with a port of the stable sort
// of Std.Collections.Sorting (stdlib/std/collections/sorting.nra): binary
// insertion sort of 16-element blocks, then bottom-up merging into a buffer,
// with every comparison made through a comparator call.
public final class Sort {
    private static final int BLOCK_SIZE = 16;
    private static final long MASK = 1073741823L;

    private interface IntComparator {
        int compare(int a, int b);
    }

    private static void insertionSort(int[] items, int start, int end, IntComparator c) {
        for (int i = start + 1; i < end; i = i + 1) {
            int value = items[i];
            int low = start;
            int high = i;
            while (low < high) {
                int middle = low + (high - low) / 2;
                if (c.compare(items[middle], value) > 0) {
                    high = middle;
                } else {
                    low = middle + 1;
                }
            }
            for (int k = i; k > low; k = k - 1) {
                items[k] = items[k - 1];
            }
            items[low] = value;
        }
    }

    private static void mergePass(int[] source, int sourceStart, int[] destination, int destinationStart, int count,
                                  int width, IntComparator c) {
        for (int left = 0; left < count;) {
            int middle = Math.min(left + width, count);
            int rightEnd = Math.min(middle + width, count);
            boolean copyOnly = middle == rightEnd;
            if (!copyOnly) {
                // A pair that is already in order is copied without merging.
                copyOnly = c.compare(source[sourceStart + middle - 1], source[sourceStart + middle]) <= 0;
            }
            if (copyOnly) {
                for (int k = left; k < rightEnd; k = k + 1) {
                    destination[destinationStart + k] = source[sourceStart + k];
                }
            } else {
                int i = left;
                int j = middle;
                int k = left;
                while (i < middle && j < rightEnd) {
                    int rightValue = source[sourceStart + j];
                    int leftValue = source[sourceStart + i];
                    // Take from the right run only when it is strictly less.
                    if (c.compare(rightValue, leftValue) < 0) {
                        destination[destinationStart + k] = rightValue;
                        j = j + 1;
                    } else {
                        destination[destinationStart + k] = leftValue;
                        i = i + 1;
                    }
                    k = k + 1;
                }
                while (i < middle) {
                    destination[destinationStart + k] = source[sourceStart + i];
                    i = i + 1;
                    k = k + 1;
                }
                while (j < rightEnd) {
                    destination[destinationStart + k] = source[sourceStart + j];
                    j = j + 1;
                    k = k + 1;
                }
            }
            left = rightEnd;
        }
    }

    private static void sort(int[] items, IntComparator c) {
        int end = items.length;
        for (int block = 0; block < end; block = Math.min(block + BLOCK_SIZE, end)) {
            insertionSort(items, block, Math.min(block + BLOCK_SIZE, end), c);
        }
        if (end <= BLOCK_SIZE) {
            return;
        }
        int[] buffer = items.clone();
        boolean inBuffer = false;
        for (int width = BLOCK_SIZE; width < end;) {
            if (inBuffer) {
                mergePass(buffer, 0, items, 0, end, width, c);
            } else {
                mergePass(items, 0, buffer, 0, end, width, c);
            }
            inBuffer = !inBuffer;
            width = width > end / 2 ? end : width * 2;
        }
        if (inBuffer) {
            System.arraycopy(buffer, 0, items, 0, end);
        }
    }

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        int[] items = new int[n];
        long seed = 12345;
        for (int i = 0; i < n; i = i + 1) {
            seed = (seed * 1103515245L + 12345) & 2147483647L;
            items[i] = (int) (seed % 1000000);
        }
        sort(items, Integer::compare);
        long checksum = 0;
        for (int i = 0; i < items.length; i = i + 1) {
            if (i > 0 && items[i - 1] > items[i]) {
                System.out.println("not sorted");
                System.exit(1);
            }
            checksum = (checksum * 31 + items[i]) & MASK;
        }
        System.out.println("sort " + checksum);
    }
}
