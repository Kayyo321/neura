#include "driver.hpp"

#include <stdexcept>

namespace neura::nvm {

std::vector<std::uint8_t> host_modes_for(const nvr::Image& image, const Bindings& bindings) {
    std::vector<std::uint8_t> modes;
    if (!image.transferable_layouts) return modes;
    modes.reserve(bindings.services.size());
    for (const auto* service : bindings.services) {
        modes.push_back(service == nullptr || !service->overlapping() ? 0 : service->cancellable() ? 2 : 1);
    }
    return modes;
}

Event drive(Execution& execution, const Bindings& bindings, TaskClock& clock, DriveHooks& hooks) {
    PendingRequests pending(clock);
    const auto deliver = [&](const Event& at, std::vector<PendingRequests::Done>& done) {
        for (auto& answer : done) {
            if (hooks.delivered) hooks.delivered(at.tick, answer.sequence, answer.cancelled, answer.completion);
            execution.deliver(answer.sequence, answer.completion);
        }
    };
    const auto read_clock = [&](const Event& at) -> std::size_t {
        const auto now = clock.now();
        const auto expired = execution.advance_clock(now);
        if (expired != 0 && hooks.clock_read) hooks.clock_read(at.tick, now);
        return expired;
    };
    for (;;) {
        const auto event = execution.advance();
        switch (event.kind) {
        case Event::Kind::HostRequest: {
            if (!event.overlapping) {
                auto answer = hooks.serve(event);
                const auto& signature = bindings.services.at(event.request.import)->signature();
                pending.apply_service_selection(event.request, signature, answer);
                execution.complete(answer);
                break;
            }
            auto request = event.request;
            request.now = clock.now();
            auto& service = *bindings.services.at(request.import);
            const bool cancellable = service.cancellable();
            if (hooks.issued) hooks.issued(event, cancellable);
            pending.add(request.sequence, service.begin(request), cancellable);
            break;
        }
        case Event::Kind::Poll: {
            auto done = pending.poll();
            deliver(event, done);
            if (event.timer) read_clock(event);
            break;
        }
        case Event::Kind::Waiting: {
            if (pending.empty() && !event.timer) {
                throw std::logic_error("the run waits for a host request this driver never started");
            }
            for (;;) {
                auto done = pending.wait(event.timer ? std::optional(event.deadline) : std::nullopt);
                deliver(event, done);
                const auto expired = event.timer ? read_clock(event) : 0;
                if (!done.empty() || expired != 0) break;
            }
            break;
        }
        case Event::Kind::Cancel:
            pending.cancel(event.sequence);
            break;
        case Event::Kind::Completed:
        case Event::Kind::Trapped:
            return event;
        }
    }
}

} // namespace neura::nvm
