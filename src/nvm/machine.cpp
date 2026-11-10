#include "machine.hpp"

#include "driver.hpp"
#include "graph_device.hpp"
#include "run_support.hpp"
#include "scalar_text.hpp"
#include "nvr/format.hpp"
#include "nvr/names.hpp"

#include <algorithm>
#include <array>
#include <thread>
#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
    case TrapKind::TaskLimit: return "task limit exceeded";
    case TrapKind::MessageLimit: return "queued message limit exceeded";
    case TrapKind::RequestLimit: return "pending request limit exceeded";
    case TrapKind::InvalidTaskOperation: return "invalid task operation";
    case TrapKind::Deadlock: return "deadlock";
    }
    return "trap";
}

int exit_status(const Trap& trap) {
    constexpr int illegal_instruction = 132;
    constexpr int abort = 134;
    constexpr int arithmetic_error = 136;
    constexpr int out_of_memory = 137;
    constexpr int segmentation_fault = 139;
    constexpr int cpu_time_exceeded = 152;
    switch (trap.kind) {
    case TrapKind::Program:
        switch (trap.reason) {
        case 1: return arithmetic_error;
        case 2: return segmentation_fault;
        case 3: return abort;
        case 4: return illegal_instruction;
        case 5: return abort;
        default: return illegal_instruction;
        }
    case TrapKind::InvalidCast:
    case TrapKind::Deadlock: return abort;
    case TrapKind::UninitializedSlot:
    case TrapKind::UninitializedField:
    case TrapKind::UninitializedGlobal:
    case TrapKind::InvalidHandle:
    case TrapKind::NilReference:
    case TrapKind::IndexOutOfBounds:
    case TrapKind::FrameLimit: return segmentation_fault;
    case TrapKind::GraphOverflow:
    case TrapKind::NonBinaryLane:
    case TrapKind::InvalidSelector:
    case TrapKind::ReferenceType:
    case TrapKind::InvalidText:
    case TrapKind::InvalidCompletion:
    case TrapKind::InvalidTaskOperation: return illegal_instruction;
    case TrapKind::HeapLimit:
    case TrapKind::HandleLimit:
    case TrapKind::TaskLimit:
    case TrapKind::MessageLimit:
    case TrapKind::RequestLimit: return out_of_memory;
    case TrapKind::StepLimit: return cpu_time_exceeded;
    }
    return illegal_instruction;
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
    if (trap.kind == TrapKind::Deadlock) return "deadlock: " + trap.detail;
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

void validate_utf8_arguments(std::span<const std::string> arguments) {
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (!nvr::valid_utf8(arguments[i])) {
            throw std::invalid_argument("argument " + std::to_string(i + 1) + " is not valid UTF-8");
        }
    }
}

std::optional<std::size_t> evaluate_nodes(const nvr::Graph& graph, std::span<std::int64_t> wires) {
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

std::span<std::int64_t> lanes(std::vector<std::int64_t>& storage, std::size_t count) {
    if (storage.size() < count) storage.resize(count);
    return std::span(storage).first(count);
}

std::optional<std::size_t> evaluate_loaded_outputs(const nvr::Graph& graph, std::span<std::int64_t> wires,
                                                   std::span<std::int64_t> outputs) {
    if (const auto failed = evaluate_nodes(graph, wires)) return failed;
    for (std::size_t i = 0; i < graph.outputs.size(); ++i) {
        outputs[i] = wires[graph.outputs[i]];
    }
    return std::nullopt;
}

} // namespace

std::optional<std::size_t> evaluate_graph(const nvr::Graph& graph, std::vector<std::int64_t>& wires) {
    wires.resize(std::size_t{graph.input_lanes} + graph.nodes.size());
    return evaluate_nodes(graph, wires);
}

std::optional<std::size_t> evaluate_outputs(const nvr::Graph& graph, std::span<const std::int64_t> inputs,
                                            std::vector<std::int64_t>& scratch, std::span<std::int64_t> outputs) {
    // Every graph wire is overwritten before it is read. Keep the scratch
    // at its largest size when alternating between different graphs.
    auto wires = lanes(scratch, std::size_t{graph.input_lanes} + graph.nodes.size());
    std::ranges::copy(inputs, wires.begin());
    return evaluate_loaded_outputs(graph, wires, outputs);
}

class Execution::State {
public:
    State(const nvr::Image& image, std::span<const std::string> arguments, RunLimits limits,
          std::optional<std::uint32_t> entry = {})
        : image_(image), types_(image), limits_(limits), entry_(entry) {
        check_device();
        check_task_settings();
        if (entry_) validate_entry_arguments(image, *entry_, arguments);
        else validate_utf8_arguments(arguments);
        arguments_.assign(arguments.begin(), arguments.end());
    }

    State(const nvr::Image& image, std::uint32_t function, std::span<const std::uint64_t> arguments, RunLimits limits)
        : image_(image), types_(image), limits_(limits), start_function_(function) {
        check_device();
        check_task_settings();
        validate_call_arguments(image, function, arguments);
        start_arguments_.assign(arguments.begin(), arguments.end());
    }

    ~State() { stop_tracking_heap(); }

    // A concurrent image needs a positive quantum; reject it before any run state exists.
    void check_task_settings() const {
        if (image_.transferable_layouts && limits_.task_quantum == 0) {
            throw std::invalid_argument("task quantum must be positive");
        }
    }

    void initialize(FleetHeapCells& heap) {
        if (started_) return;
        fleet_heap_ = &heap;
        try {
            start();
        } catch (const Fault& failure) {
            trap(failure);
        }
    }

    Event advance() {
        if (finished_) {
            return *finished_;
        }
        if (pending_) {
            throw std::logic_error("advance() called while a host request is pending");
        }
        // An image without tasks has no scheduling points; it answers a
        // dump request between host requests.
        serve_dump_request();
        for (;;) {
            if (auto event = step(wires_)) return std::move(*event);
        }
    }

    std::optional<Event> begin_step(GraphStep& step) {
        if (auto event = begin_active_step<false>()) return event;
        const auto& graph = image_.graphs[active_block_->graph];
        step = GraphStep{active_block_->graph, std::span(inputs_).first(graph.input_lanes), graph.outputs.size()};
        return std::nullopt;
    }

    std::optional<Event> finish_step(std::span<const std::int64_t> outputs) {
        if (!active_block_) throw std::logic_error("finish_step() called without a begun step");
        if (outputs.size() != image_.graphs[active_block_->graph].outputs.size()) {
            throw std::invalid_argument("finish_step() received the wrong number of output lanes");
        }
        std::ranges::copy(outputs, lanes(outputs_, outputs.size()).begin());
        return finish_active_step();
    }

    Event fail_step(std::size_t node) {
        if (!active_block_) throw std::logic_error("fail_step() called without a begun step");
        const auto graph = std::exchange(active_block_, nullptr)->graph;
        try {
            fault(TrapKind::GraphOverflow, "graph " + std::to_string(graph) + " node " + std::to_string(node));
        } catch (const Fault& failure) {
            return trap(failure);
        }
    }

    void complete(const HostCompletion& completion) {
        if (!pending_) {
            throw std::logic_error("complete() called without a pending host request");
        }
        if (finished_) {
            pending_.reset();
            return;
        }
        const auto pending = *pending_;
        pending_.reset();
        emit(TaskEvent::Kind::HostCompletion, scheduler_ ? scheduler_->current : 0, sequence_ - 1,
             [&] { return image_.imports[pending.import].name; });
        try {
            apply_completion(frames_, pending, completion);
        } catch (const Fault& fault) {
            Event event;
            event.kind = Event::Kind::Trapped;
            event.trap = fault.trap;
            set_finished(std::move(event));
        }
    }

    std::optional<HostValue> invoke_callback(std::string_view name, std::uint32_t version,
                                             const std::vector<HostValue>& arguments,
                                             const Execution::HostHandler& host) {
        if (finished_) return std::nullopt;
        if (!pending_) {
            throw std::logic_error("invoke_callback() called without a pending host request");
        }
        const auto saved = pending_;
        const auto restore = [&] { pending_ = saved; };
        try {
            const auto found = std::ranges::find_if(image_.callbacks, [&](const nvr::Callback& callback) {
                return callback.name == name && callback.interface_version == version;
            });
            if (found == image_.callbacks.end()) {
                fault(TrapKind::InvalidCompletion, "no callback " + std::string(name) + " version " +
                                                       std::to_string(version));
            }
            const auto& function = image_.functions[found->function];
            if (arguments.size() != function.parameter_count) {
                fault(TrapKind::InvalidCompletion, "callback " + std::string(name) + " takes " +
                                                       std::to_string(function.parameter_count) + " argument(s)");
            }
            std::vector<std::uint64_t> values;
            std::vector<std::uint64_t> temporary_roots;
            auto remaining = limits_.max_heap_cells;
            for (std::size_t i = 0; i < arguments.size(); ++i) {
                values.push_back(host_to_value(arguments[i], function.frame_types[i], temporary_roots, remaining, 0));
                temporary_roots.push_back(values.back());
            }
            pending_.reset();
            push_frame(found->function, values, nvr::absent, 0);
            frames_.back().boundary = true;
            std::vector<std::int64_t> scratch;
            for (;;) {
                auto event = step(scratch);
                if (!event) continue;
                if (event->kind == Event::Kind::Completed) {
                    auto budget = limits_.max_heap_cells;
                    std::optional<HostValue> result = HostValue{};
                    if (type(function.result_type).kind != nvr::TypeKind::Void) {
                        result = value_to_host(event->result, function.result_type, budget, 0);
                    }
                    restore();
                    return result;
                }
                if (event->kind == Event::Kind::Trapped) {
                    restore();
                    return std::nullopt;
                }
                if (event->kind == Event::Kind::HostRequest) {
                    complete(host(event->request));
                    if (finished_) {
                        restore();
                        return std::nullopt;
                    }
                }
            }
        } catch (const Fault& fault) {
            Event event;
            event.kind = Event::Kind::Trapped;
            event.trap = fault.trap;
            set_finished(std::move(event));
            restore();
            return std::nullopt;
        }
    }

    std::uint64_t steps() const { return steps_; }
    std::uint64_t live_heap_cells() const { return live_cells_; }
    std::uint64_t peak_heap_cells() const { return peak_cells_; }
    const std::vector<std::uint64_t>& function_steps() const { return function_steps_; }
    std::string dump_tasks(TaskDumpFormat format) const;
    std::uint64_t peak_tasks() const { return scheduler_ ? scheduler_->peak_live : 0; }
    std::uint64_t tick() const { return scheduler_ ? scheduler_->ticks : 0; }
    std::uint64_t peak_queued_messages() const { return scheduler_ ? scheduler_->peak_queued : 0; }
    std::uint64_t peak_pending_requests() const { return scheduler_ ? scheduler_->peak_requests : 0; }
    std::vector<TaskSiteProfile> task_profile() const {
        if (!scheduler_ || !limits_.profile) return {};
        // The root comes first. Released tasks were summed as they went;
        // add the ones still held.
        std::vector<TaskSiteProfile> sites{{nvr::absent, nvr::absent, 0, 0, 0, 0, 0}};
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> index{{{nvr::absent, nvr::absent}, 0}};
        for (const auto& released : scheduler_->sites) {
            auto& site = site_of(sites, index, Task{.spawn_function = released.function, .spawn_block = released.block});
            site.tasks += released.tasks;
            site.steps += released.steps;
            site.runnable_ticks += released.runnable_ticks;
            site.channel_ticks += released.channel_ticks;
            site.host_ticks += released.host_ticks;
        }
        for (const auto& [id, task] : scheduler_->tasks) add_to_site(site_of(sites, index, task), task);
        return sites;
    }
    std::string location(std::uint32_t function, std::uint32_t block) const {
        return function == nvr::absent ? std::string("root") : task_location(function, block);
    }

private:
    friend class Execution;

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
        // The first frame of a callback: its return ends invoke_callback.
        bool boundary = false;
    };

    enum class TaskState {
        Running, Runnable, Joining, LeavingScope, Finished,
        // Parked on a channel, a timer, several operations at once, or an
        // overlapping host request.
        Sending, Receiving, TimerWait, Selecting, HostWait,
    };

    struct TaskScope {
        std::uint64_t id = 0;
        std::size_t frame_depth = 0;
        std::size_t live_children = 0;
        std::vector<std::uint64_t> children;
    };

    // The cancellation state of an open scope, by scope identity. Scopes
    // are numbered in creation order.
    struct ScopeRecord {
        std::uint64_t owner = 0;
        // The context the owner inherited: the scope that spawned it.
        std::uint64_t parent = 0;
        bool cancelled = false;
        bool deadline_passed = false;
        std::uint64_t cancelled_tick = 0;
        std::int64_t deadline = nvr::no_deadline;
    };

    struct SelectEntry {
        nvr::SelectArm kind = nvr::SelectArm::Receive;
        // The channel or timer object, or the task of a join.
        std::uint64_t handle = 0;
        std::uint64_t value = 0;
        std::uint32_t value_type = nvr::absent;
    };

    struct Pending {
        std::uint32_t import = 0;
        std::uint32_t success_slot = nvr::absent;
        std::uint32_t success_block = 0;
        std::uint32_t error_slot = nvr::absent;
        std::uint32_t error_block = 0;
    };

    // The heap cells charged for scheduler records. A task record with its
    // place in the task table measured about 160 bytes (Concurrency.md
    // section 10.3); a channel and a timer are charged in proportion, and
    // each buffered message one cell more.
    static constexpr std::uint64_t task_record_cells = 20;
    static constexpr std::uint64_t channel_record_cells = 16;
    static constexpr std::uint64_t timer_record_cells = 8;

    struct Task {
        std::uint64_t parent = 0;
        std::size_t parent_scope = 0;
        // The scope that spawned the task: its cancellation context.
        std::uint64_t context = 0;
        // Spawned without a result slot: nothing can join it, so its record
        // is released when it finishes instead of when its scope closes.
        bool unjoinable = false;
        std::uint32_t spawn_function = nvr::absent;
        std::uint32_t spawn_block = nvr::absent;
        std::uint32_t result_type = nvr::absent;
        std::uint64_t result = 0;
        TaskState state = TaskState::Running;
        std::vector<Frame> frames;
        std::vector<TaskScope> scopes;
        // What the task waits for: the task of a join, the channel or timer
        // object, or the sequence number of a host request.
        std::uint64_t waiting_for = 0;
        // The result slot of the parked operation.
        std::uint32_t join_slot = nvr::absent;
        bool close_on_wake = false;
        // Sending: the value in flight.
        std::uint64_t wait_value = 0;
        std::uint32_t wait_value_type = nvr::absent;
        // Selecting: the arms in source order.
        std::vector<SelectEntry> select;
        // Selects this task has committed. A scan starts at this count
        // modulo the number of arms, so no ready arm starves another.
        std::uint64_t selects = 0;
        // A received value or a selected join's result, until Take.
        bool stashed = false;
        std::uint64_t stash = 0;
        std::uint32_t stash_type = nvr::absent;
        // HostWait: where the response goes.
        Pending pending;
        bool cancellable = false;
        bool cancel_sent = false;
        // For --profile: the steps the task executed, and the scheduling
        // decisions that passed while it was in its current state.
        bool started = false;
        std::uint64_t steps = 0;
        std::uint64_t since_tick = 0;
        std::uint64_t runnable_ticks = 0;
        std::uint64_t channel_ticks = 0;
        std::uint64_t host_ticks = 0;
    };

    // A parked operation in an object's queue: the task, and the arm when
    // the task is parked in a select.
    struct Waiter {
        std::uint64_t task = 0;
        std::uint32_t arm = nvr::absent;
    };

    // Scheduler state of an object the image opened as a channel, keyed by
    // the object's handle and released when the object is collected.
    struct Channel {
        // Creation order, for dumps: the handle is not stable across builds.
        std::uint64_t number = 0;
        std::uint32_t type = 0;
        std::uint64_t capacity = 0;
        bool closed = false;
        std::deque<std::uint64_t> buffer;
        std::deque<Waiter> senders;
        std::deque<Waiter> receivers;
    };

    struct Timer {
        std::uint64_t number = 0;
        std::int64_t deadline = 0;
        bool fired = false;
        bool armed = false;
        std::deque<Waiter> waiters;
    };

    // An armed deadline: a scope's, or a timer some task waits for.
    struct Deadline {
        bool scope = false;
        std::uint64_t id = 0;
    };

    struct Scheduler {
        std::map<std::uint64_t, Task> tasks;
        std::deque<std::uint64_t> runnable;
        std::uint64_t current = 0;
        std::uint64_t next_id = 1;
        std::uint64_t live = 1;
        std::uint64_t peak_live = 1;
        std::uint64_t polls = 0;
        // Scheduling decisions made so far.
        std::uint64_t ticks = 0;
        // No task is running: the next step begins by choosing one.
        bool deciding = false;
        // The driver has been offered this decision's Poll event.
        bool polled = false;
        std::uint64_t next_scope = 1;
        std::map<std::uint64_t, ScopeRecord> scopes;
        // Looked up by handle only, so their order never affects a run.
        std::unordered_map<std::uint64_t, Channel> channels;
        std::unordered_map<std::uint64_t, Timer> timers;
        std::uint64_t next_channel = 1;
        std::uint64_t next_timer = 1;
        std::uint64_t queued = 0;
        std::uint64_t peak_queued = 0;
        // Overlapping host requests by sequence number, with their tasks.
        std::map<std::uint64_t, std::uint64_t> requests;
        std::uint64_t peak_requests = 0;
        // Equal deadlines expire in the order they were armed.
        std::multimap<std::int64_t, Deadline> deadlines;
        // Requests whose cancellation the driver has yet to be told.
        std::deque<std::uint64_t> cancellations;
        // What released tasks did, by spawn site in order of first spawn.
        std::vector<TaskSiteProfile> sites;
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> site_index;
    };

    // Writes a host response into the frame that waits for it and moves that
    // frame to the continuation the response selects.
    void apply_completion(std::vector<Frame>& frames, const Pending& pending, const HostCompletion& completion) {
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
            set_finished(std::move(event));
            return;
        }
        const auto type = completion.success ? import.success_type : import.error_type;
        const auto slot = completion.success ? pending.success_slot : pending.error_slot;
        if (image_.types[type].kind != nvr::TypeKind::Void) {
            std::vector<std::uint64_t> temporary_roots;
            auto remaining = limits_.max_heap_cells;
            const auto value = host_to_value(completion.value, type, temporary_roots, remaining, 0);
            auto& frame = frames.back();
            frame.slots[slot] = value;
            frame.initialized[slot] = 1;
        }
        frames.back().block = completion.success ? pending.success_block : pending.error_block;
    }

    void deliver(std::uint64_t sequence, const HostCompletion& completion) {
        const auto found = scheduler_ ? scheduler_->requests.find(sequence) : decltype(scheduler_->requests.end()){};
        if (!scheduler_ || found == scheduler_->requests.end()) {
            throw std::logic_error("deliver() called for a host request that is not pending");
        }
        const auto id = found->second;
        scheduler_->requests.erase(found);
        if (finished_) return;
        auto& task = scheduler_->tasks.at(id);
        // A fault while the response is copied in belongs to the waiting task.
        scheduler_->current = id;
        emit(TaskEvent::Kind::HostCompletion, id, sequence,
             [&] { return image_.imports[task.pending.import].name; });
        try {
            apply_completion(task.frames, task.pending, completion);
            if (!finished_) make_runnable(id, task);
        } catch (const Fault& fault) {
            Event event;
            event.kind = Event::Kind::Trapped;
            event.trap = fault.trap;
            set_finished(std::move(event));
        }
    }

    std::uint8_t host_mode(std::uint32_t import) const {
        return limits_.host_modes && import < limits_.host_modes->size() ? (*limits_.host_modes)[import] : 0;
    }

    void check_device() const {
        if (limits_.graph_device && &limits_.graph_device->image() != &image_) {
            throw std::invalid_argument("graph device was created for another image");
        }
    }

    void stop_tracking_heap() {
        if (auto* heap = std::exchange(fleet_heap_, nullptr)) heap->subtract(live_cells_);
    }

    void set_finished(Event event) {
        finished_ = std::move(event);
        stop_tracking_heap();
    }

    Event trap(const Fault& failure) {
        Event event;
        event.kind = Event::Kind::Trapped;
        event.trap = failure.trap;
        set_finished(event);
        return event;
    }

    [[noreturn]] void fault(TrapKind kind, std::string detail = {}, std::uint32_t reason = 0) const {
        Trap trap;
        trap.kind = kind;
        trap.reason = reason;
        trap.detail = std::move(detail);
        if (scheduler_ && kind != TrapKind::Deadlock) {
            // The trapping task and its spawn chain. A long chain keeps its
            // ends: the task itself and the tasks nearest the root.
            constexpr std::size_t shown = 4;
            std::vector<std::uint64_t> chain;
            for (auto id = scheduler_->current; id != 0; id = scheduler_->tasks.at(id).parent) chain.push_back(id);
            if (!trap.detail.empty()) trap.detail += " ";
            trap.detail += "(task " + std::to_string(scheduler_->current);
            for (std::size_t i = 0; i < chain.size(); ++i) {
                if (chain.size() > 2 * shown && i == shown) {
                    trap.detail += " ... " + std::to_string(chain.size() - 2 * shown) + " more spawns ...";
                    i = chain.size() - shown;
                }
                const auto& task = scheduler_->tasks.at(chain[i]);
                trap.detail += " spawned at " + task_location(task.spawn_function, task.spawn_block) +
                               " by task " + std::to_string(task.parent);
            }
            trap.detail += ")";
            if (limits_.task_dump && limits_.task_dump->on_trap && limits_.task_dump->write) {
                limits_.task_dump->write(dump_tasks(limits_.task_dump->format));
            }
        }
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

    void add_live_cells(std::uint64_t cells) {
        live_cells_ += cells;
        if (fleet_heap_) fleet_heap_->add(cells);
        peak_cells_ = std::max(peak_cells_, live_cells_);
    }

    void remove_live_cells(std::uint64_t cells) {
        live_cells_ -= cells;
        if (fleet_heap_) fleet_heap_->subtract(cells);
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
            // A reachable channel keeps the values buffered in it.
            if (scheduler_ && !scheduler_->channels.empty() && scheduler_->channels.contains(handle)) {
                channel_work_.push_back(handle);
            }
        }
    }

    void mark_channel(std::uint64_t handle) {
        const auto& marked = scheduler_->channels.at(handle);
        if (!nvr::is_reference(type(marked.type).kind)) return;
        for (const auto value : marked.buffer) mark(value);
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

    void mark_frames(const std::vector<Frame>& frames) {
        for (const auto& frame : frames) {
            const auto& slots = image_.functions[frame.function].frame_types;
            for (const auto slot : trace_plan(frame_traces_, frame.function, slots)) {
                if (frame.initialized[slot]) mark(frame.slots[slot]);
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
        mark_frames(frames_);
        if (scheduler_) {
            const auto mark_typed = [&](std::uint64_t value, std::uint32_t value_type) {
                if (value_type != nvr::absent && nvr::is_reference(type(value_type).kind)) mark(value);
            };
            for (const auto& [id, task] : scheduler_->tasks) {
                mark_frames(task.frames);
                if (task.state == TaskState::Finished) mark_typed(task.result, task.result_type);
                // Values in flight: a parked send's, a select's, and one
                // received but not yet taken.
                if (task.state == TaskState::Sending) mark_typed(task.wait_value, task.wait_value_type);
                for (const auto& arm : task.select) {
                    if (arm.kind == nvr::SelectArm::Send) mark_typed(arm.value, arm.value_type);
                    else if (arm.kind != nvr::SelectArm::Join) mark(arm.handle);
                }
                if (task.stashed) mark_typed(task.stash, task.stash_type);
            }
        }
        for (const auto handle : temporary_roots) {
            mark(handle);
        }
        if (allocation != nullptr) {
            mark_children(*allocation);
        }
        while (!mark_work_.empty() || !channel_work_.empty()) {
            if (!channel_work_.empty()) {
                const auto handle = channel_work_.back();
                channel_work_.pop_back();
                mark_channel(handle);
                continue;
            }
            const auto& item = *mark_work_.back();
            mark_work_.pop_back();
            mark_children(item);
        }
        for (auto it = heap_.begin(); it != heap_.end();) {
            if (it->second.marked) {
                it->second.marked = false;
                ++it;
            } else {
                if (scheduler_) release_scheduler_object(it->first);
                remove_live_cells(entry_cells(it->second));
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
        add_live_cells(cells);
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
        if (image_.transferable_layouts) {
            if (limits_.max_tasks == 0) fault(TrapKind::TaskLimit, "starting the root task");
            scheduler_ = std::make_unique<Scheduler>();
            scheduler_->tasks.try_emplace(0);
            names_ = nvr::decode_names(image_);
        }
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
        const auto entry = entry_.value_or(image_.entry);
        const auto& entry_function = image_.functions[entry];
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
        push_frame(entry, std::vector<std::uint64_t>{arguments}, nvr::absent, 0);
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

    Frame make_frame(std::uint32_t function, std::span<const std::uint64_t> arguments,
                     std::uint32_t return_slot, std::uint32_t return_block) const {
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
        return frame;
    }

    std::uint64_t frame_cells(std::uint32_t function) const {
        return 1 + image_.functions[function].frame_types.size();
    }

    std::vector<std::uint64_t> argument_roots(std::uint32_t function,
                                             std::span<const std::uint64_t> arguments) const {
        std::vector<std::uint64_t> roots;
        const auto& types = image_.functions[function].frame_types;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (nvr::is_reference(type(types[i]).kind)) roots.push_back(arguments[i]);
        }
        return roots;
    }

    void push_frame(std::uint32_t function, const std::vector<std::uint64_t>& arguments,
                    std::uint32_t return_slot, std::uint32_t return_block) {
        if (frames_.size() >= limits_.max_frames) {
            fault(TrapKind::FrameLimit, "calling function " + std::to_string(function));
        }
        if (scheduler_) ensure_heap_cells(frame_cells(function), "task frame of ", argument_roots(function, arguments));
        frames_.push_back(make_frame(function, arguments, return_slot, return_block));
        if (scheduler_) add_live_cells(frame_cells(function));
    }

    Task& current_task() { return scheduler_->tasks.at(scheduler_->current); }

    std::string task_location(std::uint32_t function, std::uint32_t block) const {
        if (image_.source_map) {
            const auto& locations = *image_.source_map;
            const auto found = std::lower_bound(locations.begin(), locations.end(), std::pair{function, block},
                [](const auto& location, const auto& key) { return std::pair{location.function, location.block} < key; });
            if (found != locations.end() && found->function == function && found->block == block) {
                return found->path + ':' + std::to_string(found->line) + ':' + std::to_string(found->column);
            }
        }
        return "function " + std::to_string(function) + " block " + std::to_string(block);
    }

    static void write_result(Frame& frame, std::uint32_t slot, std::uint64_t value) {
        if (slot != nvr::absent) {
            frame.slots[slot] = value;
            frame.initialized[slot] = 1;
        }
    }

    // Scheduler events for an observer (Concurrency.md section 14.2). The
    // detail is built only when someone listens.
    template <typename Detail>
    void emit(TaskEvent::Kind kind, std::uint64_t task, std::uint64_t other, const Detail& detail,
              std::string where = {}) const {
        if (!limits_.observer) return;
        TaskEvent event;
        event.kind = kind;
        event.tick = scheduler_ ? scheduler_->ticks : 0;
        event.task = task;
        event.other = other;
        event.detail = detail();
        event.where = std::move(where);
        limits_.observer->event(event);
    }

    void emit(TaskEvent::Kind kind, std::uint64_t task, std::uint64_t other = 0) const {
        emit(kind, task, other, [] { return std::string(); });
    }

    // Every change of a task's state goes through here, so the decisions
    // that passed in the state it leaves are counted once.
    void set_state(Task& task, TaskState state) {
        const auto elapsed = scheduler_->ticks - task.since_tick;
        switch (task.state) {
        case TaskState::Runnable: task.runnable_ticks += elapsed; break;
        case TaskState::Sending:
        case TaskState::Receiving:
        case TaskState::Selecting:
        case TaskState::TimerWait: task.channel_ticks += elapsed; break;
        case TaskState::HostWait: task.host_ticks += elapsed; break;
        default: break;
        }
        task.since_tick = scheduler_->ticks;
        task.state = state;
    }

    static void add_to_site(TaskSiteProfile& site, const Task& task) {
        ++site.tasks;
        site.steps += task.steps;
        site.runnable_ticks += task.runnable_ticks;
        site.channel_ticks += task.channel_ticks;
        site.host_ticks += task.host_ticks;
    }

    TaskSiteProfile& site_of(std::vector<TaskSiteProfile>& sites,
                             std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t>& index,
                             const Task& task) const {
        const auto [found, inserted] = index.try_emplace({task.spawn_function, task.spawn_block}, sites.size());
        if (inserted) sites.push_back({task.spawn_function, task.spawn_block, 0, 0, 0, 0, 0});
        return sites[found->second];
    }

    // A task record is about to be released: keep what it did.
    void retire_task(const Task& task) {
        if (limits_.profile) add_to_site(site_of(scheduler_->sites, scheduler_->site_index, task), task);
    }

    // The frames of a task: the machine's own while the task runs.
    std::vector<Frame>& frames_of(std::uint64_t id, Task& task) {
        return task.state == TaskState::Running && id == scheduler_->current ? frames_ : task.frames;
    }

    void make_runnable(std::uint64_t id, Task& task) {
        set_state(task, TaskState::Runnable);
        scheduler_->runnable.push_back(id);
        emit(TaskEvent::Kind::Wake, id);
    }

    void disarm(std::int64_t deadline, bool scope, std::uint64_t id) {
        const auto [first, last] = scheduler_->deadlines.equal_range(deadline);
        for (auto entry = first; entry != last; ++entry) {
            if (entry->second.scope == scope && entry->second.id == id) {
                scheduler_->deadlines.erase(entry);
                return;
            }
        }
    }

    void close_scope(Task& task) {
        const auto& scope = task.scopes.back();
        for (const auto child : scope.children) {
            const auto found = scheduler_->tasks.find(child);
            if (found == scheduler_->tasks.end()) continue;
            retire_task(found->second);
            scheduler_->tasks.erase(found);
        }
        remove_live_cells(1 + task_record_cells * scope.children.size());
        if (const auto record = scheduler_->scopes.find(scope.id); record != scheduler_->scopes.end()) {
            if (record->second.deadline != nvr::no_deadline && !record->second.deadline_passed) {
                disarm(record->second.deadline, true, scope.id);
            }
            scheduler_->scopes.erase(record);
        }
        task.scopes.pop_back();
    }

    // The running task stops here. The next step begins with a scheduling
    // decision, which the driver can precede with deliveries and time.
    void park(TaskState state) {
        auto& task = current_task();
        set_state(task, state);
        task.frames = std::move(frames_);
        frames_.clear();
        if (state == TaskState::Runnable) scheduler_->runnable.push_back(scheduler_->current);
        scheduler_->deciding = true;
        emit(TaskEvent::Kind::Park, scheduler_->current, 0, [&] {
            return state == TaskState::Runnable ? std::string("quantum spent")
                                                : wait_text(scheduler_->current, task).substr(8);
        });
    }

    // A requested task dump is written at a scheduling point and leaves the
    // run as it was.
    void serve_dump_request() const {
        auto* request = limits_.task_dump;
        if (!request || !request->requested.load(std::memory_order_relaxed)) return;
        request->requested.store(false, std::memory_order_relaxed);
        if (request->write) request->write(dump_tasks(request->format));
    }

    void poll() {
        serve_dump_request();
        if (++scheduler_->polls >= limits_.task_quantum) {
            if (scheduler_->runnable.empty()) scheduler_->polls = 0;
            else park(TaskState::Runnable);
        }
    }

    // What the root waits for, followed through joins and scope exits to
    // the first task that waits for something other than a task.
    std::string wait_chain() const {
        std::string text = "wait chain:";
        std::unordered_set<std::uint64_t> seen;
        for (std::uint64_t id = 0; seen.insert(id).second;) {
            const auto found = scheduler_->tasks.find(id);
            if (found == scheduler_->tasks.end()) break;
            const auto& task = found->second;
            text += (id == 0 ? " task " : " -> task ") + std::to_string(id) + " (" + wait_text(id, task) + ")";
            if (task.state == TaskState::Joining) {
                id = task.waiting_for;
            } else if (task.state == TaskState::LeavingScope && !task.scopes.empty()) {
                // The lowest-numbered task the scope still waits for.
                std::uint64_t next = 0;
                for (const auto& [child_id, child] : scheduler_->tasks) {
                    if (child_id != 0 && child.parent == id && child.parent_scope + 1 == task.scopes.size() &&
                        child.state != TaskState::Finished) {
                        next = child_id;
                        break;
                    }
                }
                if (next == 0) break;
                id = next;
            } else {
                break;
            }
        }
        return text + '\n';
    }

    // Chooses the task that runs next, or reports why none can yet.
    std::optional<Event> decide() {
        auto& scheduler = *scheduler_;
        serve_dump_request();
        if (!scheduler.cancellations.empty()) {
            Event event;
            event.kind = Event::Kind::Cancel;
            event.sequence = scheduler.cancellations.front();
            event.tick = scheduler.ticks;
            scheduler.cancellations.pop_front();
            return event;
        }
        const bool timers = !scheduler.deadlines.empty();
        const bool external = timers || !scheduler.requests.empty();
        const auto external_event = [&](Event::Kind kind) {
            Event event;
            event.kind = kind;
            event.timer = timers;
            event.deadline = timers ? scheduler.deadlines.begin()->first : nvr::no_deadline;
            event.tick = scheduler.ticks;
            return event;
        };
        if (scheduler.runnable.empty()) {
            // A pending request or an armed timer can still wake a task: the
            // run is waiting, not deadlocked.
            if (external) return external_event(Event::Kind::Waiting);
            fault(TrapKind::Deadlock, "all tasks are parked\n" + wait_chain() + dump_tasks(TaskDumpFormat::Text));
        }
        if (external && !scheduler.polled) {
            scheduler.polled = true;
            return external_event(Event::Kind::Poll);
        }
        scheduler.polled = false;
        // The reference scheduler takes the first runnable task. A chooser
        // may take any: every choice is a legal schedule.
        std::size_t position = 0;
        if (limits_.chooser) {
            position = limits_.chooser->choose(scheduler.ticks, scheduler.runnable.size());
            if (position >= scheduler.runnable.size()) position = 0;
        }
        scheduler.current = scheduler.runnable[position];
        scheduler.runnable.erase(scheduler.runnable.begin() + static_cast<std::ptrdiff_t>(position));
        scheduler.polls = 0;
        auto& task = current_task();
        set_state(task, TaskState::Running);
        ++scheduler.ticks;
        if (limits_.schedule) limits_.schedule->push_back(scheduler.current);
        if (!task.started) {
            task.started = true;
            emit(TaskEvent::Kind::Start, scheduler.current);
        }
        frames_ = std::move(task.frames);
        if (task.close_on_wake) {
            task.close_on_wake = false;
            close_scope(task);
        }
        scheduler.deciding = false;
        return std::nullopt;
    }

    // Every object reachable from a value given to another task must have a
    // marked layout (docs/runtime/NVR.md, "Scheduler operations").
    void check_transferred(std::vector<std::pair<std::uint64_t, std::uint32_t>> references) {
        std::unordered_set<std::uint64_t> visited;
        while (!references.empty()) {
            const auto [handle, expected] = references.back();
            references.pop_back();
            if (handle == 0 && type(expected).kind == nvr::TypeKind::Object) continue;
            const auto& value = typed_entry(handle, expected);
            if (value.kind != EntryKind::Object || !visited.insert(handle).second) continue;
            const auto& layouts = *image_.transferable_layouts;
            if (!std::ranges::binary_search(layouts, value.type)) {
                fault(TrapKind::InvalidTaskOperation, "transferred handle has an unmarked layout");
            }
            const auto& fields = image_.layouts[value.type].fields;
            for (std::size_t field = 0; field < fields.size(); ++field) {
                // An unset field is not transferred data; reading it later traps as usual.
                if (value.initialized[field] && nvr::is_reference(type(fields[field]).kind)) {
                    references.emplace_back(value.cells[field], fields[field]);
                }
            }
        }
    }

    void check_transfer(std::uint32_t function, std::span<const std::uint64_t> arguments) {
        const auto& types = image_.functions[function].frame_types;
        std::vector<std::pair<std::uint64_t, std::uint32_t>> references;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (nvr::is_reference(type(types[i]).kind)) references.emplace_back(arguments[i], types[i]);
        }
        check_transferred(std::move(references));
    }

    void check_sent(std::uint64_t value, std::uint32_t value_type) {
        if (nvr::is_reference(type(value_type).kind)) check_transferred({{value, value_type}});
    }

    Task& child_task(std::uint64_t id) {
        const auto found = scheduler_->tasks.find(id);
        if (id == 0 || found == scheduler_->tasks.end() || found->second.parent != scheduler_->current) {
            fault(TrapKind::InvalidTaskOperation, "task handle is outside its owning scope");
        }
        return found->second;
    }

    Channel& channel(std::uint64_t object) {
        const auto found = scheduler_->channels.find(object);
        if (found == scheduler_->channels.end()) {
            fault(TrapKind::InvalidTaskOperation, "the object is not an open channel");
        }
        return found->second;
    }

    Timer& timer(std::uint64_t object) {
        const auto found = scheduler_->timers.find(object);
        if (found == scheduler_->timers.end()) {
            fault(TrapKind::InvalidTaskOperation, "the object is not an open timer");
        }
        return found->second;
    }

    // A scope handle is used by the task that executes the scope.
    ScopeRecord& owned_scope(std::uint64_t id) {
        const auto found = scheduler_->scopes.find(id);
        if (found == scheduler_->scopes.end() || found->second.owner != scheduler_->current) {
            fault(TrapKind::InvalidTaskOperation, "scope handle is outside its scope");
        }
        return found->second;
    }

    // A context is the scope that spawned a task, joined with everything
    // that scope's own task inherited.
    bool cancelled(std::uint64_t context) const {
        for (auto id = context; id != 0;) {
            const auto& scope = scheduler_->scopes.at(id);
            if (scope.cancelled) return true;
            id = scope.parent;
        }
        return false;
    }

    std::int64_t deadline_of(std::uint64_t context) const {
        auto earliest = nvr::no_deadline;
        for (auto id = context; id != 0;) {
            const auto& scope = scheduler_->scopes.at(id);
            earliest = std::min(earliest, scope.deadline);
            id = scope.parent;
        }
        return earliest;
    }

    bool within(std::uint64_t context, std::uint64_t scope) const {
        for (auto id = context; id != 0; id = scheduler_->scopes.at(id).parent) {
            if (id == scope) return true;
        }
        return false;
    }

    static void unregister(std::deque<Waiter>& queue, std::uint64_t task) {
        std::erase_if(queue, [task](const Waiter& waiter) { return waiter.task == task; });
    }

    void stash(Task& task, std::uint64_t value, std::uint32_t value_type) {
        task.stashed = true;
        task.stash = value;
        task.stash_type = value_type;
    }

    // Commits one arm of the select `task` is evaluating or parked in.
    // Exactly one arm commits; the task's place in every other arm's queue
    // is given up first.
    void commit_select(std::uint64_t id, Task& task, std::uint32_t arm, nvr::SchedulerOutcome outcome) {
        const bool parked = task.state == TaskState::Selecting;
        if (parked) {
            for (const auto& entry : task.select) {
                if (entry.kind == nvr::SelectArm::Receive) {
                    if (const auto found = scheduler_->channels.find(entry.handle); found != scheduler_->channels.end()) {
                        unregister(found->second.receivers, id);
                    }
                } else if (entry.kind == nvr::SelectArm::Send) {
                    if (const auto found = scheduler_->channels.find(entry.handle); found != scheduler_->channels.end()) {
                        unregister(found->second.senders, id);
                    }
                } else if (entry.kind == nvr::SelectArm::Timer) {
                    if (const auto found = scheduler_->timers.find(entry.handle); found != scheduler_->timers.end()) {
                        unregister(found->second.waiters, id);
                    }
                }
            }
        }
        write_result(frames_of(id, task).back(), task.join_slot,
                     std::uint64_t{arm} | (static_cast<std::uint64_t>(outcome) << 32));
        ++task.selects;
        task.select.clear();
        if (parked) make_runnable(id, task);
    }

    // Ends a parked operation with its uint32 outcome.
    void complete_parked(std::uint64_t id, Task& task, nvr::SchedulerOutcome outcome) {
        write_result(task.frames.back(), task.join_slot, static_cast<std::uint64_t>(outcome));
        task.wait_value_type = nvr::absent;
        make_runnable(id, task);
    }

    void complete_waiter(const Waiter& waiter, nvr::SchedulerOutcome outcome) {
        auto& task = scheduler_->tasks.at(waiter.task);
        if (waiter.arm == nvr::absent) complete_parked(waiter.task, task, outcome);
        else commit_select(waiter.task, task, waiter.arm, outcome);
    }

    std::uint64_t offered_value(const Waiter& waiter) const {
        const auto& task = scheduler_->tasks.at(waiter.task);
        return waiter.arm == nvr::absent ? task.wait_value : task.select[waiter.arm].value;
    }

    enum class Attempt { Done, Closed, Blocked };

    // Sends without parking. A parked receiver takes the value directly;
    // otherwise it is buffered if there is room.
    Attempt try_send(Channel& target, std::uint64_t value) {
        if (target.closed) return Attempt::Closed;
        if (!target.receivers.empty()) {
            const auto waiter = target.receivers.front();
            target.receivers.pop_front();
            stash(scheduler_->tasks.at(waiter.task), value, target.type);
            emit(TaskEvent::Kind::Send, scheduler_->current, target.number);
            emit(TaskEvent::Kind::Receive, waiter.task, target.number);
            complete_waiter(waiter, nvr::SchedulerOutcome::Done);
            return Attempt::Done;
        }
        if (target.buffer.size() < target.capacity) {
            if (scheduler_->queued >= limits_.max_queued_messages) {
                fault(TrapKind::MessageLimit, "buffering another message");
            }
            std::vector<std::uint64_t> roots;
            if (nvr::is_reference(type(target.type).kind)) roots.push_back(value);
            ensure_heap_cells(1, "channel message of ", roots);
            target.buffer.push_back(value);
            add_live_cells(1);
            scheduler_->peak_queued = std::max(scheduler_->peak_queued, ++scheduler_->queued);
            emit(TaskEvent::Kind::Send, scheduler_->current, target.number);
            return Attempt::Done;
        }
        return Attempt::Blocked;
    }

    // Receives without parking; on Done the value is stashed for `receiver`.
    // Buffered values come first, in order, and the longest-parked sender's
    // value takes the place a receive frees.
    Attempt try_receive(Channel& source, Task& receiver) {
        if (!source.buffer.empty()) {
            const auto value = source.buffer.front();
            source.buffer.pop_front();
            if (!source.senders.empty()) {
                const auto waiter = source.senders.front();
                source.senders.pop_front();
                source.buffer.push_back(offered_value(waiter));
                emit(TaskEvent::Kind::Send, waiter.task, source.number);
                complete_waiter(waiter, nvr::SchedulerOutcome::Done);
            } else {
                --scheduler_->queued;
                remove_live_cells(1);
            }
            stash(receiver, value, source.type);
            emit(TaskEvent::Kind::Receive, scheduler_->current, source.number);
            return Attempt::Done;
        }
        if (!source.senders.empty()) {
            // A rendezvous: the sender has been waiting for this receive.
            const auto waiter = source.senders.front();
            source.senders.pop_front();
            const auto value = offered_value(waiter);
            emit(TaskEvent::Kind::Send, waiter.task, source.number);
            complete_waiter(waiter, nvr::SchedulerOutcome::Done);
            stash(receiver, value, source.type);
            emit(TaskEvent::Kind::Receive, scheduler_->current, source.number);
            return Attempt::Done;
        }
        return source.closed ? Attempt::Closed : Attempt::Blocked;
    }

    void close_channel(Channel& target) {
        if (target.closed) return;
        target.closed = true;
        emit(TaskEvent::Kind::Close, scheduler_->current, target.number);
        // Parked senders learn the channel closed; their values are not
        // delivered. Receivers are parked only when nothing is buffered.
        while (!target.senders.empty()) {
            const auto waiter = target.senders.front();
            target.senders.pop_front();
            complete_waiter(waiter, nvr::SchedulerOutcome::Closed);
        }
        while (!target.receivers.empty()) {
            const auto waiter = target.receivers.front();
            target.receivers.pop_front();
            complete_waiter(waiter, nvr::SchedulerOutcome::Closed);
        }
    }

    void arm_timer(std::uint64_t object, Timer& target) {
        if (target.armed || target.fired) return;
        target.armed = true;
        scheduler_->deadlines.emplace(target.deadline, Deadline{false, object});
    }

    // Ends the wait of a parked task whose context was cancelled, where the
    // operation can be cancelled. A join and a scope exit never are.
    void cancel_wait(std::uint64_t id, Task& task) {
        switch (task.state) {
        case TaskState::Sending:
            unregister(channel(task.waiting_for).senders, id);
            complete_parked(id, task, nvr::SchedulerOutcome::Cancelled);
            break;
        case TaskState::Receiving:
            unregister(channel(task.waiting_for).receivers, id);
            complete_parked(id, task, nvr::SchedulerOutcome::Cancelled);
            break;
        case TaskState::TimerWait:
            unregister(timer(task.waiting_for).waiters, id);
            complete_parked(id, task, nvr::SchedulerOutcome::Cancelled);
            break;
        case TaskState::Selecting: {
            // No arm was ready, or the task would not be parked. Each
            // channel or timer arm can report the cancellation; the
            // rotation chooses which.
            const auto count = task.select.size();
            for (std::size_t offset = 0; offset < count; ++offset) {
                const auto arm = (task.selects + offset) % count;
                if (task.select[arm].kind != nvr::SelectArm::Join) {
                    commit_select(id, task, static_cast<std::uint32_t>(arm), nvr::SchedulerOutcome::Cancelled);
                    break;
                }
            }
            break;
        }
        case TaskState::HostWait:
            if (task.cancellable && !task.cancel_sent) {
                task.cancel_sent = true;
                scheduler_->cancellations.push_back(task.waiting_for);
            }
            break;
        default:
            break;
        }
    }

    // Requests cancellation of a scope's tasks and all their descendants.
    // Idempotent. A running task is not interrupted; it asks.
    void cancel_scope(std::uint64_t id) {
        auto& scope = scheduler_->scopes.at(id);
        if (scope.cancelled) return;
        scope.cancelled = true;
        scope.cancelled_tick = scheduler_->ticks;
        emit(TaskEvent::Kind::Cancel, scope.owner, id, [&] {
            return std::string(scope.deadline_passed ? "deadline passed" : "requested");
        });
        for (auto& [task_id, task] : scheduler_->tasks) {
            if (task_id != 0 && task.state != TaskState::Finished && within(task.context, id)) {
                cancel_wait(task_id, task);
            }
        }
    }

    // Reads the arms of a select from its exit: a kind, then its operands.
    std::vector<SelectEntry> select_arms(const nvr::Exit& exit, const std::vector<std::uint64_t>& arguments) {
        std::vector<SelectEntry> arms;
        for (std::size_t i = 0; i < arguments.size();) {
            SelectEntry arm;
            arm.kind = static_cast<nvr::SelectArm>(arguments[i]);
            arm.handle = arguments[i + 1];
            if (arm.kind == nvr::SelectArm::Send) {
                arm.value = arguments[i + 2];
                arm.value_type = exit.arguments[i + 2].type;
                i += 3;
            } else {
                i += 2;
            }
            arms.push_back(arm);
        }
        return arms;
    }

    void check_scheduler_exit(const nvr::Exit& exit, const std::vector<std::uint64_t>& arguments,
                              const std::vector<std::pair<std::uint32_t, std::uint64_t>>& assignments) {
        using Op = nvr::SchedulerOp;
        const auto& task = current_task();
        if ((exit.scheduler == Op::Spawn || exit.scheduler == Op::ScopeExit) &&
            (task.scopes.empty() || task.scopes.back().frame_depth != frames_.size())) {
            fault(TrapKind::InvalidTaskOperation, "no enclosing scope in the current function");
        }
        if (exit.scheduler == Op::Join || exit.scheduler == Op::IsFinished) {
            const auto& child = child_task(arguments.front());
            if (exit.scheduler == Op::Join) {
                const auto& function = image_.functions[frames_.back().function];
                const bool returns_void = type(child.result_type).kind == nvr::TypeKind::Void;
                if ((exit.result_slot == nvr::absent) != returns_void ||
                    (!returns_void && !types_.assignable(child.result_type, function.frame_types[exit.result_slot]))) {
                    fault(TrapKind::InvalidTaskOperation, "join result does not match the task");
                }
            }
        }
        // Operations that take heap cells check the limit, and so may
        // collect, before anything changes. Values this exit assigns are
        // roots during that collection.
        std::uint64_t cells = 0;
        std::vector<std::uint64_t> roots;
        switch (exit.scheduler) {
        case Op::ScopeEnter:
            cells = 1;
            break;
        case Op::Spawn:
            if (scheduler_->live >= limits_.max_tasks || scheduler_->next_id == std::numeric_limits<std::uint64_t>::max()) {
                fault(TrapKind::TaskLimit, "spawning another task");
            }
            if (limits_.max_frames == 0) fault(TrapKind::FrameLimit, "starting a task frame");
            check_transfer(exit.target, arguments);
            roots = argument_roots(exit.target, arguments);
            cells = task_record_cells + frame_cells(exit.target);
            break;
        case Op::ChannelOpen:
            if (entry(arguments[0]).kind != EntryKind::Object || scheduler_->channels.contains(arguments[0]) ||
                scheduler_->timers.contains(arguments[0])) {
                fault(TrapKind::InvalidTaskOperation, "the object cannot be opened as a channel");
            }
            if (static_cast<std::int64_t>(arguments[1]) < 0) {
                fault(TrapKind::InvalidTaskOperation, "a channel capacity is not negative");
            }
            roots.push_back(arguments[0]);
            cells = channel_record_cells;
            break;
        case Op::TimerOpen:
            if (entry(arguments[0]).kind != EntryKind::Object || scheduler_->channels.contains(arguments[0]) ||
                scheduler_->timers.contains(arguments[0])) {
                fault(TrapKind::InvalidTaskOperation, "the object cannot be opened as a timer");
            }
            roots.push_back(arguments[0]);
            cells = timer_record_cells;
            break;
        default:
            break;
        }
        if (cells != 0) {
            const auto& types = image_.functions[frames_.back().function].frame_types;
            for (const auto& [slot, value] : assignments) {
                if (nvr::is_reference(type(types[slot]).kind)) roots.push_back(value);
            }
            ensure_heap_cells(cells, "task allocation of ", roots);
        }
    }

    void scheduler_exit(const nvr::Exit& exit, const std::vector<std::uint64_t>& arguments) {
        using Op = nvr::SchedulerOp;
        using Outcome = nvr::SchedulerOutcome;
        const auto self = scheduler_->current;
        auto& task = current_task();
        auto& frame = frames_.back();
        const auto block = frame.block;
        frame.block = exit.resume;
        const auto outcome = [&](Outcome value) {
            write_result(frame, exit.result_slot, static_cast<std::uint64_t>(value));
        };
        // Parks the task on an object's queue, unless its context is
        // already cancelled: then the operation reports that instead.
        const auto wait_on = [&](TaskState state, std::deque<Waiter>& queue) {
            if (cancelled(task.context)) {
                outcome(Outcome::Cancelled);
                return;
            }
            task.waiting_for = arguments.front();
            task.join_slot = exit.result_slot;
            queue.push_back({self, nvr::absent});
            park(state);
        };
        switch (exit.scheduler) {
        case Op::ScopeEnter: {
            const auto id = scheduler_->next_scope++;
            scheduler_->scopes.emplace(id, ScopeRecord{self, task.context});
            task.scopes.push_back({id, frames_.size(), 0, {}});
            add_live_cells(1);
            write_result(frame, exit.result_slot, id);
            break;
        }
        case Op::ScopeExit:
            if (task.scopes.back().live_children == 0) close_scope(task);
            else park(TaskState::LeavingScope);
            break;
        case Op::Spawn: {
            const auto id = scheduler_->next_id++;
            auto& child = scheduler_->tasks.try_emplace(id).first->second;
            child.parent = self;
            child.parent_scope = task.scopes.size() - 1;
            child.context = task.scopes.back().id;
            child.spawn_function = frame.function;
            child.spawn_block = block;
            child.result_type = image_.functions[exit.target].result_type;
            child.frames.push_back(make_frame(exit.target, arguments, nvr::absent, 0));
            child.state = TaskState::Runnable;
            child.since_tick = scheduler_->ticks;
            child.unjoinable = exit.result_slot == nvr::absent;
            ++task.scopes.back().live_children;
            if (!child.unjoinable) task.scopes.back().children.push_back(id);
            scheduler_->peak_live = std::max(scheduler_->peak_live, ++scheduler_->live);
            scheduler_->runnable.push_back(id);
            add_live_cells(task_record_cells + frame_cells(exit.target));
            write_result(frame, exit.result_slot, id);
            emit(TaskEvent::Kind::Spawn, self, id, [] { return std::string(); },
                 limits_.observer ? task_location(frame.function, block) : std::string());
            break;
        }
        case Op::Join: {
            const auto& child = child_task(arguments.front());
            if (child.state == TaskState::Finished) {
                write_result(frame, exit.result_slot, child.result);
            } else {
                task.waiting_for = arguments.front();
                task.join_slot = exit.result_slot;
                park(TaskState::Joining);
            }
            break;
        }
        case Op::IsFinished:
            write_result(frame, exit.result_slot, child_task(arguments.front()).state == TaskState::Finished);
            break;
        case Op::ScopeCancel:
            owned_scope(arguments[0]);
            cancel_scope(arguments[0]);
            break;
        case Op::ScopeIsCancelled:
            write_result(frame, exit.result_slot, owned_scope(arguments[0]).cancelled);
            break;
        case Op::ScopeSetDeadline: {
            auto& scope = owned_scope(arguments[0]);
            const auto deadline = static_cast<std::int64_t>(arguments[1]);
            // A later call replaces the scope's own deadline. What its tasks
            // observe is still the earlier of it and the inherited one.
            if (scope.deadline != nvr::no_deadline && !scope.deadline_passed) disarm(scope.deadline, true, arguments[0]);
            scope.deadline = deadline;
            if (deadline != nvr::no_deadline && !scope.cancelled) {
                scope.deadline_passed = false;
                scheduler_->deadlines.emplace(deadline, Deadline{true, arguments[0]});
            }
            break;
        }
        case Op::ScopeDeadline: {
            const auto& scope = owned_scope(arguments[0]);
            const auto earliest = std::min(scope.deadline, deadline_of(scope.parent));
            write_result(frame, exit.result_slot, static_cast<std::uint64_t>(earliest));
            break;
        }
        case Op::TaskCancelled:
            write_result(frame, exit.result_slot, cancelled(task.context));
            break;
        case Op::TaskDeadline:
            write_result(frame, exit.result_slot, static_cast<std::uint64_t>(deadline_of(task.context)));
            break;
        case Op::Take: {
            const auto& types = image_.functions[frame.function].frame_types;
            if (!task.stashed || !types_.assignable(task.stash_type, types[exit.result_slot])) {
                fault(TrapKind::InvalidTaskOperation, "no received value of the slot's type to take");
            }
            write_result(frame, exit.result_slot, task.stash);
            task.stashed = false;
            break;
        }
        case Op::ChannelOpen: {
            auto& opened = scheduler_->channels[arguments[0]];
            opened.number = scheduler_->next_channel++;
            opened.type = exit.target;
            opened.capacity = arguments[1];
            add_live_cells(channel_record_cells);
            break;
        }
        case Op::ChannelSend:
        case Op::ChannelTrySend: {
            auto& target = channel(arguments[0]);
            if (!types_.assignable(exit.arguments[1].type, target.type)) {
                fault(TrapKind::InvalidTaskOperation, "the value does not have the channel's element type");
            }
            check_sent(arguments[1], exit.arguments[1].type);
            switch (try_send(target, arguments[1])) {
            case Attempt::Done: outcome(Outcome::Done); break;
            case Attempt::Closed: outcome(Outcome::Closed); break;
            case Attempt::Blocked:
                if (exit.scheduler == Op::ChannelTrySend) {
                    outcome(Outcome::Full);
                } else {
                    task.wait_value = arguments[1];
                    task.wait_value_type = exit.arguments[1].type;
                    wait_on(TaskState::Sending, target.senders);
                }
                break;
            }
            break;
        }
        case Op::ChannelReceive:
        case Op::ChannelTryReceive: {
            auto& source = channel(arguments[0]);
            switch (try_receive(source, task)) {
            case Attempt::Done: outcome(Outcome::Done); break;
            case Attempt::Closed: outcome(Outcome::Closed); break;
            case Attempt::Blocked:
                if (exit.scheduler == Op::ChannelTryReceive) outcome(Outcome::Empty);
                else wait_on(TaskState::Receiving, source.receivers);
                break;
            }
            break;
        }
        case Op::ChannelClose:
            close_channel(channel(arguments[0]));
            break;
        case Op::TimerOpen: {
            auto& opened = scheduler_->timers[arguments[0]];
            opened.number = scheduler_->next_timer++;
            opened.deadline = static_cast<std::int64_t>(arguments[1]);
            add_live_cells(timer_record_cells);
            break;
        }
        case Op::TimerWait: {
            auto& target = timer(arguments[0]);
            if (target.fired) {
                outcome(Outcome::Done);
            } else {
                wait_on(TaskState::TimerWait, target.waiters);
                if (task.state == TaskState::TimerWait) arm_timer(arguments[0], target);
            }
            break;
        }
        case Op::Select: {
            auto arms = select_arms(exit, arguments);
            const auto count = arms.size();
            task.join_slot = exit.result_slot;
            // Validate every arm before any of them can commit.
            for (const auto& arm : arms) {
                switch (arm.kind) {
                case nvr::SelectArm::Receive:
                    channel(arm.handle);
                    break;
                case nvr::SelectArm::Send:
                    if (!types_.assignable(arm.value_type, channel(arm.handle).type)) {
                        fault(TrapKind::InvalidTaskOperation, "the value does not have the channel's element type");
                    }
                    check_sent(arm.value, arm.value_type);
                    break;
                case nvr::SelectArm::Join:
                    child_task(arm.handle);
                    break;
                case nvr::SelectArm::Timer:
                    timer(arm.handle);
                    break;
                }
            }
            // The first ready arm in rotation order commits.
            std::optional<std::pair<std::size_t, Outcome>> committed;
            for (std::size_t offset = 0; offset < count && !committed; ++offset) {
                const auto index = (task.selects + offset) % count;
                const auto& arm = arms[index];
                Attempt attempt = Attempt::Blocked;
                switch (arm.kind) {
                case nvr::SelectArm::Receive:
                    attempt = try_receive(channel(arm.handle), task);
                    break;
                case nvr::SelectArm::Send:
                    attempt = try_send(channel(arm.handle), arm.value);
                    break;
                case nvr::SelectArm::Join: {
                    const auto& child = child_task(arm.handle);
                    if (child.state == TaskState::Finished) {
                        if (type(child.result_type).kind != nvr::TypeKind::Void) stash(task, child.result, child.result_type);
                        attempt = Attempt::Done;
                    }
                    break;
                }
                case nvr::SelectArm::Timer:
                    if (timer(arm.handle).fired) attempt = Attempt::Done;
                    break;
                }
                if (attempt != Attempt::Blocked) {
                    committed = {index, attempt == Attempt::Done ? Outcome::Done : Outcome::Closed};
                }
            }
            if (!committed && (exit.target & 1U) != 0) {
                // Nothing was ready and the select has a default arm, whose
                // index follows the listed ones.
                committed = {count, Outcome::Done};
            }
            if (!committed && cancelled(task.context)) {
                for (std::size_t offset = 0; offset < count && !committed; ++offset) {
                    const auto index = (task.selects + offset) % count;
                    if (arms[index].kind != nvr::SelectArm::Join) committed = {index, Outcome::Cancelled};
                }
            }
            if (committed) {
                commit_select(self, task, static_cast<std::uint32_t>(committed->first), committed->second);
                break;
            }
            // Park on every arm. The first operation another task or the
            // runtime completes for this task is the one that commits.
            for (std::size_t index = 0; index < count; ++index) {
                const auto& arm = arms[index];
                const Waiter waiter{self, static_cast<std::uint32_t>(index)};
                switch (arm.kind) {
                case nvr::SelectArm::Receive: channel(arm.handle).receivers.push_back(waiter); break;
                case nvr::SelectArm::Send: channel(arm.handle).senders.push_back(waiter); break;
                case nvr::SelectArm::Timer: {
                    auto& target = timer(arm.handle);
                    target.waiters.push_back(waiter);
                    arm_timer(arm.handle, target);
                    break;
                }
                case nvr::SelectArm::Join: break;
                }
            }
            task.select = std::move(arms);
            park(TaskState::Selecting);
            break;
        }
        }
    }

    void finish_task(std::uint64_t result) {
        const auto id = scheduler_->current;
        auto& child = current_task();
        child.result = result;
        set_state(child, TaskState::Finished);
        emit(TaskEvent::Kind::Finish, id);
        // A finished task keeps only what a join needs.
        std::vector<Frame>().swap(child.frames);
        std::vector<TaskScope>().swap(child.scopes);
        child.stashed = false;
        --scheduler_->live;
        const auto result_type = child.result_type;
        const auto parent_id = child.parent;
        auto& parent = scheduler_->tasks.at(parent_id);
        auto& scope = parent.scopes[child.parent_scope];
        --scope.live_children;
        const bool last_of_scope = child.parent_scope + 1 == parent.scopes.size() && scope.live_children == 0;
        if (child.unjoinable) {
            retire_task(child);
            scheduler_->tasks.erase(id);
            remove_live_cells(task_record_cells);
        }
        if (parent.state == TaskState::Joining && parent.waiting_for == id) {
            write_result(parent.frames.back(), parent.join_slot, result);
            make_runnable(parent_id, parent);
        } else if (parent.state == TaskState::Selecting) {
            for (std::size_t arm = 0; arm < parent.select.size(); ++arm) {
                if (parent.select[arm].kind == nvr::SelectArm::Join && parent.select[arm].handle == id) {
                    if (type(result_type).kind != nvr::TypeKind::Void) stash(parent, result, result_type);
                    commit_select(parent_id, parent, static_cast<std::uint32_t>(arm), nvr::SchedulerOutcome::Done);
                    break;
                }
            }
        } else if (parent.state == TaskState::LeavingScope && last_of_scope) {
            parent.close_on_wake = true;
            make_runnable(parent_id, parent);
        }
        scheduler_->deciding = true;
    }

    // Expires every timer and scope deadline at or before `now`.
    std::size_t advance_clock(std::int64_t now) {
        if (!scheduler_ || finished_) return 0;
        std::size_t expired = 0;
        try {
            auto& deadlines = scheduler_->deadlines;
            while (!deadlines.empty() && deadlines.begin()->first <= now) {
                const auto entry = deadlines.begin()->second;
                deadlines.erase(deadlines.begin());
                ++expired;
                if (entry.scope) {
                    if (const auto found = scheduler_->scopes.find(entry.id); found != scheduler_->scopes.end()) {
                        found->second.deadline_passed = true;
                        emit(TaskEvent::Kind::DeadlineExpired, found->second.owner, entry.id,
                             [] { return std::string("scope"); });
                        cancel_scope(entry.id);
                    }
                } else if (const auto found = scheduler_->timers.find(entry.id); found != scheduler_->timers.end()) {
                    auto& fired = found->second;
                    fired.fired = true;
                    fired.armed = false;
                    emit(TaskEvent::Kind::DeadlineExpired, 0, fired.number, [] { return std::string("timer"); });
                    while (!fired.waiters.empty()) {
                        const auto waiter = fired.waiters.front();
                        fired.waiters.pop_front();
                        complete_waiter(waiter, nvr::SchedulerOutcome::Done);
                    }
                }
            }
        } catch (const Fault& failure) {
            trap(failure);
        }
        return expired;
    }

    // Scheduler records of a collected or freed object go with it.
    void release_scheduler_object(std::uint64_t handle) {
        if (const auto found = scheduler_->channels.find(handle); found != scheduler_->channels.end()) {
            scheduler_->queued -= found->second.buffer.size();
            remove_live_cells(channel_record_cells + found->second.buffer.size());
            scheduler_->channels.erase(found);
        } else if (const auto timed = scheduler_->timers.find(handle); timed != scheduler_->timers.end()) {
            if (timed->second.armed) disarm(timed->second.deadline, false, handle);
            remove_live_cells(timer_record_cells);
            scheduler_->timers.erase(timed);
        }
    }

    void require_initialized(const Frame& frame, std::uint32_t slot) const {
        if (!frame.initialized[slot]) fault(TrapKind::UninitializedSlot, "slot " + std::to_string(slot));
    }

    std::uint64_t read(const nvr::Value& value, const Frame& frame) {
        switch (value.kind) {
        case nvr::ValueKind::Slot:
            require_initialized(frame, value.index);
            return frame.slots[value.index];
        case nvr::ValueKind::Constant:
            return const_values_[value.index];
        case nvr::ValueKind::Lanes: {
            const auto lanes = nvr::lane_count(type(value.type));
            std::uint64_t bits = 0;
            for (unsigned i = 0; i < lanes; ++i) {
                const auto lane = outputs_[value.index + i];
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

    std::uint32_t select(const nvr::Block& block) {
        if (block.exits.size() == 1) {
            return 0;
        }
        const auto lanes = nvr::selector_lanes(block.exits.size());
        std::uint32_t index = 0;
        for (unsigned i = 0; i < lanes; ++i) {
            const auto lane = outputs_[block.selector_start + i];
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

    template <bool Prepared>
    std::optional<Event> begin_active_step() {
        if (finished_) return *finished_;
        if constexpr (Prepared) {
            // One branch guards the fleet's path. A caller that advanced again
            // after a device threw would otherwise charge the step twice.
            if (!started_ || pending_ || active_block_) {
                throw std::logic_error("begin_prepared_step() called before the run started, while a host request is "
                                       "pending, or while a step is already begun");
            }
        } else {
            if (pending_) throw std::logic_error("begin_step() called while a host request is pending");
            if (active_block_) throw std::logic_error("begin_step() called while a step is already begun");
        }
        try {
            if constexpr (!Prepared) {
                if (!started_) start();
            }
            if (scheduler_ && scheduler_->deciding) {
                if (auto event = decide()) return event;
            }
            prepare_step(Prepared);
            return std::nullopt;
        } catch (const Fault& failure) {
            return trap(failure);
        }
    }

    template <bool Prepared = false>
    void write_inputs(const nvr::Block& block, const Frame& frame, std::span<std::int64_t> inputs) {
        std::size_t lane = 0;
        for (const auto& input : block.inputs) {
            std::uint64_t value;
            if constexpr (Prepared) {
                // A fleet has already checked each read. Its validated input
                // descriptors address only slots and constants, both frozen.
                value = input.kind == nvr::ValueKind::Slot ? frame.slots[input.index] : const_values_[input.index];
            } else {
                value = read(input, frame);
            }
            const auto count = nvr::lane_count(type(input.type));
            for (unsigned bit = 0; bit < count; ++bit) {
                inputs[lane++] = static_cast<std::int64_t>((value >> bit) & 1U);
            }
        }
    }

    void prepare_step(bool defer_inputs) {
        if (steps_ >= limits_.max_steps) {
            fault(TrapKind::StepLimit, std::to_string(limits_.max_steps) + " steps");
        }
        ++steps_;
        const auto& frame = frames_.back();
        if (scheduler_ && limits_.profile) ++current_task().steps;
        if (limits_.profile) {
            if (function_steps_.size() != image_.functions.size()) function_steps_.resize(image_.functions.size());
            ++function_steps_[frame.function];
        }
        const auto& function = image_.functions[frame.function];
        const auto& block = function.blocks[frame.block];
        if (defer_inputs) {
            // Validated graph inputs can only read constants or slots. Check
            // every read that can trap before offering this step to a device.
            // The begun block's frame and constants stay unchanged until it
            // settles, so packing their bits can wait.
            for (const auto& input : block.inputs) {
                if (input.kind == nvr::ValueKind::Slot) require_initialized(frame, input.index);
            }
        } else {
            const auto count = image_.graphs[block.graph].input_lanes;
            write_inputs(block, frame, lanes(inputs_, count));
        }
        active_block_ = &block;
    }

    std::span<const std::int64_t> materialize_inputs() {
        const auto count = image_.graphs[active_block_->graph].input_lanes;
        auto inputs = lanes(inputs_, count);
        write_inputs<true>(*active_block_, frames_.back(), inputs);
        return inputs;
    }

    std::optional<Event> finish_active_step() {
        const auto& block = *std::exchange(active_block_, nullptr);
        try {
            auto event = commit_step(block);
            // A callback's return leaves its suspended caller's frames in
            // place and only completes the callback, not the enclosing run.
            if (event && event->kind == Event::Kind::Completed && frames_.empty()) set_finished(*event);
            return event;
        } catch (const Fault& failure) {
            return trap(failure);
        }
    }

    std::optional<Event> finish_reference_step(std::vector<std::int64_t>& scratch, bool deferred_inputs) {
        const auto& graph = image_.graphs[active_block_->graph];
        auto outputs = lanes(outputs_, graph.outputs.size());
        std::optional<std::size_t> failed;
        if (deferred_inputs) {
            auto wires = lanes(scratch, std::size_t{graph.input_lanes} + graph.nodes.size());
            write_inputs<true>(*active_block_, frames_.back(), wires.first(graph.input_lanes));
            failed = evaluate_loaded_outputs(graph, wires, outputs);
        } else {
            failed = evaluate_outputs(graph, std::span(inputs_).first(graph.input_lanes), scratch, outputs);
        }
        if (failed) return fail_step(*failed);
        return finish_active_step();
    }

    std::optional<Event> step(std::vector<std::int64_t>& scratch) {
        GraphStep current;
        if (auto event = begin_step(current)) return event;
        const auto& graph = image_.graphs[current.graph];
        if (limits_.graph_device && !graph.nodes.empty()) {
            GraphJob job{current.graph, current.inputs, lanes(outputs_, current.output_count)};
            limits_.graph_device->evaluate(std::span{&job, 1});
            if (job.evaluated) return finish_active_step();
        }
        return finish_reference_step(scratch, false);
    }

    std::optional<Event> commit_step(const nvr::Block& block) {
        auto& frame = frames_.back();
        const auto& exit = block.exits[select(block)];

        // Resolve every value against the pre-transition state, then commit.
        std::vector<std::pair<std::uint32_t, std::uint64_t>> assignments;
        assignments.reserve(exit.assignments.size());
        for (const auto& assignment : exit.assignments) {
            assignments.emplace_back(assignment.slot, read(assignment.value, frame));
        }
        std::vector<std::uint64_t> arguments;
        arguments.reserve(exit.arguments.size());
        for (const auto& argument : exit.arguments) {
            arguments.push_back(read(argument, frame));
        }
        if (exit.kind == nvr::ExitKind::Scheduler) check_scheduler_exit(exit, arguments, assignments);
        for (const auto& [slot, value] : assignments) {
            frame.slots[slot] = value;
            frame.initialized[slot] = 1;
        }

        switch (exit.kind) {
        case nvr::ExitKind::Continue:
            frame.block = exit.resume;
            if (exit.poll) poll();
            return std::nullopt;
        case nvr::ExitKind::Call:
            push_frame(exit.target, arguments, exit.result_slot, exit.resume);
            if (exit.poll) poll();
            return std::nullopt;
        case nvr::ExitKind::Return: {
            const auto result = arguments.empty() ? 0 : arguments.front();
            const auto function = frame.function;
            const auto return_slot = frame.return_slot;
            const auto return_block = frame.return_block;
            const auto boundary = frame.boundary;
            if (scheduler_) {
                const auto& scopes = current_task().scopes;
                if (!scopes.empty() && scopes.back().frame_depth == frames_.size()) {
                    fault(TrapKind::InvalidTaskOperation, "return leaves an open scope");
                }
                remove_live_cells(frame_cells(frame.function));
            }
            frames_.pop_back();
            if (scheduler_ && frames_.empty() && scheduler_->current != 0) {
                finish_task(result);
                return std::nullopt;
            }
            if (frames_.empty() || boundary) {
                if (scheduler_ && frames_.empty()) {
                    auto& root = current_task();
                    root.result_type = image_.functions[function].result_type;
                    root.result = result;
                    root.state = TaskState::Finished;
                }
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
            if (scheduler_ && host_mode(exit.target) != 0 &&
                scheduler_->requests.size() >= limits_.max_pending_requests) {
                fault(TrapKind::RequestLimit, "issuing another overlapping host request");
            }
            Event event;
            event.kind = Event::Kind::HostRequest;
            event.request.sequence = sequence_++;
            event.request.import = exit.target;
            auto remaining = limits_.max_heap_cells;
            for (std::size_t i = 0; i < arguments.size(); ++i) {
                event.request.arguments.push_back(value_to_host(arguments[i], import.parameter_types[i], remaining, 0));
            }
            const Pending pending{exit.target, exit.result_slot, exit.resume, exit.error_slot, exit.error_resume};
            const auto mode = host_mode(exit.target);
            if (scheduler_) {
                event.task = scheduler_->current;
                event.tick = scheduler_->ticks;
            }
            emit(TaskEvent::Kind::HostRequest, event.task, event.request.sequence, [&] { return import.name; });
            if (scheduler_ && mode != 0) {
                // The service overlaps: this task parks and others run. The
                // driver supplies the response through deliver().
                auto& task = current_task();
                task.pending = pending;
                task.waiting_for = event.request.sequence;
                task.cancellable = mode == 2;
                task.cancel_sent = task.cancellable && cancelled(task.context);
                task.waiting_for = event.request.sequence;
                scheduler_->requests.emplace(event.request.sequence, scheduler_->current);
                scheduler_->peak_requests = std::max<std::uint64_t>(scheduler_->peak_requests, scheduler_->requests.size());
                if (task.cancel_sent) scheduler_->cancellations.push_back(event.request.sequence);
                park(TaskState::HostWait);
                event.overlapping = true;
                return event;
            }
            pending_ = pending;
            return event;
        }
        case nvr::ExitKind::Trap:
            fault(TrapKind::Program, {}, exit.reason);
        case nvr::ExitKind::MessageTrap:
            fault(TrapKind::Program, typed_entry(arguments.front(), exit.arguments.front().type).bytes,
                  static_cast<std::uint32_t>(nvr::TrapReason::ProgramFailure));
        case nvr::ExitKind::Scheduler:
            scheduler_exit(exit, arguments);
            return std::nullopt;
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
            if (scheduler_) release_scheduler_object(args[0]);
            remove_live_cells(entry_cells(item));
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
            add_live_cells(1);
            ++item.version;
            return 0;
        }
        case nvr::StateOp::RemoveElement: {
            auto& item = typed_entry(args[0], types[0].type);
            const auto index = checked_index(item, args[1]);
            item.cells.erase(item.cells.begin() + static_cast<std::ptrdiff_t>(index));
            remove_live_cells(1);
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
    std::optional<std::uint32_t> entry_;
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
    std::vector<std::uint64_t> channel_work_;
    std::uint64_t last_handle_ = 0;
    std::uint64_t last_identity_ = 0;
    std::uint64_t live_cells_ = 0;
    FleetHeapCells* fleet_heap_ = nullptr;
    // The largest live heap size the run reached, in cells.
    std::uint64_t peak_cells_ = 0;
    std::vector<std::uint64_t> function_steps_;
    std::uint64_t next_collection_ = std::min<std::uint64_t>(256, limits_.max_heap_cells);
    std::vector<std::uint64_t> const_values_;
    std::vector<std::uint64_t> globals_;
    std::vector<std::uint8_t> global_initialized_;
    std::vector<Frame> frames_;
    std::unique_ptr<Scheduler> scheduler_;
    // Function names for task dumps, when the image carries them.
    std::optional<nvr::Names> names_;
    std::string frame_text(const Frame& frame) const;
    std::string wait_text(std::uint64_t id, const Task& task) const;
    std::string cancellation_text(const Task& task) const;
    const nvr::Block* active_block_ = nullptr;
    std::vector<std::int64_t> inputs_;
    std::vector<std::int64_t> outputs_;
    std::vector<std::int64_t> wires_;
    std::optional<Event> finished_;
    std::optional<Pending> pending_;
    std::uint64_t sequence_ = 0;
    std::uint64_t steps_ = 0;
};

std::string Execution::State::frame_text(const Frame& frame) const {
    std::string text;
    if (names_ && frame.function < names_->functions.size() && !names_->functions[frame.function].empty()) {
        text = names_->functions[frame.function] + " at ";
    }
    return text + task_location(frame.function, frame.block);
}

std::string Execution::State::wait_text(std::uint64_t id, const Task& task) const {
    const auto channel_text = [&](std::uint64_t object, bool sending) {
        const auto found = scheduler_->channels.find(object);
        if (found == scheduler_->channels.end()) return std::string("a closed channel");
        const auto& state = found->second;
        return "channel " + std::to_string(state.number) + " (" + std::to_string(state.buffer.size()) + " of " +
               std::to_string(state.capacity) + " queued, " +
               (sending ? std::to_string(state.receivers.size()) + " receivers waiting"
                        : std::to_string(state.senders.size()) + " senders waiting") +
               (state.closed ? ", closed)" : ")");
    };
    const auto timer_text = [&](std::uint64_t object) {
        const auto found = scheduler_->timers.find(object);
        return found == scheduler_->timers.end() ? std::string("a timer") : "timer " + std::to_string(found->second.number);
    };
    switch (task.state) {
    case TaskState::Running:
        return pending_ ? "parked: host request " + std::to_string(sequence_ - 1) : std::string("running");
    case TaskState::Runnable:
        return "runnable";
    case TaskState::Joining:
        return "parked: join of task " + std::to_string(task.waiting_for);
    case TaskState::LeavingScope: {
        const auto live = task.scopes.empty() ? 0 : task.scopes.back().live_children;
        return "parked: scope exit, waiting for " + std::to_string(live) + (live == 1 ? " task" : " tasks");
    }
    case TaskState::Finished:
        return id == 0 ? "finished" : "finished, result not yet released";
    case TaskState::Sending:
        return "parked: send on " + channel_text(task.waiting_for, true);
    case TaskState::Receiving:
        return "parked: receive on " + channel_text(task.waiting_for, false);
    case TaskState::TimerWait:
        return "parked: " + timer_text(task.waiting_for);
    case TaskState::Selecting: {
        std::string text = "parked: select of";
        for (std::size_t arm = 0; arm < task.select.size(); ++arm) {
            const auto& entry = task.select[arm];
            text += arm == 0 ? " " : ", ";
            switch (entry.kind) {
            case nvr::SelectArm::Receive: text += "receive on " + channel_text(entry.handle, false); break;
            case nvr::SelectArm::Send: text += "send on " + channel_text(entry.handle, true); break;
            case nvr::SelectArm::Join: text += "join of task " + std::to_string(entry.handle); break;
            case nvr::SelectArm::Timer: text += timer_text(entry.handle); break;
            }
        }
        return text;
    }
    case TaskState::HostWait: {
        const auto& import = image_.imports[task.pending.import];
        return "parked: host request " + std::to_string(task.waiting_for) + " " + import.name +
               (task.cancel_sent ? ", cancel requested" : "");
    }
    }
    return "unknown";
}

// The cancellation a task's context carries, or nothing.
std::string Execution::State::cancellation_text(const Task& task) const {
    for (auto id = task.context; id != 0;) {
        const auto found = scheduler_->scopes.find(id);
        if (found == scheduler_->scopes.end()) break;
        if (found->second.cancelled) {
            return "cancellation: requested at tick " + std::to_string(found->second.cancelled_tick) +
                   (found->second.deadline_passed ? ", deadline passed" : "");
        }
        id = found->second.parent;
    }
    return {};
}

std::string Execution::State::dump_tasks(TaskDumpFormat format) const {
    if (!scheduler_) return {};
    std::map<std::uint64_t, std::vector<std::uint64_t>> children;
    for (const auto& [id, task] : scheduler_->tasks) {
        if (id != 0) children[task.parent].push_back(id);
    }
    const auto stack_of = [&](std::uint64_t id, const Task& task) -> const std::vector<Frame>& {
        return id == scheduler_->current && task.state == TaskState::Running ? frames_ : task.frames;
    };
    if (format == TaskDumpFormat::Json) {
        const auto quoted = [](const std::string& text) {
            std::string out = "\"";
            for (const char c : text) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                    out += c;
                } else if (static_cast<unsigned char>(c) < 0x20) {
                    constexpr std::string_view hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 15];
                    out += hex[c & 15];
                } else {
                    out += c;
                }
            }
            return out + '"';
        };
        std::string out = "{\"tasks\":[";
        bool first = true;
        for (const auto& [id, task] : scheduler_->tasks) {
            if (!first) out += ',';
            first = false;
            out += "{\"id\":" + std::to_string(id);
            out += ",\"parent\":" + (id == 0 ? std::string("null") : std::to_string(task.parent));
            out += ",\"spawned_at\":" +
                   (id == 0 ? std::string("null") : quoted(task_location(task.spawn_function, task.spawn_block)));
            out += ",\"state\":" + quoted(wait_text(id, task));
            if (const auto cancellation = cancellation_text(task); !cancellation.empty()) {
                out += ",\"cancellation\":" + quoted(cancellation);
            }
            out += ",\"stack\":[";
            const auto& stack = stack_of(id, task);
            for (auto frame = stack.rbegin(); frame != stack.rend(); ++frame) {
                if (frame != stack.rbegin()) out += ',';
                out += quoted(frame_text(*frame));
            }
            out += "]}";
        }
        return out + "]}\n";
    }
    std::string out;
    // The tree is written without recursion: a spawn chain can be as deep
    // as the task limit.
    std::vector<std::pair<std::uint64_t, std::size_t>> pending{{0, 0}};
    while (!pending.empty()) {
        const auto [id, depth] = pending.back();
        pending.pop_back();
        const auto& task = scheduler_->tasks.at(id);
        const std::string indent(2 * depth, ' ');
        out += indent + "task " + std::to_string(id) + "  ";
        out += id == 0 ? std::string("root")
                       : "spawned at " + task_location(task.spawn_function, task.spawn_block);
        out += "  " + wait_text(id, task) + '\n';
        const auto& stack = stack_of(id, task);
        for (auto frame = stack.rbegin(); frame != stack.rend(); ++frame) {
            out += indent + "    " + frame_text(*frame) + '\n';
        }
        if (const auto cancellation = cancellation_text(task); !cancellation.empty()) {
            out += indent + "    " + cancellation + '\n';
        }
        if (const auto found = children.find(id); found != children.end()) {
            for (auto child = found->second.rbegin(); child != found->second.rend(); ++child) {
                pending.emplace_back(*child, depth + 1);
            }
        }
    }
    return out;
}

Execution::Execution(const nvr::Image& image, std::span<const std::string> arguments, RunLimits limits)
    : state_(std::make_unique<State>(image, arguments, limits)) {}

Execution::Execution(const nvr::Image& image, std::uint32_t entry, std::span<const std::string> arguments,
                     RunLimits limits)
    : state_(std::make_unique<State>(image, arguments, limits, entry)) {}

Execution::Execution(const nvr::Image& image, std::uint32_t function, std::span<const std::uint64_t> arguments,
                     RunLimits limits)
    : state_(std::make_unique<State>(image, function, arguments, limits)) {}

Execution::~Execution() = default;

void Execution::validate_entry_arguments(const nvr::Image& image, std::uint32_t entry,
                                          std::span<const std::string> arguments) {
    if (entry >= image.functions.size()) {
        throw std::invalid_argument("entry function " + std::to_string(entry) + " is out of range");
    }
    const auto& function = image.functions[entry];
    bool takes_arguments = function.parameter_count == 1;
    if (takes_arguments) {
        const auto& argument = image.types[function.frame_types[0]];
        takes_arguments = argument.kind == nvr::TypeKind::Array &&
                          image.types[argument.reference].kind == nvr::TypeKind::Text;
    }
    if (!takes_arguments || image.types[function.result_type] != nvr::Type{nvr::TypeKind::Signed, 32}) {
        throw std::invalid_argument("the entry function must take one UTF-8 text array and return int32");
    }
    validate_utf8_arguments(arguments);
}

void Execution::validate_call_arguments(const nvr::Image& image, std::uint32_t function,
                                         std::span<const std::uint64_t> arguments) {
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

void Execution::initialize(FleetHeapCells& heap) { state_->initialize(heap); }

std::optional<Event> Execution::step_with_scratch(std::vector<std::int64_t>& scratch) {
    return state_->step(scratch);
}

std::optional<Event> Execution::begin_prepared_step(std::uint32_t& graph) {
    // The fleet calls only for an admitted, runnable member with no open step.
    if (auto event = state_->begin_active_step<true>()) return event;
    graph = state_->active_block_->graph;
    return std::nullopt;
}

std::optional<Event> Execution::finish_prepared_reference_step(std::vector<std::int64_t>& scratch) {
    return state_->finish_reference_step(scratch, true);
}

std::span<const std::int64_t> Execution::prepared_inputs() {
    return state_->materialize_inputs();
}

Execution::ReferenceRounds Execution::advance_reference_rounds(std::vector<std::int64_t>& scratch,
                                                               std::uint64_t limit) {
    for (std::uint64_t count = 0; count < limit; ++count) {
        if (auto event = state_->step(scratch)) return {std::move(event), count + 1};
    }
    return {std::nullopt, limit};
}

Event Execution::advance() { return state_->advance(); }
std::string Execution::dump_tasks(TaskDumpFormat format) const { return state_->dump_tasks(format); }
std::uint64_t Execution::peak_tasks() const { return state_->peak_tasks(); }
std::uint64_t Execution::tick() const { return state_->tick(); }
std::uint64_t Execution::peak_queued_messages() const { return state_->peak_queued_messages(); }
std::uint64_t Execution::peak_pending_requests() const { return state_->peak_pending_requests(); }
void Execution::deliver(std::uint64_t sequence, const HostCompletion& completion) {
    state_->deliver(sequence, completion);
}
std::size_t Execution::advance_clock(std::int64_t now) { return state_->advance_clock(now); }
std::vector<TaskSiteProfile> Execution::task_profile() const { return state_->task_profile(); }
std::string Execution::location(std::uint32_t function, std::uint32_t block) const {
    return state_->location(function, block);
}

std::optional<Event> Execution::begin_step(GraphStep& step) { return state_->begin_step(step); }

std::optional<Event> Execution::finish_step(std::span<const std::int64_t> outputs) {
    return state_->finish_step(outputs);
}

Event Execution::fail_step(std::size_t node) { return state_->fail_step(node); }

void Execution::complete(const HostCompletion& completion) { state_->complete(completion); }

std::optional<HostValue> Execution::invoke_callback(std::string_view name, std::uint32_t version,
                                                    const std::vector<HostValue>& arguments,
                                                    const HostHandler& host) {
    return state_->invoke_callback(name, version, arguments, host);
}

std::uint64_t Execution::steps() const { return state_->steps(); }
std::uint64_t Execution::live_heap_cells() const { return state_->live_heap_cells(); }
std::uint64_t Execution::peak_heap_cells() const { return state_->peak_heap_cells(); }
const std::vector<std::uint64_t>& Execution::function_steps() const { return state_->function_steps(); }

std::int64_t SteadyTaskClock::now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void SteadyTaskClock::wait_until(std::int64_t deadline) {
    const auto remaining = deadline - now();
    if (remaining > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(remaining));
}

RunResult run(const nvr::Image& image, const Bindings& bindings, std::span<const std::string> arguments,
              RunLimits limits, TaskClock* clock) {
    std::uint64_t requests = 0;
    if (!image.transferable_layouts) {
        // An image without tasks answers every request before it continues.
        Execution execution(image, arguments, limits);
        detail::ServiceCallbacks callbacks(execution, bindings, requests);
        for (;;) {
            const auto event = execution.advance();
            if (event.kind == Event::Kind::HostRequest) {
                execution.complete(callbacks.serve(event.request));
            } else {
                return detail::finished_result(execution, event, requests);
            }
        }
    }
    SteadyTaskClock steady;
    if (!clock) clock = &steady;
    const auto modes = host_modes_for(image, bindings);
    if (!limits.host_modes) limits.host_modes = &modes;
    Execution execution(image, arguments, limits);
    detail::ServiceCallbacks callbacks(execution, bindings, requests);
    DriveHooks hooks;
    hooks.serve = [&](const Event& event) { return callbacks.serve(event.request); };
    hooks.issued = [&](const Event&, bool) { ++requests; };
    const auto last = drive(execution, bindings, *clock, hooks);
    return detail::finished_result(execution, last, requests);
}

} // namespace neura::nvm
