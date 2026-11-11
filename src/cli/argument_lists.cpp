#include "argument_lists.hpp"

#include "nvr/format.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace neura::cli {
namespace {

constexpr std::size_t max_file_bytes = std::size_t{64} << 20;
constexpr std::size_t max_argument_lists = std::size_t{1} << 20;

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.string());

    std::string bytes;
    std::array<char, 16'384> chunk;
    while (input) {
        // Read at most one byte past the limit, even if the input grows or
        // cannot report a size before it is read.
        const auto available = std::min(chunk.size(), max_file_bytes - bytes.size() + 1);
        input.read(chunk.data(), static_cast<std::streamsize>(available));
        const auto count = static_cast<std::size_t>(input.gcount());
        if (count > max_file_bytes - bytes.size()) {
            throw std::runtime_error(path.string() + ": larger than 64 MiB");
        }
        bytes.append(chunk.data(), count);
    }
    if (!input.eof() || input.bad()) throw std::runtime_error("error reading " + path.string());
    return bytes;
}

int hex_digit(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

class LineReader {
public:
    LineReader(std::string_view path, std::size_t line, std::string_view text)
        : path_(path), line_(line), text_(text) {}

    ArgumentList read() {
        ArgumentList list{line_, {}};
        for (;;) {
            const auto start = text_.find_first_not_of(" \t");
            if (start == std::string_view::npos) return list;
            text_.remove_prefix(start);
            auto value = text_.front() == '"' ? quoted() : bare();
            if (!nvr::valid_utf8(value)) {
                fail("argument " + std::to_string(list.arguments.size() + 1) + " is not valid UTF-8");
            }
            list.arguments.push_back(std::move(value));
        }
    }

    [[noreturn]] void fail(std::string_view message) const {
        throw std::runtime_error(std::string(path_) + ": line " + std::to_string(line_) + ": " +
                                 std::string(message));
    }

private:
    std::string bare() {
        const auto value = text_.substr(0, text_.find_first_of(" \t"));
        if (value.find_first_of("\"\\") != std::string_view::npos) {
            fail("quote or backslash in an unquoted argument");
        }
        text_.remove_prefix(value.size());
        return std::string(value);
    }

    std::string quoted() {
        text_.remove_prefix(1);
        std::string value;
        while (!text_.empty()) {
            const auto byte = text_.front();
            text_.remove_prefix(1);
            if (byte == '"') {
                if (!text_.empty() && text_.front() != ' ' && text_.front() != '\t') {
                    fail("expected a space after a quoted argument");
                }
                return value;
            }
            value += byte == '\\' ? escape() : byte;
        }
        fail("unterminated quoted argument");
    }

    char escape() {
        if (text_.empty()) fail("invalid escape");
        const auto kind = text_.front();
        text_.remove_prefix(1);
        switch (kind) {
        case '"':
        case '\\': return kind;
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case 'x':
            if (text_.size() >= 2) {
                const int high = hex_digit(text_[0]);
                const int low = hex_digit(text_[1]);
                if (high >= 0 && low >= 0) {
                    text_.remove_prefix(2);
                    return static_cast<char>(16 * high + low);
                }
            }
            break;
        }
        fail("invalid escape");
    }

    std::string_view path_;
    std::size_t line_;
    std::string_view text_;
};

} // namespace

std::vector<ArgumentList> read_argument_lists(const std::filesystem::path& path) {
    const auto bytes = read_bytes(path);
    const auto name = path.string();
    std::string_view remaining = bytes;
    std::vector<ArgumentList> lists;
    for (std::size_t number = 1; !remaining.empty(); ++number) {
        const auto lf = remaining.find('\n');
        const auto line = remaining.substr(0, lf);
        remaining.remove_prefix(line.size() + (lf != std::string_view::npos));
        LineReader reader(name, number, line);
        if (line.ends_with('\r')) reader.fail("carriage return before the line end");
        if (line.starts_with('#')) continue;
        if (lists.size() == max_argument_lists) {
            throw std::runtime_error(name + ": more than 1048576 argument lists");
        }
        lists.push_back(reader.read());
    }
    if (lists.empty()) throw std::runtime_error(name + ": no argument lists");
    return lists;
}

} // namespace neura::cli
