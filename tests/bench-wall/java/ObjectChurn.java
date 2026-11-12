// Same algorithm, input, and output as ../neura/object-churn.nra, with
// java.util.ArrayList as the counterpart of Std.Collections.ArrayList.
import java.util.ArrayList;

public final class ObjectChurn {
    private static final int BATCH = 256;
    private static final int LIFETIME = 64;
    private static final long MASK = 1073741823L;

    private static class Particle {
        final long value;
        final int born;
        int visits = 0;

        Particle(long start, int round) {
            value = start;
            born = round;
        }
    }

    private static class Random {
        private long seed = 12345;

        long next() {
            seed = (seed * 1103515245L + 12345) & 2147483647L;
            return seed;
        }
    }

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        Random random = new Random();
        ArrayList<Particle> survivors = new ArrayList<>();
        long checksum = 0;
        for (int round = 0; round < n; round = round + 1) {
            ArrayList<Particle> generation = new ArrayList<>();
            for (int i = 0; i < survivors.size(); i = i + 1) {
                generation.add(survivors.get(i));
            }
            int firstNew = generation.size();
            for (int i = 0; i < BATCH; i = i + 1) {
                generation.add(new Particle(random.next() % 100000, round));
            }
            for (int i = firstNew; i < generation.size(); i = i + 3) {
                generation.set(i, new Particle(random.next() % 100000, round));
            }
            ArrayList<Particle> next = new ArrayList<>();
            long total = 0;
            for (int i = 0; i < generation.size(); i = i + 1) {
                Particle particle = generation.get(i);
                particle.visits = particle.visits + 1;
                if (particle.value % 16 == 0 && round - particle.born < LIFETIME) {
                    next.add(particle);
                    total = (total + particle.value + (long) particle.visits) & MASK;
                }
            }
            survivors = next;
            checksum = (checksum * 31 + (long) survivors.size() + total) & MASK;
        }
        System.out.println("object-churn " + survivors.size() + " " + checksum);
    }
}
