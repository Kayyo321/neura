#pragma once

// JSON log lines of the server host and its workers (docs/design/Backend030.md,
// "Operation"): time, level, request identifier, message and named fields,
// one object per line on standard error, within S-13 bytes.

#include "nvm/app_services.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace neura::server {

inline constexpr std::size_t max_log_record_bytes = 16 * 1024; // Apps030 S-13

inline void append_json_text(std::string& out, std::string_view text) {
    constexpr std::string_view hex = "0123456789abcdef";
    out += '"';
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (byte < 0x20 || byte == 0x7f) {
            out += "\\u00";
            out += hex[byte >> 4];
            out += hex[byte & 15];
        } else {
            out += c;
        }
    }
    out += '"';
}

inline std::string_view log_level_name(std::uint32_t level) {
    switch (level) {
    case 0: return "trace";
    case 1: return "debug";
    case 2: return "info";
    case 3: return "warn";
    default: return "error";
    }
}

inline std::string json_log_line(std::uint32_t level, std::string_view request_id, std::string_view message,
                                 const nvm::HeaderPairs& fields = {}) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string line = "{\"time_ms\":" + std::to_string(now) + ",\"level\":";
    append_json_text(line, log_level_name(level));
    if (!request_id.empty()) {
        line += ",\"request_id\":";
        append_json_text(line, request_id);
    }
    line += ",\"message\":";
    append_json_text(line, message);
    for (const auto& [name, value] : fields) {
        line += ',';
        append_json_text(line, name);
        line += ':';
        append_json_text(line, value);
    }
    line += '}';
    if (line.size() > max_log_record_bytes - 1) {
        // An oversized record keeps its time, level and identifier, and as
        // much of the message as fits, marked as truncated.
        std::string_view kept = message.substr(0, max_log_record_bytes / 8);
        while (!kept.empty() && (static_cast<unsigned char>(kept.back()) & 0xc0) == 0x80) kept.remove_suffix(1);
        if (!kept.empty() && static_cast<unsigned char>(kept.back()) >= 0xc0) kept.remove_suffix(1);
        return json_log_line(level, request_id.substr(0, 256), kept, {{"truncated", "true"}});
    }
    line += '\n';
    return line;
}

} // namespace neura::server
