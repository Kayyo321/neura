#pragma once

#include "util/diagnostic.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace neura::project {

struct TomlValue {
    using Array = std::vector<TomlValue>;

    std::variant<std::string, std::int64_t, bool, Array> value;
    Span span;
};

enum class TomlTableId : std::size_t {};
using TomlTableArray = std::vector<TomlTableId>;

struct TomlEntry {
    std::variant<TomlValue, TomlTableId, TomlTableArray> item;
    Span key_span;
};

struct TomlTable {
    std::map<std::string, TomlEntry, std::less<>> entries;
    // The header, or the key segment that introduced an implicit table.
    Span span;
};

struct TomlDocument {
    // Table links are indices, so table paths can be walked without recursion.
    std::vector<TomlTable> tables;

    const TomlTable& root() const { return tables.front(); }
    const TomlTable& table(TomlTableId id) const { return tables.at(static_cast<std::size_t>(id)); }
};

// Parse the shared project/lock syntax. The caller supplies the source name
// for diagnostics and owns any source registration, model checks, or file I/O.
// Throws a Diagnostic (E1020) at the first error.
TomlDocument parse_toml(std::string_view bytes, std::string_view file);

} // namespace neura::project
