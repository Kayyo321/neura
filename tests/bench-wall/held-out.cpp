#include "cli/benchmark.hpp"
#include "cli/commands.hpp"
#include "nvm/compile_observer.hpp"
#include "tooling/test_protocol.hpp"
#include "util/source_map.hpp"
#ifdef NEURA_HAS_SQL
#include "sql/sql_engine.hpp"
#endif

#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace neura;
using Clock = std::chrono::steady_clock;
using tooling::Json;

std::int64_t nanos(Clock::duration duration) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

std::int64_t process_cpu_nanos() {
    std::timespec value{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0)
        throw std::runtime_error("process CPU clock failed");
    return static_cast<std::int64_t>(value.tv_sec) * 1'000'000'000 + value.tv_nsec;
}

void check_stop() {
    const char* stop = std::getenv("NEURA_HELD_OUT_STOP");
    if (stop && *stop && fs::exists(stop)) throw std::runtime_error("benchmark STOP requested");
}

std::uint32_t iterations(std::string_view text) {
    std::uint32_t result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size() || result > 1'000'000)
        throw std::runtime_error("invalid iteration count");
    return result;
}

cli::Options parse_options(std::vector<std::string>& arguments) {
    auto argv = tool::argv_of(arguments);
    return cli::read_options(static_cast<int>(arguments.size()), argv.data());
}

class Report {
public:
    explicit Report(std::string_view endpoint) {
        constexpr std::string_view option = "--report-fd=";
        if (!endpoint.starts_with(option)) {
            path_ = endpoint;
            return;
        }
        const auto digits = endpoint.substr(option.size());
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), descriptor_);
        if (error != std::errc{} || end != digits.data() + digits.size() || descriptor_ < 3 ||
            fcntl(descriptor_, F_SETFD, FD_CLOEXEC) < 0)
            throw std::runtime_error("invalid benchmark report descriptor");
    }

    const fs::path& path() const { return path_; }

    void write(const Json& value) const {
        const auto text = value.dump() + '\n';
        if (descriptor_ < 0) {
            runner::write_file(path_, {text.begin(), text.end()});
            return;
        }
        std::string_view remaining = text;
        while (!remaining.empty()) {
            const auto count = ::write(descriptor_, remaining.data(), remaining.size());
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw std::runtime_error("cannot write benchmark receipt to inherited descriptor");
            remaining.remove_prefix(static_cast<std::size_t>(count));
        }
    }

private:
    fs::path path_;
    int descriptor_ = -1;
};

struct Entry {
    std::string id;
    std::string label;
    std::uint32_t index;
};

struct Suite {
    explicit Suite(const fs::path& path, const nvr::Image& image) {
        const auto bytes = runner::read_file(path);
        const auto catalog = Json::parse({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        if (catalog["version"].integer() != 1 || !catalog["tests"].is_array() || !catalog["source_labels"].is_array())
            throw std::runtime_error("invalid benchmark suite catalog");
        for (const auto& label : catalog["source_labels"].items()) source_labels.insert(label.string());
        std::set<std::string> ids;
        for (const auto& item : catalog["tests"].items()) {
            const auto index = item["entry"].integer(-1);
            Entry entry{item["id"].string(), item["label"].string(), static_cast<std::uint32_t>(index)};
            if (index < 0 || static_cast<std::size_t>(index) != entries.size() || entries.size() >= image.functions.size() ||
                entry.id.empty() || entry.label.empty() || !ids.insert(entry.id).second)
                throw std::runtime_error("invalid benchmark suite entry");
            entries.push_back(std::move(entry));
        }
        if (entries.empty()) throw std::runtime_error("empty benchmark suite");
    }

    std::vector<Entry> entries;
    std::set<std::string> source_labels;
};

class CompileLog final : public nvm::CompileObserver {
public:
    void compilation(const nvm::CompileEvent& event) noexcept override {
        const std::lock_guard lock(mutex_);
        try {
            events_.push_back(event);
        } catch (...) {
            lost_ = true;
        }
    }

    Json events(Clock::time_point invocation) const {
        if (lost_) throw std::runtime_error("unable to keep complete compilation log");
        std::map<std::thread::id, std::size_t> threads;
        Json result = Json::Array{};
        for (const auto& event : events_) {
            const auto thread = threads.try_emplace(event.thread, threads.size()).first->second;
            result.push_back(Json{{"kind", event.kind}, {"outcome", event.outcome}, {"thread", thread},
                {"function", event.function}, {"program", event.program}, {"variant", event.variant},
                {"entry", event.entry == nvr::absent ? Json() : Json(event.entry)},
                {"start_ns", nanos(event.start - invocation)}, {"end_ns", nanos(event.end - invocation)},
                {"cpu_ns", event.cpu_nanoseconds < 0 ? Json() : Json(event.cpu_nanoseconds)}});
        }
        return result;
    }

private:
    std::mutex mutex_;
    std::vector<nvm::CompileEvent> events_;
    bool lost_ = false;
};

Json activity_delta(const nvm::CompileActivity& before, const nvm::CompileActivity& after) {
    return {{"lifted_attempts", after.lifted_attempts - before.lifted_attempts},
            {"tier2_compiled", after.tier2_compiled - before.tier2_compiled},
            {"tier3_programs", after.tier3_programs - before.tier3_programs},
            {"tier3_failed", after.tier3_failed - before.tier3_failed},
            {"tier3_lazy_entries", after.tier3_lazy_entries - before.tier3_lazy_entries},
            {"tier3_osr_programs", after.tier3_osr_programs - before.tier3_osr_programs},
            {"tier3_bytes", after.tier3_bytes - before.tier3_bytes}};
}

Json outcome(const nvm::RunResult& result, const nvr::Image& image) {
    const auto trap_status = result.completed ? 0 : nvm::exit_status(result.trap);
    return {{"completed", result.completed}, {"status", result.completed ? result.status : trap_status},
            {"trap", result.completed ? Json() : Json(nvm::describe(result.trap, image))},
            {"trap_kind", result.completed ? Json() : Json(nvm::describe(result.trap.kind))},
            {"steps", result.steps}, {"peak_heap_cells", result.peak_heap_cells},
            {"peak_stack_cells", result.peak_stack_cells}};
}

int execute(const cli::Options& options, std::string_view action, const Report& report,
            std::uint32_t warmups, std::uint32_t samples, const fs::path& catalog) {
    const bool replay = action == "replay";
    const bool captured = action != "cold";
    const bool suite = !catalog.empty();
    if (options.gpu.mode != cli::GpuMode::Off || options.stats || options.tier_stats || options.limits.profile ||
        options.record || options.replay || options.each || options.trace_events || options.detach ||
        (options.runtime && options.runtime != cli::RuntimeMode::Off) || options.schedule || options.schedules > 1 ||
        options.save_schedule || options.list_tests || options.test_coverage || options.test_reporter_json)
        throw std::runtime_error("benchmark replay requires direct CPU execution and the reference schedule");
    if (options.files.size() != 1 || options.command != (suite ? cli::Command::Test : cli::Command::Run))
        throw std::runtime_error("expected run IMAGE or test IMAGE with a suite catalog");

    const auto read_start = Clock::now();
    const auto bytes = runner::read_file(options.files.front());
    const auto read_end = Clock::now();
    auto image = cli::load_image_bytes(bytes, options.files.front());
    const auto decode_end = Clock::now();
    auto tests = suite ? std::optional<Suite>(std::in_place, catalog, image) : std::nullopt;
    const auto catalog_end = Clock::now();
#ifdef NEURA_HAS_SQL
    sql::LocalSql sql(options.sql_pools);
    runner::Host host(options.capabilities, nullptr, nvm::default_response_bytes, sql.exchange());
#else
    if (!options.sql_pools.empty()) throw std::runtime_error("this build has no SQL providers");
    runner::Host host(options.capabilities);
#endif
    runner::provide_native_library(image, host.services);
    const auto bindings = nvm::bind(image, host.registry);
    const auto bindings_end = Clock::now();
    auto limits = options.limits;
    runner::TaskDumps dumps(options.task_dump);
    limits.task_dump = dumps.request();
    CompileLog log;
    const auto tiers = cli::tier_cache(image, limits);
    if (replay && tiers) tiers->observe_compilation(&log);
    const auto cache_end = Clock::now();
    Json invocations = Json::Array{};
    int status = 0;
    for (std::uint32_t i = 0; i < warmups + samples; ++i) {
        check_stop();
        const auto before = tiers ? tiers->compile_activity() : nvm::CompileActivity{};
        std::vector<nvm::RunResult> results;
        results.reserve(tests ? tests->entries.size() : 1);
        std::optional<tooling::TestOutputCapture> capture;
        if (captured) capture.emplace();
        // The coarse lower-tier timeline is meaningful for the serial diagnostic replay only.
        std::optional<nvm::TierTimes> times;
        if (replay && runner::times_runs(limits)) times.emplace(Clock::now());
        limits.tier_times = times ? &*times : nullptr;
        const auto cpu_start = process_cpu_nanos();
        const auto start = Clock::now();
        std::size_t failed = 0;
        Json test_output = Json::Array{};
        if (tests) {
            for (const auto& entry : tests->entries) {
                image.entry = entry.index;
                std::optional<tooling::TestOutputCapture> per_test_capture;
                if (replay) per_test_capture.emplace();
                auto result = runner::run_bound(image, host, bindings, {}, limits, options.virtual_time);
                runner::end_run(host);
                if (per_test_capture) {
                    per_test_capture->finish([&](std::string_view stream, std::string_view bytes) {
                        test_output.push_back(tooling::test_output_json(entry.id, stream, bytes));
                        auto* sink = stream == "stdout" ? stdout : stderr;
                        if (std::fwrite(bytes.data(), 1, bytes.size(), sink) != bytes.size())
                            throw std::runtime_error("could not replay test console bytes");
                    });
                }
                const auto message = cli::benchmark::test_failure(result, image, tests->source_labels);
                if (!message.empty()) ++failed;
                std::cout << (message.empty() ? "PASS " : "FAIL ") << entry.label;
                if (!message.empty()) std::cout << ": " << message;
                std::cout << '\n';
                results.push_back(std::move(result));
            }
            const auto size = results.size();
            std::cout << size << " test(s), " << size - failed << " passed, " << failed << " failed\n";
            std::cout.flush();
            status = failed ? 1 : 0;
        } else {
            results.push_back(runner::run_bound(image, host, bindings, options.arguments, limits, options.virtual_time));
            runner::end_run(host);
            const auto& result = results.front();
            status = result.completed ? result.status : nvm::exit_status(result.trap);
        }
        const auto end = Clock::now();
        const auto cpu_end = process_cpu_nanos();
        const auto after = tiers ? tiers->compile_activity() : nvm::CompileActivity{};
        std::string output;
        std::string error;
        if (capture) capture->finish([&](std::string_view stream, std::string_view bytes) {
            (stream == "stdout" ? output : error).append(bytes);
        });
        Json entries = Json::Array{};
        for (std::size_t j = 0; j < results.size(); ++j) {
            auto value = outcome(results[j], image);
            if (tests) {
                const auto& entry = tests->entries[j];
                value["id"] = entry.id;
                value["label"] = entry.label;
                value["entry"] = entry.index;
            }
            entries.push_back(std::move(value));
        }
        Json invocation{{"index", i}, {"warmup", i < warmups}, {"wall_ns", nanos(end - start)},
            {"cpu_ns", cpu_end - cpu_start}, {"status", status}, {"results", std::move(entries)},
            {"compilation", activity_delta(before, after)}};
        if (captured) {
            invocation["stdout"] = tooling::test_output_json("", "stdout", output);
            invocation["stderr"] = tooling::test_output_json("", "stderr", error);
        }
        if (times) invocation["serial_tier_times"] = times->describe(end);
        if (replay) {
            invocation["compile_events"] = log.events(start);
            if (tests) invocation["test_output"] = std::move(test_output);
            if (tiers) {
                const auto statistics = tiers->statistics();
                invocation["lift_proof_us"] = statistics.hint_microseconds;
                invocation["lift_search_us"] = statistics.search_microseconds;
            }
        }
        invocations.push_back(std::move(invocation));
    }
    report.write({{"version", 1}, {"parallel_workers", limits.parallel_workers},
        {"max_heap_cells", limits.max_heap_cells}, {"virtual_time", options.virtual_time},
        {"image_read_ns", nanos(read_end - read_start)}, {"image_decode_ns", nanos(decode_end - read_end)},
        {"catalog_read_ns", nanos(catalog_end - decode_end)}, {"host_bind_ns", nanos(bindings_end - catalog_end)},
        {"tier_cache_setup_ns", nanos(cache_end - bindings_end)}, {"invocations", std::move(invocations)}});
    return captured ? 0 : status;
}

} // namespace

int main(int argc, char** argv) try {
    const auto started = Clock::now();
    if (argc == 2 && std::string_view(argv[1]) == "--abi") {
        std::cout << Json{{"version", 1}, {"receipt_fd_protocol", 1}, {"compiler", NEURA_BENCH_COMPILER},
            {"headers_sha256", NEURA_BENCH_HEADER_SHA256}, {"options_size", sizeof(cli::Options)},
            {"run_limits_size", sizeof(nvm::RunLimits)}, {"tier_cache_size", sizeof(nvm::TierCache)}}.dump() << '\n';
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << tool::version_line() << '\n';
        return 0;
    }
    check_stop();
    if (argc < 4) throw std::runtime_error("expected suite-build, program-build, cold, warm, or replay");
    const std::string_view action = argv[1];
    const Report report(argv[2]);
    if (action == "suite-build" || action == "program-build") {
        const bool suite = action == "suite-build";
        if (argc < (suite ? 6 : 5)) throw std::runtime_error("benchmark build requires output and source paths");
        const fs::path image = argv[3];
        const fs::path catalog = suite ? argv[4] : "";
        if (image == report.path() || (suite && (image == catalog || report.path() == catalog)))
            throw std::runtime_error("benchmark output paths must differ");
        std::vector<std::string> arguments{argv[0], suite ? "test" : "build"};
        if (!suite) arguments.insert(arguments.end(), {"-o", image.string()});
        arguments.insert(arguments.end(), argv + (suite ? 5 : 4), argv + argc);
        cli::benchmark::BuildTiming timing;
        DiagnosticSink diagnostics;
        const DiagnosticSinkScope diagnostic_scope(diagnostics);
        const auto options = parse_options(arguments);
        const auto status = suite ? cli::benchmark::export_suite(options, image, catalog, timing)
                                  : cli::benchmark::export_program(options, timing);
        if (!diagnostics.entries().empty()) {
            cli::DiagnosticPrinter printer(options.diagnostics, {}, SourceMap::global());
            if (printer.warnings(std::cerr, diagnostics.entries()) > 0 && options.diagnostics.warnings_are_errors)
                return 1;
        }
        if (status == 0) {
            const auto validated = timing.validated.value_or(timing.encoded);
            report.write({{"version", 1}, {"compile_ns", nanos(timing.encoded - started)},
                {"encoded_at_ns", nanos(timing.encoded.time_since_epoch())},
                {"source_through_encoding_cpu_seconds", timing.encoded_cpu == std::clock_t{-1} ? Json() :
                    Json(static_cast<double>(timing.encoded_cpu) / CLOCKS_PER_SEC)},
                {"image_validation_ns", nanos(validated - timing.encoded)},
                {"image_write_ns", nanos(timing.written - validated)}});
        }
        return status;
    }
    if (action != "warm" && action != "cold" && action != "replay")
        throw std::runtime_error("unknown benchmark action");
    int first = 3;
    std::uint32_t warmups = 0;
    std::uint32_t samples = 1;
    if (action == "warm") {
        if (argc < 7) throw std::runtime_error("warm requires warmup and sample counts");
        warmups = iterations(argv[first++]);
        samples = iterations(argv[first++]);
        if (!warmups || !samples) throw std::runtime_error("positive warmup and sample counts required");
    }
    fs::path catalog;
    if (std::string_view(argv[first]) == "--suite") {
        if (argc < first + 3) throw std::runtime_error("suite replay requires a catalog and execution arguments");
        catalog = argv[first + 1];
        first += 2;
    }
    std::vector<std::string> arguments{argv[0]};
    arguments.insert(arguments.end(), argv + first, argv + argc);
    return execute(parse_options(arguments), action, report, warmups, samples, catalog);
} catch (const std::exception& error) {
    std::cerr << "neura-held-out: " << error.what() << '\n';
    return 1;
}
