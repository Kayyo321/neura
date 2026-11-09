#include "names.hpp"

#include <algorithm>
#include <cstdint>

namespace neura::nvr {
namespace {

enum NameKind : std::uint8_t { FunctionName = 0, LayoutName = 1, FieldName = 2, GlobalName = 3, SlotName = 4 };

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    bool done() const { return position_ == bytes_.size(); }
    std::optional<std::uint64_t> number(int width) {
        if (bytes_.size() - position_ < static_cast<std::size_t>(width)) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        for (int i = 0; i < width; ++i) {
            value |= std::uint64_t{bytes_[position_++]} << (8 * i);
        }
        return value;
    }
    std::optional<std::string> text() {
        const auto length = number(4);
        if (!length || bytes_.size() - position_ < *length) {
            return std::nullopt;
        }
        std::string value(bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
                          bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + *length));
        position_ += *length;
        return value;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

} // namespace

std::optional<Names> decode_names(const Image& image) {
    const auto section = std::find_if(image.other_sections.begin(), image.other_sections.end(),
                                      [](const RawSection& s) { return s.tag == names_tag; });
    if (section == image.other_sections.end()) {
        return std::nullopt;
    }
    Names names;
    Reader in(section->data);
    const auto count = in.number(4);
    if (!count) {
        return std::nullopt;
    }
    const auto place = [](std::vector<std::string>& list, std::uint64_t index, std::string name) {
        if (index > (1U << 20)) {
            return false;
        }
        if (list.size() <= index) {
            list.resize(index + 1);
        }
        list[index] = std::move(name);
        return true;
    };
    for (std::uint64_t i = 0; i < *count; ++i) {
        const auto kind = in.number(1);
        const auto index = in.number(4);
        const auto sub = in.number(4);
        const auto name = in.text();
        if (!kind || !index || !sub || !name || *index > (1U << 20)) {
            return std::nullopt;
        }
        bool ok = true;
        switch (*kind) {
        case FunctionName: ok = place(names.functions, *index, *name); break;
        case LayoutName: ok = place(names.layouts, *index, *name); break;
        case GlobalName: ok = place(names.globals, *index, *name); break;
        case FieldName:
            if (names.fields.size() <= *index) names.fields.resize(*index + 1);
            ok = place(names.fields[*index], *sub, *name);
            break;
        case SlotName:
            if (names.slots.size() <= *index) names.slots.resize(*index + 1);
            ok = place(names.slots[*index], *sub, *name);
            break;
        default: return std::nullopt;
        }
        if (!ok) {
            return std::nullopt;
        }
    }
    if (!in.done()) {
        return std::nullopt;
    }
    return names;
}

RawSection encode_names(const Names& names) {
    std::vector<std::uint8_t> bytes;
    const auto put = [&](std::uint64_t value, int width) {
        for (int i = 0; i < width; ++i) {
            bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    };
    std::uint32_t count = 0;
    std::vector<std::uint8_t> body;
    const auto entry = [&](std::uint8_t kind, std::size_t index, std::size_t sub, const std::string& name) {
        if (name.empty()) {
            return;
        }
        ++count;
        std::swap(bytes, body);
        put(kind, 1);
        put(index, 4);
        put(sub, 4);
        put(name.size(), 4);
        bytes.insert(bytes.end(), name.begin(), name.end());
        std::swap(bytes, body);
    };
    for (std::size_t i = 0; i < names.functions.size(); ++i) entry(FunctionName, i, 0, names.functions[i]);
    for (std::size_t i = 0; i < names.layouts.size(); ++i) entry(LayoutName, i, 0, names.layouts[i]);
    for (std::size_t i = 0; i < names.fields.size(); ++i) {
        for (std::size_t j = 0; j < names.fields[i].size(); ++j) entry(FieldName, i, j, names.fields[i][j]);
    }
    for (std::size_t i = 0; i < names.globals.size(); ++i) entry(GlobalName, i, 0, names.globals[i]);
    for (std::size_t i = 0; i < names.slots.size(); ++i) {
        for (std::size_t j = 0; j < names.slots[i].size(); ++j) entry(SlotName, i, j, names.slots[i][j]);
    }
    put(count, 4);
    bytes.insert(bytes.end(), body.begin(), body.end());
    return {names_tag, std::move(bytes)};
}

} // namespace neura::nvr
