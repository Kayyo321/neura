#pragma once

#include "machine.hpp"

namespace neura::nvm::detail {

// Serves requests from an Execution or Fleet, counting nested requests
// from callbacks in the same member as the original request.
template <class Executor>
class ServiceCallbacks final : public CallbackContext {
public:
    ServiceCallbacks(Executor& executor, const Bindings& bindings, std::uint64_t& requests)
        : executor_(executor), bindings_(bindings), requests_(requests) {}

    HostCompletion serve(const HostRequest& request) {
        ++requests_;
        auto with_callbacks = request;
        with_callbacks.callbacks = this;
        return bindings_.services.at(request.import)->invoke(with_callbacks);
    }

    std::optional<HostValue> call(std::string_view name, std::uint32_t version,
                                  const std::vector<HostValue>& arguments) override {
        return executor_.invoke_callback(name, version, arguments,
                                         [this](const HostRequest& nested) { return serve(nested); });
    }

private:
    Executor& executor_;
    const Bindings& bindings_;
    std::uint64_t& requests_;
};

inline RunResult finished_result(const Execution& execution, const Event& event, std::uint64_t requests = 0) {
    RunResult result{event.kind == Event::Kind::Completed, event.status, event.trap, execution.steps(), requests,
                     execution.peak_heap_cells(), execution.function_steps(), execution.peak_tasks(),
                     execution.peak_queued_messages(), execution.peak_pending_requests()};
    result.task_profile = execution.task_profile();
    for (const auto& site : result.task_profile) {
        result.task_profile_sites.push_back(execution.location(site.function, site.block));
    }
    return result;
}

} // namespace neura::nvm::detail
