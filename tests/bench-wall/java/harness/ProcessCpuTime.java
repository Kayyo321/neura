package benchwall.jmh;

import com.sun.management.OperatingSystemMXBean;
import java.lang.management.ManagementFactory;
import java.util.Collection;
import java.util.List;
import org.openjdk.jmh.infra.BenchmarkParams;
import org.openjdk.jmh.infra.IterationParams;
import org.openjdk.jmh.profile.InternalProfiler;
import org.openjdk.jmh.results.AggregationPolicy;
import org.openjdk.jmh.results.IterationResult;
import org.openjdk.jmh.results.Result;
import org.openjdk.jmh.results.ScalarResult;

/** User plus system CPU time for the fork, including compiler and GC threads. */
public final class ProcessCpuTime implements InternalProfiler {
    private final OperatingSystemMXBean clock = ManagementFactory.getPlatformMXBean(OperatingSystemMXBean.class);
    private long start;

    @Override
    public String getDescription() {
        return "JVM process CPU time (user plus system) per measured workload";
    }

    @Override
    public void beforeIteration(BenchmarkParams benchmark, IterationParams iteration) {
        start = clock == null ? -1 : clock.getProcessCpuTime();
    }

    @Override
    public Collection<? extends Result> afterIteration(BenchmarkParams benchmark, IterationParams iteration,
                                                        IterationResult result) {
        if (start < 0) {
            return List.of();
        }
        long elapsed = clock.getProcessCpuTime() - start;
        long operations = result.getMetadata().getMeasuredOps();
        if (elapsed < 0 || operations == 0) {
            return List.of();
        }
        return List.of(new ScalarResult("processCpu", elapsed / 1_000_000.0 / operations, "ms/op",
            AggregationPolicy.AVG));
    }
}
