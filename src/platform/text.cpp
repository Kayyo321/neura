#include "text.hpp"

#include "internal.hpp"

#include <cerrno>
#include <climits>
#include <cstdint>

#ifdef _WIN32
#include "windows.hpp"
#endif

namespace neura::platform {

std::error_code invalid_text_error() noexcept {
#ifdef _WIN32
    return {ERROR_NO_UNICODE_TRANSLATION, std::system_category()};
#else
    return {EILSEQ, std::system_category()};
#endif
}

bool is_valid_utf8(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        std::size_t length = 0;
        std::uint32_t code = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            code = lead & 0x1Fu;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            code = lead & 0x0Fu;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            code = lead & 0x07u;
        } else {
            return false;
        }
        if (text.size() - i < length) return false;
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0u) != 0x80u) return false;
            code = (code << 6) | (next & 0x3Fu);
        }
        if ((length == 3 && code < 0x800) || (length == 4 && (code < 0x10000 || code > 0x10FFFF)) ||
            (code >= 0xD800 && code <= 0xDFFF)) {
            return false;
        }
        i += length;
    }
    return true;
}

#ifdef _WIN32
std::optional<std::wstring> try_to_utf16(std::string_view utf8) {
    if (utf8.empty()) return std::wstring();
    if (utf8.size() > static_cast<std::size_t>(INT_MAX)) return std::nullopt;
    const int size = static_cast<int>(utf8.size());
    const int needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), size, nullptr, 0);
    if (needed <= 0) return std::nullopt;
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), size, wide.data(), needed) != needed) {
        return std::nullopt;
    }
    return wide;
}

std::wstring to_utf16(std::string_view utf8) {
    auto wide = try_to_utf16(utf8);
    if (!wide) throw std::system_error(invalid_text_error(), "text is not valid UTF-8");
    return std::move(*wide);
}

std::optional<std::string> try_to_utf8(std::wstring_view utf16) {
    if (utf16.empty()) return std::string();
    if (utf16.size() > static_cast<std::size_t>(INT_MAX)) return std::nullopt;
    const int size = static_cast<int>(utf16.size());
    const int needed =
        ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16.data(), size, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::nullopt;
    std::string narrow(static_cast<std::size_t>(needed), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16.data(), size, narrow.data(), needed, nullptr,
                              nullptr) != needed) {
        return std::nullopt;
    }
    return narrow;
}

std::string to_utf8(std::wstring_view utf16) {
    auto narrow = try_to_utf8(utf16);
    if (!narrow) throw std::system_error(invalid_text_error(), "text is not valid UTF-16");
    return std::move(*narrow);
}

std::filesystem::path path_from_utf8(std::string_view utf8) {
    return std::filesystem::path(to_utf16(utf8));
}

std::string utf8_path(const std::filesystem::path& path) {
    return to_utf8(path.generic_wstring());
}

std::string native_utf8_path(const std::filesystem::path& path) {
    return to_utf8(std::filesystem::path(path).make_preferred().native());
}
#else
std::filesystem::path path_from_utf8(std::string_view utf8) {
    return std::filesystem::path(std::string(utf8));
}

std::string utf8_path(const std::filesystem::path& path) {
    return path.generic_string();
}

std::string native_utf8_path(const std::filesystem::path& path) {
    return path.native();
}
#endif

} // namespace neura::platform
