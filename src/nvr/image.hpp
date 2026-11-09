#pragma once

// In-memory model of an NVR 1.0 image. The writer encodes this model and the
// loader decodes and validates it, so both sides share one definition of the
// records in docs/runtime/NVR.md.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace neura::nvr {

inline constexpr std::uint32_t absent = 0xffffffffU;
inline constexpr std::uint16_t major_version = 1;
inline constexpr std::uint16_t minor_version = 0;
inline constexpr std::uint32_t graph_profile = 1;

enum class TypeKind : std::uint8_t {
    Void = 0, Bool = 1, Unsigned = 2, Signed = 3, Text = 4, Bytes = 5, Object = 6, Array = 7, List = 8
};

struct Type {
    TypeKind kind = TypeKind::Void;
    // Width in bits for Unsigned and Signed; zero otherwise.
    std::uint8_t width = 0;
    // Layout for Object, element type for Array and List; zero otherwise.
    std::uint32_t reference = 0;

    bool operator==(const Type&) const = default;
};

struct Constant {
    std::uint32_t type = 0;
    std::vector<std::uint8_t> payload;

    bool operator==(const Constant&) const = default;
};

struct Layout {
    std::uint32_t parent = absent;
    std::vector<std::uint32_t> fields;

    bool operator==(const Layout&) const = default;
};

struct Global {
    std::uint32_t type = 0;
    std::uint32_t initializer = absent;

    bool operator==(const Global&) const = default;
};

enum class Activation : std::uint8_t { Identity = 0, Relu = 1 };

struct Edge {
    std::uint32_t source = 0;
    std::int64_t weight = 0;

    bool operator==(const Edge&) const = default;
};

struct Node {
    Activation activation = Activation::Identity;
    std::int64_t bias = 0;
    std::vector<Edge> edges;

    bool operator==(const Node&) const = default;
};

struct Graph {
    std::uint32_t input_lanes = 0;
    std::vector<Node> nodes;
    std::vector<std::uint32_t> outputs;

    bool operator==(const Graph&) const = default;
};

enum class ValueKind : std::uint8_t { Slot = 0, Constant = 1, Lanes = 2 };

struct Value {
    ValueKind kind = ValueKind::Slot;
    std::uint32_t type = 0;
    std::uint32_t index = 0;

    bool operator==(const Value&) const = default;
};

struct Assignment {
    std::uint32_t slot = 0;
    Value value;

    bool operator==(const Assignment&) const = default;
};

enum class ExitKind : std::uint8_t { Continue = 0, Call = 1, Return = 2, State = 3, Host = 4, Trap = 5 };

enum class StateOp : std::uint8_t {
    AllocateObject = 0, ReadTag = 1, ReadField = 2, WriteField = 3, FreeObject = 4,
    ReadGlobal = 5, WriteGlobal = 6, AllocateCollection = 7, ReadLength = 8, ReadElement = 9,
    WriteElement = 10, AppendElement = 11, RemoveElement = 12, ConstructText = 13
};

inline constexpr std::uint8_t state_op_count = 14;

enum class TrapReason : std::uint32_t {
    Arithmetic = 1, Bounds = 2, ProgramFailure = 3, InvalidDispatch = 4
};

// One record for every exit kind. Fields that a kind does not use keep their
// default values; the writer emits only the fields listed for the kind.
struct Exit {
    ExitKind kind = ExitKind::Continue;
    std::vector<Assignment> assignments;
    // Call: function index. Host: import index.
    std::uint32_t target = 0;
    // State operation code and immediate.
    StateOp operation = StateOp::AllocateObject;
    std::uint32_t immediate = 0;
    // Call, State, and Host arguments. Return holds its value here: one value
    // for a nonvoid function and none otherwise.
    std::vector<Value> arguments;
    // Call and State result slot, Host success slot.
    std::uint32_t result_slot = absent;
    // Continue, Call, and State resume block, Host success block.
    std::uint32_t resume = 0;
    // Host error slot and block.
    std::uint32_t error_slot = absent;
    std::uint32_t error_resume = 0;
    // Trap reason.
    std::uint32_t reason = 0;

    bool operator==(const Exit&) const = default;
};

struct Block {
    std::uint32_t graph = 0;
    std::vector<Value> inputs;
    std::uint32_t selector_start = 0;
    std::vector<Exit> exits;

    bool operator==(const Block&) const = default;
};

struct Function {
    std::uint32_t parameter_count = 0;
    std::uint32_t result_type = 0;
    std::vector<std::uint32_t> frame_types;
    std::vector<Block> blocks;

    bool operator==(const Function&) const = default;
};

struct Import {
    std::string name;
    std::uint32_t interface_version = 0;
    std::vector<std::uint32_t> parameter_types;
    std::uint32_t success_type = 0;
    std::uint32_t error_type = 0;

    bool operator==(const Import&) const = default;
};

struct SourceLocation {
    std::uint32_t function = 0;
    std::uint32_t block = 0;
    std::string path;
    std::uint32_t line = 1;
    std::uint32_t column = 1;

    bool operator==(const SourceLocation&) const = default;
};

struct Image {
    std::vector<Type> types;
    std::vector<Constant> constants;
    std::vector<Layout> layouts;
    std::vector<Global> globals;
    std::vector<Graph> graphs;
    std::vector<Import> imports;
    std::vector<Function> functions;
    std::uint32_t entry = 0;
    // The optional SMAP section. Absent when the image carries no source map.
    std::optional<std::vector<SourceLocation>> source_map;

    bool operator==(const Image&) const = default;
};

// Number of graph lanes a value of `type` occupies at the graph boundary.
unsigned lane_count(const Type& type);

// Number of selector lanes for a block with `exit_count` exits: the minimum
// number of bits that represents the highest exit index.
unsigned selector_lanes(std::size_t exit_count);

bool is_reference(TypeKind kind);

} // namespace neura::nvr
