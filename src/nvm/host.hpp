#pragma once

#include "nvr/image.hpp"

#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neura::nvm {

// A value crossing the host boundary. Booleans and integers use `bits` with
// the same encoding as frame slots: the low bits hold the value, two's
// complement for signed types. Text and bytes use `bytes`.
struct HostValue {
    std::uint64_t bits = 0;
    std::string bytes;

    bool operator==(const HostValue&) const = default;
};

// The typed interface of a host service. Host signatures use only Boolean,
// integer, text, and bytes types, so they are compared structurally with an
// image's import declaration.
struct HostSignature {
    std::string name;
    std::uint32_t version = 0;
    std::vector<nvr::Type> parameters;
    nvr::Type success;
    nvr::Type error;
};

struct HostRequest {
    // Execution-local sequence number, starting at zero for each run.
    std::uint64_t sequence = 0;
    std::uint32_t import = 0;
    std::vector<HostValue> arguments;
};

struct HostCompletion {
    bool success = true;
    // Payload of the success or error type; ignored when that type is void.
    HostValue value;

    static HostCompletion ok(HostValue value = {}) { return {true, std::move(value)}; }
    static HostCompletion failed(HostValue value = {}) { return {false, std::move(value)}; }
};

class HostService {
public:
    virtual ~HostService() = default;
    virtual const HostSignature& signature() const = 0;
    virtual HostCompletion invoke(const HostRequest& request) = 0;
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

// Destination for console output. The console service hands each line,
// including its LF, to one write call.
class ConsoleSink {
public:
    virtual ~ConsoleSink() = default;
    // Writes `bytes` and returns true when all of them were written. A sink
    // may write a prefix before returning false.
    virtual bool write(std::string_view bytes) = 0;
};

// Writes to a C stream and flushes after each line.
class StreamSink final : public ConsoleSink {
public:
    explicit StreamSink(std::FILE* stream) : stream_(stream) {}
    bool write(std::string_view bytes) override;

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
