#include "nvm/providers.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

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

    // The shim fails the first open with EMFILE and the first read with EIO.
    // The next call observes a valid VMClock generation on the same handle.
    const auto initial = generation();
    assert(generation() == initial);
    const auto recovered = generation();
    assert(recovered == initial + 1);
    assert(generation() == recovered);
}
