#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace neura {

inline std::filesystem::path module_path(std::string_view name) {
    auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    std::string path;
    std::size_t component = 0;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '.') {
            path += '/';
            component = i + 1;
        } else if (upper(c)) {
            const bool followed_by_lower = i + 1 < name.size()
                && name[i + 1] >= 'a' && name[i + 1] <= 'z';
            if (i > component && (!upper(name[i - 1]) || followed_by_lower)
                && name[i - 1] != '_') {
                path += '-';
            }
            path += static_cast<char>(c - 'A' + 'a');
        } else {
            path += c == '_' ? '-' : c;
        }
    }
    return path + ".nra";
}

} // namespace neura
