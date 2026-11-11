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

// The registered title of `code`, or empty for an unknown code.
constexpr std::string_view diagnostic_title(std::string_view code) {
    for (const auto& entry : diagnostic_codes)
        if (entry.code == code) return entry.title;
    return {};
}

} // namespace neura
