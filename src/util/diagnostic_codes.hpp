#pragma once

#include <string_view>

namespace neura {

struct DiagnosticCode {
    std::string_view code;
    std::string_view title;
};

inline constexpr DiagnosticCode diagnostic_codes[] = {
#define NEURA_DIAGNOSTIC(code, title) {#code, title},
#include "diagnostic_codes.def"
};

struct WarningCode {
    std::string_view code;
    // The name that -W<name>, -Wno-<name>, #Allow, and #Deny accept.
    std::string_view name;
    std::string_view title;
};

inline constexpr WarningCode warning_codes[] = {
#define NEURA_DIAGNOSTIC(code, title)
#define NEURA_WARNING(code, name, title) {#code, name, title},
#include "diagnostic_codes.def"
};

// The warning whose code or name is `key`, or null.
constexpr const WarningCode* find_warning(std::string_view key) {
    for (const auto& entry : warning_codes)
        if (entry.code == key || entry.name == key) return &entry;
    return nullptr;
}

// The registered title of `code`, or empty for an unknown code.
constexpr std::string_view diagnostic_title(std::string_view code) {
    for (const auto& entry : diagnostic_codes)
        if (entry.code == code) return entry.title;
    return {};
}

} // namespace neura
