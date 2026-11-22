#pragma once

// The driver of an image with tasks (docs/lang/Concurrency.md section 10.4):
// it starts overlapping host requests without blocking, keeps them while the
// other tasks run, and supplies their completions at scheduling points. The
// live run and recorder share it, so a recorded run and an unrecorded one
// make the same calls. It starts no thread.

#include "machine.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <variant>
#include <vector>

namespace neura::nvm {

// How each import of `image` is served, by import index: 0 completes before
// the scheduler continues, 1 overlaps, 2 overlaps and can be cancelled. Empty
// for an image without tasks, which serves every request synchronously.
std::vector<std::uint8_t> host_modes_for(const nvr::Image& image, const Bindings& bindings);

// How the driver asks the operating system which descriptors are ready.
// The process default may select epoll or io_uring via NEURA_IO_BACKEND.
// A live driver can select a supported backend for one execution. Ready
// operations are applied in request sequence regardless of the backend.
enum class IoBackend { Poll, Epoll, IoUring, Kqueue, WaitForMultipleObjects };
IoBackend io_backend();
const char* io_backend_name(IoBackend backend);

// The overlapping requests a driver has started and not yet delivered.
class PendingRequests {
public:
    explicit PendingRequests(TaskClock& clock, IoBackend backend = io_backend()) : clock_(clock), backend_(backend) {}

    // What a completed request reports back.
    struct Done {
        std::uint64_t sequence = 0;
        HostCompletion completion;
        // A cancel request reached the operation before this answer was
        // collected, whatever the answer is.
        bool cancelled = false;
    };

    // Keeps request `sequence`, which `service.begin` answered with `started`.
    void add(std::uint64_t sequence, std::variant<HostCompletion, PendingOperation> started, bool cancellable);
    // The task waiting for `sequence` asked to cancel. A cancellable
    // operation that has not finished answers at once, with its completion if
    // it can finish now and with host_error::cancelled otherwise; the answer
    // is delivered at the next check like any other.
    void cancel(std::uint64_t sequence);
    // Checks without blocking and returns answers in ascending sequence order.
    // An embedding loop can keep this queue and poll it when a provider
    // publishes external readiness or a timer passes.
    std::vector<Done> poll();
    // Blocks until an operation can finish or the clock reads `clock_deadline`
    // (the earliest timer of the scheduler, when it has one), then returns
    // what poll() would. May return nothing; the caller then checks the clock
    // and waits again. Under virtual time it never blocks on the clock.
    // This is for a native run that can block. An event loop uses poll().
    std::vector<Done> wait(std::optional<std::int64_t> clock_deadline);

    // A successful event-loop host call records its selection in the response.
    // Applying it here only selects the implementation for a live driver.
    void apply_service_selection(const HostRequest& request, const HostSignature& signature,
                                 const HostCompletion& completion);
    IoBackend backend() const { return backend_; }

    bool empty() const { return entries_.empty(); }
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        PendingOperation operation;
        std::optional<HostCompletion> answer;
        bool cancellable = false;
        bool cancelled = false;
    };

    // Tries every entry that may have progressed, or only `only`. The native
    // wait timeout is in milliseconds. Zero never blocks and -1 waits
    // without a clock limit.
    void check(int timeout_ms, std::optional<std::uint64_t> only = std::nullopt);
    std::vector<Done> collect();

    TaskClock& clock_;
    IoBackend backend_;
    std::map<std::uint64_t, Entry> entries_;
};

// What the driver asks of its caller. Every function may be empty except
// `serve`.
struct DriveHooks {
    // Answers a request that does not overlap, before the run continues.
    std::function<HostCompletion(const Event&)> serve;
    // A request that overlaps was issued; called before begin().
    std::function<void(const Event&, bool cancellable)> issued;
    // An answer is about to be delivered at the scheduling point `tick`.
    std::function<void(std::uint64_t tick, std::uint64_t sequence, bool cancelled, const HostCompletion&)> delivered;
    // The clock reading `now` expired at least one timer or deadline at
    // scheduling point `tick`.
    std::function<void(std::uint64_t tick, std::int64_t now)> clock_read;
};

// Advances `execution` to its end and returns the final event.
Event drive(Execution& execution, const Bindings& bindings, TaskClock& clock, DriveHooks& hooks);

} // namespace neura::nvm
