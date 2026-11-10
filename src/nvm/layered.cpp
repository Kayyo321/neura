#include "layered.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <numeric>
#include <utility>

namespace neura::nvm {
namespace {

constexpr std::uint32_t directory_words = 8;
constexpr std::uint32_t node_words = 4;
constexpr std::uint32_t edge_words = 2;
constexpr std::uint32_t evaluated_status = 2;
constexpr std::uint32_t escaped_status = 3;

struct GraphOrder {
    std::vector<std::uint32_t> nodes;
    std::vector<std::uint32_t> starts;
};

std::uint64_t magnitude(std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    return value < 0 ? std::uint64_t{0} - bits : bits;
}

bool within_weight_budget(const nvr::Node& node) {
    const auto bias = magnitude(node.bias);
    if (bias > sum_limit) return false;
    std::uint64_t remaining = (sum_limit - bias) / wire_bound;
    for (const auto& edge : node.edges) {
        const auto weight = magnitude(edge.weight);
        if (weight > remaining) return false;
        remaining -= weight;
    }
    return true;
}

GraphOrder order_graph(const nvr::Graph& graph, GraphShape& shape) {
    shape.nodes = graph.nodes.size();
    shape.io_words = std::uint64_t{graph.input_lanes} + graph.outputs.size() + 5;
    if (graph.nodes.empty()) {
        shape.reason = IneligibleReason::no_nodes;
        return {};
    }

    bool weights_fit = true;
    std::vector<std::uint32_t> layer_of_node(graph.nodes.size());
    std::uint32_t layer_count = 0;
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        const auto& node = graph.nodes[index];
        std::uint32_t layer = 1;
        for (const auto& edge : node.edges) {
            if (edge.source >= graph.input_lanes) {
                layer = std::max(layer, layer_of_node[edge.source - graph.input_lanes] + 1);
            }
        }
        layer_of_node[index] = layer;
        layer_count = std::max(layer_count, layer);
        shape.edges += node.edges.size();
        weights_fit = weights_fit && within_weight_budget(node);
    }

    // A stable counting sort by layer preserves node index order within each layer.
    GraphOrder order;
    order.starts.resize(std::size_t{layer_count} + 1);
    for (const auto layer : layer_of_node) ++order.starts[layer];
    std::partial_sum(order.starts.begin(), order.starts.end(), order.starts.begin());
    auto next = order.starts;
    order.nodes.resize(graph.nodes.size());
    for (std::uint32_t index = 0; index < graph.nodes.size(); ++index) {
        order.nodes[next[layer_of_node[index] - 1]++] = index;
    }

    shape.layers = layer_count;
    for (std::uint32_t layer = 0; layer < layer_count; ++layer) {
        std::array<std::uint64_t, local_size> invocation_edges{};
        std::uint64_t critical = 0;
        const auto first = order.starts[layer];
        for (auto pos = first; pos < order.starts[layer + 1]; ++pos) {
            auto& work = invocation_edges[(pos - first) % local_size];
            work += graph.nodes[order.nodes[pos]].edges.size();
            critical = std::max(critical, work);
        }
        shape.critical += critical;
    }

    shape.eligible = weights_fit;
    if (!weights_fit) shape.reason = IneligibleReason::weight_budget;
    return order;
}

void append_graph(const nvr::Graph& graph, const GraphOrder& order, const GraphShape& shape,
                  std::uint32_t slot, std::vector<std::uint32_t>& words) {
    // All offsets and counts here fit in a word by the table and graph wire limits.
    const auto node_base = static_cast<std::uint32_t>(words.size());
    const auto edge_base = node_base + node_words * static_cast<std::uint32_t>(shape.nodes);
    const auto layer_base = edge_base + edge_words * static_cast<std::uint32_t>(shape.edges);
    const auto output_base = layer_base + static_cast<std::uint32_t>(order.starts.size());
    const std::array entry{
        graph.input_lanes, node_base, edge_base, layer_base, static_cast<std::uint32_t>(shape.layers),
        output_base, static_cast<std::uint32_t>(graph.outputs.size()),
        graph.input_lanes + static_cast<std::uint32_t>(shape.nodes),
    };
    std::copy(entry.begin(), entry.end(), words.begin() + directory_words * slot);

    std::uint32_t edge_first = 0;
    for (const auto index : order.nodes) {
        const auto& node = graph.nodes[index];
        const auto edge_count = static_cast<std::uint32_t>(node.edges.size());
        const auto relu = static_cast<std::uint32_t>(node.activation == nvr::Activation::Relu);
        words.insert(words.end(), {static_cast<std::uint32_t>(node.bias), edge_first, (edge_count << 1) | relu,
                                   graph.input_lanes + index});
        edge_first += edge_count;
    }
    for (const auto index : order.nodes) {
        for (const auto& edge : graph.nodes[index].edges) {
            words.insert(words.end(), {edge.source, static_cast<std::uint32_t>(edge.weight)});
        }
    }
    words.insert(words.end(), order.starts.begin(), order.starts.end());
    words.insert(words.end(), graph.outputs.begin(), graph.outputs.end());
}

template <class KeepOrder>
std::uint64_t analyze_graphs(const nvr::Image& image, std::uint64_t max_table_bytes,
                             std::uint64_t max_request_bytes, std::vector<GraphShape>& shapes,
                             KeepOrder keep_order) {
    const auto word_limit = std::min(max_table_bytes, layered_table_limit) / sizeof(std::uint32_t);
    const auto request_word_limit = max_request_bytes / sizeof(std::uint32_t);
    shapes.resize(image.graphs.size());
    std::uint64_t word_count = 0;
    for (std::size_t index = 0; index < image.graphs.size(); ++index) {
        const auto& graph = image.graphs[index];
        auto& shape = shapes[index];
        auto order = order_graph(graph, shape);
        if (!shape.eligible) continue;
        const auto non_edge_words = directory_words + node_words * shape.nodes +
                                    order.starts.size() + graph.outputs.size();
        const auto remaining = word_limit - word_count;
        if (shape.io_words > request_word_limit ||
            graph.input_lanes + shape.nodes > request_word_limit ||
            non_edge_words > remaining || shape.edges > (remaining - non_edge_words) / edge_words) {
            shape.eligible = false;
            shape.reason = IneligibleReason::table_limit;
            continue;
        }
        keep_order(index, std::move(order));
        word_count += non_edge_words + edge_words * shape.edges;
    }
    return word_count;
}

} // namespace

LayeredTables pack_layered(const nvr::Image& image, std::uint64_t max_table_bytes, std::uint64_t max_request_bytes) {
    LayeredTables tables;
    tables.slots.resize(image.graphs.size(), nvr::absent);
    std::vector<GraphOrder> orders;
    const auto word_count = analyze_graphs(image, max_table_bytes, max_request_bytes, tables.shapes,
        [&](std::size_t index, GraphOrder&& order) {
            tables.slots[index] = static_cast<std::uint32_t>(orders.size());
            orders.push_back(std::move(order));
        });
    tables.words.reserve(static_cast<std::size_t>(word_count));
    tables.words.resize(directory_words * orders.size());
    for (std::size_t index = 0; index < image.graphs.size(); ++index) {
        const auto slot = tables.slots[index];
        if (slot != nvr::absent) {
            append_graph(image.graphs[index], orders[slot], tables.shapes[index], slot, tables.words);
        }
    }
    return tables;
}

std::vector<GraphShape> layered_shapes(const nvr::Image& image, std::uint64_t max_table_bytes) {
    std::vector<GraphShape> shapes;
    analyze_graphs(image, max_table_bytes, std::numeric_limits<std::uint64_t>::max(),
                   shapes, [](std::size_t, GraphOrder&&) {});
    return shapes;
}

ModelDevice::ModelDevice(const nvr::Image& image, std::uint64_t max_table_bytes)
    : image_(image), tables_(pack_layered(image, max_table_bytes)) {}

const nvr::Image& ModelDevice::image() const { return image_; }
std::size_t ModelDevice::batch_hint() const { return std::numeric_limits<std::size_t>::max(); }
GraphDeviceStatistics ModelDevice::statistics() const { return statistics_; }
std::span<const GraphShape> ModelDevice::graph_shapes() const { return tables_.shapes; }
const LayeredTables& ModelDevice::tables() const { return tables_; }

std::uint32_t ModelDevice::evaluate_job(GraphJob& job) {
    const auto& words = tables_.words;
    const auto entry = directory_words * tables_.slots[job.graph];
    const auto input_lanes = words[entry];
    const auto node_base = words[entry + 1];
    const auto edge_base = words[entry + 2];
    const auto layer_base = words[entry + 3];
    const auto layers = words[entry + 4];
    const auto output_base = words[entry + 5];
    const auto output_count = words[entry + 6];
    wires_.resize(words[entry + 7]);
    for (std::uint32_t lane = 0; lane < input_lanes; ++lane) {
        wires_[lane] = static_cast<std::int32_t>(job.inputs[lane]);
    }

    bool escaped = false;
    for (std::uint32_t layer = 0; layer < layers; ++layer) {
        const auto first = words[layer_base + layer];
        const auto last = words[layer_base + layer + 1];
        for (auto pos = first; pos < last; ++pos) {
            const auto node = node_base + node_words * pos;
            // After an escape a later layer may wrap. Unsigned arithmetic
            // models the shader's two's-complement bit pattern in that case.
            auto sum = words[node];
            auto edge = edge_base + edge_words * words[node + 1];
            const auto packed = words[node + 2];
            const auto edge_end = edge + edge_words * (packed >> 1);
            for (; edge < edge_end; edge += edge_words) {
                sum += words[edge + 1] * static_cast<std::uint32_t>(wires_[words[edge]]);
            }
            auto value = std::bit_cast<std::int32_t>(sum);
            if ((packed & 1) != 0) value = std::max(value, 0);
            escaped = escaped || value > wire_bound || value < -wire_bound;
            wires_[words[node + 3]] = value;
        }
    }
    if (escaped) return escaped_status;
    for (std::uint32_t index = 0; index < output_count; ++index) {
        job.outputs[index] = wires_[words[output_base + index]];
    }
    return evaluated_status;
}

void ModelDevice::evaluate(std::span<GraphJob> jobs) {
    validate_graph_jobs(image_, jobs);
    std::vector<std::uint32_t> demoted;
    bool dispatched = false;
    for (auto& job : jobs) {
        job.evaluated = false;
        if (!tables_.shapes[job.graph].eligible) {
            ++statistics_.declined_ineligible;
            continue;
        }
        dispatched = true;
        if (evaluate_job(job) == escaped_status) {
            ++statistics_.escaped;
            demoted.push_back(job.graph);
        } else {
            job.evaluated = true;
            ++statistics_.evaluated;
        }
    }
    // All eligible jobs in this call model one submitted dispatch. An escape
    // affects admission to future dispatches, including jobs for this graph.
    for (const auto index : demoted) {
        auto& shape = tables_.shapes[index];
        shape.eligible = false;
        shape.reason = IneligibleReason::escaped;
    }
    if (dispatched) ++statistics_.dispatches;
}

std::vector<SelfTestGraph> self_test_graphs() {
    using enum nvr::Activation;
    const std::vector<std::vector<std::int64_t>> bits{{0}, {1}};
    const std::vector<std::vector<std::int64_t>> pairs{{0, 0}, {0, 1}, {1, 0}, {1, 1}};
    std::vector<SelfTestGraph> tests;
    tests.push_back({{2, {{Relu, -1, {{0, 1}, {1, 1}}}}, {2}}, pairs});
    tests.push_back({{2, {{Identity, 1, {{0, -2}}}, {Relu, 1, {{1, -2}}}}, {2, 3}}, pairs});

    nvr::Graph chain{1, {}, {200}};
    for (std::uint32_t index = 0; index < 200; ++index) {
        chain.nodes.push_back({Relu, 1, {{index, -1}}});
    }
    tests.push_back({std::move(chain), bits});

    nvr::Graph wide{200, {}, {400}};
    nvr::Node total{Identity, 0, {}};
    for (std::uint32_t index = 0; index < 200; ++index) {
        wide.nodes.push_back({Identity, 0, {{index, 1}}});
        total.edges.push_back({index + 200, 1});
    }
    wide.nodes.push_back(std::move(total));

    // Along with the other graphs these inputs make one dispatch of 128 jobs.
    std::vector<std::vector<std::int64_t>> wide_inputs;
    wide_inputs.emplace_back(200, 0);
    wide_inputs.emplace_back(200, 1);
    for (std::uint32_t sample = 1; sample <= 112; ++sample) {
        auto& lanes = wide_inputs.emplace_back(200);
        for (std::uint32_t lane = 0; lane < 200; ++lane) {
            lanes[lane] = (67 * lane + sample) % 200 < sample;
        }
    }
    tests.push_back({std::move(wide), std::move(wide_inputs)});
    tests.push_back({{1, {{Identity, 0, {{0, -1}}}}, {1, 0, 1}}, bits});
    tests.push_back({{1, {{Identity, 0, {{0, 40000}}}}, {1}}, bits});
    return tests;
}

} // namespace neura::nvm
