#include "fleet.hpp"

#include "graph_device.hpp"
#include "run_support.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <variant>

namespace neura::nvm {

class Fleet::State {
public:
    State(const nvr::Image& image, FleetOptions options) : image_(image), options_(options) {
        if (options_.window == 0) throw std::invalid_argument("a fleet's window must be positive");
        if (options_.limits.graph_device) throw std::invalid_argument("a fleet's device is given in FleetOptions");
        if (options_.device && &options_.device->image() != &image_) {
            throw std::invalid_argument("graph device was created for another image");
        }
    }

    std::size_t add(std::uint32_t entry, std::vector<std::string> arguments) {
        require_unstarted();
        Execution::validate_entry_arguments(image_, entry, arguments);
        members_.push_back({entry, std::move(arguments)});
        return members_.size() - 1;
    }

    std::size_t add_call(std::uint32_t function, std::vector<std::uint64_t> arguments) {
        require_unstarted();
        Execution::validate_call_arguments(image_, function, arguments);
        members_.push_back({function, std::move(arguments)});
        return members_.size() - 1;
    }

    std::size_t size() const { return members_.size(); }
    bool done() const { return head_ == members_.size(); }
    std::uint64_t rounds() const { return rounds_; }

    FleetEvent advance() {
        started_ = true;
        if (done()) throw std::logic_error("advance() called on a finished fleet");
        if (awaiting_) throw std::logic_error("advance() called while a host request is pending");
        for (;;) {
            auto& state = members_[head_].state;
            if (auto* final = std::get_if<Finished>(&state)) {
                auto event = std::move(**final);
                state = std::monostate{};
                ++head_;
                return event;
            }
            if (!active_.empty() && active_.front().request) {
                // Members start in order. Once finished heads are reported,
                // the first active member is the head.
                auto request = std::move(active_.front().request);
                awaiting_ = true;
                return {head_, std::move(*request), {}};
            }
            const auto width = options_.device
                ? std::min<std::size_t>(options_.window, std::max<std::size_t>(1, options_.device->batch_hint()))
                : options_.window;
            admit(width);
            if (!options_.device && next_waiting_ == members_.size()) {
                reference_rounds();
            } else {
                round(std::min(width, active_.size()));
                ++rounds_;
            }
        }
    }

    void complete(const HostCompletion& completion) {
        if (!awaiting_) throw std::logic_error("complete() called without a pending host request");
        active_.front().execution->complete(completion);
        awaiting_ = false;
    }

    std::optional<HostValue> invoke_callback(std::string_view name, std::uint32_t version,
                                             const std::vector<HostValue>& arguments,
                                             const Execution::HostHandler& host) {
        if (!awaiting_) throw std::logic_error("invoke_callback() called without a pending host request");
        return active_.front().execution->invoke_callback(name, version, arguments, host);
    }

private:
    // A live member with a stored request is Parked. Once the request has
    // been returned, the head is Awaiting until its completion.
    struct Active {
        std::size_t member;
        std::unique_ptr<Execution> execution;
        std::unique_ptr<Event> request;
    };

    using Finished = std::unique_ptr<FleetEvent>;

    struct Member {
        std::uint32_t function;
        // Once admitted, a member's live state is in active_. A reported
        // member also has an empty slot and is below head_.
        std::variant<std::vector<std::string>, std::vector<std::uint64_t>, Finished, std::monostate> state;
    };

    struct PendingGraph {
        Active* member;
        Execution::GraphStep step;
    };

    void require_unstarted() const {
        if (started_) throw std::logic_error("cannot add a member after advancing the fleet");
    }

    void admit(std::size_t width) {
        if (active_.size() >= width || next_waiting_ == members_.size()) return;
        const auto limit = options_.limits.max_heap_cells;
        std::uint64_t live_cells = 0;
        for (const auto& live : active_) {
            live_cells += std::min(limit - live_cells, live.execution->admission_heap_cells());
            if (live_cells == limit) break;
        }

        while (active_.size() < width && next_waiting_ < members_.size() &&
               (next_waiting_ == head_ || live_cells < limit)) {
            auto& member = members_[next_waiting_];
            std::unique_ptr<Execution> started;
            if (const auto* arguments = std::get_if<std::vector<std::string>>(&member.state)) {
                started = std::make_unique<Execution>(image_, member.function, *arguments, options_.limits);
            } else {
                started = std::make_unique<Execution>(image_, member.function,
                    std::get<std::vector<std::uint64_t>>(member.state), options_.limits);
            }
            // Charge argument and constant allocation before admitting the
            // next member. A startup trap is returned by the first step.
            started->initialize();
            live_cells += std::min(limit - live_cells, started->admission_heap_cells());
            active_.push_back({next_waiting_, std::move(started), nullptr});
            member.state = std::monostate{};
            ++next_waiting_;
        }
    }

    bool settle(Active& live, std::optional<Event>&& event) {
        if (!event) return false;
        if (event->kind == Event::Kind::HostRequest) {
            live.request = std::make_unique<Event>(std::move(*event));
            return false;
        }
        auto result = detail::finished_result(*live.execution, *event);
        members_[live.member].state = std::make_unique<FleetEvent>(
            FleetEvent{live.member, std::move(*event), std::move(result)});
        live.execution.reset();
        return true;
    }

    std::optional<Event> reference_step(Execution& run, const Execution::GraphStep& step) {
        reference_outputs_.resize(step.output_count);
        if (const auto node = evaluate_outputs(image_.graphs[step.graph], step.inputs, scratch_, reference_outputs_)) {
            return run.fail_step(*node);
        }
        return run.finish_step(reference_outputs_);
    }

    bool dispatch() {
        std::size_t output_count = 0;
        for (const auto& pending : pending_graphs_) {
            if (pending.step.output_count > arena_.max_size() - output_count) {
                throw std::length_error("fleet graph output arena exceeds storage limits");
            }
            output_count += pending.step.output_count;
        }
        arena_.resize(output_count);
        auto outputs = std::span(arena_);
        jobs_.clear();
        jobs_.reserve(pending_graphs_.size());
        for (const auto& pending : pending_graphs_) {
            const auto& step = pending.step;
            jobs_.push_back({step.graph, step.inputs, outputs.first(step.output_count)});
            outputs = outputs.subspan(step.output_count);
        }
        options_.device->evaluate(jobs_);

        bool finished = false;
        for (std::size_t i = 0; i < pending_graphs_.size(); ++i) {
            const auto& pending = pending_graphs_[i];
            const auto& job = jobs_[i];
            auto& run = *pending.member->execution;
            auto event = job.evaluated ? run.finish_step(job.outputs) : reference_step(run, pending.step);
            finished |= settle(*pending.member, std::move(event));
        }
        return finished;
    }

    void reference_rounds() {
        // With no device or admissions, consecutive rounds commute across
        // members. Run the head to its next event and give every other
        // member exactly the rounds it would get before that event.
        auto& head = active_.front();
        auto [event, count] = head.execution->advance_reference_rounds(
            scratch_, std::numeric_limits<std::uint64_t>::max());
        bool finished = settle(head, std::move(event));
        for (auto& live : std::span(active_).subspan(1)) {
            if (!live.request) {
                finished |= settle(live, live.execution->advance_reference_rounds(scratch_, count).event);
            }
        }
        rounds_ += count;
        if (finished) remove_finished();
    }

    void remove_finished() {
        std::erase_if(active_, [](const Active& live) { return !live.execution; });
    }

    void round(std::size_t width) {
        bool finished = false;
        pending_graphs_.clear();
        for (auto& live : std::span(active_).first(width)) {
            if (live.request) continue;
            if (!options_.device) {
                // Use the same step driver as Execution with the fleet's
                // scratch and write directly to the member's output lanes.
                finished |= settle(live, live.execution->step_with_scratch(scratch_));
                continue;
            }
            Execution::GraphStep step;
            if (auto event = live.execution->begin_step(step)) {
                finished |= settle(live, std::move(event));
            } else if (image_.graphs[step.graph].nodes.empty()) {
                finished |= settle(live, reference_step(*live.execution, step));
            } else {
                pending_graphs_.push_back({&live, step});
            }
        }
        if (!pending_graphs_.empty()) finished |= dispatch();
        if (finished) remove_finished();
    }

    const nvr::Image& image_;
    FleetOptions options_;
    std::vector<Member> members_;
    // Started, unfinished members, in index order. Keep them separate from
    // stored final results so later rounds can skip those in constant time.
    std::vector<Active> active_;
    std::vector<PendingGraph> pending_graphs_;
    std::vector<GraphJob> jobs_;
    std::vector<std::int64_t> scratch_;
    std::vector<std::int64_t> reference_outputs_;
    std::vector<std::int64_t> arena_;
    std::size_t head_ = 0;
    std::size_t next_waiting_ = 0;
    std::uint64_t rounds_ = 0;
    bool started_ = false;
    bool awaiting_ = false;
};

Fleet::Fleet(const nvr::Image& image, FleetOptions options) : state_(std::make_unique<State>(image, options)) {}
Fleet::~Fleet() = default;

std::size_t Fleet::add(std::uint32_t entry, std::vector<std::string> arguments) {
    return state_->add(entry, std::move(arguments));
}

std::size_t Fleet::add_call(std::uint32_t function, std::vector<std::uint64_t> arguments) {
    return state_->add_call(function, std::move(arguments));
}

std::size_t Fleet::size() const { return state_->size(); }
bool Fleet::done() const { return state_->done(); }
FleetEvent Fleet::advance() { return state_->advance(); }
void Fleet::complete(const HostCompletion& completion) { state_->complete(completion); }
std::uint64_t Fleet::rounds() const { return state_->rounds(); }

std::optional<HostValue> Fleet::invoke_callback(std::string_view name, std::uint32_t version,
                                               const std::vector<HostValue>& arguments,
                                               const Execution::HostHandler& host) {
    return state_->invoke_callback(name, version, arguments, host);
}

void run_fleet(Fleet& fleet, const Bindings& bindings, const MemberFinished& finished) {
    std::uint64_t requests = 0;
    detail::ServiceCallbacks callbacks(fleet, bindings, requests);
    while (!fleet.done()) {
        auto event = fleet.advance();
        if (event.event.kind == Event::Kind::HostRequest) {
            fleet.complete(callbacks.serve(event.event.request));
        } else {
            event.result.host_requests = std::exchange(requests, 0);
            finished(event.member, event.result);
        }
    }
}

} // namespace neura::nvm
