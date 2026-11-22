package benchwall.jmh;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.concurrent.TimeUnit;
import org.openjdk.jmh.annotations.Benchmark;
import org.openjdk.jmh.annotations.BenchmarkMode;
import org.openjdk.jmh.annotations.Level;
import org.openjdk.jmh.annotations.Mode;
import org.openjdk.jmh.annotations.OutputTimeUnit;
import org.openjdk.jmh.annotations.Param;
import org.openjdk.jmh.annotations.Scope;
import org.openjdk.jmh.annotations.Setup;
import org.openjdk.jmh.annotations.State;
import org.openjdk.jmh.annotations.TearDown;
import org.openjdk.jmh.annotations.Threads;

@State(Scope.Thread)
@Threads(1)
@BenchmarkMode(Mode.AverageTime)
@OutputTimeUnit(TimeUnit.MILLISECONDS)
public class Workload {
    @Param({"Collatz"})
    public String program;

    @Param({"1"})
    public int size;

    @Param({""})
    public String checksum;

    private MethodHandle workload;
    private String expected;
    private String last;

    @Setup(Level.Trial)
    public void prepare() throws ReflectiveOperationException {
        if (checksum.isEmpty()) {
            throw new IllegalArgumentException("Supply the Base64-encoded expected output with -p checksum=…");
        }
        expected = new String(Base64.getDecoder().decode(checksum), StandardCharsets.UTF_8);
        var entry = MethodHandles.publicLookup().findStatic(Class.forName(program), "workload",
            MethodType.methodType(String.class, int.class));
        workload = MethodHandles.insertArguments(entry, 0, size);
    }

    @Benchmark
    public String run() throws Throwable {
        last = (String) workload.invokeExact();
        return last;
    }

    @TearDown(Level.Iteration)
    public void verifyChecksum() {
        if (!expected.equals(last)) {
            throw new IllegalStateException(program + " output differed from the reference: " + last);
        }
    }
}
