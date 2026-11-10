#include "machine.hpp"

#include "scalar_text.hpp"
#include "nvr/format.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace neura::nvm {

std::string_view describe(TrapKind kind) {
    switch (kind) {
    case TrapKind::Program: return "program trap";
    case TrapKind::GraphOverflow: return "graph arithmetic overflow";
    case TrapKind::NonBinaryLane: return "output lane is not zero or one";
    case TrapKind::InvalidSelector: return "invalid exit selector";
    case TrapKind::UninitializedSlot: return "read of an uninitialized frame slot";
    case TrapKind::UninitializedField: return "read of an uninitialized field";
    case TrapKind::UninitializedGlobal: return "read of an uninitialized global";
    case TrapKind::InvalidHandle: return "invalid reference handle";
    case TrapKind::NilReference: return "nil class reference";
    case TrapKind::InvalidCast: return "invalid class cast";
    case TrapKind::ReferenceType: return "reference has an incompatible runtime type";
    case TrapKind::IndexOutOfBounds: return "index out of bounds";
    case TrapKind::InvalidText: return "text is not valid UTF-8";
    case TrapKind::InvalidCompletion: return "ill-typed host completion";
    case TrapKind::StepLimit: return "step limit exceeded";
    case TrapKind::FrameLimit: return "frame depth limit exceeded";
    case TrapKind::HeapLimit: return "heap limit exceeded";
    case TrapKind::HandleLimit: return "handle space exhausted";
    }
    return "trap";
}

namespace {

constexpr auto max_text_bytes = static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());

std::string_view reason_text(std::uint32_t reason) {
    switch (reason) {
    case 1: return "source arithmetic failure";
    case 2: return "source bounds failure";
    case 3: return "explicit program failure";
    case 4: return "invalid dispatch";
    case 5: return "collection modified during iteration";
    default: return "undefined trap reason";
    }
}

} // namespace

std::string describe(const Trap& trap, const nvr::Image& image, bool include_indices) {
    std::string text = trap.kind == TrapKind::Program
        ? std::string(reason_text(trap.reason)) + " (trap reason " + std::to_string(trap.reason) + ")"
        : std::string(describe(trap.kind));
    if (!trap.detail.empty()) {
        text += ": ";
        constexpr std::string_view hex = "0123456789abcdef";
        for (const auto c : trap.detail) {
            const auto byte = static_cast<unsigned char>(c);
            if (c == '\n') text += "\\n";
            else if (c == '\r') text += "\\r";
            else if (c == '\0') text += "\\0";
            else if (byte < 0x20U || byte == 0x7fU) {
                text += "\\x";
                text += hex[byte >> 4];
                text += hex[byte & 0xfU];
            }
            else text += c;
        }
    }
    if (trap.function == nvr::absent) {
        return text + " before entry";
    }
    if (include_indices)
        text += " in function " + std::to_string(trap.function) + " block " + std::to_string(trap.block);
    if (image.source_map) {
        const auto& entries = *image.source_map;
        const auto it = std::lower_bound(entries.begin(), entries.end(), std::make_pair(trap.function, trap.block),
                                         [](const nvr::SourceLocation& entry, const auto& key) {
                                             return std::make_pair(entry.function, entry.block) < key;
                                         });
        if (it != entries.end() && it->function == trap.function && it->block == trap.block) {
            text += " at " + it->path + ":" + std::to_string(it->line) + ":" + std::to_string(it->column);
        }
    }
    return text;
}

namespace {

struct Fault {
    Trap trap;
};

bool checked_add(std::int64_t a, std::int64_t b, std::int64_t& result) {
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) {
        return false;
    }
    result = a + b;
    return true;
}

bool checked_multiply(std::int64_t a, std::int64_t b, std::int64_t& result) {
    if (a == 0 || b == 0) {
        result = 0;
        return true;
    }
    const auto magnitude = [](std::int64_t v) {
        return v < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
    };
    const auto ma = magnitude(a);
    const auto mb = magnitude(b);
    if (ma > std::numeric_limits<std::uint64_t>::max() / mb) {
        return false;
    }
    const auto product = ma * mb;
    constexpr auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if ((a < 0) != (b < 0)) {
        if (product > max + 1) {
            return false;
        }
        result = product == max + 1 ? std::numeric_limits<std::int64_t>::min()
                                    : -static_cast<std::int64_t>(product);
        return true;
    }
    if (product > max) {
        return false;
    }
    result = static_cast<std::int64_t>(product);
    return true;
}

std::uint64_t mask(unsigned width) {
    return width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

} // namespace

std::optional<std::size_t> evaluate_graph(const nvr::Graph& graph, std::vector<std::int64_t>& wires) {
    wires.resize(std::size_t{graph.input_lanes} + graph.nodes.size());
    for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
        const auto& node = graph.nodes[n];
        std::int64_t sum = node.bias;
        for (const auto& edge : node.edges) {
            std::int64_t product = 0;
            if (!checked_multiply(wires[edge.source], edge.weight, product) || !checked_add(sum, product, sum)) {
                return n;
            }
        }
        wires[graph.input_lanes + n] = node.activation == nvr::Activation::Relu ? std::max<std::int64_t>(0, sum) : sum;
    }
    return std::nullopt;
}

class Execution::State {
public:
    State(const nvr::Image& image, std::span<const std::string> arguments, RunLimits limits)
        : image_(image), types_(image), limits_(limits), arguments_(arguments.begin(), arguments.end()) {
        for (std::size_t i = 0; i < arguments_.size(); ++i) {
            if (!nvr::valid_utf8(arguments_[i])) {
                throw std::invalid_argument("argument " + std::to_string(i + 1) + " is not valid UTF-8");
            }
        }
    }

    State(const nvr::Image& image, std::uint32_t function, std::span<const std::uint64_t> arguments, RunLimits limits)
        : image_(image), types_(image), limits_(limits), start_function_(function),
          start_arguments_(arguments.begin(), arguments.end()) {
        if (function >= image.functions.size()) {
            throw std::invalid_argument("function " + std::to_string(function) + " is out of range");
        }
        const auto& callee = image.functions[function];
        if (arguments.size() != callee.parameter_count) {
            throw std::invalid_argument("expected " + std::to_string(callee.parameter_count) + " argument(s)");
        }
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto& t = image.types[callee.frame_types[i]];
            const bool scalar = t.kind == nvr::TypeKind::Bool || t.kind == nvr::TypeKind::Signed ||
                                t.kind == nvr::TypeKind::Unsigned || t.kind == nvr::TypeKind::Float ||
                                t.kind == nvr::TypeKind::BFloat;
            const auto width = t.kind == nvr::TypeKind::Bool ? 1U : t.width;
            if (!scalar || (arguments[i] & ~mask(width)) != 0) {
                throw std::invalid_argument("argument " + std::to_string(i) + " does not match its parameter");
            }
        }
    }

    Event advance() {
        if (finished_) {
            return *finished_;
        }
        if (pending_) {
            throw std::logic_error("advance() called while a host request is pending");
        }
        try {
            if (!started_) {
                start();
            }
            for (;;) {
                if (auto event = step()) {
                    if (event->kind != Event::Kind::HostRequest) {
                        finished_ = event;
                    }
                    return *event;
                }
            }
        } catch (const Fault& fault) {
            Event event;
            event.kind = Event::Kind::Trapped;
            event.trap = fault.trap;
            finished_ = event;
            return event;
        }
    }

    void complete(const HostCompletion& completion) {
        if (!pending_) {
            throw std::logic_error("complete() called without a pending host request");
        }
        const auto pending = *pending_;
        pending_.reset();
        try {
            const auto& import = image_.imports[pending.import];
            if (completion.exit_status) {
                const auto status = *completion.exit_status;
                using Kind = nvr::TypeKind;
                if (!completion.success || import.name != "neura.process.exit" || import.interface_version != 1 ||
                    import.parameter_types.size() != 1 || type(import.parameter_types.front()) != nvr::Type{Kind::Signed, 32} ||
                    type(import.success_type).kind != Kind::Void || type(import.error_type) != nvr::Type{Kind::Unsigned, 32} ||
                    status < 0 || status > 255) {
                    fault(TrapKind::InvalidCompletion, "invalid process exit completion");
                }
                Event event;
                event.kind = Event::Kind::Completed;
                event.status = status;
                event.result = static_cast<std::uint64_t>(status);
                finished_ = std::move(event);
                return;
            }
            const auto type = completion.success ? import.success_type : import.error_type;
            const auto slot = completion.success ? pending.success_slot : pending.error_slot;
            if (image_.types[type].kind != nvr::TypeKind::Void) {
                std::vector<std::uint64_t> temporary_roots;
                auto remaining = limits_.max_heap_cells;
                const auto value = host_to_value(completion.value, type, temporary_roots, remaining, 0);
                auto& frame = frames_.back();
                frame.slots[slot] = value;
                frame.initialized[slot] = 1;
            }
            frames_.back().block = completion.success ? pending.success_block : pending.error_block;
        } catch (const Fault& fault) {
            Event event;
            event.kind = Event::Kind::Trapped;
            event.trap = fault.trap;
            finished_ = event;
        }
    }

    std::uint64_t steps() const { return steps_; }
    std::uint64_t peak_heap_cells() const { return peak_cells_; }
    const std::vector<std::uint64_t>& function_steps() const { return function_steps_; }

private:
    enum class EntryKind : std::uint8_t { Object, Array, List, Text, Bytes };

    struct HeapEntry {
        EntryKind kind = EntryKind::Object;
        bool marked = false;
        // Layout of an object; type index of a collection, text, or bytes.
        std::uint32_t type = 0;
        std::vector<std::uint64_t> cells;
        std::vector<std::uint8_t> initialized;
        std::string bytes;
        // Structural version of a list: incremented by every successful
        // append or removal, modulo 2^64.
        std::uint64_t version = 0;
        std::uint64_t identity = 0;
    };

    struct Frame {
        std::uint32_t function = 0;
        std::uint32_t block = 0;
        std::vector<std::uint64_t> slots;
        std::vector<std::uint8_t> initialized;
        // Where the caller receives the result.
        std::uint32_t return_slot = nvr::absent;
        std::uint32_t return_block = 0;
    };

    struct Pending {
        std::uint32_t import = 0;
        std::uint32_t success_slot = nvr::absent;
        std::uint32_t success_block = 0;
        std::uint32_t error_slot = nvr::absent;
        std::uint32_t error_block = 0;
    };

    [[noreturn]] void fault(TrapKind kind, std::string detail = {}, std::uint32_t reason = 0) const {
        Trap trap;
        trap.kind = kind;
        trap.reason = reason;
        trap.detail = std::move(detail);
        if (!frames_.empty()) {
            trap.function = frames_.back().function;
            trap.block = frames_.back().block;
            for (auto frame = frames_.rbegin() + 1; frame != frames_.rend(); ++frame) {
                trap.callers.emplace_back(frame->function, frame->block);
            }
        }
        throw Fault{std::move(trap)};
    }

    const nvr::Type& type(std::uint32_t index) const { return image_.types[index]; }

    static std::uint64_t entry_cells(const HeapEntry& entry) {
        return 1 + entry.cells.size() + entry.bytes.size();
    }

    using TracePlans = std::unordered_map<std::uint32_t, std::vector<std::size_t>>;

    const std::vector<std::size_t>& trace_plan(TracePlans& plans, std::uint32_t id,
                                               const std::vector<std::uint32_t>& types) {
        auto [entry, inserted] = plans.try_emplace(id);
        auto& plan = entry->second;
        if (inserted) {
            for (std::size_t i = 0; i < types.size(); ++i) {
                if (nvr::is_reference(type(types[i]).kind)) plan.push_back(i);
            }
        }
        return plan;
    }

    void mark(std::uint64_t handle) {
        if (handle == 0) {
            return;
        }
        const auto found = heap_.find(handle);
        // Explicit destruction can leave stale aliases in typed roots and
        // fields. They keep their invalid-handle behavior when used.
        if (found != heap_.end() && !found->second.marked) {
            auto& entry = found->second;
            entry.marked = true;
            if (!entry.cells.empty()) {
                mark_work_.push_back(&entry);
            }
        }
    }

    void mark_children(const HeapEntry& item) {
        if (item.kind == EntryKind::Object) {
            const auto& fields = image_.layouts[item.type].fields;
            for (const auto field : trace_plan(layout_traces_, item.type, fields)) {
                if (item.initialized[field]) mark(item.cells[field]);
            }
        } else if ((item.kind == EntryKind::Array || item.kind == EntryKind::List) &&
                   nvr::is_reference(type(type(item.type).reference).kind)) {
            for (const auto handle : item.cells) {
                mark(handle);
            }
        }
    }

    void collect(std::span<const std::uint64_t> temporary_roots, const HeapEntry* allocation) {
        for (std::size_t i = 0; i < const_values_.size(); ++i) {
            if (nvr::is_reference(type(image_.constants[i].type).kind)) {
                mark(const_values_[i]);
            }
        }
        for (std::size_t i = 0; i < globals_.size(); ++i) {
            if (global_initialized_[i] && nvr::is_reference(type(image_.globals[i].type).kind)) {
                mark(globals_[i]);
            }
        }
        for (const auto& frame : frames_) {
            const auto& slots = image_.functions[frame.function].frame_types;
            for (const auto slot : trace_plan(frame_traces_, frame.function, slots)) {
                if (frame.initialized[slot]) mark(frame.slots[slot]);
            }
        }
        for (const auto handle : temporary_roots) {
            mark(handle);
        }
        if (allocation != nullptr) {
            mark_children(*allocation);
        }
        while (!mark_work_.empty()) {
            const auto& item = *mark_work_.back();
            mark_work_.pop_back();
            mark_children(item);
        }
        for (auto it = heap_.begin(); it != heap_.end();) {
            if (it->second.marked) {
                it->second.marked = false;
                ++it;
            } else {
                live_cells_ -= entry_cells(it->second);
                it = heap_.erase(it);
            }
        }
        // Drop large bucket arrays after a substantial decrease in live
        // entries, without churning the table during steady allocation.
        if (heap_.bucket_count() > 256 && heap_.size() < heap_.bucket_count() / 4) {
            heap_.reserve(heap_.size());
        }
        const auto headroom = limits_.max_heap_cells - live_cells_;
        next_collection_ = live_cells_ + std::min(headroom, std::max<std::uint64_t>(256, live_cells_ / 2));
    }

    void ensure_heap_cells(std::uint64_t cells, std::string_view action,
                           std::span<const std::uint64_t> temporary_roots = {}, const HeapEntry* allocation = nullptr) {
        if (cells > limits_.max_heap_cells) {
            fault(TrapKind::HeapLimit, std::string(action) + std::to_string(cells) + " cells");
        }
        if (cells > next_collection_ - std::min(live_cells_, next_collection_)) {
            collect(temporary_roots, allocation);
        }
        if (cells > limits_.max_heap_cells - live_cells_) {
            fault(TrapKind::HeapLimit, std::string(action) + std::to_string(cells) + " cells");
        }
        // An individual large request can cross the soft limit even after
        // collection. Give the next allocation a new growth window.
        if (cells > next_collection_ - live_cells_) {
            const auto projected = live_cells_ + cells;
            next_collection_ = projected + std::min(limits_.max_heap_cells - projected,
                                                    std::max<std::uint64_t>(256, projected / 2));
        }
    }

    std::uint64_t allocate(HeapEntry entry, std::span<const std::uint64_t> temporary_roots = {}) {
        if (entry.kind == EntryKind::Text && entry.bytes.size() > max_text_bytes) {
            fault(TrapKind::InvalidText, "text exceeds the int32 byte length limit");
        }
        if (last_handle_ >= limits_.max_handles) {
            fault(TrapKind::HandleLimit);
        }
        const auto cells = entry_cells(entry);
        ensure_heap_cells(cells, "allocation of ", temporary_roots, &entry);
        return insert_allocation(std::move(entry), cells);
    }

    std::uint64_t insert_allocation(HeapEntry entry, std::uint64_t cells) {
        const auto handle = last_handle_ + 1;
        heap_.emplace(handle, std::move(entry));
        last_handle_ = handle;
        live_cells_ += cells;
        peak_cells_ = std::max(peak_cells_, live_cells_);
        return handle;
    }

    HeapEntry& entry(std::uint64_t handle) {
        const auto found = heap_.find(handle);
        if (found == heap_.end()) {
            fault(TrapKind::InvalidHandle, "handle " + std::to_string(handle));
        }
        return found->second;
    }

    // The entry for a reference whose static type is `type_index`.
    HeapEntry& typed_entry(std::uint64_t handle, std::uint32_t type_index) {
        if (handle == 0 && type(type_index).kind == nvr::TypeKind::Object &&
            image_.minor_version >= 2) {
            fault(TrapKind::NilReference, {});
        }
        auto& result = entry(handle);
        if (!compatible(result, type_index)) {
            fault(TrapKind::ReferenceType, "handle " + std::to_string(handle));
        }
        return result;
    }

    bool compatible(const HeapEntry& item, std::uint32_t type_index) const {
        const auto& t = type(type_index);
        switch (t.kind) {
        case nvr::TypeKind::Object:
            return item.kind == EntryKind::Object && types_.derives(item.type, t.reference);
        case nvr::TypeKind::Array:
            return item.kind == EntryKind::Array && types_.same(item.type, type_index);
        case nvr::TypeKind::List:
            return item.kind == EntryKind::List && types_.same(item.type, type_index);
        case nvr::TypeKind::Text:
            return item.kind == EntryKind::Text;
        case nvr::TypeKind::Bytes:
            return item.kind == EntryKind::Bytes;
        default:
            return false;
        }
    }

    void start() {
        started_ = true;
        const_values_.resize(image_.constants.size());
        for (std::size_t i = 0; i < image_.constants.size(); ++i) {
            const auto& constant = image_.constants[i];
            const auto kind = type(constant.type).kind;
            if (kind == nvr::TypeKind::Text || kind == nvr::TypeKind::Bytes) {
                HeapEntry item;
                item.kind = kind == nvr::TypeKind::Text ? EntryKind::Text : EntryKind::Bytes;
                item.type = constant.type;
                item.bytes.assign(constant.payload.begin(), constant.payload.end());
                const_values_[i] = allocate(std::move(item));
            } else {
                std::uint64_t bits = 0;
                for (std::size_t b = 0; b < constant.payload.size(); ++b) {
                    bits |= std::uint64_t{constant.payload[b]} << (8 * b);
                }
                const_values_[i] = bits;
            }
        }

        if (start_function_) {
            push_frame(*start_function_, start_arguments_, nvr::absent, 0);
            initialize_globals();
            return;
        }
        const auto& entry_function = image_.functions[image_.entry];
        const auto array_type = entry_function.frame_types[0];
        HeapEntry array;
        array.kind = EntryKind::Array;
        array.type = array_type;
        for (const auto& argument : arguments_) {
            HeapEntry text;
            text.kind = EntryKind::Text;
            text.type = type(array_type).reference;
            text.bytes = argument;
            array.cells.push_back(allocate(std::move(text), array.cells));
        }
        const auto arguments = allocate(std::move(array));
        initialize_globals();
        push_frame(image_.entry, std::vector<std::uint64_t>{arguments}, nvr::absent, 0);
    }

    void initialize_globals() {
        globals_.assign(image_.globals.size(), 0);
        global_initialized_.assign(image_.globals.size(), 0);
        for (std::size_t i = 0; i < image_.globals.size(); ++i) {
            if (image_.globals[i].initializer != nvr::absent) {
                globals_[i] = const_values_[image_.globals[i].initializer];
                global_initialized_[i] = 1;
            }
        }
    }

    void push_frame(std::uint32_t function, const std::vector<std::uint64_t>& arguments,
                    std::uint32_t return_slot, std::uint32_t return_block) {
        if (frames_.size() >= limits_.max_frames) {
            fault(TrapKind::FrameLimit, "calling function " + std::to_string(function));
        }
        const auto& callee = image_.functions[function];
        Frame frame;
        frame.function = function;
        frame.slots.assign(callee.frame_types.size(), 0);
        frame.initialized.assign(callee.frame_types.size(), 0);
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            frame.slots[i] = arguments[i];
            frame.initialized[i] = 1;
        }
        frame.return_slot = return_slot;
        frame.return_block = return_block;
        frames_.push_back(std::move(frame));
    }

    std::uint64_t read(const nvr::Value& value, const Frame& frame, const nvr::Graph& graph) {
        switch (value.kind) {
        case nvr::ValueKind::Slot:
            if (!frame.initialized[value.index]) {
                fault(TrapKind::UninitializedSlot, "slot " + std::to_string(value.index));
            }
            return frame.slots[value.index];
        case nvr::ValueKind::Constant:
            return const_values_[value.index];
        case nvr::ValueKind::Lanes: {
            const auto lanes = nvr::lane_count(type(value.type));
            std::uint64_t bits = 0;
            for (unsigned i = 0; i < lanes; ++i) {
                const auto lane = wires_[graph.outputs[value.index + i]];
                if (lane != 0 && lane != 1) {
                    fault(TrapKind::NonBinaryLane, "output lane " + std::to_string(value.index + i) +
                                                       " holds " + std::to_string(lane));
                }
                bits |= static_cast<std::uint64_t>(lane) << i;
            }
            const auto kind = type(value.type).kind;
            if (nvr::is_reference(kind) &&
                !(kind == nvr::TypeKind::Object && bits == 0 && image_.minor_version >= 2)) {
                typed_entry(bits, value.type);
            }
            return bits;
        }
        }
        return 0;
    }

    void evaluate(const nvr::Block& block, const Frame& frame, const nvr::Graph& graph) {
        // Inputs are resolved before any lane is written, so an input that
        // traps leaves no partial state.
        std::vector<std::uint64_t> values;
        values.reserve(block.inputs.size());
        for (const auto& input : block.inputs) {
            values.push_back(read(input, frame, graph));
        }
        wires_.assign(std::size_t{graph.input_lanes} + graph.nodes.size(), 0);
        std::size_t lane = 0;
        for (std::size_t i = 0; i < block.inputs.size(); ++i) {
            const auto lanes = nvr::lane_count(type(block.inputs[i].type));
            for (unsigned b = 0; b < lanes; ++b) {
                wires_[lane++] = static_cast<std::int64_t>((values[i] >> b) & 1U);
            }
        }
        if (const auto failed = evaluate_graph(graph, wires_)) {
            fault(TrapKind::GraphOverflow, "graph " + std::to_string(block.graph) + " node " + std::to_string(*failed));
        }
    }

    std::uint32_t select(const nvr::Block& block, const nvr::Graph& graph) {
        if (block.exits.size() == 1) {
            return 0;
        }
        const auto lanes = nvr::selector_lanes(block.exits.size());
        std::uint32_t index = 0;
        for (unsigned i = 0; i < lanes; ++i) {
            const auto lane = wires_[graph.outputs[block.selector_start + i]];
            if (lane != 0 && lane != 1) {
                fault(TrapKind::NonBinaryLane, "selector lane " + std::to_string(i) + " holds " + std::to_string(lane));
            }
            index |= static_cast<std::uint32_t>(lane) << i;
        }
        if (index >= block.exits.size()) {
            fault(TrapKind::InvalidSelector, "selector " + std::to_string(index) + " for " +
                                                 std::to_string(block.exits.size()) + " exits");
        }
        return index;
    }

    std::optional<Event> step() {
        if (steps_ >= limits_.max_steps) {
            fault(TrapKind::StepLimit, std::to_string(limits_.max_steps) + " steps");
        }
        ++steps_;
        auto& frame = frames_.back();
        if (limits_.profile) {
            if (function_steps_.size() != image_.functions.size()) function_steps_.resize(image_.functions.size());
            ++function_steps_[frame.function];
        }
        const auto& function = image_.functions[frame.function];
        const auto& block = function.blocks[frame.block];
        const auto& graph = image_.graphs[block.graph];
        evaluate(block, frame, graph);
        const auto& exit = block.exits[select(block, graph)];

        // Resolve every value against the pre-transition state, then commit.
        std::vector<std::pair<std::uint32_t, std::uint64_t>> assignments;
        assignments.reserve(exit.assignments.size());
        for (const auto& assignment : exit.assignments) {
            assignments.emplace_back(assignment.slot, read(assignment.value, frame, graph));
        }
        std::vector<std::uint64_t> arguments;
        arguments.reserve(exit.arguments.size());
        for (const auto& argument : exit.arguments) {
            arguments.push_back(read(argument, frame, graph));
        }
        for (const auto& [slot, value] : assignments) {
            frame.slots[slot] = value;
            frame.initialized[slot] = 1;
        }

        switch (exit.kind) {
        case nvr::ExitKind::Continue:
            frame.block = exit.resume;
            return std::nullopt;
        case nvr::ExitKind::Call:
            push_frame(exit.target, arguments, exit.result_slot, exit.resume);
            return std::nullopt;
        case nvr::ExitKind::Return: {
            const auto result = arguments.empty() ? 0 : arguments.front();
            const auto return_slot = frame.return_slot;
            const auto return_block = frame.return_block;
            frames_.pop_back();
            if (frames_.empty()) {
                Event event;
                event.kind = Event::Kind::Completed;
                event.status = static_cast<std::int32_t>(static_cast<std::uint32_t>(result & mask(32)));
                event.result = result;
                return event;
            }
            auto& caller = frames_.back();
            if (return_slot != nvr::absent) {
                caller.slots[return_slot] = result;
                caller.initialized[return_slot] = 1;
            }
            caller.block = return_block;
            return std::nullopt;
        }
        case nvr::ExitKind::State: {
            const auto result = state(exit, arguments);
            if (exit.result_slot != nvr::absent) {
                frame.slots[exit.result_slot] = result;
                frame.initialized[exit.result_slot] = 1;
            }
            frame.block = exit.resume;
            return std::nullopt;
        }
        case nvr::ExitKind::Host: {
            const auto& import = image_.imports[exit.target];
            Event event;
            event.kind = Event::Kind::HostRequest;
            event.request.sequence = sequence_++;
            event.request.import = exit.target;
            auto remaining = limits_.max_heap_cells;
            for (std::size_t i = 0; i < arguments.size(); ++i) {
                event.request.arguments.push_back(value_to_host(arguments[i], import.parameter_types[i], remaining, 0));
            }
            pending_ = Pending{exit.target, exit.result_slot, exit.resume, exit.error_slot, exit.error_resume};
            return event;
        }
        case nvr::ExitKind::Trap:
            fault(TrapKind::Program, {}, exit.reason);
        case nvr::ExitKind::MessageTrap:
            fault(TrapKind::Program, typed_entry(arguments.front(), exit.arguments.front().type).bytes,
                  static_cast<std::uint32_t>(nvr::TrapReason::ProgramFailure));
        }
        return std::nullopt;
    }

    void consume_host_cells(std::size_t cells, std::uint64_t& remaining) {
        if (cells > remaining) fault(TrapKind::HeapLimit, "host value exceeds the copy budget");
        remaining -= cells;
    }

    bool byte_element(std::uint32_t type_index) const {
        const auto& element = type(type_index);
        return element.kind == nvr::TypeKind::Unsigned && element.width == 8;
    }

    HostValue value_to_host(std::uint64_t bits, std::uint32_t type_index, std::uint64_t& remaining, unsigned depth) {
        if (depth > 128) fault(TrapKind::ReferenceType, "host value nesting exceeds 128");
        HostValue value;
        const auto& t = type(type_index);
        switch (t.kind) {
        case nvr::TypeKind::Text:
        case nvr::TypeKind::Bytes: {
            const auto& bytes = typed_entry(bits, type_index).bytes;
            consume_host_cells(bytes.size(), remaining);
            value.bytes = bytes;
            break;
        }
        case nvr::TypeKind::Object: {
            const auto& item = typed_entry(bits, type_index);
            const auto& fields = image_.layouts[t.reference].fields;
            consume_host_cells(fields.size(), remaining);
            value.elements.reserve(fields.size());
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (!item.initialized[i]) fault(TrapKind::UninitializedField, "host tuple field " + std::to_string(i));
                value.elements.push_back(value_to_host(item.cells[i], fields[i], remaining, depth + 1));
            }
            break;
        }
        case nvr::TypeKind::Array:
        case nvr::TypeKind::List: {
            const auto& cells = typed_entry(bits, type_index).cells;
            consume_host_cells(cells.size(), remaining);
            if (byte_element(t.reference)) {
                value.bytes.reserve(cells.size());
                for (auto cell : cells) value.bytes.push_back(static_cast<char>(static_cast<unsigned char>(cell)));
            } else {
                value.elements.reserve(cells.size());
                for (auto cell : cells) value.elements.push_back(value_to_host(cell, t.reference, remaining, depth + 1));
            }
            break;
        }
        default:
            value.bits = bits;
            break;
        }
        return value;
    }

    std::uint64_t host_to_value(const HostValue& value, std::uint32_t type_index,
                                std::vector<std::uint64_t>& temporary_roots, std::uint64_t& remaining,
                                unsigned depth) {
        if (depth > 128) fault(TrapKind::InvalidCompletion, "host value nesting exceeds 128");
        const auto& t = type(type_index);
        switch (t.kind) {
        case nvr::TypeKind::Bool:
            if (value.bits > 1) {
                fault(TrapKind::InvalidCompletion, "Boolean completion is not zero or one");
            }
            return value.bits;
        case nvr::TypeKind::Unsigned:
        case nvr::TypeKind::Signed:
        case nvr::TypeKind::Float:
        case nvr::TypeKind::BFloat:
            if ((value.bits & ~mask(t.width)) != 0) {
                fault(TrapKind::InvalidCompletion, "numeric completion exceeds its width");
            }
            return value.bits;
        case nvr::TypeKind::Text:
        case nvr::TypeKind::Bytes: {
            if (t.kind == nvr::TypeKind::Text && !nvr::valid_utf8(value.bytes)) {
                fault(TrapKind::InvalidCompletion, "text completion is not valid UTF-8");
            }
            consume_host_cells(value.bytes.size(), remaining);
            HeapEntry item;
            item.kind = t.kind == nvr::TypeKind::Text ? EntryKind::Text : EntryKind::Bytes;
            item.type = type_index;
            item.bytes = value.bytes;
            return allocate(std::move(item), temporary_roots);
        }
        case nvr::TypeKind::Object:
        case nvr::TypeKind::Array:
        case nvr::TypeKind::List: {
            HeapEntry item;
            const bool tuple = t.kind == nvr::TypeKind::Object;
            const bool bytes = !tuple && byte_element(t.reference);
            const auto count = bytes ? value.bytes.size() : value.elements.size();
            consume_host_cells(count, remaining);
            if (tuple && image_.layouts[t.reference].fields.size() != count)
                fault(TrapKind::InvalidCompletion, "host tuple completion has the wrong number of elements");
            if ((bytes && !value.elements.empty()) || (!bytes && !value.bytes.empty()))
                fault(TrapKind::InvalidCompletion, "host completion uses the wrong collection representation");
            item.kind = tuple ? EntryKind::Object : t.kind == nvr::TypeKind::Array ? EntryKind::Array : EntryKind::List;
            item.type = tuple ? t.reference : type_index;
            item.cells.reserve(count);
            const auto root_start = temporary_roots.size();
            for (std::size_t i = 0; i < count; ++i) {
                if (bytes) {
                    item.cells.push_back(static_cast<unsigned char>(value.bytes[i]));
                    continue;
                }
                const auto element_type = tuple ? image_.layouts[t.reference].fields[i] : t.reference;
                const auto cell = host_to_value(value.elements[i], element_type, temporary_roots, remaining, depth + 1);
                item.cells.push_back(cell);
                if (nvr::is_reference(type(element_type).kind)) temporary_roots.push_back(cell);
            }
            if (tuple) item.initialized.assign(count, 1);
            const auto result = allocate(std::move(item), temporary_roots);
            temporary_roots.resize(root_start);
            return result;
        }
        default:
            fault(TrapKind::InvalidCompletion, "host results cannot have this type");
        }
    }

    std::size_t length(const HeapEntry& item) const {
        return item.kind == EntryKind::Text || item.kind == EntryKind::Bytes ? item.bytes.size() : item.cells.size();
    }

    std::size_t checked_index(const HeapEntry& item, std::uint64_t index) {
        if (index >= length(item)) {
            fault(TrapKind::IndexOutOfBounds, "index " + std::to_string(index) + " for length " +
                                                  std::to_string(length(item)));
        }
        return static_cast<std::size_t>(index);
    }

    std::uint64_t state(const nvr::Exit& exit, const std::vector<std::uint64_t>& args) {
        const auto& types = exit.arguments;
        switch (exit.operation) {
        case nvr::StateOp::ClassHash:
        case nvr::StateOp::ClassText: {
            auto& item = typed_entry(args[0], types[0].type);
            if (item.identity == 0) item.identity = ++last_identity_;
            if (exit.operation == nvr::StateOp::ClassHash)
                return static_cast<std::uint32_t>(item.identity) * std::uint32_t{2654435761U};
            HeapEntry text;
            text.kind = EntryKind::Text;
            text.type = types[1].type;
            text.bytes = typed_entry(args[1], types[1].type).bytes + "#" + std::to_string(item.identity);
            return allocate(std::move(text));
        }
        case nvr::StateOp::ClassIs:
        case nvr::StateOp::ClassCast: {
            const bool test = exit.operation == nvr::StateOp::ClassIs;
            if (args[0] == 0) return 0;
            const auto& item = typed_entry(args[0], types[0].type);
            const bool matches = types_.derives(item.type, exit.immediate);
            if (test) return matches;
            if (!matches) fault(TrapKind::InvalidCast, {});
            return args[0];
        }
        case nvr::StateOp::AllocateObject: {
            HeapEntry item;
            item.kind = EntryKind::Object;
            item.type = exit.immediate;
            item.cells.assign(image_.layouts[exit.immediate].fields.size(), 0);
            item.initialized.assign(item.cells.size(), 0);
            return allocate(std::move(item));
        }
        case nvr::StateOp::ReadTag:
            return typed_entry(args[0], types[0].type).type;
        case nvr::StateOp::ReadField: {
            auto& item = typed_entry(args[0], types[0].type);
            if (!item.initialized[exit.immediate]) {
                fault(TrapKind::UninitializedField, "field " + std::to_string(exit.immediate));
            }
            return item.cells[exit.immediate];
        }
        case nvr::StateOp::WriteField: {
            auto& item = typed_entry(args[0], types[0].type);
            item.cells[exit.immediate] = args[1];
            item.initialized[exit.immediate] = 1;
            return 0;
        }
        case nvr::StateOp::FreeObject: {
            const auto& item = typed_entry(args[0], types[0].type);
            live_cells_ -= entry_cells(item);
            heap_.erase(args[0]);
            return 0;
        }
        case nvr::StateOp::ReadGlobal:
            if (!global_initialized_[exit.immediate]) {
                fault(TrapKind::UninitializedGlobal, "global " + std::to_string(exit.immediate));
            }
            return globals_[exit.immediate];
        case nvr::StateOp::WriteGlobal:
            globals_[exit.immediate] = args[0];
            global_initialized_[exit.immediate] = 1;
            return 0;
        case nvr::StateOp::RepeatCollection:
        case nvr::StateOp::AllocateCollection: {
            HeapEntry item;
            item.kind = type(exit.immediate).kind == nvr::TypeKind::Array ? EntryKind::Array : EntryKind::List;
            item.type = exit.immediate;
            if (exit.operation == nvr::StateOp::RepeatCollection) {
                if (last_handle_ >= limits_.max_handles) fault(TrapKind::HandleLimit);
                const auto count = args[0];
                if (count > item.cells.max_size() || count == std::numeric_limits<std::uint64_t>::max())
                    fault(TrapKind::HeapLimit, "collection length exceeds storage limits");
                const auto cells = count + 1;
                const bool reference = nvr::is_reference(type(type(item.type).reference).kind);
                const std::array roots{reference && count != 0 ? args[1] : std::uint64_t{0}};
                ensure_heap_cells(cells, "allocation of ", roots);
                item.cells.assign(static_cast<std::size_t>(count), args[1]);
                return insert_allocation(std::move(item), cells);
            }
            item.cells = args;
            return allocate(std::move(item));
        }
        case nvr::StateOp::ReadLength:
            return length(typed_entry(args[0], types[0].type));
        case nvr::StateOp::ReadElement: {
            auto& item = typed_entry(args[0], types[0].type);
            const auto index = checked_index(item, args[1]);
            if (item.kind == EntryKind::Text || item.kind == EntryKind::Bytes) {
                return static_cast<unsigned char>(item.bytes[index]);
            }
            return item.cells[index];
        }
        case nvr::StateOp::WriteElement: {
            auto& item = typed_entry(args[0], types[0].type);
            item.cells[checked_index(item, args[1])] = args[2];
            return 0;
        }
        case nvr::StateOp::AppendElement: {
            auto& item = typed_entry(args[0], types[0].type);
            const auto element_type = type(item.type).reference;
            const std::array roots{args[0], nvr::is_reference(type(element_type).kind) ? args[1] : std::uint64_t{0}};
            ensure_heap_cells(1, "growth by ", roots);
            item.cells.push_back(args[1]);
            ++live_cells_;
            peak_cells_ = std::max(peak_cells_, live_cells_);
            ++item.version;
            return 0;
        }
        case nvr::StateOp::RemoveElement: {
            auto& item = typed_entry(args[0], types[0].type);
            const auto index = checked_index(item, args[1]);
            item.cells.erase(item.cells.begin() + static_cast<std::ptrdiff_t>(index));
            --live_cells_;
            ++item.version;
            return 0;
        }
        case nvr::StateOp::ReadVersion:
            return typed_entry(args[0], types[0].type).version;
        case nvr::StateOp::ConstructText: {
            const auto& source = typed_entry(args[0], types[0].type);
            HeapEntry item;
            item.kind = type(exit.immediate).kind == nvr::TypeKind::Text ? EntryKind::Text : EntryKind::Bytes;
            item.type = exit.immediate;
            item.bytes.reserve(source.cells.size());
            for (auto cell : source.cells) {
                item.bytes.push_back(static_cast<char>(static_cast<unsigned char>(cell)));
            }
            if (item.kind == EntryKind::Text && !nvr::valid_utf8(item.bytes)) {
                fault(TrapKind::InvalidText);
            }
            return allocate(std::move(item));
        }
        case nvr::StateOp::TextSlice: {
            const auto& source = typed_entry(args[0], types[0].type).bytes;
            const auto start = args[1];
            const auto end = args[2];
            if (start > end || end > source.size()) {
                fault(TrapKind::IndexOutOfBounds, "text range [" + std::to_string(start) + ", " +
                      std::to_string(end) + ") for length " + std::to_string(source.size()));
            }
            const auto boundary = [&](std::uint64_t offset) {
                return offset == source.size() || (static_cast<unsigned char>(source[offset]) & 0xc0U) != 0x80U;
            };
            if (!boundary(start) || !boundary(end)) {
                fault(TrapKind::InvalidText, "text slice endpoints must be on UTF-8 boundaries");
            }
            HeapEntry text;
            text.kind = EntryKind::Text;
            text.type = types[0].type;
            text.bytes = source.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
            return allocate(std::move(text));
        }
        case nvr::StateOp::TextFromUtf8: {
            const auto& source = typed_entry(args[0], types[0].type);
            HeapEntry text;
            text.kind = EntryKind::Text;
            text.type = image_.layouts[exit.immediate].fields[1];
            bool valid = source.cells.size() <= max_text_bytes;
            if (valid) {
                text.bytes.reserve(source.cells.size());
                for (const auto cell : source.cells)
                    text.bytes.push_back(static_cast<char>(static_cast<unsigned char>(cell)));
                valid = nvr::valid_utf8(text.bytes);
                if (!valid) std::string{}.swap(text.bytes);
            }
            const auto handle = allocate(std::move(text));
            HeapEntry result;
            result.kind = EntryKind::Object;
            result.type = exit.immediate;
            result.cells = {static_cast<std::uint64_t>(valid), handle};
            result.initialized = {1, 1};
            return allocate(std::move(result));
        }
        case nvr::StateOp::TextEqual: {
            const auto& left = typed_entry(args[0], types[0].type).bytes;
            const auto& right = typed_entry(args[1], types[1].type).bytes;
            return left == right;
        }
        case nvr::StateOp::ScalarText: {
            if (args.size() > 1) {
                const auto ordinal = args[0];
                if (ordinal >= args.size() - 1) {
                    fault(TrapKind::IndexOutOfBounds, "enum ordinal " + std::to_string(ordinal));
                }
                return args[static_cast<std::size_t>(ordinal) + 1];
            }
            HeapEntry text;
            text.kind = EntryKind::Text;
            text.type = image_.functions[frames_.back().function].frame_types[exit.result_slot];
            text.bytes = scalar_text(type(types[0].type), args[0]);
            return allocate(std::move(text));
        }
        }
        return 0;
    }

    const nvr::Image& image_;
    nvr::TypeSystem types_;
    RunLimits limits_;
    std::vector<std::string> arguments_;
    std::optional<std::uint32_t> start_function_;
    std::vector<std::uint64_t> start_arguments_;
    bool started_ = false;
    // Handles are monotonically assigned even as storage for dead entries
    // is reclaimed. The table only holds entries that have not been freed.
    std::unordered_map<std::uint64_t, HeapEntry> heap_;
    // Build scanning plans only when a layout or frame participates in GC.
    TracePlans layout_traces_;
    TracePlans frame_traces_;
    std::vector<HeapEntry*> mark_work_;
    std::uint64_t last_handle_ = 0;
    std::uint64_t last_identity_ = 0;
    std::uint64_t live_cells_ = 0;
    // The largest live heap size the run reached, in cells.
    std::uint64_t peak_cells_ = 0;
    std::vector<std::uint64_t> function_steps_;
    std::uint64_t next_collection_ = std::min<std::uint64_t>(256, limits_.max_heap_cells);
    std::vector<std::uint64_t> const_values_;
    std::vector<std::uint64_t> globals_;
    std::vector<std::uint8_t> global_initialized_;
    std::vector<Frame> frames_;
    std::vector<std::int64_t> wires_;
    std::optional<Event> finished_;
    std::optional<Pending> pending_;
    std::uint64_t sequence_ = 0;
    std::uint64_t steps_ = 0;
};

Execution::Execution(const nvr::Image& image, std::span<const std::string> arguments, RunLimits limits)
    : state_(std::make_unique<State>(image, arguments, limits)) {}

Execution::Execution(const nvr::Image& image, std::uint32_t function, std::span<const std::uint64_t> arguments,
                     RunLimits limits)
    : state_(std::make_unique<State>(image, function, arguments, limits)) {}

Execution::~Execution() = default;

Event Execution::advance() { return state_->advance(); }

void Execution::complete(const HostCompletion& completion) { state_->complete(completion); }

std::uint64_t Execution::steps() const { return state_->steps(); }
std::uint64_t Execution::peak_heap_cells() const { return state_->peak_heap_cells(); }
const std::vector<std::uint64_t>& Execution::function_steps() const { return state_->function_steps(); }

RunResult run(const nvr::Image& image, const Bindings& bindings, std::span<const std::string> arguments,
              RunLimits limits) {
    Execution execution(image, arguments, limits);
    RunResult result;
    for (;;) {
        const auto event = execution.advance();
        switch (event.kind) {
        case Event::Kind::HostRequest:
            ++result.host_requests;
            execution.complete(bindings.services.at(event.request.import)->invoke(event.request));
            break;
        case Event::Kind::Completed:
            result.completed = true;
            result.status = event.status;
            result.steps = execution.steps();
            result.peak_heap_cells = execution.peak_heap_cells();
            result.function_steps = execution.function_steps();
            return result;
        case Event::Kind::Trapped:
            result.trap = event.trap;
            result.steps = execution.steps();
            result.peak_heap_cells = execution.peak_heap_cells();
            result.function_steps = execution.function_steps();
            return result;
        }
    }
}

} // namespace neura::nvm
