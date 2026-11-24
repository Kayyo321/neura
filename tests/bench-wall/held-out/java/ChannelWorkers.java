import java.util.ArrayList;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

public final class ChannelWorkers {
    private static final int WORKERS = 3;
    private static final int CAPACITY = 16;

    private static int value(int job) {
        long v = job;
        for (int i = 0; i < 3; ++i) {
            v = (v * 1103515245 + 12345) & 1073741823;
        }
        return (int)(v & 65535);
    }

    public static String workload(int count) {
        ArrayBlockingQueue<Integer> jobs = new ArrayBlockingQueue<>(CAPACITY);
        ArrayBlockingQueue<Integer> results = new ArrayBlockingQueue<>(CAPACITY);
        ExecutorService executor = Executors.newFixedThreadPool(WORKERS + 1);
        try {
            Future<Long> collector = executor.submit(() -> {
                long total = 0;
                for (int i = 0; i < count; ++i) {
                    total += results.take();
                }
                return total;
            });
            Callable<Void> worker = () -> {
                int job;
                while ((job = jobs.take()) != -1) {
                    results.put(value(job));
                }
                return null;
            };
            var workers = new ArrayList<Future<Void>>();
            for (int i = 0; i < WORKERS; ++i) {
                workers.add(executor.submit(worker));
            }
            for (int job = 0; job < count; ++job) {
                jobs.put(job);
            }
            for (int i = 0; i < WORKERS; ++i) {
                jobs.put(-1);
            }
            for (Future<Void> handle : workers) {
                handle.get();
            }
            return "channel-workers " + count + " " + collector.get() + "\n";
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(error);
        } catch (java.util.concurrent.ExecutionException error) {
            throw new IllegalStateException(error.getCause());
        } finally {
            executor.shutdownNow();
        }
    }

    public static void main(String[] args) {
        if (args.length == 1 && args[0].matches("[+-]?[0-9]+")) {
            try {
                int count = Integer.parseInt(args[0]);
                if (count >= 0) {
                    System.out.print(workload(count));
                    return;
                }
            } catch (NumberFormatException ignored) {
                // Out of the signed 32-bit input range.
            }
        }
        System.exit(1);
    }
}
