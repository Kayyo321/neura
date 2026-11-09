#pragma once

// A byte-level NVR assembler for wire-format fixtures. It is written from the
// tables in docs/runtime/NVR.md and shares no code with the production
// writer, so a fixture checks the loader against the specification rather
// than against the writer's interpretation of it.

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace neura::testing::nvr_bytes {

using Bytes = std::vector<std::uint8_t>;

class Out {
public:
    Out& u8(std::uint64_t value) { return put(value, 1); }
    Out& u16(std::uint64_t value) { return put(value, 2); }
    Out& u32(std::uint64_t value) { return put(value, 4); }
    Out& u64(std::uint64_t value) { return put(value, 8); }
    Out& i64(std::int64_t value) { return put(static_cast<std::uint64_t>(value), 8); }
    Out& tag(std::string_view text) {
        for (char c : text) {
            bytes.push_back(static_cast<std::uint8_t>(c));
        }
        return *this;
    }
    Out& text(std::string_view value) {
        u32(value.size());
        return tag(value);
    }
    Out& raw(const Bytes& data) {
        bytes.insert(bytes.end(), data.begin(), data.end());
        return *this;
    }

    Bytes bytes;

private:
    Out& put(std::uint64_t value, int width) {
        for (int i = 0; i < width; ++i) {
            bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
        return *this;
    }
};

struct Section {
    std::string tag;
    std::uint32_t flags = 1;
    Bytes data;
};

// Header fields that a fixture may override to produce an invalid image.
struct Header {
    std::string magic = "NEUR";
    std::uint16_t major = 1;
    std::uint16_t minor = 0;
    std::uint32_t profile = 1;
};

// Lays out a header, a directory, and the sections in the given order with
// contiguous offsets. The file size field is computed unless `file_size` is
// given.
Bytes assemble(const std::vector<Section>& sections, const Header& header = {},
               const std::uint64_t* file_size = nullptr);

// Offset of the first directory record and the size of one record.
inline constexpr std::size_t directory_offset = 24;
inline constexpr std::size_t directory_record = 24;

// Sections of a small valid program in ascending tag order. Its entry
// function writes "hi" through neura.console.write_line and returns 0; a
// failed write traps with reason 3.
//
// Types: 0 void, 1 bool, 2 int32, 3 text, 4 text[], 5 uint32.
// Constants: 0 int32 0, 1 text "hi".
// Function 0 frame: slot 0 text[] (arguments), slot 1 uint32 (error code).
std::vector<Section> hello_sections();

// Replaces the section with `tag`.
void replace(std::vector<Section>& sections, std::string_view tag, Bytes data);

// Encoders for single records, following the NVR 1.0 field order.
Bytes value(std::uint8_t kind, std::uint32_t type, std::uint32_t index);
Bytes vector_of(std::initializer_list<Bytes> items);

} // namespace neura::testing::nvr_bytes
