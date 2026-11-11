#include "graph_test_support.hpp"
#include "nvm/layered.hpp"

#include <array>
#include <limits>

namespace neura::testing {
namespace {

using nvr::Activation;
using nvm::IneligibleReason;

nvr::Graph constant_lanes(std::initializer_list<std::int64_t> values) {
    nvr::Graph graph;
    for (const auto value : values) {
        graph.outputs.push_back(static_cast<std::uint32_t>(graph.nodes.size()));
        graph.nodes.push_back({Activation::Identity, value, {}});
    }
    return graph;
}

bool evaluate_one(nvm::GraphDevice& device, std::uint32_t graph, std::span<const std::int64_t> inputs) {
    std::vector<std::int64_t> outputs(device.image().graphs.at(graph).outputs.size());
    nvm::GraphJob job{graph, inputs, outputs, true};
    device.evaluate(std::span(&job, 1));
    return job.evaluated;
}

class IncorrectDevice final : public nvm::GraphDevice {
public:
    explicit IncorrectDevice(const nvr::Image& image) : image_(image) {}
    const nvr::Image& image() const override { return image_; }
    void evaluate(std::span<nvm::GraphJob> jobs) override {
        for (auto& job : jobs) {
            job.outputs[0] = 99;
            job.evaluated = true;
        }
    }
    std::size_t batch_hint() const override { return 1; }
    nvm::GraphDeviceStatistics statistics() const override { return {}; }

private:
    const nvr::Image& image_;
};

} // namespace

NEURA_TEST(layered, weight_budget_boundaries) {
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    nvr::Image image;
    image.graphs = {{1, {{Activation::Identity, 1, {{0, 65538}}}}, {1}},
                    {1, {{Activation::Identity, 2, {{0, 65538}}}}, {1}},
                    {1, {{Activation::Identity, 0, {{0, minimum}}}}, {1}},
                    constant_lanes({minimum}),
                    {1, {}, {0}}};
    const auto packed = nvm::pack_layered(image);
    check(packed.slots == std::vector<std::uint32_t>{0, nvr::absent, nvr::absent, nvr::absent, nvr::absent},
          "only the graph exactly at the weight budget is packed");
    check(packed.shapes[0].eligible && packed.shapes[0].reason == IneligibleReason::none,
          "1 + 32767 * 65538 equals the budget");
    for (const auto index : {1, 2, 3}) {
        const auto& shape = packed.shapes[index];
        check(!shape.eligible && shape.reason == IneligibleReason::weight_budget, "weight budget rejects graph");
    }
    check(!packed.shapes[4].eligible && packed.shapes[4].reason == IneligibleReason::no_nodes,
          "a graph without neurons is ineligible even when it has an output");
}

NEURA_TEST(layered, table_layout_and_limit) {
    const nvr::Graph graph{2,
                           {{Activation::Identity, -3, {{0, 2}, {1, -1}}},
                            {Activation::Relu, 4, {{1, -2}, {2, 3}}},
                            {Activation::Identity, 7, {}},
                            {Activation::Identity, -2, {{3, 1}, {4, -4}}}},
                           {5, 0, 3, 5, 4}};
    const auto word = [](std::int32_t value) { return static_cast<std::uint32_t>(value); };
    const std::vector<std::uint32_t> expected{
        2, 16, 32, 44, 3, 48, 5, 6, // Graph directory.
        0, 53, 57, 57, 1, 59, 1, 1, // Constant graph directory.
        word(-3), 0, 4, 2, 7, 2, 0, 4, 4, 2, 5, 3, word(-2), 4, 4, 5,
        0, 2, 1, word(-1), 1, word(-2), 2, 3, 3, 1, 4, word(-4),
        0, 2, 3, 4, // Layer start positions, then the node count.
        5, 0, 3, 5, 4, // Output wire list, with an input and a repeat.
        1, 0, 0, 0, 0, 1, 0, // Constant node, layer starts, output wire.
    };
    nvr::Image image;
    image.graphs = {graph, graph, constant_lanes({1})};
    const auto packed = nvm::pack_layered(image, expected.size() * sizeof(std::uint32_t));
    check(packed.words == expected, "all directory, node, edge, layer and output words match their layout");
    check(packed.slots == std::vector<std::uint32_t>{0, nvr::absent, 1}, "table limit skips only the larger graph");
    check(!packed.shapes[1].eligible && packed.shapes[1].reason == IneligibleReason::table_limit,
          "the skipped graph reports the table limit");
    for (const auto index : {0, 1}) {
        const auto& shape = packed.shapes[index];
        check(shape.layers == 3 && shape.edges == 6 && shape.nodes == 4 && shape.critical == 6 && shape.io_words == 12,
              "a multi-layer graph's shape is available even if it did not fit");
    }
    const auto& constant = packed.shapes[2];
    check(constant.layers == 1 && constant.edges == 0 && constant.nodes == 1 &&
          constant.critical == 0 && constant.io_words == 6, "constant graph shape");

    nvr::Graph wide;
    wide.input_lanes = 1;
    wide.nodes.resize(65, {Activation::Identity, 0, {{0, 1}}});
    wide.nodes.push_back({Activation::Identity, 0, {{1, 1}, {65, 1}}});
    wide.outputs = {66};
    image.graphs = {std::move(wide)};
    const auto packed_wide = nvm::pack_layered(image);
    const auto& shape = packed_wide.shapes.front();
    check(shape.eligible && shape.layers == 2 && shape.edges == 67 && shape.nodes == 66 &&
          shape.critical == 4 && shape.io_words == 7, "invocation zero handles the 1st and 65th nodes in layer one");
}

NEURA_TEST(layered, escape_demotion_and_overflow) {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    nvr::Image image;
    image.graphs = {{1, {{Activation::Identity, nvm::wire_bound, {{0, 1}}}}, {1}},
                    {1, {{Activation::Identity, maximum, {{0, 1}}}}, {1}},
                    {1, {{Activation::Identity, -nvm::wire_bound, {{0, -1}}}}, {1}}};
    nvr::Graph chain;
    chain.input_lanes = 1;
    for (std::uint32_t wire = 0; wire < 5; ++wire) {
        chain.nodes.push_back({Activation::Identity, 0, {{wire, 40000}}});
    }
    chain.outputs = {5};
    image.graphs.push_back(std::move(chain));
    nvm::ModelDevice model(image);
    const std::array<std::int64_t, 1> zero{0};
    const std::array<std::int64_t, 1> one{1};
    std::array<std::int64_t, 1> output{};
    nvm::GraphJob job{0, zero, output};
    model.evaluate(std::span(&job, 1));
    check(job.evaluated && output[0] == nvm::wire_bound, "wire_bound is inclusive");
    check(!evaluate_one(model, 0, one), "wire_bound plus one escapes");
    const auto& escaped = model.tables().shapes[0];
    check(!escaped.eligible && escaped.reason == IneligibleReason::escaped, "the escaping graph is demoted");
    check(!evaluate_one(model, 0, zero), "a formerly valid request declines after demotion");
    std::vector<std::int64_t> wires{1};
    check(nvm::evaluate_graph(image.graphs[1], wires) == 0, "the reference evaluator overflows at node zero");
    check(!evaluate_one(model, 1, one), "a request that overflows cannot come back evaluated");
    const auto stats = model.statistics();
    check(stats.evaluated == 1 && stats.escaped == 1 && stats.declined_ineligible == 2 && stats.dispatches == 2 &&
          stats.declined_placement == 0 && stats.declined_failed == 0, "model job counters");

    job.graph = 2;
    model.evaluate(std::span(&job, 1));
    check(job.evaluated && output[0] == -nvm::wire_bound, "negative wire_bound is inclusive");
    check(!evaluate_one(model, 2, one), "negative wire_bound minus one escapes");
    wires = {1};
    check(nvm::evaluate_graph(image.graphs[3], wires) == 4, "reference chain overflows at the fifth node");
    check(model.tables().shapes[3].eligible, "chain satisfies the static weight budget");
    check(!evaluate_one(model, 3, one), "an eligible request declines after escape before reference overflow");
}

NEURA_TEST(layered, self_test_graphs_match_reference) {
    nvr::Image image;
    const auto fixtures = nvm::self_test_graphs();
    check_equal(fixtures.size(), std::size_t{6}, "self-test graph count");
    for (const auto& fixture : fixtures) {
        image.graphs.push_back(fixture.graph);
    }
    struct Request {
        std::uint32_t graph;
        std::span<const std::int64_t> inputs;
        std::vector<std::int64_t> outputs;
    };
    std::vector<Request> requests;
    for (std::uint32_t graph = 0; graph < fixtures.size(); ++graph) {
        for (const auto& inputs : fixtures[graph].inputs) {
            requests.push_back({graph, inputs, std::vector<std::int64_t>(image.graphs[graph].outputs.size())});
        }
    }
    check(requests.size() >= 128, "self-test dispatch holds at least 128 requests");
    std::vector<nvm::GraphJob> jobs;
    for (auto& request : requests) {
        jobs.push_back({request.graph, request.inputs, request.outputs});
    }
    nvm::ModelDevice model(image);
    model.evaluate(jobs);
    bool saw_escape = false;
    bool saw_evaluation_of_escaping_graph = false;
    for (const auto& job : jobs) {
        const auto& graph = image.graphs[job.graph];
        std::vector<std::int64_t> wires(job.inputs.begin(), job.inputs.end());
        check(!nvm::evaluate_graph(graph, wires), "self-test graphs never overflow in the reference evaluator");
        const bool escapes = job.graph == 5 && job.inputs[0] == 1;
        check(job.evaluated != escapes, "self-test graph status");
        if (escapes) {
            saw_escape = true;
            continue;
        }
        for (std::size_t lane = 0; lane < graph.outputs.size(); ++lane) {
            check_equal(job.outputs[lane], wires[graph.outputs[lane]], "self-test output lane");
        }
        saw_evaluation_of_escaping_graph |= job.graph == 5;
    }
    check(saw_escape && saw_evaluation_of_escaping_graph, "self-test graph six reports both outcomes");
}

NEURA_TEST(layered, graph_device_diagnostics) {
    nvr::Image image;
    image.graphs = {constant_lanes({1}),
                    {1, {{Activation::Identity, std::numeric_limits<std::int64_t>::max(), {{0, 1}}}}, {1}}};
    IncorrectDevice incorrect(image);
    nvm::VerifyingDevice verifying(incorrect);
    const std::array<std::int64_t, 1> one{1};
    const auto expect_mismatch = [&](std::uint32_t graph, std::span<const std::int64_t> inputs, const char* message) {
        try {
            evaluate_one(verifying, graph, inputs);
            fail("verifier accepted an incorrect device result");
        } catch (const nvm::GraphDeviceMismatch& error) {
            check_equal(std::string(error.what()), std::string(message), "graph mismatch diagnostic");
        }
    };
    expect_mismatch(0, {}, "graph device mismatch: graph 0 output 0: device 99, reference 1");
    expect_mismatch(1, one, "graph device mismatch: graph 1: reference evaluation overflows at node 0");

    nvm::ModelDevice model(image);
    std::array<std::int64_t, 1> output{};
    const std::array<std::int64_t, 1> nonbinary{2};
    for (auto job : {nvm::GraphJob{2, {}, output}, nvm::GraphJob{1, {}, output},
                     nvm::GraphJob{0, {}, {}}, nvm::GraphJob{1, nonbinary, output}}) {
        try {
            model.evaluate(std::span(&job, 1));
            fail("device accepted a malformed job");
        } catch (const std::invalid_argument&) {
        }
    }
}

NEURA_TEST(layered, traps_are_unchanged) {
    for (const auto kind : {nvm::TrapKind::NonBinaryLane, nvm::TrapKind::InvalidSelector, nvm::TrapKind::GraphOverflow}) {
        auto image = logic_model();
        if (kind == nvm::TrapKind::NonBinaryLane) {
            image.graphs[1].nodes[0] = {Activation::Identity, 1, {{0, 1}}};
        } else if (kind == nvm::TrapKind::InvalidSelector) {
            auto& block = image.functions[0].blocks[1];
            block.graph = 3;
            block.inputs.clear();
            block.exits.push_back(block.exits.back());
            image.graphs.push_back(constant_lanes({1, 1}));
        } else {
            image.graphs[1].nodes[0] = {Activation::Relu, std::numeric_limits<std::int64_t>::max(), {{0, 1}, {1, 1}}};
        }
        image = nvr::load(nvr::encode(image));
        nvm::ModelDevice model(image);
        nvm::VerifyingDevice verifying(model);
        nvm::RunLimits limits;
        limits.profile = true;
        const auto reference = run_captured(image, limits);
        limits.graph_device = &verifying;
        const auto actual = run_captured(image, limits);
        check(!reference.result.completed && reference.result.trap.kind == kind, "reference produces the intended trap");
        check_same_run(actual.result, reference.result, std::string(nvm::describe(kind)));
        if (kind == nvm::TrapKind::GraphOverflow) {
            check(model.tables().shapes[1].reason == IneligibleReason::weight_budget &&
                  model.statistics().declined_ineligible == 1, "the overflowing graph failed the weight budget");
        } else if (kind == nvm::TrapKind::NonBinaryLane) {
            check_equal(model.statistics().evaluated, std::uint64_t{1}, "device evaluated the non-binary output lane");
        }
    }
}

NEURA_TEST(layered, corpus_runs_match_reference) {
    const auto& corpus = image_corpus(environment);
    check(!corpus.empty(), "layered corpus is populated");
    std::size_t entries = 0;
    std::uint64_t evaluated = 0;
    for (const auto& item : corpus) {
        const auto image = nvr::load(item.bytes);
        if (image.entry != nvr::absent) ++entries;
        nvm::ModelDevice model(image);
        nvm::VerifyingDevice verifying(model);
        auto limits = graph_test_limits(item);
        const std::vector<std::string> arguments{"héllo", "two"};
        const auto reference = run_captured(image, limits, std::nullopt, arguments);
        limits.graph_device = &verifying;
        const auto actual = run_captured(image, limits, std::nullopt, arguments);
        check_same_run(actual.result, reference.result, item.label);
        check_equal(actual.output, reference.output, item.label + " console output");
        evaluated += model.statistics().evaluated;
    }
    check(entries != 0 && evaluated != 0, "layered corpus ran entry points and evaluated device jobs");
}

} // namespace neura::testing
