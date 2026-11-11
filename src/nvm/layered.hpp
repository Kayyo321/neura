#pragma once

#include "graph_device.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace neura::nvm {

inline constexpr std::int32_t wire_bound = 32767;
inline constexpr std::int32_t sum_limit = 2147483647;
inline constexpr std::uint32_t local_size = 64;
inline constexpr std::uint64_t layered_table_limit = std::uint64_t{2} << 30;

struct LayeredTables {
    std::vector<std::uint32_t> words;
    // Slots and shapes are indexed by graph. Statically ineligible graphs have no slot.
    std::vector<std::uint32_t> slots;
    std::vector<GraphShape> shapes;
};

// Packs the validated graph table in graph index order. Graphs too large for
// the remaining table space or either per-request buffer are skipped.
LayeredTables pack_layered(const nvr::Image& image, std::uint64_t max_table_bytes = layered_table_limit,
                           std::uint64_t max_request_bytes = std::numeric_limits<std::uint64_t>::max());
std::vector<GraphShape> layered_shapes(const nvr::Image& image,
                                      std::uint64_t max_table_bytes = layered_table_limit);

class ModelDevice final : public GraphDevice {
public:
    explicit ModelDevice(const nvr::Image& image, std::uint64_t max_table_bytes = layered_table_limit);

    const nvr::Image& image() const override;
    void evaluate(std::span<GraphJob> jobs) override;
    std::size_t batch_hint() const override;
    GraphDeviceStatistics statistics() const override;
    std::span<const GraphShape> graph_shapes() const override;
    const LayeredTables& tables() const;

private:
    std::uint32_t evaluate_job(GraphJob& job);

    const nvr::Image& image_;
    LayeredTables tables_;
    std::vector<std::int32_t> wires_;
    GraphDeviceStatistics statistics_;
};

struct SelfTestGraph {
    nvr::Graph graph;
    std::vector<std::vector<std::int64_t>> inputs;
};

std::vector<SelfTestGraph> self_test_graphs();

} // namespace neura::nvm
