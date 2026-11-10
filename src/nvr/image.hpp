#pragma once

// In-memory model of an NVR 1.0 image. The writer encodes this model and the
// loader decodes and validates it, so both sides share one definition of the
// records in docs/runtime/NVR.md.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neura::nvr {

inline constexpr std::uint32_t absent = 0xffffffffU;
inline constexpr std::uint16_t major_version = 1;
// The highest minor version of major version 1 that this code reads and
// writes. The writer emits the lowest minor version whose features the image
// uses, so images that need nothing newer stay byte-identical to 1.0.
inline constexpr std::uint16_t minor_version = 5;
inline constexpr std::uint32_t graph_profile = 1;

enum class TypeKind : std::uint8_t {
    Void = 0, Bool = 1, Unsigned = 2, Signed = 3, Text = 4, Bytes = 5, Object = 6, Array = 7, List = 8,
    // NVR 1.1: IEEE 754 binary16, binary32, or binary64 by width, and
    // bfloat16 (width 16).
    Float = 9, BFloat = 10
};

struct Type {
    TypeKind kind = TypeKind::Void;
    // Width in bits for Unsigned, Signed, Float, and BFloat; zero otherwise.
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
    std::vector<std::uint32_t> interfaces{};

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

enum class ExitKind : std::uint8_t {
    Continue = 0, Call = 1, Return = 2, State = 3, Host = 4, Trap = 5,
    // NVR 1.2. Program failure with one text argument.
    MessageTrap = 6,
    // NVR 1.5.
    Scheduler = 7
};

inline constexpr std::uint8_t scheduler_poll_bit = 0x80;

// Scheduler operations (docs/runtime/NVR.md, "Scheduler operations").
enum class SchedulerOp : std::uint8_t {
    ScopeEnter = 0, ScopeExit = 1, Spawn = 2, Join = 3, IsFinished = 4,
    // Cancellation: a scope's signal and deadline, and the running task's context.
    ScopeCancel = 5, ScopeIsCancelled = 6, ScopeSetDeadline = 7, ScopeDeadline = 8,
    TaskCancelled = 9, TaskDeadline = 10,
    // The value a receive or a selected join left for the task.
    Take = 11,
    ChannelOpen = 12, ChannelSend = 13, ChannelReceive = 14, ChannelTrySend = 15, ChannelTryReceive = 16,
    ChannelClose = 17,
    TimerOpen = 18, TimerWait = 19,
    Select = 20,
    // A guard owns one value; a task enters to get exclusive use of it.
    GuardOpen = 21, GuardEnter = 22, GuardExit = 23,
    // An isolated item of a parallel map: start it, then finish it for its
    // result. The target of a start is the function the item runs.
    ParallelStart = 24, ParallelFinish = 25
};

inline constexpr std::uint8_t scheduler_op_count = 26;

inline constexpr std::array<std::string_view, scheduler_op_count> scheduler_op_names{
    "scope-enter", "scope-exit", "spawn", "join", "is-finished",
    "scope-cancel", "scope-is-cancelled", "scope-set-deadline", "scope-deadline",
    "task-cancelled", "task-deadline", "take",
    "channel-open", "channel-send", "channel-receive", "channel-try-send", "channel-try-receive", "channel-close",
    "timer-open", "timer-wait", "select",
    "guard-open", "guard-enter", "guard-exit",
    "parallel-start", "parallel-finish",
};

// The uint32 result of an operation that can park or find nothing to do.
enum class SchedulerOutcome : std::uint32_t { Done = 0, Closed = 1, Cancelled = 2, Full = 3, Empty = 4 };

// The kinds of a select arm, each a uint32 constant that begins the arm's
// arguments: a receive and a timer wait take the object, a send the object
// and the value, a join the task.
enum class SelectArm : std::uint32_t { Receive = 0, Send = 1, Join = 2, Timer = 3 };
inline constexpr std::uint32_t select_arm_count = 4;

// The deadline of a scope or task that has none.
inline constexpr std::int64_t no_deadline = INT64_MAX;

enum class StateOp : std::uint8_t {
    AllocateObject = 0, ReadTag = 1, ReadField = 2, WriteField = 3, FreeObject = 4,
    ReadGlobal = 5, WriteGlobal = 6, AllocateCollection = 7, ReadLength = 8, ReadElement = 9,
    WriteElement = 10, AppendElement = 11, RemoveElement = 12, ConstructText = 13,
    // NVR 1.1.
    ReadVersion = 14,
    // NVR 1.2.
    TextSlice = 15, TextFromUtf8 = 16, TextEqual = 17,
    ClassHash = 18, ClassText = 19, ClassIs = 20, ClassCast = 21, ScalarText = 22,
    // NVR 1.3.
    RepeatCollection = 23
};

inline constexpr std::uint8_t state_op_count = 24;

enum class TrapReason : std::uint32_t {
    Arithmetic = 1, Bounds = 2, ProgramFailure = 3, InvalidDispatch = 4,
    // NVR 1.1.
    ModifiedDuringIteration = 5
};

// One record for every exit kind. Fields that a kind does not use keep their
// default values; the writer emits only the fields listed for the kind.
struct Exit {
    ExitKind kind = ExitKind::Continue;
    bool poll = false;
    std::vector<Assignment> assignments;
    SchedulerOp scheduler = SchedulerOp::ScopeEnter;
    // Call and Spawn: function index. Host: import index.
    std::uint32_t target = 0;
    // State operation code and immediate.
    StateOp operation = StateOp::AllocateObject;
    std::uint32_t immediate = 0;
    // Call, State, Host, and Scheduler arguments. Return holds its value here: one value
    // for a nonvoid function and none otherwise. MessageTrap has one text.
    std::vector<Value> arguments;
    // Call, State, and Scheduler result slot, Host success slot.
    std::uint32_t result_slot = absent;
    // Continue, Call, State, and Scheduler resume block, Host success block.
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

// NVR 1.4. A function the runtime may call while a host request is in
// progress, named like an import: a host service delivers the arguments and
// receives the result (Std.DynamicForeign upcalls).
struct Callback {
    std::string name;
    std::uint32_t interface_version = 0;
    std::uint32_t function = 0;

    bool operator==(const Callback&) const = default;
};

// NVR 1.4. A shared object linked from C imports. A runner loads it only for
// neura.ffi.library.embedded, and only on the target it names.
struct NativeLibrary {
    std::string triple;
    std::vector<std::uint8_t> object;
    // The symbols the image's C imports use, in ascending byte order.
    std::vector<std::string> symbols;

    bool operator==(const NativeLibrary&) const = default;
};

struct SourceLocation {
    std::uint32_t function = 0;
    std::uint32_t block = 0;
    std::string path;
    std::uint32_t line = 1;
    std::uint32_t column = 1;

    bool operator==(const SourceLocation&) const = default;
};

// An optional section this code does not interpret, kept so that decoding
// and re-encoding an image preserves it.
struct RawSection {
    std::array<char, 4> tag{};
    std::vector<std::uint8_t> data;

    bool operator==(const RawSection&) const = default;
};

struct Image {
    // The minor version recorded in the header. The writer emits at least
    // this version and raises it when the image uses a newer feature.
    std::uint16_t minor_version = 0;
    std::vector<Type> types;
    std::vector<Constant> constants;
    std::vector<Layout> layouts;
    std::vector<Global> globals;
    std::vector<Graph> graphs;
    std::vector<Import> imports;
    std::vector<Function> functions;
    std::uint32_t entry = 0;
    // NVR 1.4: the CALB and NATV sections, present only when used.
    std::vector<Callback> callbacks;
    std::optional<NativeLibrary> native_library;
    // NVR 1.5. Required TRNS table for scheduler exits and polls.
    std::optional<std::vector<std::uint32_t>> transferable_layouts;
    // The optional SMAP section. Absent when the image carries no source map.
    std::optional<std::vector<SourceLocation>> source_map;
    // Unknown optional sections in ascending tag order.
    std::vector<RawSection> other_sections;

    bool operator==(const Image&) const = default;
};

// The lowest minor version that can represent `image`.
std::uint16_t required_minor_version(const Image& image);

// Number of graph lanes a value of `type` occupies at the graph boundary.
unsigned lane_count(const Type& type);

// Number of selector lanes for a block with `exit_count` exits: the minimum
// number of bits that represents the highest exit index.
unsigned selector_lanes(std::size_t exit_count);

bool is_reference(TypeKind kind);

} // namespace neura::nvr
