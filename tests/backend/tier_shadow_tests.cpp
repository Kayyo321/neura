// The shadow-run oracle of NEURA_TIER_CHECK=2 (docs/runtime/NVM.md,
// "Controls"; src/nvm/tier_shadow.inc): a run checked against a shadow at
// tier 1 reports no difference and does what it does without the shadow; a
// difference planted in the run is reported, naming what differs; the
// shadow makes no host request; and a mode the oracle cannot check says so
// and runs as before. The tests turn the oracle on through
// RunLimits::tier_shadow rather than the environment. Part of the `tier`
// suite (backend-tier).
#include "graph_test_support.hpp"

#include "nvm/fleet.hpp"
#include "nvm/graph_device.hpp"
#include "nvm/schedule.hpp"
#include "nvm/tier1.hpp"

#include <algorithm>
#include <functional>
#include <tuple>

namespace neura::testing {
namespace {

using Perturb = nvm::TierShadowHooks::Perturb;

// Lifts every block and forms every region the first time it runs.
nvm::TierOptions eager_regions() {
    nvm::TierOptions options;
    options.hint_after = 1;
    options.search_after = 1;
    options.minimum_proof_work = std::uint64_t{1} << 30;
    options.region_after = 1;
    return options;
}

nvm::RunLimits tier_2(nvm::TierCache* cache) {
    nvm::RunLimits limits;
    limits.max_steps = 20'000'000;
    limits.tier_cache = cache;
    limits.profile = true;
    return limits;
}

bool regions_enabled() { return nvm::tier_ceiling() >= 2; }

void check_same(const Captured& actual, const Captured& expected, const std::string& label) {
    check_same_run(actual.result, expected.result, label);
    check_equal(actual.output, expected.output, label + " output");
    check_equal(actual.requests.size(), expected.requests.size(), label + " request count");
    for (std::size_t i = 0; i < std::min(actual.requests.size(), expected.requests.size()); ++i) {
        check_same_request(actual.requests[i], expected.requests[i], label);
    }
}

// The oracle's hooks with their reports and notices collected.
struct Hooks {
    nvm::TierShadowHooks hooks;
    std::vector<std::string> reports;
    std::vector<std::string> notices;

    Hooks() {
        hooks.report = [this](const std::string& text) { reports.push_back(text); };
        hooks.notice = [this](const std::string& text) { notices.push_back(text); };
    }
    Hooks(const Hooks&) = delete;
    Hooks& operator=(const Hooks&) = delete;
};

// What a report says differs: the text after "the run is now in ...: ".
std::string item_of(const std::string& report) {
    const auto now = report.find("the run is now in ");
    if (now == std::string::npos) return report;
    const auto colon = report.find("): ", now);
    return colon == std::string::npos ? report : report.substr(colon + 3);
}

bool starts_with(const std::string& text, std::string_view prefix) { return text.rfind(prefix, 0) == 0; }
bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

using Adjust = std::function<void(nvm::RunLimits&)>;

// Runs `image` at tier 2 without the oracle and with it (each with a fresh
// eager cache) and checks that the oracle reached sync points, reported no
// difference, said nothing about modes, and left the run as it was.
void check_clean(const nvr::Image& image, const std::string& label, const std::vector<std::string>& arguments = {},
                 const Adjust& adjust = {}, std::optional<std::size_t> capacity = std::nullopt) {
    nvm::TierCache plain_cache(image, eager_regions());
    auto plain_limits = tier_2(&plain_cache);
    if (adjust) adjust(plain_limits);
    const auto plain = run_captured(image, plain_limits, capacity, arguments);
    nvm::TierCache cache(image, eager_regions());
    auto limits = tier_2(&cache);
    if (adjust) adjust(limits);
    Hooks hooks;
    limits.tier_shadow = &hooks.hooks;
    const auto checked = run_captured(image, limits, capacity, arguments);
    check(hooks.reports.empty(), label + ": no difference" + (hooks.reports.empty() ? "" : ": " + hooks.reports[0]));
    check(hooks.notices.empty(), label + ": no notice");
    check_same(checked, plain, label + " with the oracle");
    if (regions_enabled()) check(hooks.hooks.syncs > 0, label + ": the oracle reached sync points");
}

fs::path tier_program(const Environment& environment, const std::string& name) {
    return backend_dir(environment) / "tier" / (name + ".nra");
}

fs::path concurrency_program(const Environment& environment, const std::string& name) {
    return environment.source_dir / "tests" / "concurrency" / "programs" / (name + ".nra");
}

// neura.clock.monotonic version 1 on a virtual task clock.
class VirtualClockService final : public nvm::HostService {
public:
    explicit VirtualClockService(nvm::TaskClock& clock) : clock_(clock) {
        signature_.name = "neura.clock.monotonic";
        signature_.version = 1;
        signature_.success = nvm::HostType(nvr::TypeKind::Signed, 64);
        signature_.error = nvm::HostType(nvr::TypeKind::Unsigned, 32);
    }
    const nvm::HostSignature& signature() const override { return signature_; }
    nvm::HostCompletion invoke(const nvm::HostRequest&) override {
        nvm::HostValue now;
        now.bits = static_cast<std::uint64_t>(clock_.now());
        return nvm::HostCompletion::ok(now);
    }

private:
    nvm::TaskClock& clock_;
    nvm::HostSignature signature_;
};

// A run of an image with tasks, timers, and a clock service on virtual time.
Captured run_on_virtual_time(const nvr::Image& image, nvm::RunLimits limits) {
    nvm::VirtualTaskClock clock;
    CapturingConsole console;
    VirtualClockService monotonic(clock);
    nvm::HostRegistry registry;
    registry.add(console);
    registry.add(monotonic);
    Captured captured;
    captured.result = nvm::run(image, nvm::bind(image, registry), {}, limits, &clock);
    captured.output = console.output();
    captured.requests = std::move(console.requests);
    return captured;
}

// Runs `image` with the oracle told to perturb the run, and returns the
// item the report names. `every_heap`: the heaps are compared at every sync
// point, as under NEURA_TIER_CHECK_HEAP=1; otherwise when the oracle's
// spacing rule says, and at the end. `report` receives the whole report.
std::string perturbed_item(const nvr::Image& image, Perturb perturb, std::uint64_t at_step, const std::string& label,
                           const Adjust& adjust = {}, bool every_heap = true, std::string* report_text = nullptr) {
    nvm::TierCache cache(image, eager_regions());
    auto limits = tier_2(&cache);
    if (adjust) adjust(limits);
    Hooks hooks;
    hooks.hooks.perturb = perturb;
    hooks.hooks.at_step = at_step;
    hooks.hooks.every_heap = every_heap;
    limits.tier_shadow = &hooks.hooks;
    run_captured(image, limits);
    check(hooks.hooks.perturbed, label + ": the run was perturbed");
    check_equal(hooks.reports.size(), std::size_t{hooks.hooks.perturbed ? 1U : 0U}, label + ": one report");
    if (hooks.reports.empty()) return {};
    const auto& report = hooks.reports.front();
    check(starts_with(report, "neura: NEURA_TIER_CHECK: the shadow run differs at step "),
          label + ": the report's form: " + report);
    if (report_text) *report_text = report;
    return item_of(report);
}

} // namespace

NEURA_TEST(tier, the_shadow_finds_no_difference_in_clean_runs) {
    // Loops, calls, field and element operations, list growth, floats.
    const auto regions = build_program({tier_program(environment, "regions")});
    check_clean(regions, "regions");
    check_clean(regions, "regions with 12", {"12"});
    // Every step limit up to 400, inside and between regions.
    for (std::uint64_t steps = 1; steps <= 400; steps += 7) {
        check_clean(regions, "regions with " + std::to_string(steps) + " steps", {},
                    [&](nvm::RunLimits& limits) { limits.max_steps = steps; });
    }
    for (const std::uint32_t frames : {1U, 2U, 3U}) {
        check_clean(regions, "regions with " + std::to_string(frames) + " frames", {},
                    [&](nvm::RunLimits& limits) { limits.max_frames = frames; });
    }
    // Allocation and collection under heap limits from tight to loose.
    const auto gc = build_program({tier_program(environment, "region-gc")});
    const auto peak = run_captured(gc, tier_2(nullptr)).result.peak_heap_cells;
    for (std::uint64_t cells = 64; cells <= peak + 64; cells += std::max<std::uint64_t>(1, peak / 23)) {
        check_clean(gc, "region-gc with " + std::to_string(cells) + " heap cells", {},
                    [&](nvm::RunLimits& limits) { limits.max_heap_cells = cells; });
    }
    // Calls, recursion, objects, closures, and collections.
    for (const auto* name : {"computation", "collections", "objects", "closures", "control-and-updates"}) {
        check_clean(build_program({program_path(environment, name)}), name);
    }
    // Traps of each kind, with the step limits that stop them early.
    for (const auto* name : {"overflow", "index-out-of-bounds", "deep-recursion", "endless-loop", "message-trap",
                             "nil-receiver", "use-after-destroy", "uninitialized-field", "invalid-class-cast"}) {
        const auto image = build_program({backend_dir(environment) / "traps" / (std::string(name) + ".nra")});
        for (const std::uint64_t steps : {7ULL, 1'000ULL, 200'000ULL}) {
            check_clean(image, std::string(name) + " with " + std::to_string(steps) + " steps", {},
                        [&](nvm::RunLimits& limits) { limits.max_steps = steps; });
        }
    }
    check_clean(build_program({backend_dir(environment) / "traps" / "range-heap-limit.nra"}), "range-heap-limit", {},
                [](nvm::RunLimits& limits) { limits.max_heap_cells = 32; });
    // A console that fails: the request's error continuation.
    check_clean(regions, "regions with a failing console", {}, {}, std::size_t{20});
    // Tasks with channels under quanta that end turns inside loops.
    for (const auto* name : {"fan-out", "worker-group", "select-rotation", "channel-order", "backpressure"}) {
        const auto image = build_program({concurrency_program(environment, name)});
        for (const std::uint64_t quantum : {1ULL, 3ULL, 1024ULL}) {
            check_clean(image, std::string(name) + " with quantum " + std::to_string(quantum), {},
                        [&](nvm::RunLimits& limits) { limits.task_quantum = quantum; });
        }
    }
    // Parallel items evaluated in order, each with its own shadow.
    for (const auto* name : {"map", "traps"}) {
        check_clean(build_program({environment.source_dir / "tests" / "concurrency" / "parallel" /
                                   (std::string(name) + ".nra")}),
                    std::string("parallel ") + name);
    }
}

NEURA_TEST(tier, the_shadow_follows_timers_and_deadlines_on_the_clock_it_is_given) {
    // Timers and scope deadlines expire at the clock readings the driver
    // gives the run; the shadow is given the same readings.
    const auto image = build_program({concurrency_program(environment, "deadline")});
    for (const std::uint64_t quantum : {1ULL, 1024ULL}) {
        nvm::TierCache plain_cache(image, eager_regions());
        auto plain_limits = tier_2(&plain_cache);
        plain_limits.task_quantum = quantum;
        const auto plain = run_on_virtual_time(image, plain_limits);
        nvm::TierCache cache(image, eager_regions());
        auto limits = tier_2(&cache);
        limits.task_quantum = quantum;
        Hooks hooks;
        limits.tier_shadow = &hooks.hooks;
        const auto checked = run_on_virtual_time(image, limits);
        const auto label = "deadline with quantum " + std::to_string(quantum);
        check(hooks.reports.empty(), label + ": no difference" + (hooks.reports.empty() ? "" : ": " + hooks.reports[0]));
        check_same(checked, plain, label);
        check(plain.result.completed && contains(plain.output, "select: timer Fired"), label + ": the timer fired");
        if (regions_enabled()) check(hooks.hooks.syncs > 0, label + ": the oracle reached sync points");
    }
}

NEURA_TEST(tier, a_difference_planted_in_the_run_is_reported_by_name) {
    if (!regions_enabled()) return;
    const auto regions = build_program({tier_program(environment, "regions")});
    // In the walk's loops the innermost frame holds integers, the global
    // `total` is initialized, and Cell objects and lists are live.
    const auto slot = perturbed_item(regions, Perturb::SlotValue, 2000, "slot value");
    check(starts_with(slot, "frame ") && contains(slot, " slot ") && !contains(slot, "initialized"),
          "a slot value is named: " + slot);
    const auto flag = perturbed_item(regions, Perturb::SlotInitialized, 2000, "slot flag");
    check(starts_with(flag, "frame ") && contains(flag, " slot ") && contains(flag, " initialized: 0 in the run, 1"),
          "a slot's initialized flag is named: " + flag);
    const auto block = perturbed_item(regions, Perturb::FrameBlock, 2000, "frame block");
    check(starts_with(block, "frame ") && contains(block, " block: "), "a frame's block is named: " + block);
    const auto global = perturbed_item(regions, Perturb::Global, 0, "global");
    check(starts_with(global, "global ") && !contains(global, "initialized"), "a global is named: " + global);
    const auto field = perturbed_item(regions, Perturb::HeapField, 0, "heap field");
    check(starts_with(field, "heap handle ") && contains(field, " cell ") && !contains(field, "initialized"),
          "a heap object's field is named: " + field);
    const auto field_flag = perturbed_item(regions, Perturb::HeapInitialized, 0, "heap flag");
    check(starts_with(field_flag, "heap handle ") && contains(field_flag, " cell ") &&
              contains(field_flag, " initialized: 0 in the run, 1"),
          "a heap entry's initialized flag is named: " + field_flag);
    const auto version = perturbed_item(regions, Perturb::ListVersion, 0, "list version");
    check(starts_with(version, "heap handle ") && contains(version, " version: "), "a list's version is named: " + version);
    // Without a heap comparison at every sync point, a change made to the
    // heap near the end of the run, which nothing reads afterwards, is found
    // by the comparison at the end of the run.
    for (const auto perturb : {Perturb::HeapField, Perturb::HeapInitialized, Perturb::ListVersion}) {
        std::string report;
        const auto late = perturbed_item(regions, perturb, 9000, "a late heap change", {}, false, &report);
        check(starts_with(late, "heap handle ") && contains(report, " at a completion event;"),
              "a late heap change is found at the end of the run: " + report);
    }
    const auto cells = perturbed_item(regions, Perturb::LiveCells, 1000, "live cells");
    check(starts_with(cells, "live heap cells: "), "the live cells are named: " + cells);
    const auto steps = perturbed_item(regions, Perturb::Steps, 1000, "steps");
    check(starts_with(steps, "steps: "), "the step count is named: " + steps);
    // A channel holding values: producers fill a buffer of two.
    const auto backpressure = build_program({concurrency_program(environment, "backpressure")});
    const auto channel = perturbed_item(backpressure, Perturb::ChannelBuffer, 0, "channel buffer",
                                        [](nvm::RunLimits& limits) { limits.task_quantum = 1; });
    check(starts_with(channel, "channel ") && contains(channel, " buffer value 0: "),
          "a channel's buffer is named: " + channel);
    // A field of the scheduler's task records: the root task's count of
    // committed selects, in a program that selects.
    const auto rotation = build_program({concurrency_program(environment, "select-rotation")});
    const auto selects = perturbed_item(rotation, Perturb::TaskSelects, 0, "task selects");
    check(starts_with(selects, "task 0 selects: "), "a task's committed selects are named: " + selects);
}

NEURA_TEST(tier, a_coverage_observer_sees_each_block_once_with_the_oracle) {
    // RunLimits::block_observer belongs to the run: the shadow's steps do
    // not reach it.
    const auto regions = build_program({tier_program(environment, "regions")});
    const auto observed = [&](bool oracle) {
        nvm::TierCache cache(regions, eager_regions());
        auto limits = tier_2(&cache);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> blocks;
        limits.block_observer = [&](std::uint32_t function, std::uint32_t block) { blocks.emplace_back(function, block); };
        Hooks hooks;
        if (oracle) limits.tier_shadow = &hooks.hooks;
        const auto run = run_captured(regions, limits);
        check(hooks.reports.empty(), "the observed run: no difference");
        if (oracle && regions_enabled()) check(hooks.hooks.syncs > 0, "the observed run reached sync points");
        return std::make_pair(blocks, run);
    };
    const auto [plain, plain_run] = observed(false);
    const auto [checked, checked_run] = observed(true);
    check(!plain.empty(), "the observer saw blocks");
    check_equal(checked.size(), plain.size(), "the observer is called as often with the oracle as without it");
    check(checked == plain, "the observer sees the same blocks in the same order");
    check_same(checked_run, plain_run, "the observed run");
}

NEURA_TEST(tier, a_run_with_a_silent_oracle_behaves_as_without_one) {
    // With the oracle on (advance() hands the run to shadowed_advance()) and
    // nothing perturbed, a run makes the same host requests, scheduler
    // events, steps, output, and tier statistics as with the oracle off: a
    // program with host requests, one with tasks, and one that traps.
    struct Events final : nvm::TaskObserver {
        std::vector<std::string> seen;
        void event(const nvm::TaskEvent& event) override {
            seen.push_back(std::to_string(static_cast<int>(event.kind)) + " " + std::to_string(event.tick) + " " +
                           std::to_string(event.task) + " " + std::to_string(event.other) + " " + event.detail);
        }
    };
    const std::vector<std::pair<std::string, fs::path>> programs{
        {"regions", tier_program(environment, "regions")},
        {"fan-out", concurrency_program(environment, "fan-out")},
        {"select-rotation", concurrency_program(environment, "select-rotation")},
        {"overflow", backend_dir(environment) / "traps" / "overflow.nra"},
        {"deep-recursion", backend_dir(environment) / "traps" / "deep-recursion.nra"},
    };
    for (const auto& [name, path] : programs) {
        const auto image = build_program({path});
        const auto observed = [&](bool oracle) {
            nvm::TierCache cache(image, eager_regions());
            auto limits = tier_2(&cache);
            limits.task_quantum = 3;
            Events events;
            limits.observer = &events;
            Hooks hooks;
            if (oracle) limits.tier_shadow = &hooks.hooks;
            auto run = run_captured(image, limits);
            check(hooks.reports.empty(), name + ": no difference");
            if (oracle && regions_enabled()) check(hooks.hooks.syncs > 0, name + ": the oracle reached sync points");
            return std::make_tuple(std::move(run), std::move(events.seen), cache.statistics());
        };
        const auto [plain, plain_events, plain_statistics] = observed(false);
        const auto [checked, checked_events, checked_statistics] = observed(true);
        check_same(checked, plain, name + " with a silent oracle");
        check(checked_events == plain_events, name + ": the same scheduler events");
        // The step line of --tier-stats, and the counts of the first and
        // region lines (the first also holds proof times).
        const auto steps_line = [](const nvm::TierStatistics& statistics) {
            const auto text = nvm::describe(statistics);
            const auto start = text.find("tier: steps ");
            return start == std::string::npos ? std::string() : text.substr(start, text.find('\n', start) - start);
        };
        check_equal(steps_line(checked_statistics), steps_line(plain_statistics), name + ": the tier step line");
        const auto counts = [](const nvm::TierStatistics& s) {
            const auto& r = s.region_counts;
            return std::vector<std::uint64_t>{s.blocks, s.hinted, s.attempted, s.lifted_by_hint, s.lifted_by_search,
                                              s.hints_rejected, s.regions, s.region_blocks, r.entries, r.leaves,
                                              r.refused, r.budget, r.checks, r.selectors, r.references,
                                              r.state_traps, r.exits};
        };
        check(counts(checked_statistics) == counts(plain_statistics), name + ": the tier block and region counts");
    }
}

NEURA_TEST(tier, the_shadow_makes_no_host_request) {
    // The service counts every request it serves: a run with the oracle
    // makes as many as without it, and one with a callback that makes a
    // request of its own replays the callback into the shadow with the
    // recorded answer.
    const auto regions = build_program({tier_program(environment, "regions")});
    {
        nvm::TierCache cache(regions, eager_regions());
        auto limits = tier_2(&cache);
        Hooks hooks;
        limits.tier_shadow = &hooks.hooks;
        const auto checked = run_captured(regions, limits);
        check(hooks.reports.empty(), "regions: no difference");
        check_equal(checked.requests.size(), std::size_t{6}, "regions: each line is requested once");
        check_equal(checked.result.host_requests, std::uint64_t{6}, "regions: the run counts six requests");
    }
    auto image = hello_model();
    image.constants.push_back({3, {'n', 'e', 's', 't', 'e', 'd'}});
    image.constants.push_back({2, {37, 0, 0, 0}});
    auto callback = image.functions[0];
    callback.parameter_count = 0;
    callback.blocks[0].exits[0].arguments[0].index = 2;
    callback.blocks[1].exits[0].arguments[0].index = 3;
    image.functions.push_back(std::move(callback));
    image.callbacks = {{"neura.test.callback", 1, 1}};
    image = nvr::load(nvr::encode(image));

    class CallbackConsole final : public nvm::HostService {
    public:
        const nvm::HostSignature& signature() const override { return console.signature(); }
        nvm::HostCompletion invoke(const nvm::HostRequest& request) override {
            ++served;
            console.invoke(request);
            if (request.arguments[0].bytes == "hi") {
                const auto result = request.callbacks->call("neura.test.callback", 1, {});
                check(result && result->bits == 37, "the callback returns its result");
            }
            return nvm::HostCompletion::ok();
        }
        CapturingConsole console;
        std::size_t served = 0;
    };
    CallbackConsole console;
    nvm::HostRegistry registry;
    registry.add(console);
    nvm::TierCache cache(image, eager_regions());
    auto limits = tier_2(&cache);
    Hooks hooks;
    limits.tier_shadow = &hooks.hooks;
    const auto result = nvm::run(image, nvm::bind(image, registry), {}, limits);
    check(result.completed && result.status == 0, "the callback run completes");
    check(hooks.reports.empty(), "the callback run: no difference" + (hooks.reports.empty() ? "" : ": " + hooks.reports[0]));
    check_equal(console.served, std::size_t{2}, "the outer request and the callback's are each served once");
    check_equal(result.host_requests, std::uint64_t{2}, "the run counts both requests");
    check_equal(console.console.output(), std::string("hi\nnested\n"), "the output is written once");
    if (regions_enabled()) check(hooks.hooks.syncs >= 3, "the requests, the callback, and the end are sync points");
}

NEURA_TEST(tier, modes_the_oracle_cannot_check_say_so_and_run_as_before) {
    const auto notice = [](std::string_view reason) {
        return "neura: NEURA_TIER_CHECK=2 is not applied: " + std::string(reason) + "\n";
    };
    const auto check_off = [&](const nvr::Image& image, const std::string& label, std::string_view reason,
                               const Adjust& adjust) {
        nvm::TierCache plain_cache(image, eager_regions());
        auto plain_limits = tier_2(&plain_cache);
        adjust(plain_limits);
        const auto plain = run_captured(image, plain_limits);
        nvm::TierCache cache(image, eager_regions());
        auto limits = tier_2(&cache);
        adjust(limits);
        Hooks hooks;
        limits.tier_shadow = &hooks.hooks;
        const auto run = run_captured(image, limits);
        if (regions_enabled()) {
            check_equal(hooks.notices.size(), std::size_t{1}, label + ": one notice");
            if (!hooks.notices.empty()) check_equal(hooks.notices[0], notice(reason), label + ": the notice");
        }
        check_equal(hooks.hooks.syncs, std::uint64_t{0}, label + ": no sync point");
        check(hooks.reports.empty(), label + ": no report");
        check_same(run, plain, label + " runs as before");
    };
    const auto fan_out = build_program({concurrency_program(environment, "fan-out")});
    nvm::ExploringChooser chooser(7);
    nvm::ExploringChooser same_chooser(7);
    bool first = true;
    check_off(fan_out, "an explored schedule", "a schedule chooser picks the tasks", [&](nvm::RunLimits& limits) {
        limits.chooser = first ? &same_chooser : &chooser;
        first = false;
    });
    const auto map = build_program({environment.source_dir / "tests" / "concurrency" / "parallel" / "map.nra"});
    check_off(map, "four parallel workers", "parallel items run on more than one worker",
              [](nvm::RunLimits& limits) { limits.parallel_workers = 4; });
    const auto regions = build_program({tier_program(environment, "regions")});
    check_off(regions, "a caller's reason", "the run replays a trace",
              [](nvm::RunLimits& limits) { limits.tier_check_off = "the run replays a trace"; });

    // A graph device that declines every job: the reference evaluates.
    class DecliningDevice final : public nvm::GraphDevice {
    public:
        explicit DecliningDevice(const nvr::Image& image) : image_(image) {}
        const nvr::Image& image() const override { return image_; }
        std::size_t batch_hint() const override { return 1; }
        nvm::GraphDeviceStatistics statistics() const override { return {}; }
        void evaluate(std::span<nvm::GraphJob> jobs) override {
            for (auto& job : jobs) job.evaluated = false;
        }

    private:
        const nvr::Image& image_;
    };
    DecliningDevice device(regions);
    check_off(regions, "a graph device", "the run uses a graph device",
              [&](nvm::RunLimits& limits) { limits.graph_device = &device; });

    // Each member of a fleet says so.
    {
        nvm::TierCache cache(regions, eager_regions());
        auto limits = tier_2(&cache);
        Hooks hooks;
        limits.tier_shadow = &hooks.hooks;
        nvm::Fleet fleet(regions, {limits, nullptr, 2});
        fleet.add(regions.entry, {"3"});
        fleet.add(regions.entry, {"4"});
        CapturingConsole console;
        nvm::HostRegistry registry;
        registry.add(console);
        std::size_t finished = 0;
        nvm::run_fleet(fleet, nvm::bind(regions, registry), [&](std::size_t, const nvm::RunResult& result) {
            check(result.completed && result.status == 0, "a fleet member completes");
            ++finished;
        });
        check_equal(finished, std::size_t{2}, "both members finish");
        if (regions_enabled()) {
            check_equal(hooks.notices.size(), std::size_t{2}, "a fleet: a notice for each member");
            for (const auto& text : hooks.notices) check_equal(text, notice("the run is a member of a fleet"), "fleet notice");
        }
        check_equal(hooks.hooks.syncs, std::uint64_t{0}, "a fleet: no sync point");
    }

    // Below tier 2 nothing runs compiled code: no shadow and no notice.
    for (const std::uint8_t tier : {0, 1}) {
        nvm::TierCache cache(regions, eager_regions());
        auto limits = tier_2(&cache);
        limits.tier = tier;
        Hooks hooks;
        limits.tier_shadow = &hooks.hooks;
        run_captured(regions, limits);
        check(hooks.notices.empty() && hooks.reports.empty() && hooks.hooks.syncs == 0,
              "tier " + std::to_string(tier) + ": no shadow and no notice");
    }
}

} // namespace neura::testing
