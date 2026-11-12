#pragma once
#include "json.hpp"
#include <cctype>
#include <charconv>
#include <istream>
#include <limits>
#include <mutex>
#include <optional>
#include <ostream>

namespace neura::tooling {
inline std::optional<Json> read_message(std::istream &input) {
    constexpr std::size_t maximum = 16 * 1024 * 1024;
    std::string line;
    std::optional<std::size_t> length;
    std::size_t header = 0;
    while (std::getline(input, line)) {
        header += line.size();
        if (header > 8192)
            throw std::runtime_error("protocol header too large");
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            throw std::runtime_error("invalid protocol header");
        auto name = line.substr(0, colon);
        for (char &c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name == "content-length") {
            if (length)
                throw std::runtime_error("duplicate content length");
            auto text = std::string_view(line).substr(colon + 1);
            while (!text.empty() && text.front() == ' ')
                text.remove_prefix(1);
            std::size_t n{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), n);
            if (error != std::errc{} || end != text.data() + text.size() || n > maximum)
                throw std::runtime_error("invalid content length");
            length = n;
        }
    }
    if (!length) {
        if (input.eof())
            return std::nullopt;
        throw std::runtime_error("missing content length");
    }
    std::string body(*length, '\0');
    input.read(body.data(), static_cast<std::streamsize>(*length));
    if (static_cast<std::size_t>(input.gcount()) != *length)
        throw std::runtime_error("truncated protocol message");
    return Json::parse(body);
}
inline void write_message(std::ostream &output, const Json &message) {
    static std::mutex mutex;
    const std::lock_guard guard(mutex);
    const auto body = message.dump();
    output << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    output.flush();
}
} // namespace neura::tooling
