#include "fleet_tests.hpp"
#include "graph_test_support.hpp"
#include "nvm/fleet.hpp"
#include "nvm/placement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace neura::testing {
namespace {

using nvm::AutoDevice;
using nvm::CpuModel;
using nvm::DeviceModel;
using nvm::GraphJob;
using nvm::GraphShape;

constexpr std::size_t batch_size = 512;
constexpr std::size_t opening_call = 134;

void check_near(double actual, double expected, double tolerance, const char* label) {
    if (std::abs(actual - expected) <= tolerance) return;
    fail(std::string(label) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

nvr::Graph wide_graph() {
    // L=1, E=N=1024, C=16, W=7. Under the fixed optimistic and
    // nominal models a 512-job call adds 1124.80376 microseconds of gain.
    return {1, std::vector<nvr::Node>(1024, {nvr::Activation::Identity, 0, {{0, 1}}}), {1}};
}

nvr::Graph chain_graph(std::uint32_t count) {
    nvr::Graph graph;
    graph.input_lanes = 1;
    for (std::uint32_t wire = 0; wire < count; ++wire) {
        graph.nodes.push_back({nvr::Activation::Identity, 0, {{wire, 1}}});
    }
    graph.outputs = {count};
    return graph;
}

class Jobs {
public:
    Jobs(std::uint32_t graph, std::size_t count) : outputs(count) {
        for (auto& output : outputs) jobs.push_back({graph, input, std::span(&output, 1)});
    }
    Jobs(const Jobs&) = delete;
    Jobs& operator=(const Jobs&) = delete;

    std::array<std::int64_t, 1> input{1};
    std::vector<std::int64_t> outputs;
    std::vector<GraphJob> jobs;
};

// Each inner evaluate uses two reads. Tests can prescribe its elapsed time
// without depending on the speed of the graph model or the caller.
struct ScriptedClock {
    double elapsed = 0;
    double now = 0;
    std::uint64_t reads = 0;

    double read() {
        if (reads++ % 2 != 0) now += elapsed;
        return now;
    }
};

class FailingModel final : public nvm::GraphDevice {
public:
    explicit FailingModel(const nvr::Image& image) : model_(image) {}
    const nvr::Image& image() const override { return model_.image(); }
    std::size_t batch_hint() const override { return model_.batch_hint(); }
    std::string_view failure_reason() const override { return failed_ ? "injected failure" : ""; }
    nvm::GraphDeviceStatistics statistics() const override {
        auto counters = model_.statistics();
        counters.declined_failed += declined_failed_;
        return counters;
    }
    void evaluate(std::span<GraphJob> jobs) override {
        if (!fail_next) {
            model_.evaluate(jobs);
            return;
        }
        failed_ = true;
        declined_failed_ += jobs.size();
        for (auto& job : jobs) job.evaluated = false;
    }

    bool fail_next = false;

private:
    // Intentionally keep graph_shapes() opaque to check that the wrapper
    // preserves the inner device's decline accounting after escape.
    nvm::ModelDevice model_;
    bool failed_ = false;
    std::uint64_t declined_failed_ = 0;
};

void offer_opening_work(AutoDevice& device, std::uint32_t graph = 0) {
    Jobs work(graph, batch_size);
    for (std::size_t call = 1; call <= opening_call; ++call) {
        check(device.state() == AutoDevice::State::Gated, "device stayed gated up to the opening call");
        device.evaluate(work.jobs);
        check(std::ranges::none_of(work.jobs, &GraphJob::evaluated), "all gated jobs, even the opening call, decline");
    }
    check(device.state() != AutoDevice::State::Gated, "the budget was crossed on call 134");
    check_equal(device.statistics().declined_placement, opening_call * batch_size, "gated placement declines");
}

constexpr DeviceModel quick_model{1.0 / 128, 1.0 / 256, 0, 64, 0};
constexpr CpuModel slow_cpu{1000, 0, 0};

} // namespace

NEURA_TEST(placement, calibration_fit_and_clamps) {
    const auto device = nvm::fit_device({9.2, 253.9, 4605.6, 15.2, 371.9, 152.6});
    check_near(device.a, 9.2, 1e-12, "RTX round trip a");
    check_near(device.b0, 0.343, 0.0005, "RTX layer cost b0");
    check_near(device.b1, 0.135, 0.0005, "RTX edge cost b1");
    check_equal(std::lround(device.K), 2123L, "RTX side-by-side requests K");
    check_near(device.d, 0.000259, 0.0000005, "RTX I/O cost d");

    const auto clamped = nvm::fit_device({4, 3, 2, 5, 3, 1});
    check(clamped.a == 4 && clamped.b0 == 0 && clamped.b1 == 0 && clamped.K == 1 && clamped.d == 0,
          "negative costs and K below 1 clamp");
    const auto unbounded = nvm::fit_device({1, 100, 100, 1, 100, 1});
    check_equal(unbounded.K, 65536.0, "K clamps even when the repeated dispatch took the same time");

    const auto cpu = nvm::fit_cpu({0.0828, 1.5136, 27.3184});
    check_near(cpu.c0, 0.08, 1e-12, "CPU fixed work c0");
    check_near(cpu.c1, 0.0008, 1e-12, "CPU edge work c1");
    check_near(cpu.c2, 0.002, 1e-12, "CPU node work c2");
    const auto descending = nvm::fit_cpu({3, 2, 1});
    check(descending.c0 == 3 && descending.c1 == 0 && descending.c2 == 0, "negative CPU slopes clamp");
    const auto negative_fixed = nvm::fit_cpu({1, 1023, 1023});
    check(negative_fixed.c0 == 0 && negative_fixed.c1 == 0 && negative_fixed.c2 == 2, "negative CPU intercept clamps");
}

NEURA_TEST(placement, calibration_probes_have_the_specified_shapes) {
    const auto image = nvm::probe_image();
    const auto tables = nvm::pack_layered(image);
    using Shape = std::array<std::uint64_t, 5>;
    const std::array expected{
        Shape{1, 1, 1, 1, 7}, Shape{512, 512, 512, 512, 7},
        Shape{512, 32'768, 512, 32'768, 70}, Shape{1, 256, 256, 4, 517},
    };
    check_equal(tables.shapes.size(), expected.size(), "calibration graph count");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& shape = tables.shapes[i];
        check(shape.eligible && Shape{shape.layers, shape.edges, shape.nodes, shape.critical, shape.io_words} == expected[i],
              "calibration graph has the specified L, E, N, C, W shape");
        const auto& graph = image.graphs[i];
        std::vector<std::int64_t> wires(graph.input_lanes, 1);
        check(!nvm::evaluate_graph(graph, wires), "calibration graph does not overflow");
        for (const auto wire : graph.outputs) check_equal(wires[wire], std::int64_t{1}, "all-one probe output");
    }
}

NEURA_TEST(placement, prefix_gain_and_ties) {
    const std::array shapes{
        GraphShape{true, nvm::IneligibleReason::none, 1, 1, 1, 1, 7},
        GraphShape{true, nvm::IneligibleReason::none, 10, 10, 10, 10, 7},
        GraphShape{false, nvm::IneligibleReason::no_nodes, 0, 0, 0, 0, 7},
    };
    const CpuModel cpu{12, 0, 0};
    const DeviceModel device{10, 2, 0, 10, 0};
    const std::array one{GraphJob{0, {}, {}}};
    check(nvm::place_jobs(shapes, one, device, cpu).jobs.empty(), "one shallow graph is below break-even");

    // Each shallow job contributes 8.8, each deep job 7. The extra
    // critical cost of including the deep graph is 18.
    const std::array mixed{GraphJob{1, {}, {}}, GraphJob{0, {}, {}}, GraphJob{2, {}, {}}, GraphJob{0, {}, {}}};
    const auto shallow_only = nvm::place_jobs(shapes, mixed, device, cpu);
    check(shallow_only.jobs == std::vector<std::size_t>{1, 3}, "the deep and ineligible graphs are left on the CPU");
    check_near(shallow_only.gain, 5.6, 1e-12, "shallow batch gain");
    check_near(shallow_only.predicted, 12.4, 1e-12, "shallow dispatch time");

    const std::array larger{mixed[0], mixed[1], mixed[0], mixed[2], mixed[3], mixed[0]};
    const auto both = nvm::place_jobs(shapes, larger, device, cpu);
    check(both.jobs == std::vector<std::size_t>{0, 1, 2, 4, 5}, "three deep jobs repay the cost in original job order");
    check_near(both.gain, 8.6, 1e-12, "mixed batch gain");
    check_near(both.predicted, 36.4, 1e-12, "mixed dispatch time");

    auto tied_shapes = shapes;
    tied_shapes[1].layers = 2;
    const std::array tied{mixed[0], mixed[1]};
    const auto tie = nvm::place_jobs(tied_shapes, tied, DeviceModel{2, 4, 0, 1, 0}, CpuModel{16, 0, 0});
    check(tie.jobs == std::vector<std::size_t>{1} && tie.gain == 2 && tie.predicted == 10,
          "equal prefix gains choose the shorter prefix");
    const auto zero = nvm::place_jobs(tied_shapes, one, DeviceModel{4, 4, 0, 1, 0}, CpuModel{16, 0, 0});
    check(zero.jobs.empty() && zero.gain == 0, "a gain of exactly zero declines");
}

NEURA_TEST(placement, prospect_budget_giveup_and_verdict) {
    nvr::Image image;
    image.graphs = {wide_graph(), chain_graph(20'000)};
    std::size_t opens = 0;
    AutoDevice no_prospect(image, [&](std::string&) -> std::optional<nvm::Opened> {
        ++opens;
        return std::nullopt;
    });
    check(no_prospect.state() == AutoDevice::State::Rejected && no_prospect.batch_hint() == 1 &&
          no_prospect.reason() == "no graph can gain at this batch size" && no_prospect.device_name().empty() &&
          opens == 0, "no single job can repay opening a device");

    // Both verdicts see the same 1,024-node graph and fixed CPU estimate.
    // The RTX model wins at width 512; llvmpipe's per-job cost alone
    // exceeds 0.75 * 0.13 on this graph.
    const DeviceModel rtx{9.2, 0.343, 0.135, 2123, 0.000259};
    const DeviceModel software{25.1, 0.416, 0.007, 5, 0.001895};
    const CpuModel fixed_cpu{0.13, 0, 0};
    for (const auto& [model, accept] : {std::pair{rtx, true}, std::pair{rtx, true}, std::pair{software, false}}) {
        nvr::Image verdict_image;
        verdict_image.graphs = {wide_graph()};
        AutoDevice automatic(verdict_image, [&](std::string&) {
            ++opens;
            return nvm::Opened{std::make_unique<nvm::ModelDevice>(verdict_image), model, fixed_cpu, "fixed models"};
        }, {batch_size, {}});
        check(automatic.batch_hint() == batch_size && automatic.device_name().empty(), "gated device has no name yet");
        offer_opening_work(automatic);
        check(automatic.device_name() == "fixed models", "an opened device keeps its name");
        if (accept) {
            check(automatic.state() == AutoDevice::State::Active && automatic.batch_hint() == batch_size &&
                  automatic.reason().empty(), "the RTX 5080 parameters pass the verdict");
        } else {
            check(automatic.state() == AutoDevice::State::Rejected && automatic.batch_hint() == 1 &&
                  automatic.reason() == "the device cannot beat the CPU", "llvmpipe parameters fail the same verdict");
        }
    }
    check_equal(opens, std::size_t{3}, "one open per run, on the same call on both repeated runs");

    AutoDevice unavailable(image, [](std::string& reason) -> std::optional<nvm::Opened> {
        reason = "probe failed";
        return std::nullopt;
    }, {batch_size, {}});
    offer_opening_work(unavailable);
    check(unavailable.state() == AutoDevice::State::Rejected && unavailable.device_name().empty() &&
          unavailable.reason() == "unavailable: probe failed", "opening failure is reported and stays rejected");

    AutoDevice no_gain(image, [&](std::string&) -> std::optional<nvm::Opened> {
        ++opens;
        return std::nullopt;
    }, {8, {}});
    Jobs expensive(1, 8);
    // 8 jobs * (0.08 + (0.0008 + 0.002) * 20,000) = 448.64 per call.
    // Their estimated gain is zero, and work reaches 3,000,000 on 6,687.
    for (std::size_t call = 1; call <= 6'687; ++call) {
        check(no_gain.state() == AutoDevice::State::Gated, "work remained below the give-up threshold");
        no_gain.evaluate(expensive.jobs);
    }
    check(no_gain.state() == AutoDevice::State::Rejected && no_gain.reason() == "the workload cannot gain" &&
          no_gain.statistics().declined_placement == 6'687 * 8 && opens == 3, "the give-up call rejects without opening");
}

NEURA_TEST(placement, mixed_graph_gate_opens_on_fixed_call) {
    nvr::Image image;
    image.graphs = {wide_graph(), chain_graph(1)};
    Jobs wide(0, batch_size / 2);
    Jobs narrow(1, batch_size / 2);
    std::vector<GraphJob> mixed;
    for (std::size_t i = 0; i < wide.jobs.size(); ++i) {
        mixed.push_back(wide.jobs[i]);
        mixed.push_back(narrow.jobs[i]);
    }
    // 256 copies of each shape give 574.9 microseconds per call when both
    // groups are selected. Call 260 is below 150,000 and call 261 above.
    constexpr std::size_t expected_open = 261;
    for (int run = 0; run < 2; ++run) {
        std::size_t call = 0;
        std::size_t opened = 0;
        AutoDevice device(image, [&](std::string&) {
            opened = call;
            return nvm::Opened{std::make_unique<nvm::ModelDevice>(image), quick_model, slow_cpu, "model"};
        }, {batch_size, {}});
        for (call = 1; call <= expected_open; ++call) {
            check(device.state() == AutoDevice::State::Gated, "mixed sequence stayed gated before the opening call");
            device.evaluate(mixed);
            check(std::ranges::none_of(mixed, &GraphJob::evaluated), "mixed gate declines every job up to opening");
        }
        check_equal(opened, expected_open, "mixed sequence opened at the fixed call");
        check(device.state() == AutoDevice::State::Active, "mixed sequence activated the model");
    }
}

NEURA_TEST(placement, active_state_statistics_and_correction) {
    nvr::Image image;
    image.graphs = {wide_graph(), {1, {}, {0}}, {1, {{nvr::Activation::Identity, 0, {{0, 40000}}}}, {1}}};
    ScriptedClock clock;
    FailingModel* scripted = nullptr;
    AutoDevice automatic(image, [&](std::string&) {
        auto model = std::make_unique<FailingModel>(image);
        scripted = model.get();
        return nvm::Opened{std::move(model), quick_model, slow_cpu, "scripted model"};
    }, {batch_size, [&] { return clock.read(); }});
    offer_opening_work(automatic);
    check(automatic.state() == AutoDevice::State::Active && automatic.correction() == 1 && clock.reads == 0,
          "opening does not sample elapsed time");

    Jobs shallow(0, 1);
    // Predicted = 1/128 + 1/256 + (1/256)/64. A 1.5x sample
    // increases the correction by (1.5 - 1)/8 = 1/16.
    clock.elapsed = (1.0 / 128 + 1.0 / 256 + 1.0 / 16384) * 1.5;
    automatic.evaluate(shallow.jobs);
    check(shallow.jobs[0].evaluated && shallow.outputs[0] == 1 && clock.reads == 2, "active device evaluates one job");
    check_equal(automatic.correction(), 1.0625, "a sample inside the relative clamps");

    clock.elapsed = 100;
    automatic.evaluate(shallow.jobs);
    check_equal(automatic.correction(), 1.1953125, "a slow sample clamps to twice the previous correction");
    for (int call = 0; call < 50; ++call) automatic.evaluate(shallow.jobs);
    check_equal(automatic.correction(), 64.0, "absolute upper correction clamp");

    clock.elapsed = 0;
    automatic.evaluate(shallow.jobs);
    check_equal(automatic.correction(), 60.0, "a fast sample clamps to half the previous correction");
    for (int call = 0; call < 120; ++call) automatic.evaluate(shallow.jobs);
    check_equal(automatic.correction(), 1.0 / 16, "absolute lower correction clamp");
    constexpr std::uint64_t evaluated = 173;
    check(automatic.statistics().evaluated == evaluated && automatic.statistics().dispatches == evaluated,
          "one model dispatch for each sampled job");

    Jobs ineligible(1, 1);
    automatic.evaluate(ineligible.jobs);
    check(!ineligible.jobs[0].evaluated && automatic.statistics().declined_placement == batch_size * opening_call + 1,
          "a statically ineligible graph is declined by placement");

    Jobs escaping(2, 1);
    automatic.evaluate(escaping.jobs);
    check(!escaping.jobs[0].evaluated && automatic.statistics().escaped == 1, "escape is reported by the inner model");
    automatic.evaluate(escaping.jobs);
    check(!escaping.jobs[0].evaluated && automatic.statistics().declined_ineligible == 1,
          "the opaque inner model reports its demotion");

    scripted->fail_next = true;
    automatic.evaluate(shallow.jobs);
    check(!shallow.jobs[0].evaluated && automatic.state() == AutoDevice::State::Rejected && automatic.batch_hint() == 1 &&
          automatic.reason() == "device failed: injected failure" && automatic.device_name() == "scripted model",
          "inner device failure rejects while preserving its name");
    const auto counts = automatic.statistics();
    check(counts.evaluated == evaluated && counts.declined_placement == batch_size * opening_call + 1 &&
          counts.declined_ineligible == 1 && counts.escaped == 1 && counts.declined_failed == 1 &&
          counts.dispatches == evaluated + 1, "every final counter includes the inner device's work");
    const auto reads_at_rejection = clock.reads;
    shallow.jobs[0].evaluated = true;
    automatic.evaluate(shallow.jobs);
    const auto rejected = automatic.statistics();
    check(!shallow.jobs[0].evaluated && clock.reads == reads_at_rejection && rejected.evaluated == counts.evaluated &&
          rejected.declined_placement == counts.declined_placement && rejected.declined_ineligible == counts.declined_ineligible &&
          rejected.escaped == counts.escaped && rejected.declined_failed == counts.declined_failed &&
          rejected.dispatches == counts.dispatches, "a rejected wrapper declines without accounting or retrying");

    AutoDevice shape_reporting(image, [&](std::string&) {
        return nvm::Opened{std::make_unique<nvm::ModelDevice>(image), quick_model, slow_cpu, "model"};
    }, {batch_size, [&] { return clock.read(); }});
    offer_opening_work(shape_reporting);
    shape_reporting.evaluate(escaping.jobs);
    shape_reporting.evaluate(escaping.jobs);
    const auto exposed = shape_reporting.statistics();
    check(exposed.escaped == 1 && exposed.declined_placement == batch_size * opening_call + 1 &&
          exposed.declined_ineligible == 0, "exposed inner shapes prevent another placement after demotion");
}

NEURA_TEST(placement, corpus_fleets_match_serial_runs) {
    std::uint64_t evaluated = 0;
    std::uint64_t declined = 0;
    for (const auto& item : image_corpus(environment)) {
        auto image = nvr::load(item.bytes);
        if (image.entry == nvr::absent) continue;
        const auto limits = fleet_corpus_limits(item);
        const auto reference = capture_serial_members(image, limits);
        const auto warmup = static_cast<std::uint32_t>(image.graphs.size());
        image.graphs.push_back(wide_graph());
        ScriptedClock clock{.elapsed = 8};
        AutoDevice automatic(image, [&](std::string&) {
            // One dispatch costs eight microseconds. Expensive rounds can
            // repay it; cheap rounds stay on the CPU. The clock keeps s=1.
            return nvm::Opened{std::make_unique<nvm::ModelDevice>(image), {8, 0, 0, 1, 0}, {0.2, 0, 0.01}, "model"};
        }, {batch_size, [&] { return clock.read(); }});
        // The unused warmup graph opens the gate without changing any
        // corpus block or issuing any host effect.
        offer_opening_work(automatic, warmup);
        check(automatic.state() == AutoDevice::State::Active, item.label + " automatic device activated");
        const auto before = automatic.statistics();
        nvm::VerifyingDevice verifying(automatic);
        check_corpus_fleet(image, limits, &verifying, 4096, reference, item.label + " with auto placement");
        const auto after = automatic.statistics();
        evaluated += after.evaluated - before.evaluated;
        declined += after.declined_placement - before.declined_placement;
        check_equal(automatic.correction(), 1.0, "scripted elapsed time matches the model throughout corpus");
    }
    check(evaluated > 0 && declined > 0, "corpus fleets exercised both device placement and CPU fallback");
}

NEURA_TEST(placement, zero_predicted_and_elapsed_leave_correction_unchanged) {
    nvr::Image image;
    image.graphs = {wide_graph()};
    ScriptedClock clock;
    AutoDevice device(image, [&](std::string&) {
        return nvm::Opened{std::make_unique<nvm::ModelDevice>(image), {0, 0, 0, 1, 0}, slow_cpu, "model"};
    }, {batch_size, [&] { return clock.read(); }});
    offer_opening_work(device);
    Jobs batch(0, 1);
    device.evaluate(batch.jobs);
    check(batch.jobs[0].evaluated && clock.reads == 2, "the zero-time dispatch was evaluated and timed");
    check_equal(device.correction(), 1.0, "zero predicted and measured time leave correction unchanged");
}

NEURA_TEST(placement, prepared_corpus_matches_eager_and_serial) {
    const auto& corpus = image_corpus(environment);
    check(!corpus.empty(), "prepared corpus is populated");
    std::size_t entries = 0;
    for (const auto window : {1U, 3U, 4'096U}) {
        std::uint64_t evaluated = 0;
        std::uint64_t declined = 0;
        for (const auto& item : corpus) {
            auto image = nvr::load(item.bytes);
            if (image.entry == nvr::absent) continue;
            ++entries;
            const auto limits = fleet_corpus_limits(item);
            const auto reference = capture_serial_members(image, limits);
            const auto warmup = static_cast<std::uint32_t>(image.graphs.size());
            image.graphs.push_back(wide_graph());
            const auto open_model = [&](std::string&) {
                return nvm::Opened{std::make_unique<nvm::ModelDevice>(image), {8, 0, 0, 1, 0},
                    {0.2, 0, 0.01}, "model"};
            };
            const auto label = item.label + " with window " + std::to_string(window);
            ScriptedClock prepared_clock{.elapsed = 8};
            ScriptedClock eager_clock{.elapsed = 8};
            AutoDevice prepared(image, open_model, {batch_size, [&] { return prepared_clock.read(); }});
            AutoDevice eager(image, open_model, {batch_size, [&] { return eager_clock.read(); }});
            for (auto* device : {&prepared, &eager}) {
                offer_opening_work(*device, warmup);
                check(device->state() == AutoDevice::State::Active, label + " model activated");
            }
            nvm::VerifyingDevice eager_only(eager);
            const auto prepared_rounds = check_corpus_fleet(image, limits, &prepared, window, reference, label + " prepared");
            const auto eager_rounds = check_corpus_fleet(image, limits, &eager_only, window, reference, label + " eager");
            check(prepared_rounds == eager_rounds, label + " rounds at member completion");
            const auto counters = prepared.statistics();
            check_equal(counters.evaluated, eager.statistics().evaluated, label + " evaluated jobs");
            check_equal(counters.declined_placement, eager.statistics().declined_placement, label + " placement declines");
            evaluated += counters.evaluated;
            declined += counters.declined_placement - opening_call * batch_size;
        }
        check(evaluated > 0 && declined > 0, "prepared corpus placed and declined jobs at window " + std::to_string(window));
    }
    check(entries != 0, "prepared corpus ran entry points");
}

NEURA_TEST(placement, prepared_and_eager_fleets_agree) {
    auto image = hello_model();
    image.graphs.push_back(wide_graph());
    image.graphs.push_back(chain_graph(1));
    image.constants.push_back({1, {0}});
    image.constants.push_back({1, {1}});
    image.constants.push_back({3, {'s', 'e', 'c', 'o', 'n', 'd'}});
    image.constants.push_back({2, {7, 0, 0, 0}});
    const auto trap = image.functions[0].blocks[2].exits[0];
    const auto branch = [&](std::uint32_t graph, bool input, std::uint32_t target) {
        nvr::Block block;
        block.graph = graph;
        block.inputs = {{nvr::ValueKind::Constant, 1, input ? 3U : 2U}};
        block.exits = {trap, trap};
        auto& success = block.exits[input ? 1 : 0];
        success = {};
        success.kind = nvr::ExitKind::Continue;
        success.resume = target;
        return block;
    };
    auto& first = image.functions[0];
    auto second = first;
    first.blocks.insert(first.blocks.begin(), {branch(1, true, 1), branch(2, false, 2)});
    second.blocks.insert(second.blocks.begin(), {branch(2, true, 1), branch(1, false, 2)});
    for (auto* function : {&first, &second}) {
        auto& request = function->blocks[2].exits[0];
        request.resume = 3;
        request.error_resume = 4;
    }
    second.blocks[2].exits[0].arguments[0].index = 4;
    second.blocks[3].exits[0].arguments[0].index = 5;
    image.functions.push_back(std::move(second));
    image = nvr::load(nvr::encode(image));

    const auto open_model = [&](std::string&) {
        // Sending one wide job costs a fixed 8 plus 7 I/O words. The
        // single-neuron job loses even without the fixed dispatch cost.
        return nvm::Opened{std::make_unique<nvm::ModelDevice>(image), {8, 0, 0, 1, 1}, {0, 0, 1}, "model"};
    };
    ScriptedClock prepared_clock{.elapsed = 15};
    ScriptedClock eager_clock{.elapsed = 15};
    AutoDevice prepared(image, open_model, {batch_size, [&] { return prepared_clock.read(); }});
    AutoDevice eager(image, open_model, {batch_size, [&] { return eager_clock.read(); }});
    offer_opening_work(prepared, 1);
    offer_opening_work(eager, 1);
    check(prepared.state() == AutoDevice::State::Active && eager.state() == AutoDevice::State::Active,
          "both auto devices are active before the fleet starts");

    // VerifyingDevice uses the eager interface. Direct AutoDevice chooses
    // first and materializes only the wide job, at a different batch index
    // and with a different output bit in each of the first two rounds.
    nvm::VerifyingDevice verifying(eager);
    nvm::RunLimits limits;
    limits.profile = true;
    nvm::Fleet prepared_fleet(image, {limits, &prepared, 2});
    nvm::Fleet eager_fleet(image, {limits, &verifying, 2});
    for (auto* fleet : {&prepared_fleet, &eager_fleet}) {
        fleet->add(0, {});
        fleet->add(1, {});
    }
    CapturingConsole prepared_console;
    CapturingConsole eager_console;
    constexpr std::array<std::uint64_t, 4> rounds{3, 4, 4, 5};
    for (std::size_t i = 0; i < rounds.size(); ++i) {
        const auto actual = prepared_fleet.advance();
        const auto expected = eager_fleet.advance();
        check(actual.member == i / 2 && actual.member == expected.member, "both fleets report members in order");
        check_equal(prepared_fleet.rounds(), rounds[i], "prepared round at event");
        check_equal(eager_fleet.rounds(), rounds[i], "eager round at event");
        if (i % 2 == 0) {
            check(actual.event.kind == nvm::Event::Kind::HostRequest && actual.event.kind == expected.event.kind,
                  "each member first presents its host request");
            check_same_request(actual.event.request, expected.event.request, "prepared host request");
            prepared_fleet.complete(prepared_console.invoke(actual.event.request));
            eager_fleet.complete(eager_console.invoke(expected.event.request));
        } else {
            const auto status = i == 1 ? 0 : 7;
            check(actual.event.kind == nvm::Event::Kind::Completed && actual.event.kind == expected.event.kind &&
                  actual.event.status == status && actual.event.result == expected.event.result &&
                  actual.result.steps == 4, "both prepared and CPU-completed blocks preserve the result");
            check_same_run(actual.result, expected.result, "prepared final result");
        }
    }
    check(prepared_fleet.done() && eager_fleet.done(), "both fleets completed");
    check_equal(prepared_console.output(), eager_console.output(), "prepared and eager console bytes");
    check_equal(prepared_console.output(), std::string("hi\nsecond\n"), "serial console order");
    for (auto* device : {&prepared, &eager}) {
        const auto stats = device->statistics();
        check(stats.evaluated == 2 && stats.declined_placement == batch_size * opening_call + 2 &&
              stats.declined_ineligible == 0 && stats.escaped == 0 && stats.declined_failed == 0 &&
              stats.dispatches == 2 && device->correction() == 1, "both devices count the same selected and declined jobs");
    }
    check(prepared_clock.reads == 4 && eager_clock.reads == 4, "only the two selected batches were timed");
}

} // namespace neura::testing
