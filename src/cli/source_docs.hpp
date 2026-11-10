#pragma once

#include <filesystem>
#include <iosfwd>
#include <vector>

namespace neura {

// Write a versioned documentation index for the given source files.
void write_source_docs(std::ostream& output, const std::vector<std::filesystem::path>& files);

// Emit declaration stubs with the original line numbering for Doxygen's input filter.
void write_doxygen_source(std::ostream& output, const std::filesystem::path& file);

} // namespace neura
