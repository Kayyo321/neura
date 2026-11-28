import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

public final class TimedWorkload {
    private static native long processCpuNanos();

    public static void main(String[] args) throws Throwable {
        MethodHandle workload = MethodHandles.publicLookup().findStatic(
            Class.forName(args[0]), "workload", MethodType.methodType(String.class, int.class));
        int size = Integer.parseInt(args[1]);
        int warmups = Integer.parseInt(args[2]);
        int samples = Integer.parseInt(args[3]);
        byte[] expected = Files.readAllBytes(Path.of(args[4]));
        String stop = System.getenv("NEURA_HELD_OUT_STOP");
        System.loadLibrary("held-out-clock");
        long[] walls = new long[warmups + samples];
        long[] cpus = new long[walls.length];
        for (int i = 0; i < walls.length; i++) {
            if (stop != null && Files.exists(Path.of(stop))) throw new IllegalStateException("benchmark STOP requested");
            long cpuStart = processCpuNanos();
            long start = System.nanoTime();
            String result = (String)workload.invokeExact(size);
            long wall = System.nanoTime() - start;
            long cpuEnd = processCpuNanos();
            if (cpuStart < 0 || cpuEnd < cpuStart) throw new IllegalStateException("process CPU clock failed");
            if (!Arrays.equals(expected, result.getBytes(StandardCharsets.UTF_8)))
                throw new IllegalStateException("output differs in invocation " + i);
            walls[i] = wall;
            cpus[i] = cpuEnd - cpuStart;
        }
        for (int i = 0; i < walls.length; i++) System.err.printf("sample %d %d %d%n", i, walls[i], cpus[i]);
        System.out.write(expected);
    }
}
