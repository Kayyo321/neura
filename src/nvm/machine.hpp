#pragma once

#include "host.hpp"
#include "jit/jit.hpp"
#include "nvr/image.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace neura::nvm {

class GraphDevice;
class Fleet;
class TierCache;

struct TaskDumpRequest;
class ScheduleChooser;
class TaskObserver;

// Bounds a run enforces. Exhaustion traps at the operation that would exceed
// the bound, before the allocation, frame, or block it would start.
struct RunLimits {
    // Block evaluations, counting every block entered.
    std::uint64_t max_steps = 100'000'000;
    // Active frames, including the entry frame.
    std::uint32_t max_frames = 10'000;
    // Heap storage after reclaiming unreachable entries: one cell per entry
    // plus one per object field, collection element, and text or bytes byte.
    std::uint64_t max_heap_cells = std::uint64_t{1} << 26;
    // Reference handles assigned during the run. Handles are never reused.
    std::uint64_t max_handles = std::uint64_t{1} << 32;
    // Count the steps of each function, for `neura run --profile`. Counting
    // does not change execution.
    bool profile = false;
    // Observes every begun reference-tier block, including one that later
    // traps. Tooling coverage supplies this with tier=0, no device, and one
    // parallel worker. It must not mutate or call back into the execution.
    std::function<void(std::uint32_t function, std::uint32_t block)> block_observer{};
    // Evaluates graphs for this run, or nullptr for the reference evaluator.
    // The device must have been created for the image the run executes.
    GraphDevice* graph_device = nullptr;
    // The highest execution tier (docs/runtime/NVM.md, "Execution tiers"):
    // 0 evaluates every graph by reference; 1 also runs proven lifted blocks
    // natively; 2 also runs regions of them without returning to the
    // dispatcher between blocks. The tier never changes what a run does,
    // only its speed. NEURA_TIER=0 or 1 in the environment lowers it for
    // every run. Without a graph device only: a device evaluates the graphs
    // it accepts.
    std::uint8_t tier = 2;
    // Lifted blocks shared by the runs of one image (src/nvm/tier1.hpp), or
    // null for a cache of the run's own, which its parallel items share. It
    // must be for the image the run executes and outlive the run.
    TierCache* tier_cache = nullptr;
    // Whether tier 2 runs its regions as native code (docs/runtime/NVM.md,
    // "Native compilation"): Off runs them on the region interpreter, On
    // compiles them and fails the run at its start when this platform
    // cannot, Auto compiles them where that is enabled by default. Like the
    // tier, it never changes what a run does. Only at tier 2; NEURA_JIT in
    // the environment combines with it (jit::combine).
    jit::Mode jit = jit::Mode::Auto;
    std::uint64_t max_tasks = 65'536;
    std::uint64_t task_quantum = 1'024;
    // Values buffered in all channels, and overlapping host requests issued
    // and not yet delivered.
    std::uint64_t max_queued_messages = 1'048'576;
    std::uint64_t max_pending_requests = 4'096;
    // How each import is served in an image with tasks, by import index:
    // 0 completes before the scheduler continues, 1 overlaps, 2 overlaps
    // and can be cancelled. Null or short: every import is synchronous. It
    // must outlive the run.
    const std::vector<std::uint8_t>* host_modes = nullptr;
    // Worker threads that evaluate the isolated items of a parallel map
    // (docs/lang/Concurrency.md section 12). One evaluates them in order on
    // the scheduler's thread. More may change only wall time and peak
    // process memory.
    std::uint32_t parallel_workers = 1;
    // Which runnable task goes next, or null for the first: the FIFO order
    // of the reference scheduler. Schedule exploration and the replay of a
    // saved schedule supply one. It must outlive the run.
    ScheduleChooser* chooser = nullptr;
    // Receives scheduler events as they happen, or null. It must outlive
    // the run and must not call back into it.
    TaskObserver* observer = nullptr;
    // Where the run writes a task dump when one is requested, or nullptr.
    // It must outlive the run.
    TaskDumpRequest* task_dump = nullptr;
    // When set, receives the task chosen by each scheduling decision, in
    // order: entry N is the task that ran from tick N. It must outlive the
    // run. Tests and tools compare schedules with it.
    std::vector<std::uint64_t>* schedule = nullptr;
};

// Chooses among the runnable tasks at a scheduling decision
// (docs/lang/Concurrency.md section 14.4). Every choice is a legal schedule:
// the memory model of section 5.4 holds under all of them.
class ScheduleChooser {
public:
    virtual ~ScheduleChooser() = default;
    // `tick` decisions have been made; `runnable` tasks, at least one, can
    // run. Returns the position in the runnable queue of the task to run;
    // zero is the reference scheduler's choice.
    virtual std::size_t choose(std::uint64_t tick, std::size_t runnable) = 0;
};

// One scheduler event (docs/lang/Concurrency.md section 14.2). Every event
// carries the tick at which it happened and the task it concerns.
struct TaskEvent {
    enum class Kind {
        // `other` is the new task; `where` its spawn site.
        Spawn,
        Start,
        // `detail` is the wait reason.
        Park,
        Wake,
        Finish,
        // `other` is the channel's number.
        Send, Receive, Close,
        // `other` is the scope whose cancellation was requested.
        Cancel,
        // `other` is the scope or timer number; `detail` says which.
        DeadlineExpired,
        // `other` is the request's sequence number; `detail` the service.
        HostRequest, HostCompletion
    };
    Kind kind = Kind::Spawn;
    std::uint64_t tick = 0;
    std::uint64_t task = 0;
    std::uint64_t other = 0;
    std::string detail;
    std::string where;
};

class TaskObserver {
public:
    virtual ~TaskObserver() = default;
    virtual void event(const TaskEvent& event) = 0;
};

// What the tasks started at one spawn site did, summed over them
// (`neura run --profile`): the steps they executed and the scheduling
// decisions that passed while they were runnable and waiting for a turn,
// parked on a channel or timer, and parked on a host request.
struct TaskSiteProfile {
    // The spawn site; nvr::absent for the root task.
    std::uint32_t function = nvr::absent;
    std::uint32_t block = nvr::absent;
    std::uint64_t tasks = 0;
    std::uint64_t steps = 0;
    std::uint64_t runnable_ticks = 0;
    std::uint64_t channel_ticks = 0;
    std::uint64_t host_ticks = 0;
};

enum class TaskDumpFormat { Text, Json };

// Asks a running image for a task dump (docs/lang/Concurrency.md section
// 14.1). Setting `requested` is safe from a signal handler. The run takes
// the snapshot at its next scheduling point, hands it to `write`, clears the
// request, and continues: a dump never changes the run.
struct TaskDumpRequest {
    std::atomic<bool> requested{false};
    TaskDumpFormat format = TaskDumpFormat::Text;
    // Also write the full dump when a task traps, before the run ends.
    bool on_trap = false;
    // Receives each dump. The text is empty for an image without tasks.
    std::function<void(const std::string&)> write;
};

enum class TrapKind {
    // A trap exit selected by the program, with its reason code.
    Program,
    // Runtime faults detected by NVM.
    GraphOverflow,
    NonBinaryLane,
    InvalidSelector,
    UninitializedSlot,
    UninitializedField,
    UninitializedGlobal,
    InvalidHandle,
    NilReference,
    InvalidCast,
    ReferenceType,
    IndexOutOfBounds,
    InvalidText,
    InvalidCompletion,
    // Resource limits.
    StepLimit,
    FrameLimit,
    HeapLimit,
    HandleLimit,
    TaskLimit,
    InvalidTaskOperation,
    Deadlock,
    MessageLimit,
    RequestLimit,
};

struct Trap {
    TrapKind kind = TrapKind::Program;
    // Reason code of a program trap.
    std::uint32_t reason = 0;
    // Function and block that were executing, or nvr::absent before entry.
    std::uint32_t function = nvr::absent;
    std::uint32_t block = nvr::absent;
    std::string detail;
    // The calling frames below the trapping one, innermost first, each as
    // its function and the block that made the call.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> callers{};
};

std::string_view describe(TrapKind kind);
// The process status for a run that ends in `trap`. Each class of trap uses
// the status of the POSIX signal its native equivalent raises (128 + n on
// Linux), named by Std.Program.ExitCode: arithmetic 136, bounds, nil and
// uninitialized reads and frame depth 139, explicit failures and failed
// casts 134, invalid dispatch and malformed graphs 132, heap and handle
// exhaustion 137, and the step limit 152.
int exit_status(const Trap& trap);
// A one-line diagnostic for `trap`, with a source location when available.
// Test reports omit internal function and block numbers.
std::string describe(const Trap& trap, const nvr::Image& image, bool include_indices = true);

// Evaluates `graph` with the checked arithmetic of graph profile 1. `wires`
// holds the input lanes on entry and every wire on return. Returns the index
// of the first node whose weighted sum overflows signed 64-bit arithmetic,
// or nothing when evaluation succeeds.
std::optional<std::size_t> evaluate_graph(const nvr::Graph& graph, std::vector<std::int64_t>& wires);

// Evaluates the graph by reference and stores wire graph.outputs[k] in
// outputs[k]. Returns the index of the node whose sum overflows, if any.
// The spans must match the graph's lane counts and not alias scratch.
std::optional<std::size_t> evaluate_outputs(const nvr::Graph& graph, std::span<const std::int64_t> inputs,
                                            std::vector<std::int64_t>& scratch, std::span<std::int64_t> outputs);

struct Event {
    // Completed, Trapped, and HostRequest are the events of every image. An
    // image with tasks (docs/lang/Concurrency.md section 10.4) adds three:
    //
    // - HostRequest with `overlapping` set: the task that issued it is
    //   parked. The driver starts the operation, keeps advancing, and
    //   supplies the response later with deliver().
    // - Poll: a scheduling point with overlapping requests pending or a
    //   timer armed. The driver may call deliver() for requests that have
    //   completed, in the order their provider reported them, and
    //   advance_clock() when `timer` is set. It must not block.
    // - Waiting: no task can run until a request completes or the clock
    //   reaches `deadline`. The driver blocks for one of them, then calls
    //   deliver() or advance_clock().
    // - Cancel: the task waiting for request `sequence` was cancelled. The
    //   driver asks the provider to cancel; the provider still answers
    //   exactly once, with the completion or with error 15.
    enum class Kind { Completed, Trapped, HostRequest, Poll, Waiting, Cancel };

    Kind kind = Kind::Completed;
    bool overlapping = false;
    // Poll and Waiting: a timer is armed, and when the earliest one is due
    // on the monotonic clock, in nanoseconds.
    bool timer = false;
    std::int64_t deadline = nvr::no_deadline;
    // Cancel: the request. Poll, Waiting, HostRequest: the scheduling
    // decisions made so far.
    std::uint64_t sequence = 0;
    std::uint64_t tick = 0;
    // HostRequest: the task that issued it; zero in an image without tasks.
    std::uint64_t task = 0;
    // The entry function's int32 result, when completed.
    std::int32_t status = 0;
    // The returned value's slot encoding, when completed: the low bits of a
    // Boolean or integer, or a reference handle.
    std::uint64_t result = 0;
    Trap trap;
    HostRequest request;
};

// One run of a validated image. The run owns its frames, heap, globals, and
// counters; constructing another Execution starts again from the image.
// Read-only debugger data copied between completed graph steps. These are
// runtime values, never evaluated expressions: inspection cannot invoke a
// getter, run a function, initialize a slot, or retain a heap object.
struct DebugValue {
    std::uint32_t type = nvr::absent;
    std::uint64_t bits = 0;
    bool initialized = false;
    std::string text;
    std::size_t children = 0;
    std::uint32_t layout = nvr::absent;
};

struct DebugFrame {
    std::uint32_t function = nvr::absent;
    std::uint32_t block = nvr::absent;
    // Slots retain image order; NAME supplies optional source names.
    std::vector<DebugValue> slots;
};

struct DebugThread {
    std::uint64_t id = 0;
    std::string state;
    // Innermost frame first.
    std::vector<DebugFrame> frames;
};

struct DebugPosition {
    std::uint64_t task = 0;
    std::uint32_t function = nvr::absent;
    std::uint32_t block = nvr::absent;
    std::size_t depth = 0;
};

struct DebugSnapshot {
    std::uint64_t current_task = 0;
    std::vector<DebugThread> threads;
    std::vector<DebugValue> globals;
};

class Execution {
public:
    // Throws std::invalid_argument when an argument is not valid UTF-8.
    Execution(const nvr::Image& image, std::span<const std::string> arguments, RunLimits limits = {});
    // Starts at an explicit entry-signature function (one text array,
    // returning int32). Throws std::invalid_argument for a mismatched
    // signature or an argument that is not valid UTF-8.
    Execution(const nvr::Image& image, std::uint32_t entry, std::span<const std::string> arguments,
              RunLimits limits = {});
    // Starts at `function` instead of the entry point, for embedders and
    // tests. Every parameter must be a Boolean or integer, given in its slot
    // encoding. Throws std::invalid_argument for a mismatched argument list.
    Execution(const nvr::Image& image, std::uint32_t function, std::span<const std::uint64_t> arguments,
              RunLimits limits = {});
    ~Execution();
    Execution(const Execution&) = delete;
    Execution& operator=(const Execution&) = delete;

    // Runs blocks until the run completes, traps, or selects a host call
    // exit. A host request suspends the run until complete() supplies its
    // response. Advancing a finished run returns its final event again.
    Event advance();
    // Snapshot of scheduler state between block steps: the task tree with
    // each task's spawn site, state, and stack. Empty for a serial image.
    std::string dump_tasks(TaskDumpFormat format = TaskDumpFormat::Text) const;
    // Call only while execution is stopped between steps (or after
    // begin_step and before finish_step). Copies are bounded by the caller's
    // variable window; they never alter program or collector state.
    DebugPosition debug_position() const;
    DebugSnapshot debug_snapshot() const;
    std::vector<DebugValue> debug_children(std::uint32_t type, std::uint64_t handle,
                                           std::size_t start = 0, std::size_t count = 100) const;

    // The most tasks that were live at once, counting the root. Zero for a
    // serial image.
    std::uint64_t peak_tasks() const;

    struct GraphStep {
        std::uint32_t graph = 0;
        // Valid until the step is finished.
        std::span<const std::int64_t> inputs;
        std::size_t output_count = 0;
    };

    // Start a block, then finish it with its graph outputs or report the
    // reference evaluator's overflow. A begun step must be settled before
    // advancing or beginning another one.
    std::optional<Event> begin_step(GraphStep& step);
    std::optional<Event> finish_step(std::span<const std::int64_t> outputs);
    Event fail_step(std::size_t node);

    // Supplies the response to the pending host request. The value is
    // type-checked against the import; an ill-typed response traps. After a
    // callback ended the run, the response is ignored.
    void complete(const HostCompletion& completion);
    // Supplies the response to the overlapping request `sequence` and makes
    // its task runnable. Valid after a Poll or Waiting event; throws
    // std::logic_error for a request that is not pending.
    void deliver(std::uint64_t sequence, const HostCompletion& completion);
    // Tells the scheduler the monotonic clock reads `now` nanoseconds: every
    // timer and scope deadline at or before it expires, in deadline order.
    // Valid after a Poll or Waiting event. Returns how many expired.
    std::size_t advance_clock(std::int64_t now);
    // The scheduling decisions made so far; zero for an image without tasks.
    std::uint64_t tick() const;
    // The most values buffered in channels at once, and the most
    // overlapping requests pending at once.
    std::uint64_t peak_queued_messages() const;
    std::uint64_t peak_pending_requests() const;
    // Per spawn site, in order of first spawn, when RunLimits::profile is
    // set; the root task comes first. Empty for an image without tasks.
    std::vector<TaskSiteProfile> task_profile() const;
    // The source location of a block for task reports: path:line:column
    // from the source map, or the function and block numbers.
    std::string location(std::uint32_t function, std::uint32_t block) const;

    // While a host request is pending, runs the callback `name` `version`
    // (NVR CALB) with `arguments` until it returns, answering the host
    // requests it makes with `host`. Returns its result, or nothing when the
    // run ended during the callback (a trap, a process exit, or an
    // unknown callback); the next advance() then reports how it ended.
    using HostHandler = std::function<HostCompletion(const HostRequest&)>;
    std::optional<HostValue> invoke_callback(std::string_view name, std::uint32_t version,
                                             const std::vector<HostValue>& arguments, const HostHandler& host);

    std::uint64_t steps() const;
    std::uint64_t live_heap_cells() const;
    // The largest live heap size reached so far, in the cells of
    // RunLimits::max_heap_cells.
    std::uint64_t peak_heap_cells() const;
    // Steps per image function, in function table order, when
    // RunLimits::profile is set; otherwise empty.
    const std::vector<std::uint64_t>& function_steps() const;

private:
    friend class Fleet;

    static void validate_entry_arguments(const nvr::Image& image, std::uint32_t entry,
                                         std::span<const std::string> arguments);
    static void validate_call_arguments(const nvr::Image& image, std::uint32_t function,
                                        std::span<const std::uint64_t> arguments);
    // The sum can exceed an individual member's uint64_t limit. Keep the
    // carry so an over-limit fleet can become eligible after reclamation.
    struct FleetHeapCells {
        void add(std::uint64_t cells) {
            low += cells;
            high += low < cells;
        }
        void subtract(std::uint64_t cells) {
            high -= low < cells;
            low -= cells;
        }
        bool below(std::uint64_t limit) const { return high == 0 && low < limit; }

        std::uint64_t low = 0;
        std::uint64_t high = 0;
    };
    void initialize(FleetHeapCells& heap);
    std::optional<Event> step_with_scratch(std::vector<std::int64_t>& scratch);
    std::optional<Event> begin_prepared_step(std::uint32_t& graph);
    std::optional<Event> finish_prepared_reference_step(std::vector<std::int64_t>& scratch);
    std::span<const std::int64_t> prepared_inputs();
    struct ReferenceRounds {
        std::optional<Event> event;
        std::uint64_t count = 0;
    };
    ReferenceRounds advance_reference_rounds(std::vector<std::int64_t>& scratch, std::uint64_t limit);

    class State;
    std::unique_ptr<State> state_;
};

struct RunResult {
    bool completed = false;
    std::int32_t status = 0;
    Trap trap;
    std::uint64_t steps = 0;
    std::uint64_t host_requests = 0;
    std::uint64_t peak_heap_cells = 0;
    // Steps per image function when RunLimits::profile is set.
    std::vector<std::uint64_t> function_steps{};
    // The most tasks live at once; zero for an image without tasks.
    std::uint64_t peak_tasks = 0;
    std::uint64_t peak_queued_messages = 0;
    std::uint64_t peak_pending_requests = 0;
    // Per spawn site when RunLimits::profile is set, with each site's
    // location resolved.
    std::vector<TaskSiteProfile> task_profile{};
    std::vector<std::string> task_profile_sites{};
};

// The monotonic clock a driver gives the scheduler's timers and deadlines
// (docs/lang/Concurrency.md section 7). It is the clock of
// neura.clock.monotonic, in nanoseconds.
class TaskClock {
public:
    virtual ~TaskClock() = default;
    virtual std::int64_t now() = 0;
    // Returns once the clock reads at least `deadline`.
    virtual void wait_until(std::int64_t deadline) = 0;
    // Whether time passes only when wait_until moves it (virtual time, which
    // never blocks). A driver then checks descriptors without blocking and
    // moves the clock to the earliest deadline instead of sleeping.
    virtual bool virtual_time() const { return false; }
};

// The host's steady clock; wait_until sleeps.
class SteadyTaskClock final : public TaskClock {
public:
    std::int64_t now() override;
    void wait_until(std::int64_t deadline) override;
};

// Runs `image` to completion, issuing each host request to its bound service.
// A service answers at once, except that in an image with tasks a service
// that overlaps (HostService::overlapping) is started without blocking, and
// the other tasks run until it can complete (src/nvm/driver.hpp). Timers and
// deadlines of an image with tasks follow `clock`, or the steady clock when
// it is null. An image without tasks is served exactly as before: every
// request is answered before the run continues.
RunResult run(const nvr::Image& image, const Bindings& bindings, std::span<const std::string> arguments,
              RunLimits limits = {}, TaskClock* clock = nullptr);

} // namespace neura::nvm
