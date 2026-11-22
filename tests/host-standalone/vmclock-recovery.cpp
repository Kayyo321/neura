#include "nvm/providers.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <string_view>

int main() {
    using namespace neura::nvm;
    StringSink output, error;
    StringSource input("");
    Capabilities capabilities;
    capabilities.entropy = true;
    StandardServices services(output, error, input, capabilities);
    HostRegistry registry;
    services.register_all(registry);
    auto* service = registry.find("neura.process.generation", 1);
    assert(service);
    const auto generation = [&] {
        const auto result = service->invoke(HostRequest{});
        assert(result.success);
        return result.value.bits;
    };

    const std::string_view mode = std::getenv("VMCLOCK_MODE") ? std::getenv("VMCLOCK_MODE") : "";
    if (mode == "short-read") {
        // The first read is shorter than the header; the next one, on the
        // same handle, observes the generation.
        const auto initial = generation();
        const auto recovered = generation();
        assert(recovered == initial + 1);
        assert(generation() == recovered);
        return 0;
    }
    if (mode == "short-always") {
        // Every read is short: after three VMClock is no longer read (the
        // shim exits on a fourth), and the generation stays unchanged.
        const auto initial = generation();
        for (int call = 0; call < 10; ++call) assert(generation() == initial);
        return 0;
    }
    // The shim fails the first open with EMFILE and the first read with EIO.
    // The next call observes a valid VMClock generation on the same handle.
    const auto initial = generation();
    assert(generation() == initial);
    const auto recovered = generation();
    assert(recovered == initial + 1);
    assert(generation() == recovered);
}
