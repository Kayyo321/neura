#include "coverage.hpp"

#include <limits>

namespace neura::tooling {
namespace {
std::uint64_t block_key(std::uint32_t function, std::uint32_t block) {
    return (std::uint64_t{function} << 32) | block;
}
} // namespace

LineCoverage::LineCoverage(const nvr::Image& image,
                           const std::map<std::string, std::filesystem::path>& source_files) {
    if (!image.source_map) return;
    for (const auto& location : *image.source_map) {
        const auto file = source_files.find(location.path);
        if (file == source_files.end() || location.line == 0) continue;
        std::error_code error;
        auto path = std::filesystem::weakly_canonical(file->second, error);
        if (error) path = std::filesystem::absolute(file->second).lexically_normal();
        const auto source = path.generic_string();
        locations_.insert_or_assign(block_key(location.function, location.block), Location{source, location.line});
        lines_[source].try_emplace(location.line, 0);
    }
}

void LineCoverage::block(std::uint32_t function, std::uint32_t block) {
    const auto found = locations_.find(block_key(function, block));
    if (found == locations_.end()) return;
    auto& count = lines_.at(found->second.file).at(found->second.line);
    // JSON numbers and editor APIs use signed counters. Saturation is safer
    // than wrapping if an exceptionally large test run reaches that limit.
    if (count < static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) ++count;
}

Json LineCoverage::report() const {
    Json files = Json::Array{};
    for (const auto& [path, counts] : lines_) {
        Json statements = Json::Array{};
        for (const auto& [line, count] : counts)
            statements.items().emplace_back(Json::Object{{"line", line}, {"count", count}});
        files.push_back(Json{{"path", path}, {"statements", std::move(statements)}});
    }
    return Json{{"version", 1}, {"granularity", "mapped-block-line"},
                {"measurement", "blockEntries"}, {"files", std::move(files)}};
}

} // namespace neura::tooling
