#pragma once

#include "nvr/image.hpp"
#include "tooling/json.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace neura::tooling {

// Coverage of source locations carried by actual emitted NVM blocks. A line
// can label several blocks: its count is their total entries, not an inferred
// source-statement or branch count. Locations with no mapped block are absent.
class LineCoverage {
public:
    LineCoverage(const nvr::Image& image,
                 const std::map<std::string, std::filesystem::path>& source_files);
    void block(std::uint32_t function, std::uint32_t block);
    Json report() const;

private:
    struct Location { std::string file; std::uint32_t line; };
    std::unordered_map<std::uint64_t, Location> locations_;
    std::map<std::string, std::map<std::uint32_t, std::uint64_t>> lines_;
};

} // namespace neura::tooling
