#pragma once

#include "fixtures.hpp"

namespace neura::testing {

const std::vector<std::vector<std::string>>& fleet_corpus_arguments();
nvm::RunLimits fleet_corpus_limits(const CorpusImage& item);
std::vector<Captured> capture_serial_members(const nvr::Image& image, nvm::RunLimits limits);
void check_corpus_fleet(const nvr::Image& image, nvm::RunLimits limits, nvm::GraphDevice* device, std::uint32_t window,
                        const std::vector<Captured>& reference, const std::string& label);

} // namespace neura::testing
