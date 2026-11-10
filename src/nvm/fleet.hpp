#pragma once

#include "machine.hpp"

namespace neura::nvm {

struct FleetOptions {
    RunLimits limits;
    GraphDevice* device = nullptr;
    std::uint32_t window = 4096;
};

struct FleetEvent {
    std::size_t member = 0;
    Event event;
    RunResult result;
};

// Independent executions of one image. Graph work is batched, while
// externally visible requests and final events remain in member order.
class Fleet {
public:
    Fleet(const nvr::Image& image, FleetOptions options);
    ~Fleet();
    Fleet(const Fleet&) = delete;
    Fleet& operator=(const Fleet&) = delete;

    std::size_t add(std::uint32_t entry, std::vector<std::string> arguments);
    std::size_t add_call(std::uint32_t function, std::vector<std::uint64_t> arguments);

    std::size_t size() const;
    bool done() const;
    FleetEvent advance();
    void complete(const HostCompletion& completion);
    std::optional<HostValue> invoke_callback(std::string_view name, std::uint32_t version,
                                             const std::vector<HostValue>& arguments,
                                             const Execution::HostHandler& host);
    std::uint64_t rounds() const;

private:
    class State;
    std::unique_ptr<State> state_;
};

using MemberFinished = std::function<void(std::size_t member, const RunResult& result)>;

// Serve host requests and report each final result before serving the
// next member's first request.
void run_fleet(Fleet& fleet, const Bindings& bindings, const MemberFinished& finished);

} // namespace neura::nvm
