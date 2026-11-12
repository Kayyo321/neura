// Same algorithm, input, and output as ../neura/closures.nra, with lambdas
// of java.util.function's long interfaces. Neura's int64[list] holds its
// values unboxed, so the counterpart is LongList, a growable long array,
// rather than ArrayList<Long>, which would box every element.
import java.util.Arrays;
import java.util.function.LongBinaryOperator;
import java.util.function.LongPredicate;
import java.util.function.LongUnaryOperator;

public final class Closures {
    private static final int VALUES = 1000;
    private static final long MASK = 1073741823L;

    private static final class LongList {
        private long[] items = new long[8];
        private int length = 0;

        void append(long value) {
            if (length == items.length) {
                items = Arrays.copyOf(items, length * 2);
            }
            items[length] = value;
            length = length + 1;
        }

        long get(int index) {
            if (index >= length) {
                throw new IndexOutOfBoundsException(index);
            }
            return items[index];
        }

        int length() {
            return length;
        }
    }

    private static LongList mapValues(LongList values, LongUnaryOperator transform) {
        LongList mapped = new LongList();
        for (int i = 0; i < values.length(); i = i + 1) {
            mapped.append(transform.applyAsLong(values.get(i)));
        }
        return mapped;
    }

    private static LongList filterValues(LongList values, LongPredicate keep) {
        LongList kept = new LongList();
        for (int i = 0; i < values.length(); i = i + 1) {
            if (keep.test(values.get(i))) {
                kept.append(values.get(i));
            }
        }
        return kept;
    }

    private static long foldValues(LongList values, long initial, LongBinaryOperator combine) {
        long total = initial;
        for (int i = 0; i < values.length(); i = i + 1) {
            total = combine.applyAsLong(total, values.get(i));
        }
        return total;
    }

    private static LongUnaryOperator makeStep(long factor, long offset) {
        return value -> (value * factor + offset) & MASK;
    }

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        LongList values = new LongList();
        long seed = 12345;
        for (int i = 0; i < VALUES; i = i + 1) {
            seed = (seed * 1103515245L + 12345) & 2147483647L;
            values.append(seed % 100000);
        }
        long checksum = 0;
        for (int round = 0; round < n; round = round + 1) {
            long offset = round;
            long threshold = round % 10;
            long weight = round % 7 + 1;
            LongList shifted = mapValues(values, value -> value + offset);
            LongList kept = filterValues(shifted, value -> value % 10 != threshold);
            long total = foldValues(kept, 0, (sum, value) -> (sum + value * weight) & MASK);
            LongUnaryOperator step = makeStep(weight + 2, offset);
            long walked = total;
            for (int i = 0; i < 100; i = i + 1) {
                walked = step.applyAsLong(walked);
            }
            checksum = (checksum * 31 + (long) kept.length() + total + walked) & MASK;
        }
        System.out.println("closures " + checksum);
    }
}
