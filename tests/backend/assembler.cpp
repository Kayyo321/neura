#include "assembler.hpp"

namespace neura::testing::nvr_bytes {

Bytes assemble(const std::vector<Section>& sections, const Header& header, const std::uint64_t* file_size) {
    std::uint64_t offset = directory_offset + directory_record * sections.size();
    std::uint64_t total = offset;
    for (const auto& section : sections) {
        total += section.data.size();
    }
    Out out;
    out.tag(header.magic).u16(header.major).u16(header.minor).u32(header.profile).u32(sections.size());
    out.u64(file_size != nullptr ? *file_size : total);
    for (const auto& section : sections) {
        out.tag(section.tag).u32(section.flags).u64(offset).u64(section.data.size());
        offset += section.data.size();
    }
    for (const auto& section : sections) {
        out.raw(section.data);
    }
    return out.bytes;
}

Bytes value(std::uint8_t kind, std::uint32_t type, std::uint32_t index) {
    return Out().u8(kind).u32(type).u32(index).bytes;
}

Bytes vector_of(std::initializer_list<Bytes> items) {
    Out out;
    out.u32(items.size());
    for (const auto& item : items) {
        out.raw(item);
    }
    return out.bytes;
}

void replace(std::vector<Section>& sections, std::string_view tag, Bytes data) {
    for (auto& section : sections) {
        if (section.tag == tag) {
            section.data = std::move(data);
            return;
        }
    }
}

std::vector<Section> hello_sections() {
    constexpr std::uint32_t none = 0xffffffffU;
    // CONS: int32 0, text "hi".
    Out constants;
    constants.u32(2);
    constants.u32(2).u32(4).u8(0).u8(0).u8(0).u8(0);
    constants.u32(3).text("hi");

    // FUNC: one function with three blocks.
    Out functions;
    functions.u32(1);
    functions.u32(1).u32(2);                 // parameter count, result type int32
    functions.u32(2).u32(4).u32(5);          // frame: text[], uint32
    functions.u32(3);                        // blocks
    // Block 0: host call writing constant 1.
    functions.u32(0).u32(0).u32(0);          // graph 0, no inputs, selector 0
    functions.u32(1);                        // one exit
    functions.u8(4).u32(0);                  // host call, no assignments
    functions.u32(0);                        // import 0
    functions.raw(vector_of({value(1, 3, 1)}));
    functions.u32(none).u32(1);              // no success slot, success block 1
    functions.u32(1).u32(2);                 // error slot 1, error block 2
    // Block 1: return constant 0.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(2).u32(0).raw(value(1, 2, 0));
    // Block 2: trap with reason 3.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(5).u32(0).u32(3);

    Out globals;
    globals.u32(0);

    // GRPH: one empty graph.
    Out graphs;
    graphs.u32(1).u32(0).u32(0).u32(0);

    Out imports;
    imports.u32(1).text("neura.console.write_line").u32(1);
    imports.u32(1).u32(3);                   // parameters: text
    imports.u32(0).u32(5);                   // success void, error uint32

    Out layouts;
    layouts.u32(0);

    Out meta;
    meta.u32(0);

    Out types;
    types.u32(6);
    types.u8(0);                             // 0 void
    types.u8(1);                             // 1 bool
    types.u8(3).u8(32);                      // 2 int32
    types.u8(4);                             // 3 text
    types.u8(7).u32(3);                      // 4 text[]
    types.u8(2).u8(32);                      // 5 uint32

    return {
        {"CONS", 1, constants.bytes}, {"FUNC", 1, functions.bytes}, {"GLOB", 1, globals.bytes},
        {"GRPH", 1, graphs.bytes},    {"IMPT", 1, imports.bytes},   {"LAYO", 1, layouts.bytes},
        {"META", 1, meta.bytes},      {"TYPE", 1, types.bytes},
    };
}

std::vector<Section> multi_result_sections(unsigned returned_words) {
    Out constants;
    constants.u32(2);
    constants.u32(2).u32(4).u32(7);                  // int32 7
    constants.u32(5).u32(8).u32(9).u32(0);           // int64 9

    Out functions;
    functions.u32(2);
    // Function 0: parameters 1, result int32, no more results, frame text[], int32, int64.
    functions.u32(1).u32(2);
    functions.u32(0);
    functions.u32(3).u32(4).u32(2).u32(5);
    functions.u32(2);
    // Block 0: call function 1 with no arguments, result slot 1 (and so slot 2), resume 1.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(1).u32(0).u32(1).u32(0).u32(1).u32(1);
    // Block 1: return slot 1.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(2).u32(0).raw(value(0, 2, 1));
    // Function 1: parameters 0, result int32, more results [int64], empty frame, one block.
    functions.u32(0).u32(2);
    functions.u32(1).u32(5);
    functions.u32(0);
    functions.u32(1);
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(2).u32(0);
    functions.raw(value(1, 2, 0));
    if (returned_words > 1) functions.raw(value(1, 5, 1));

    Out graphs;
    graphs.u32(1).u32(0).u32(0).u32(0);
    Out types;
    types.u32(6).u8(0).u8(1).u8(3).u8(32).u8(4).u8(7).u32(3).u8(3).u8(64);
    Out empty;
    empty.u32(0);
    return {
        {"CONS", 1, constants.bytes}, {"FUNC", 1, functions.bytes}, {"GLOB", 1, empty.bytes},
        {"GRPH", 1, graphs.bytes},    {"IMPT", 1, empty.bytes},     {"LAYO", 1, empty.bytes},
        {"META", 1, empty.bytes},     {"TYPE", 1, types.bytes},
    };
}

std::vector<Section> aggregate_sections(unsigned minor) {
    Out constants;
    constants.u32(3);
    constants.u32(2).u32(4).u32(0);                              // int32 0
    constants.u32(5).u32(8).u64(1);                              // int64 1
    constants.u32(6).u32(8).u64(0x4004000000000000ULL);          // float64 2.5

    Out functions;
    functions.u32(1);
    functions.u32(1).u32(2);                 // parameter count, result type int32
    if (minor >= 7) functions.u32(0);        // no more result words
    functions.u32(2).u32(4).u32(8);          // frame: text[], array of aggregates
    functions.u32(2);                        // blocks
    // Block 0: allocate the array of one element, its words int64 1 and float64 2.5.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(3).u32(0).u8(7).u32(8);
    functions.raw(vector_of({value(1, 5, 1), value(1, 6, 2)}));
    functions.u32(1).u32(1);                 // result slot 1, resume block 1
    // Block 1: return int32 0.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(2).u32(0).raw(value(1, 2, 0));

    Out graphs;
    graphs.u32(1).u32(0).u32(0).u32(0);
    Out empty;
    empty.u32(0);
    Out layouts;
    layouts.u32(1);
    layouts.u32(0xffffffffU).u32(2).u32(5).u32(6).u32(0);        // no parent, fields int64 float64, no interfaces
    Out types;
    types.u32(9);
    types.u8(0).u8(1).u8(3).u8(32).u8(4).u8(7).u32(3).u8(3).u8(64).u8(9).u8(64);
    types.u8(11).u32(0);                     // 7 aggregate of layout 0
    types.u8(7).u32(7);                      // 8 array of type 7
    return {
        {"CONS", 1, constants.bytes}, {"FUNC", 1, functions.bytes}, {"GLOB", 1, empty.bytes},
        {"GRPH", 1, graphs.bytes},    {"IMPT", 1, empty.bytes},     {"LAYO", 1, layouts.bytes},
        {"META", 1, empty.bytes},     {"TYPE", 1, types.bytes},
    };
}

std::vector<Section> region_sections(unsigned minor) {
    constexpr std::uint32_t none = 0xffffffffU;
    Out constants;
    constants.u32(1);
    constants.u32(2).u32(4).u32(0);                              // int32 0

    Out functions;
    functions.u32(1);
    functions.u32(1).u32(2);                 // parameter count, result type int32
    if (minor >= 7) functions.u32(0);        // no more result words
    functions.u32(2).u32(4).u32(5);          // frame: text[], uint64
    functions.u32(3);                        // blocks
    // Block 0: enter a region, the token in slot 1.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(3).u32(0).u8(27).u32(0);
    functions.u32(0);                        // no arguments
    functions.u32(1).u32(1);                 // result slot 1, resume block 1
    // Block 1: release it.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(3).u32(0).u8(28).u32(0);
    functions.raw(vector_of({value(0, 5, 1)}));
    functions.u32(none).u32(2);
    // Block 2: return int32 0.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(2).u32(0).raw(value(1, 2, 0));

    Out graphs;
    graphs.u32(1).u32(0).u32(0).u32(0);
    Out empty;
    empty.u32(0);
    Out types;
    types.u32(6);
    types.u8(0).u8(1).u8(3).u8(32).u8(4).u8(7).u32(3).u8(2).u8(64);  // void bool int32 text text[] uint64
    return {
        {"CONS", 1, constants.bytes}, {"FUNC", 1, functions.bytes}, {"GLOB", 1, empty.bytes},
        {"GRPH", 1, graphs.bytes},    {"IMPT", 1, empty.bytes},     {"LAYO", 1, empty.bytes},
        {"META", 1, empty.bytes},     {"TYPE", 1, types.bytes},
    };
}

} // namespace neura::testing::nvr_bytes
