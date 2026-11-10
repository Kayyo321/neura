#include "fleet.hpp"

#include "graph_device.hpp"
#include "run_support.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <new>
#include <ranges>
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
            if (!active_members().empty() && active_members().front().request) {
                // Members start in order. Once finished heads are reported,
                // the first active member is the head.
                auto request = std::move(active_members().front().request);
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
                round(std::min(width, active_members().size()));
                ++rounds_;
            }
        }
    }

    void complete(const HostCompletion& completion) {
        if (!awaiting_) throw std::logic_error("complete() called without a pending host request");
        active_members().front().execution->complete(completion);
        awaiting_ = false;
        ++runnable_count_;
    }

    std::optional<HostValue> invoke_callback(std::string_view name, std::uint32_t version,
                                             const std::vector<HostValue>& arguments,
                                             const Execution::HostHandler& host) {
        if (!awaiting_) throw std::logic_error("invoke_callback() called without a pending host request");
        return active_members().front().execution->invoke_callback(name, version, arguments, host);
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

    void require_unstarted() const {
        if (started_) throw std::logic_error("cannot add a member after advancing the fleet");
    }

    std::span<Active> active_members() { return std::span(active_).subspan(first_active_); }

    void admit(std::size_t width) {
        if (active_members().size() >= width || next_waiting_ == members_.size()) return;
        const auto limit = options_.limits.max_heap_cells;
        while (active_members().size() < width && next_waiting_ < members_.size() &&
               (next_waiting_ == head_ || heap_cells_.below(limit))) {
            if (first_active_ != 0 && first_active_ >= active_members().size()) {
                // Amortize compaction of members removed from the head.
                active_.erase(active_.begin(), active_.begin() + static_cast<std::ptrdiff_t>(first_active_));
                first_active_ = 0;
            }
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
            started->initialize(heap_cells_);
            active_.push_back({next_waiting_, std::move(started), nullptr});
            member.state = std::monostate{};
            ++next_waiting_;
            ++runnable_count_;
        }
    }

    bool settle(Active& live, std::optional<Event>&& event) {
        if (!event) return false;
        if (event->kind == Event::Kind::HostRequest) {
            live.request = std::make_unique<Event>(std::move(*event));
            --runnable_count_;
            return false;
        }
        auto result = detail::finished_result(*live.execution, *event);
        members_[live.member].state = std::make_unique<FleetEvent>(
            FleetEvent{live.member, std::move(*event), std::move(result)});
        live.execution.reset();
        --runnable_count_;
        return true;
    }

    // Returns the jobs for a nonempty subset of the round, or an empty span
    // when memory for them cannot be allocated. In that case the caller sends
    // nothing to the device and every job of the round is evaluated by
    // reference. Nothing is evaluated or marked by this function.
    template <class Indices>
    std::span<GraphJob> materialize_jobs(const Indices& indices) {
        try {
            std::size_t output_count = 0;
            for (const auto index : indices) {
                const auto count = image_.graphs[graph_ids_[index]].outputs.size();
                if (count > arena_.max_size() - output_count) {
                    throw std::length_error("fleet graph output arena exceeds storage limits");
                }
                output_count += count;
            }
            if (arena_.size() < output_count) arena_.resize(output_count);
            auto outputs = std::span(arena_).first(output_count);
            jobs_.reserve(std::ranges::size(indices));
            for (const auto index : indices) {
                const auto graph = graph_ids_[index];
                const auto count = image_.graphs[graph].outputs.size();
                jobs_.push_back({graph, pending_members_[index]->execution->prepared_inputs(), outputs.first(count)});
                outputs = outputs.subspan(count);
            }
            return jobs_;
        } catch (const std::bad_alloc&) {
        } catch (const std::length_error&) {
        }
        jobs_.clear();
        return {};
    }

    std::size_t dispatch() {
        jobs_.clear();
        selected_.clear();
        const bool prepared = options_.device->evaluate_prepared(graph_ids_,
            [&](std::span<const std::size_t> indices) {
                if (indices.empty() || !selected_.empty() || indices.back() >= pending_members_.size() ||
                    std::ranges::adjacent_find(indices, std::greater_equal{}) != indices.end()) {
                    throw std::logic_error("graph device selected invalid prepared jobs");
                }
                std::span<GraphJob> jobs;
                try {
                    selected_.assign(indices.begin(), indices.end());
                    jobs = materialize_jobs(indices);
                } catch (const std::bad_alloc&) {
                    jobs_.clear();
                } catch (const std::length_error&) {
                    jobs_.clear();
                }
                // An empty result declines the whole batch to the reference evaluator.
                if (jobs.empty()) selected_.clear();
                return jobs;
            });
        if (!prepared) {
            if (!selected_.empty()) throw std::logic_error("graph device did not finish a prepared batch");
            const auto jobs = materialize_jobs(std::views::iota(std::size_t{0}, pending_members_.size()));
            if (!jobs.empty()) options_.device->evaluate(jobs);
        }

        std::size_t finished = 0;
        std::size_t selected = 0;
        for (std::size_t i = 0; i < pending_members_.size(); ++i) {
            auto& live = *pending_members_[i];
            const GraphJob* job = nullptr;
            if (!prepared) job = jobs_.empty() ? nullptr : &jobs_[i];
            else if (selected < selected_.size() && selected_[selected] == i) job = &jobs_[selected++];
            auto& run = *live.execution;
            auto event = job && job->evaluated ? run.finish_step(job->outputs)
                                               : run.finish_prepared_reference_step(scratch_);
            finished += settle(live, std::move(event));
        }
        return finished;
    }

    void reference_rounds() {
        // With no device or admissions, consecutive rounds commute across
        // members. Run the head to its next event and give every other
        // member exactly the rounds it would get before that event.
        const auto runnable = runnable_count_;
        auto& head = active_members().front();
        auto [event, count] = head.execution->advance_reference_rounds(
            scratch_, std::numeric_limits<std::uint64_t>::max());
        std::size_t finished = settle(head, std::move(event));
        if (runnable > 1) {
            for (auto& live : active_members().subspan(1)) {
                if (!live.request) {
                    finished += settle(live, live.execution->advance_reference_rounds(scratch_, count).event);
                }
            }
        }
        rounds_ += count;
        if (finished) remove_finished(finished);
    }

    void remove_finished(std::size_t count) {
        while (count != 0 && !active_[first_active_].execution) {
            ++first_active_;
            --count;
        }
        if (first_active_ == active_.size()) {
            active_.clear();
            first_active_ = 0;
        } else if (count != 0) {
            const auto first = active_.begin() + static_cast<std::ptrdiff_t>(first_active_);
            active_.erase(std::remove_if(first, active_.end(), [](const Active& live) { return !live.execution; }),
                          active_.end());
        }
    }

    void round(std::size_t width) {
        std::size_t finished = 0;
        pending_members_.clear();
        graph_ids_.clear();
        // Only the head can be runnable when every other member is parked.
        const auto runnable_span = active_members().first(runnable_count_ == 1 ? 1 : width);
        for (auto& live : runnable_span) {
            if (live.request) continue;
            if (!options_.device) {
                // Use the same step driver as Execution with the fleet's
                // scratch and write directly to the member's output lanes.
                finished += settle(live, live.execution->step_with_scratch(scratch_));
                continue;
            }
            std::uint32_t graph = 0;
            if (auto event = live.execution->begin_prepared_step(graph)) {
                finished += settle(live, std::move(event));
            } else if (image_.graphs[graph].nodes.empty()) {
                finished += settle(live, live.execution->finish_prepared_reference_step(scratch_));
            } else {
                pending_members_.push_back(&live);
                graph_ids_.push_back(graph);
            }
        }
        if (!pending_members_.empty()) finished += dispatch();
        if (finished) remove_finished(finished);
    }

    const nvr::Image& image_;
    FleetOptions options_;
    std::vector<Member> members_;
    // Must outlive every admitted execution that reports heap mutations.
    Execution::FleetHeapCells heap_cells_;
    // Started, unfinished members, in index order. Keep them separate from
    // stored final results so later rounds can skip those in constant time.
    std::vector<Active> active_;
    std::size_t first_active_ = 0;
    std::size_t runnable_count_ = 0;
    std::vector<Active*> pending_members_;
    std::vector<std::uint32_t> graph_ids_;
    std::vector<std::size_t> selected_;
    std::vector<GraphJob> jobs_;
    std::vector<std::int64_t> scratch_;
    std::vector<std::int64_t> arena_;
    std::size_t head_ = 0;
    std::size_t next_waiting_ = 0;
    std::uint64_t rounds_ = 0;
    bool started_ = false;
    bool awaiting_ = false;
};

namespace {

// Members are created as they are admitted, so a setting no member could
// run with is rejected here, before any of them starts.
const FleetOptions& checked(const nvr::Image& image, const FleetOptions& options) {
    if (image.transferable_layouts && options.limits.task_quantum == 0) {
        throw std::invalid_argument("task quantum must be positive");
    }
    return options;
}

} // namespace

Fleet::Fleet(const nvr::Image& image, FleetOptions options)
    : state_(std::make_unique<State>(image, checked(image, options))) {}
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
