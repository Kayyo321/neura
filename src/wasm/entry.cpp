#include "nvm/machine.hpp"
#include "nvm/provider_internal.hpp"
#include "nvm/tier1.hpp"
#include "nvr/format.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <memory>
#include <set>
#include <string_view>

// The browser adapter interface (docs/runtime/Providers.md, "Browser adapter
// interface"). hosts/web/nvm.js checks neura_wasm_interface() before it
// calls anything else, and rejects a module whose version it does not know.

namespace {

using namespace neura::nvm;
namespace nvr = neura::nvr;
namespace provider = neura::nvm::provider;

constexpr std::uint32_t interface_version = 1;

// These records are private to the wasm target and hosts/web/nvm.js.
struct Text {
    const char* bytes = nullptr;
    std::uint32_t size = 0;
};

enum class Signal : std::uint32_t { Yield, Completed, Trapped, HostRequest, Poll, Waiting, Cancel };

enum Flag : std::uint32_t { Overlapping = 1, Cancellable = 2, Timer = 4 };

struct Report {
    std::uint64_t steps = 0;
    std::uint64_t heap = 0;
    std::uint64_t stack = 0;
    std::uint64_t sequence = 0;
    std::int64_t deadline = nvr::no_deadline;
    Signal signal = Signal::Yield;
    std::uint32_t flags = 0;
    // Trapped: the diagnostic. HostRequest: the encoded arguments.
    Text text;
    std::int32_t status = 0;
    std::uint32_t reason = 0;
    std::uint32_t function = nvr::absent;
    std::uint32_t block = nvr::absent;
    // HostRequest: the service name and interface version.
    Text service;
    std::uint32_t version = 0;
    std::uint32_t reserved = 0;
};

static_assert(sizeof(Text) == 8 && offsetof(Text, size) == 4);
static_assert(sizeof(Report) == 88 && offsetof(Report, steps) == 0 && offsetof(Report, heap) == 8 &&
              offsetof(Report, stack) == 16 && offsetof(Report, sequence) == 24 && offsetof(Report, deadline) == 32 &&
              offsetof(Report, signal) == 40 && offsetof(Report, flags) == 44 && offsetof(Report, text) == 48 &&
              offsetof(Report, status) == 56 && offsetof(Report, reason) == 60 && offsetof(Report, function) == 64 &&
              offsetof(Report, block) == 68 && offsetof(Report, service) == 72 && offsetof(Report, version) == 80);

Text borrow(std::string_view text) {
    return {text.data(), static_cast<std::uint32_t>(text.size())};
}

// The value wire: a host value is its 64 bits, its byte count and bytes, then
// its element count and elements, all little-endian. A completion is one
// byte, 1 for success and 0 for an error, then the value of that type.
void put32(std::string& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
}

void encode(std::string& out, const HostValue& value) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((value.bits >> (8 * i)) & 0xff));
    if (value.bytes.size() > UINT32_MAX || value.elements.size() > UINT32_MAX) {
        throw std::length_error("a host value exceeds the browser wire");
    }
    put32(out, static_cast<std::uint32_t>(value.bytes.size()));
    out += value.bytes;
    put32(out, static_cast<std::uint32_t>(value.elements.size()));
    for (const auto& element : value.elements) encode(out, element);
}

class Reader {
public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}

    std::uint64_t take(std::size_t width) {
        if (bytes_.size() - at_ < width) throw std::invalid_argument("a host reply is truncated");
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < width; ++i) {
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes_[at_ + i])) << (8 * i);
        }
        at_ += width;
        return value;
    }

    HostValue value(unsigned depth = 0) {
        if (depth > 64) throw std::invalid_argument("a host reply is nested too deeply");
        HostValue result;
        result.bits = take(8);
        const auto size = take(4);
        if (bytes_.size() - at_ < size) throw std::invalid_argument("a host reply is truncated");
        result.bytes.assign(bytes_.substr(at_, size));
        at_ += size;
        const auto count = take(4);
        // Each element takes at least 16 bytes, which bounds the reservation.
        if (count > (bytes_.size() - at_) / 16) throw std::invalid_argument("a host reply is truncated");
        result.elements.reserve(count);
        for (std::uint64_t i = 0; i < count; ++i) result.elements.push_back(value(depth + 1));
        return result;
    }

    void finish() const {
        if (at_ != bytes_.size()) throw std::invalid_argument("a host reply has trailing bytes");
    }

private:
    std::string_view bytes_;
    std::size_t at_ = 0;
};

HostCompletion decode_completion(std::string_view bytes) {
    Reader reader(bytes);
    const auto success = reader.take(1);
    if (success > 1) throw std::invalid_argument("a host reply has an invalid status byte");
    auto value = reader.value();
    reader.finish();
    return success ? HostCompletion::ok(std::move(value)) : HostCompletion::failed(std::move(value));
}

// How a browser service is served. The adapter answers a forwarded service;
// the runtime answers the others itself.
enum class Served { Forwarded, Exit, Denied };

struct BrowserService {
    HostSignature signature;
    bool overlapping = false;
    bool cancellable = false;
};

// The services this runtime can bind (docs/runtime/Providers.md, "Browser
// services"). A service the embedder does not serve still binds and answers
// error 14, so an image that reaches it runs and fails locally.
std::vector<BrowserService> browser_services() {
    using namespace provider;
    const auto ui = [](std::string name, std::vector<HostType> parameters, HostType success) {
        return signature(std::move(name), std::move(parameters), std::move(success));
    };
    const auto v2 = [](HostSignature signature) {
        signature.version = 2;
        return signature;
    };
    const auto sleep = signature("neura.clock.sleep", {int64()}, void_type());
    return {
        {signature("neura.console.write_line", {text()}, void_type())},
        {signature("neura.clock.wall", {}, tuple({int64(), int32()}))},
        {signature("neura.clock.monotonic", {}, int64())},
        {sleep, true, false},
        {v2(sleep), true, true},
        {signature("neura.entropy.fill", {int32()}, byte_list())},
        {ui("neura.ui.root.open", {text()}, int64())},
        {ui("neura.ui.root.close", {int64()}, void_type())},
        {ui("neura.ui.template", {int64(), text()}, int64())},
        {ui("neura.ui.commit", {int64(), byte_list()}, void_type())},
        {ui("neura.ui.event.next", {int64(), int32()}, byte_list()), true, true},
        {ui("neura.ui.stats", {int64()}, tuple({int64(), int64(), int64(), int64()}))},
        {ui("neura.history.current", {}, text())},
        {ui("neura.history.push", {text()}, text())},
        {ui("neura.history.replace", {text()}, text())},
        {ui("neura.history.next", {}, text()), true, true},
    };
}

// Standard services of other capability groups. The browser never grants
// them; each binds with the image's own parameter and success types and
// answers error 14. Its error type must still be uint32.
bool denied_everywhere(std::string_view name) {
    static const std::set<std::string_view> names{
        "neura.console.write", "neura.console.read", "neura.env.get", "neura.http.fetch", "neura.bridge.call"};
    static constexpr std::array<std::string_view, 7> groups{
        "neura.fs.", "neura.net.", "neura.ffi.", "neura.sql.", "neura.app.", "neura.desktop.", "neura.http.server."};
    if (names.contains(name)) return true;
    return std::any_of(groups.begin(), groups.end(), [&](std::string_view group) { return name.starts_with(group); });
}

HostType host_type(const nvr::Image& image, std::uint32_t index, unsigned depth = 0) {
    if (depth > 64 || index >= image.types.size()) throw BindError("an import type is not valid");
    const auto& type = image.types[index];
    HostType result(type.kind, type.width);
    if (type.kind == nvr::TypeKind::Object) {
        const auto& layout = image.layouts.at(type.reference);
        for (const auto field : layout.fields) result.components.push_back(host_type(image, field, depth + 1));
    } else if (type.kind == nvr::TypeKind::Array || type.kind == nvr::TypeKind::List) {
        result.components.push_back(host_type(image, type.reference, depth + 1));
    }
    return result;
}

class Service final : public HostService {
public:
    Service(BrowserService service, Served served)
        : signature_(std::move(service.signature)), served_(served), overlapping_(service.overlapping),
          cancellable_(service.cancellable) {}
    const HostSignature& signature() const override { return signature_; }
    HostCompletion invoke(const HostRequest&) override { throw std::logic_error("browser services are not invoked"); }
    bool overlapping() const override { return overlapping_; }
    bool cancellable() const override { return cancellable_; }
    Served served() const { return served_; }

private:
    HostSignature signature_;
    Served served_;
    bool overlapping_;
    bool cancellable_;
};

class Runner {
public:
    Runner(nvr::Image image, std::span<const Text> arguments, std::string_view granted, unsigned tier,
           std::uint64_t max_steps)
        : image_(std::move(image)), cache_(image_, TierOptions::browser()) {
        std::set<std::string, std::less<>> grants;
        for (std::size_t start = 0; start < granted.size();) {
            const auto end = std::min(granted.find('\n', start), granted.size());
            grants.emplace(granted.substr(start, end - start));
            start = end + 1;
        }
        HostRegistry registry;
        const auto add = [&](BrowserService service, Served served) {
            services_.push_back(std::make_unique<Service>(std::move(service), served));
            registry.add(*services_.back());
        };
        for (auto& service : browser_services()) {
            const auto key = service.signature.name + "/" + std::to_string(service.signature.version);
            const auto served = grants.erase(key) != 0 ? Served::Forwarded : Served::Denied;
            add(std::move(service), served);
        }
        add({provider::signature("neura.process.exit", {provider::int32()}, provider::void_type())}, Served::Exit);
        if (!grants.empty()) throw std::invalid_argument("the browser has no service " + *grants.begin());
        for (const auto& import : image_.imports) {
            if (registry.find(import.name, import.interface_version) || !denied_everywhere(import.name)) continue;
            BrowserService service;
            service.signature.name = import.name;
            service.signature.version = import.interface_version;
            for (const auto parameter : import.parameter_types) {
                service.signature.parameters.push_back(host_type(image_, parameter));
            }
            service.signature.success = host_type(image_, import.success_type);
            service.signature.error = provider::uint32();
            add(std::move(service), Served::Denied);
        }
        bindings_ = bind(image_, registry);
        // The same modes as host_modes_for (src/nvm/driver.cpp), which the
        // browser build does not link.
        if (image_.transferable_layouts) {
            for (const auto* bound : bindings_.services) {
                modes_.push_back(!bound->overlapping() ? 0 : bound->cancellable() ? 2 : 1);
            }
        }
        std::vector<std::string> args;
        args.reserve(arguments.size());
        for (const auto& arg : arguments) args.emplace_back(arg.bytes, arg.size);
        RunLimits limits;
        limits.tier = static_cast<std::uint8_t>(tier);
        limits.jit = neura::jit::Mode::Off;
        limits.tier_cache = &cache_;
        limits.host_modes = &modes_;
        if (max_steps != 0) limits.max_steps = max_steps;
        execution_ = std::make_unique<Execution>(image_, args, limits);
    }

    const Report* advance(std::uint32_t budget) {
        if (is_host_request()) throw std::logic_error("the host request must be answered before advancing");
        event_ = execution_->advance_slice(budget);
        report_ = Report{};
        if (event_) describe_event();
        report_.steps = execution_->steps();
        report_.heap = execution_->peak_heap_cells();
        report_.stack = execution_->peak_stack_cells();
        return &report_;
    }

    void respond(std::string_view reply) {
        if (!is_host_request() || event_->overlapping) throw std::logic_error("there is no host request to answer");
        auto completion = decode_completion(reply);
        event_.reset();
        execution_->complete(completion);
    }

    void deliver(std::uint64_t sequence, std::string_view reply) {
        if (!at_scheduling_point()) throw std::logic_error("a reply is delivered only after a poll or wait");
        execution_->deliver(sequence, decode_completion(reply));
    }

    std::uint64_t advance_clock(std::int64_t now) {
        if (!at_scheduling_point()) throw std::logic_error("the clock advances only after a poll or wait");
        return execution_->advance_clock(now);
    }

    bool has_proof() const { return cache_.proofs_pending(); }

    double prove_one_idle() {
        if (is_host_request()) return -1;
        const auto proof = cache_.prove_one_idle();
        return proof ? std::chrono::duration<double, std::milli>(*proof).count() : -1;
    }

private:
    bool is_host_request() const { return event_ && event_->kind == Event::Kind::HostRequest && !event_->overlapping; }
    bool at_scheduling_point() const {
        return event_ && (event_->kind == Event::Kind::Poll || event_->kind == Event::Kind::Waiting);
    }

    void describe_event() {
        auto& event = *event_;
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
            const auto& service = static_cast<const Service&>(*bindings_.services.at(event.request.import));
            if (service.served() != Served::Forwarded) {
                serve_locally(service, event);
                return;
            }
            report_.signal = Signal::HostRequest;
            report_.sequence = event.request.sequence;
            report_.flags = (event.overlapping ? Overlapping : 0) | (event.overlapping && service.cancellable() ? Cancellable : 0);
            arguments_.clear();
            for (const auto& argument : event.request.arguments) encode(arguments_, argument);
            report_.text = borrow(arguments_);
            report_.service = borrow(service.signature().name);
            report_.version = service.signature().version;
            break;
        }
        case Event::Kind::Poll:
        case Event::Kind::Waiting: {
            // Answers the runtime gave itself are delivered first, in the
            // order their requests were issued.
            const bool delivered = !local_.empty();
            while (!local_.empty()) {
                execution_->deliver(local_.front().first, local_.front().second);
                local_.pop_front();
            }
            if (event.kind == Event::Kind::Waiting && delivered) {
                event_.reset();
                return;
            }
            report_.signal = event.kind == Event::Kind::Poll ? Signal::Poll : Signal::Waiting;
            report_.flags = event.timer ? Timer : 0;
            report_.deadline = event.deadline;
            break;
        }
        case Event::Kind::Cancel:
            // A request answered by the runtime is already settled.
            if (std::any_of(local_.begin(), local_.end(), [&](const auto& entry) { return entry.first == event.sequence; })) {
                event_.reset();
                return;
            }
            report_.signal = Signal::Cancel;
            report_.sequence = event.sequence;
            break;
        }
    }

    // Exit and denied services are answered here and reported as a yield.
    void serve_locally(const Service& service, const Event& event) {
        auto completion = service.served() == Served::Exit
            ? HostCompletion::terminate(provider::as_int32(event.request.arguments.at(0)))
            : provider::denied();
        if (event.overlapping) {
            local_.emplace_back(event.request.sequence, std::move(completion));
        } else {
            execution_->complete(completion);
        }
        event_.reset();
    }

    nvr::Image image_;
    TierCache cache_;
    std::vector<std::unique_ptr<Service>> services_;
    Bindings bindings_;
    std::vector<std::uint8_t> modes_;
    std::unique_ptr<Execution> execution_;
    std::optional<Event> event_;
    std::deque<std::pair<std::uint64_t, HostCompletion>> local_;
    Report report_;
    std::string diagnostic_;
    std::string arguments_;
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

extern "C" std::uint32_t neura_wasm_interface() { return interface_version; }

extern "C" const char* neura_wasm_error() { return last_error.data(); }

extern "C" Runner* neura_wasm_open(const std::uint8_t* bytes, std::size_t size, const Text* args,
                                   std::uint32_t argc, const char* granted, std::size_t granted_size,
                                   unsigned tier, std::uint64_t max_steps) try {
    if (tier > 2) throw std::invalid_argument("the browser requires a tier from 0 to 2");
    return new Runner(nvr::load({bytes, size}), {args, argc}, {granted, granted_size}, tier, max_steps);
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

extern "C" bool neura_wasm_respond(Runner* runner, const char* reply, std::size_t size) try {
    runner->respond({reply, size});
    return true;
} catch (const std::exception& failure) {
    report_error(failure);
    return false;
}

extern "C" bool neura_wasm_deliver(Runner* runner, std::uint64_t sequence, const char* reply, std::size_t size) try {
    runner->deliver(sequence, {reply, size});
    return true;
} catch (const std::exception& failure) {
    report_error(failure);
    return false;
}

// The number of timers and deadlines that expired, or -1 on failure.
extern "C" double neura_wasm_clock(Runner* runner, std::int64_t now) try {
    return static_cast<double>(runner->advance_clock(now));
} catch (const std::exception& error) {
    report_error(error);
    return -1;
}

extern "C" bool neura_wasm_has_proof(Runner* runner) { return runner->has_proof(); }

extern "C" double neura_wasm_prove_idle(Runner* runner) try {
    return runner->prove_one_idle();
} catch (const std::exception& error) {
    report_error(error);
    return -2;
}
