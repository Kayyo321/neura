#include "nvm/machine.hpp"
#include "nvm/provider_internal.hpp"
#include "nvm/tier1.hpp"
#include "nvr/format.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace {

using namespace neura::nvm;
namespace nvr = neura::nvr;

// These records are private to the wasm target and hosts/web/nvm.js.
struct Text {
    const char* bytes = nullptr;
    std::uint32_t size = 0;
};

enum class Signal : std::uint32_t { Yield, Completed, Trapped, HostRequest, Poll, Waiting, Cancel };
enum class Service : std::uint32_t { None, Console, Wall, Monotonic };

struct Report {
    std::uint64_t steps = 0;
    std::uint64_t heap = 0;
    std::uint64_t stack = 0;
    std::uint64_t sequence = 0;
    std::int64_t deadline = nvr::no_deadline;
    Signal signal = Signal::Yield;
    Service service = Service::None;
    Text text;
    std::int32_t status = 0;
    std::uint32_t reason = 0;
    std::uint32_t function = nvr::absent;
    std::uint32_t block = nvr::absent;
};

static_assert(sizeof(Text) == 8 && offsetof(Text, size) == 4);
static_assert(sizeof(Report) == 72 && offsetof(Report, steps) == 0 && offsetof(Report, heap) == 8 &&
              offsetof(Report, stack) == 16 && offsetof(Report, sequence) == 24 && offsetof(Report, deadline) == 32 &&
              offsetof(Report, signal) == 40 && offsetof(Report, service) == 44 && offsetof(Report, text) == 48 &&
              offsetof(Report, status) == 56 && offsetof(Report, reason) == 60 && offsetof(Report, function) == 64 &&
              offsetof(Report, block) == 68);

Text borrow(std::string_view text) {
    return {text.data(), static_cast<std::uint32_t>(text.size())};
}

// ConsoleWriteLine owns the service signature and interpretation of a failed
// write. The browser performs the actual write after the VM yields the request.
class ReplySink final : public ConsoleSink {
public:
    bool accepted = false;
    bool write(std::string_view) override { return accepted; }
};

class Runner {
public:
    Runner(nvr::Image image, std::span<const Text> arguments, unsigned capabilities, unsigned tier,
           std::uint64_t max_steps)
        : image_(std::move(image)), cache_(image_, TierOptions::browser()) {
        HostRegistry registry;
        if ((capabilities & 1U) != 0) registry.add(console_);
        if ((capabilities & 2U) != 0) registry.add(wall_);
        if ((capabilities & 4U) != 0) registry.add(monotonic_);
        bindings_ = bind(image_, registry);
        std::vector<std::string> args;
        args.reserve(arguments.size());
        for (const auto& arg : arguments) args.emplace_back(arg.bytes, arg.size);
        RunLimits limits;
        limits.tier = static_cast<std::uint8_t>(tier);
        limits.jit = neura::jit::Mode::Off;
        limits.tier_cache = &cache_;
        if (max_steps != 0) limits.max_steps = max_steps;
        execution_ = std::make_unique<Execution>(image_, args, limits);
    }

    const Report* advance(std::uint32_t budget) {
        if (is_host_request()) throw std::logic_error("the host request must be answered before advancing");
        event_ = execution_->advance_slice(budget);
        report_ = Report{};
        report_.steps = execution_->steps();
        report_.heap = execution_->peak_heap_cells();
        report_.stack = execution_->peak_stack_cells();
        if (!event_) return &report_;
        const auto& event = *event_;
        switch (event.kind) {
        case Event::Kind::Completed:
            report_.signal = Signal::Completed;
            report_.status = event.status;
            break;
        case Event::Kind::Trapped:
            report_.signal = Signal::Trapped;
            report_.status = exit_status(event.trap);
            report_.reason = event.trap.reason;
            report_.function = event.trap.function;
            report_.block = event.trap.block;
            diagnostic_ = describe(event.trap, image_);
            report_.text = borrow(diagnostic_);
            break;
        case Event::Kind::HostRequest: {
            report_.signal = Signal::HostRequest;
            const auto* service = bindings_.services.at(event.request.import);
            if (service == &console_) {
                report_.service = Service::Console;
                report_.text = borrow(event.request.arguments.at(0).bytes);
            } else {
                report_.service = service == &wall_ ? Service::Wall : Service::Monotonic;
            }
            report_.sequence = event.request.sequence;
            break;
        }
        case Event::Kind::Poll:
            report_.signal = Signal::Poll;
            report_.deadline = event.deadline;
            break;
        case Event::Kind::Waiting:
            report_.signal = Signal::Waiting;
            report_.deadline = event.deadline;
            break;
        case Event::Kind::Cancel:
            report_.signal = Signal::Cancel;
            report_.sequence = event.sequence;
            break;
        }
        return &report_;
    }

    void respond(std::uint32_t error, std::int64_t number, std::int32_t nanos) {
        if (!is_host_request()) throw std::logic_error("there is no host request to answer");
        response_error_ = error;
        response_number_ = number;
        response_nanos_ = nanos;
        sink_.accepted = error == 0;
        const auto& request = event_->request;
        execution_->complete(bindings_.services.at(request.import)->invoke(request));
        event_.reset();
    }

    void advance_clock(std::int64_t now) {
        if (is_host_request()) throw std::logic_error("the clock cannot advance during a host request");
        execution_->advance_clock(now);
    }

    bool has_proof() const { return cache_.proofs_pending(); }

    double prove_one_idle() {
        if (is_host_request()) return -1;
        const auto proof = cache_.prove_one_idle();
        return proof ? std::chrono::duration<double, std::milli>(*proof).count() : -1;
    }

private:
    bool is_host_request() const { return event_ && event_->kind == Event::Kind::HostRequest; }

    nvr::Image image_;
    TierCache cache_;
    ReplySink sink_;
    ConsoleWriteLine console_{sink_};
    FunctionService wall_{
        provider::signature("neura.clock.wall", {}, provider::tuple({provider::int64(), provider::int32()})),
        [this](const HostRequest&) {
            return response_error_ ? provider::fail(response_error_)
                : HostCompletion::ok(provider::tuple_value(
                    {provider::int64_value(response_number_), provider::int32_value(response_nanos_)}));
        }};
    FunctionService monotonic_{provider::signature("neura.clock.monotonic", {}, provider::int64()),
        [this](const HostRequest&) {
            return response_error_ ? provider::fail(response_error_)
                : HostCompletion::ok(provider::int64_value(response_number_));
        }};
    Bindings bindings_;
    std::unique_ptr<Execution> execution_;
    std::optional<Event> event_;
    Report report_;
    std::string diagnostic_;
    std::uint32_t response_error_ = 0;
    std::int64_t response_number_ = 0;
    std::int32_t response_nanos_ = 0;
};

// JS reads this synchronously after a failing call. Storing an error must
// work when linear memory is exhausted too.
std::array<char, 1024> last_error;

void report_error(const std::exception& error) {
    const std::string_view message = error.what();
    const auto length = std::min(message.size(), last_error.size() - 1);
    std::memcpy(last_error.data(), message.data(), length);
    last_error[length] = '\0';
}

} // namespace

extern "C" const char* neura_wasm_error() { return last_error.data(); }

extern "C" Runner* neura_wasm_open(const std::uint8_t* bytes, std::size_t size, const Text* args,
                                   std::uint32_t argc, unsigned capabilities, unsigned tier,
                                   std::uint64_t max_steps) try {
    if (tier > 2) throw std::invalid_argument("the browser requires a tier from 0 to 2");
    return new Runner(nvr::load({bytes, size}), {args, argc}, capabilities, tier, max_steps);
} catch (const std::exception& error) {
    report_error(error);
    return nullptr;
}

extern "C" void neura_wasm_close(Runner* runner) { delete runner; }

extern "C" const Report* neura_wasm_advance(Runner* runner, std::uint32_t budget) try {
    return runner->advance(budget);
} catch (const std::exception& error) {
    report_error(error);
    return nullptr;
}

extern "C" bool neura_wasm_respond(Runner* runner, std::uint32_t error, std::int64_t number,
                                   std::int32_t nanos) try {
    runner->respond(error, number, nanos);
    return true;
} catch (const std::exception& failure) {
    report_error(failure);
    return false;
}

extern "C" bool neura_wasm_clock(Runner* runner, std::int64_t now) try {
    runner->advance_clock(now);
    return true;
} catch (const std::exception& error) {
    report_error(error);
    return false;
}

extern "C" bool neura_wasm_has_proof(Runner* runner) { return runner->has_proof(); }

extern "C" double neura_wasm_prove_idle(Runner* runner) try {
    return runner->prove_one_idle();
} catch (const std::exception& error) {
    report_error(error);
    return -2;
}
