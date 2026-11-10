#pragma once

#include "nvr/image.hpp"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace neura::nvm {

// A copied value crossing the host boundary. Numeric and Boolean values use
// the same bit encoding as frame slots. Text and uint8 sequences use bytes.
// Tuples and other sequences use elements in declaration or index order.
struct HostValue {
    std::uint64_t bits = 0;
    std::string bytes;
    std::vector<HostValue> elements{};

    bool operator==(const HostValue&) const = default;
};

// Host types describe shape without referring to any specific NVR image.
// Object describes a tuple, Array and List each take one element type.
struct HostType {
    nvr::TypeKind kind = nvr::TypeKind::Void;
    std::uint8_t width = 0;
    std::vector<HostType> components;

    HostType() = default;
    HostType(nvr::TypeKind kind, std::uint8_t width = 0, std::uint32_t = 0) : kind(kind), width(width) {}
    HostType(nvr::Type type) : HostType(type.kind, type.width) {}
    HostType(nvr::TypeKind kind, std::vector<HostType> components)
        : kind(kind), components(std::move(components)) {}
};

struct HostSignature {
    std::string name;
    std::uint32_t version = 0;
    std::vector<HostType> parameters;
    HostType success;
    HostType error;
};

// Calls the image's callbacks (NVR CALB) while a host request is in
// progress, for providers whose external code calls back into the program,
// such as a native function that calls an upcall stub.
class CallbackContext {
public:
    virtual ~CallbackContext() = default;
    // Runs the callback registered as `name` and `version` to completion and
    // returns its result, or nothing when the run ended during it (a trap or
    // a process exit), or when the image has no such callback.
    virtual std::optional<HostValue> call(std::string_view name, std::uint32_t version,
                                          const std::vector<HostValue>& arguments) = 0;
};

struct HostRequest {
    // Execution-local sequence number, starting at zero for each run.
    std::uint64_t sequence = 0;
    std::uint32_t import = 0;
    std::vector<HostValue> arguments;
    // Set by the runner for the duration of the request; null when callbacks
    // are unavailable.
    CallbackContext* callbacks = nullptr;
    // The task clock's reading, in nanoseconds, when an overlapping request
    // is started (HostService::begin); zero otherwise. A service computes
    // the deadline of a timeout from it.
    std::int64_t now = 0;
};

struct HostCompletion {
    bool success = true;
    // Payload of the success or error type; ignored when that type is void.
    HostValue value;
    // A successful neura.process.exit completion ends this execution.
    std::optional<std::int32_t> exit_status{};

    static HostCompletion ok(HostValue value = {}) { return {true, std::move(value)}; }
    static HostCompletion failed(HostValue value = {}) { return {false, std::move(value)}; }
    static HostCompletion terminate(std::int32_t status) { return {true, {}, status}; }
};

// An overlapping request that has not finished (docs/lang/Concurrency.md
// section 10.4). The driver keeps it until it can supply the completion; it
// never blocks on it and starts no thread for it. Everything the operation
// holds, such as a descriptor it owns, is released when the object is
// destroyed, whether or not it finished.
struct PendingOperation {
    static constexpr std::int64_t no_deadline = std::numeric_limits<std::int64_t>::max();

    // The operation can proceed when this descriptor is readable or writable
    // (POSIX poll). -1: it waits for no descriptor.
    int descriptor = -1;
    bool readable = false;
    bool writable = false;
    // Optional: the descriptor the operation waits for now, when it changes
    // while the operation is pending (a connect that moves to its next
    // address). Replaces `descriptor` when set.
    std::function<int()> current_descriptor{};
    // The operation can also proceed when the task clock reads this many
    // nanoseconds.
    std::int64_t deadline = no_deadline;
    // Tries to finish without blocking: the completion, or nothing when it
    // must wait longer. The driver calls it when the descriptor is ready,
    // at every check when there is no descriptor, and after a cancel
    // request with cancelled set. A cancellable operation answers a call
    // with cancelled set with its completion if it finished first and with
    // host_error::cancelled otherwise; it is never asked again.
    std::function<std::optional<HostCompletion>(bool cancelled)> attempt;
    // The completion when the deadline passes before the operation finishes.
    // Required when there is a deadline.
    std::function<HostCompletion()> expire{};
};

class HostService {
public:
    virtual ~HostService() = default;
    virtual const HostSignature& signature() const = 0;
    virtual HostCompletion invoke(const HostRequest& request) = 0;

    // Whether the service overlaps: in an image with tasks, a request does
    // not hold up the other tasks. Then begin() is used instead of invoke().
    virtual bool overlapping() const { return false; }
    // Whether an overlapping request can be cancelled: begin()'s operation
    // honors attempt(true). Only meaningful when overlapping.
    virtual bool cancellable() const { return false; }
    // Starts an overlapping request without blocking. Returns its completion
    // when it finished at once (a capability error, an argument error, data
    // already there), or how to wait for it. The default completes by
    // invoking the service.
    virtual std::variant<HostCompletion, PendingOperation> begin(const HostRequest& request) {
        return invoke(request);
    }
};

// The services a runner offers, keyed by name and interface version.
class HostRegistry {
public:
    void add(HostService& service);
    HostService* find(std::string_view name, std::uint32_t version) const;

private:
    std::map<std::pair<std::string, std::uint32_t>, HostService*, std::less<>> services_;
};

class BindError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One bound service per import, in import table order.
struct Bindings {
    std::vector<HostService*> services;
};

// Binds every import of a loaded image to a registered service with the same
// name, version, and signature. Throws BindError naming the first import that
// is missing or whose signature differs.
Bindings bind(const nvr::Image& image, const HostRegistry& registry);

// The result of a write that may store only a prefix of its bytes.
struct SinkWrite {
    // Bytes stored, from the start of the request.
    std::size_t written = 0;
    // True when the sink failed before storing every byte.
    bool failed = false;
};

// Destination for console output. The console service hands each line,
// including its LF, to one write call.
class ConsoleSink {
public:
    virtual ~ConsoleSink() = default;
    // Writes `bytes` and returns true when all of them were written. A sink
    // may write a prefix before returning false.
    virtual bool write(std::string_view bytes) = 0;
    // Writes `bytes` and reports how many were stored. The default treats a
    // failed write() as storing nothing.
    virtual SinkWrite write_some(std::string_view bytes);
};

// Writes to a C stream and flushes after each line.
class StreamSink final : public ConsoleSink {
public:
    explicit StreamSink(std::FILE* stream) : stream_(stream) {}
    bool write(std::string_view bytes) override;
    SinkWrite write_some(std::string_view bytes) override;

private:
    std::FILE* stream_;
};

// Collects output in memory. With a capacity, writes fail once the total
// would exceed it, after storing the bytes that still fit.
class StringSink final : public ConsoleSink {
public:
    StringSink() = default;
    explicit StringSink(std::size_t capacity) : capacity_(capacity) {}
    bool write(std::string_view bytes) override;
    SinkWrite write_some(std::string_view bytes) override;
    const std::string& contents() const { return contents_; }

private:
    std::string contents_;
    std::optional<std::size_t> capacity_;
};

// neura.console.write_line version 1: takes UTF-8 text, emits its bytes and
// one LF, and returns success with no payload or the uint32 error code 1
// when the write fails. A failed write is not retried.
class ConsoleWriteLine final : public HostService {
public:
    explicit ConsoleWriteLine(ConsoleSink& sink);
    const HostSignature& signature() const override { return signature_; }
    HostCompletion invoke(const HostRequest& request) override;

private:
    ConsoleSink& sink_;
    HostSignature signature_;
};

inline constexpr std::uint32_t console_write_failed = 1;

} // namespace neura::nvm
