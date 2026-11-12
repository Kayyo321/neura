#pragma once

// Small dependency-free JSON value shared by the editor protocols. Parsing is
// deliberately bounded: neither protocol should let an untrusted message use
// an unbounded stack or accept non-JSON numbers / invalid Unicode escapes.
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace neura::tooling {

class Json {
  public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    using array = Array;
    using object = Object;
    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool value) : value_(value) {}
    Json(int value) : value_(static_cast<std::int64_t>(value)) {}
    Json(unsigned value) : value_(static_cast<std::int64_t>(value)) {}
    Json(long value) : value_(static_cast<std::int64_t>(value)) {}
    Json(unsigned long value) : value_(static_cast<std::int64_t>(value)) {}
    Json(long long value) : value_(static_cast<std::int64_t>(value)) {}
    Json(unsigned long long value) : value_(static_cast<std::int64_t>(value)) {}
    Json(double value) : value_(value) {
        if (!std::isfinite(value))
            throw std::runtime_error("nonfinite JSON number");
    }
    Json(const char *value) : value_(std::string(value)) {}
    Json(std::string value) : value_(std::move(value)) {}
    Json(std::string_view value) : value_(std::string(value)) {}
    Json(Array value) : value_(std::move(value)) {}
    Json(Object value) : value_(std::move(value)) {}
    Json(std::initializer_list<Object::value_type> value) : value_(Object(value)) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(value_); }
    bool is_bool() const { return std::holds_alternative<bool>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_array() const { return std::holds_alternative<Array>(value_); }
    bool is_object() const { return std::holds_alternative<Object>(value_); }
    bool is_number() const {
        return std::holds_alternative<std::int64_t>(value_) || std::holds_alternative<double>(value_);
    }
    bool boolean(bool fallback = false) const {
        const auto *p = std::get_if<bool>(&value_);
        return p ? *p : fallback;
    }
    std::int64_t integer(std::int64_t fallback = 0) const {
        const auto *p = std::get_if<std::int64_t>(&value_);
        return p ? *p : fallback;
    }
    double number(double fallback = 0) const {
        if (const auto *p = std::get_if<double>(&value_))
            return *p;
        if (const auto *p = std::get_if<std::int64_t>(&value_))
            return static_cast<double>(*p);
        return fallback;
    }
    std::string string(std::string fallback = {}) const {
        const auto *p = std::get_if<std::string>(&value_);
        return p ? *p : fallback;
    }
    const Array &items() const {
        static const Array none;
        const auto *p = std::get_if<Array>(&value_);
        return p ? *p : none;
    }
    Array &items() {
        if (is_null())
            value_ = Array{};
        return std::get<Array>(value_);
    }
    const Object &fields() const {
        static const Object none;
        const auto *p = std::get_if<Object>(&value_);
        return p ? *p : none;
    }
    bool contains(std::string_view key) const { return fields().contains(key); }
    const Json &operator[](std::string_view key) const {
        static const Json none;
        const auto &o = fields();
        const auto it = o.find(key);
        return it == o.end() ? none : it->second;
    }
    Json &operator[](std::string_view key) {
        if (is_null())
            value_ = Object{};
        return std::get<Object>(value_)[std::string(key)];
    }
    const Json &operator[](std::size_t index) const {
        static const Json none;
        const auto &a = items();
        return index < a.size() ? a[index] : none;
    }
    void push_back(Json value) { items().push_back(std::move(value)); }
    std::size_t size() const {
        if (is_array())
            return items().size();
        if (is_object())
            return fields().size();
        return 0;
    }

    std::string dump() const {
        std::ostringstream out;
        write(out);
        return out.str();
    }
    static Json parse(std::string_view text) {
        Parser parser{text};
        auto result = parser.value(0);
        parser.space();
        if (parser.position != text.size())
            throw std::runtime_error("trailing JSON input");
        return result;
    }

  private:
    std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object> value_;
    static void quote(std::ostream &out, std::string_view text) {
        constexpr char digits[] = "0123456789abcdef";
        out.put('"');
        for (unsigned char c : text) {
            switch (c) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (c < 32)
                    out << "\\u00" << digits[c >> 4] << digits[c & 15];
                else
                    out.put(static_cast<char>(c));
            }
        }
        out.put('"');
    }
    void write(std::ostream &out) const {
        if (is_null())
            out << "null";
        else if (const auto *boolean_value = std::get_if<bool>(&value_))
            out << (*boolean_value ? "true" : "false");
        else if (const auto *integer_value = std::get_if<std::int64_t>(&value_))
            out << *integer_value;
        else if (const auto *real_value = std::get_if<double>(&value_))
            out << std::setprecision(17) << *real_value;
        else if (const auto *string_value = std::get_if<std::string>(&value_))
            quote(out, *string_value);
        else if (const auto *array_value = std::get_if<Array>(&value_)) {
            out << '[';
            bool first = true;
            for (const auto &v : *array_value) {
                if (!first)
                    out << ',';
                first = false;
                v.write(out);
            }
            out << ']';
        } else {
            out << '{';
            bool first = true;
            for (const auto &[k, v] : fields()) {
                if (!first)
                    out << ',';
                first = false;
                quote(out, k);
                out << ':';
                v.write(out);
            }
            out << '}';
        }
    }
    struct Parser {
        std::string_view input;
        std::size_t position{};
        void space() {
            while (position < input.size() && (input[position] == ' ' || input[position] == '\t' ||
                                               input[position] == '\r' || input[position] == '\n'))
                ++position;
        }
        char take() {
            if (position >= input.size())
                throw std::runtime_error("unexpected end of JSON");
            return input[position++];
        }
        bool consume(char c) {
            space();
            if (position < input.size() && input[position] == c) {
                ++position;
                return true;
            }
            return false;
        }
        std::uint32_t hex4() {
            std::uint32_t n = 0;
            for (int i = 0; i < 4; ++i) {
                char c = take();
                n <<= 4;
                if (c >= '0' && c <= '9')
                    n |= c - '0';
                else if (c >= 'a' && c <= 'f')
                    n |= c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    n |= c - 'A' + 10;
                else
                    throw std::runtime_error("invalid JSON unicode escape");
            }
            return n;
        }
        static void utf8(std::string &s, std::uint32_t cp) {
            if (cp < 0x80)
                s += static_cast<char>(cp);
            else if (cp < 0x800) {
                s += static_cast<char>(0xc0 | (cp >> 6));
                s += static_cast<char>(0x80 | (cp & 63));
            } else if (cp < 0x10000) {
                s += static_cast<char>(0xe0 | (cp >> 12));
                s += static_cast<char>(0x80 | ((cp >> 6) & 63));
                s += static_cast<char>(0x80 | (cp & 63));
            } else {
                s += static_cast<char>(0xf0 | (cp >> 18));
                s += static_cast<char>(0x80 | ((cp >> 12) & 63));
                s += static_cast<char>(0x80 | ((cp >> 6) & 63));
                s += static_cast<char>(0x80 | (cp & 63));
            }
        }
        std::string text() {
            if (take() != '"')
                throw std::runtime_error("expected JSON string");
            std::string out;
            for (;;) {
                char c = take();
                if (c == '"')
                    return out;
                if (static_cast<unsigned char>(c) < 32)
                    throw std::runtime_error("control character in JSON string");
                if (c != '\\') {
                    out += c;
                    continue;
                }
                switch (take()) {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    auto cp = hex4();
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (take() != '\\' || take() != 'u')
                            throw std::runtime_error("unpaired JSON surrogate");
                        auto low = hex4();
                        if (low < 0xdc00 || low > 0xdfff)
                            throw std::runtime_error("unpaired JSON surrogate");
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff)
                        throw std::runtime_error("unpaired JSON surrogate");
                    utf8(out, cp);
                    break;
                }
                default:
                    throw std::runtime_error("invalid JSON escape");
                }
            }
        }
        Json value(unsigned depth) {
            if (depth > 128)
                throw std::runtime_error("JSON nesting limit exceeded");
            space();
            if (position >= input.size())
                throw std::runtime_error("missing JSON value");
            const char c = input[position];
            if (c == '"')
                return text();
            if (c == '{') {
                ++position;
                Object o;
                if (consume('}'))
                    return o;
                do {
                    space();
                    auto k = text();
                    if (!consume(':'))
                        throw std::runtime_error("missing JSON colon");
                    if (o.contains(k))
                        throw std::runtime_error("duplicate JSON key");
                    o.emplace(std::move(k), value(depth + 1));
                } while (consume(','));
                if (!consume('}'))
                    throw std::runtime_error("missing JSON object end");
                return o;
            }
            if (c == '[') {
                ++position;
                Array a;
                if (consume(']'))
                    return a;
                do {
                    a.push_back(value(depth + 1));
                } while (consume(','));
                if (!consume(']'))
                    throw std::runtime_error("missing JSON array end");
                return a;
            }
            for (auto [word, result] :
                 {std::pair<std::string_view, Json>{"null", Json{}}, {"true", Json{true}}, {"false", Json{false}}})
                if (input.substr(position, word.size()) == word) {
                    position += word.size();
                    return result;
                }
            auto start = position;
            if (input[position] == '-')
                ++position;
            if (position >= input.size())
                throw std::runtime_error("invalid JSON number");
            if (input[position] == '0')
                ++position;
            else {
                if (input[position] < '1' || input[position] > '9')
                    throw std::runtime_error("invalid JSON value");
                while (position < input.size() && input[position] >= '0' && input[position] <= '9')
                    ++position;
            }
            bool decimal = false;
            if (position < input.size() && input[position] == '.') {
                decimal = true;
                ++position;
                auto begin = position;
                while (position < input.size() && input[position] >= '0' && input[position] <= '9')
                    ++position;
                if (begin == position)
                    throw std::runtime_error("invalid JSON fraction");
            }
            if (position < input.size() && (input[position] == 'e' || input[position] == 'E')) {
                decimal = true;
                ++position;
                if (position < input.size() && (input[position] == '+' || input[position] == '-'))
                    ++position;
                auto begin = position;
                while (position < input.size() && input[position] >= '0' && input[position] <= '9')
                    ++position;
                if (begin == position)
                    throw std::runtime_error("invalid JSON exponent");
            }
            const auto s = input.substr(start, position - start);
            if (!decimal) {
                std::int64_t n{};
                auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
                if (error == std::errc{} && end == s.data() + s.size())
                    return Json{static_cast<long long>(n)};
            }
            std::string copy(s);
            char *end{};
            double n = std::strtod(copy.c_str(), &end);
            if (!std::isfinite(n) || end != copy.c_str() + copy.size())
                throw std::runtime_error("invalid JSON number");
            return n;
        }
    };
};

} // namespace neura::tooling
