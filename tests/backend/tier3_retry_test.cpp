// Standalone fault-injection regression. Compile this translation unit instead
// of tier3_build.cpp and link against the NVM's static libraries. Renaming the
// inliner here leaves the production library and verifier unchanged.
// Use -DNEURA_TEST_FRAME_HOOKS=1 when linking a hook-enabled NVM archive.
#define inline_calls tier3_retry_test_inline_calls
#include "../../src/nvm/tier3_build.cpp"
#undef inline_calls

#include <iostream>
#include <stdexcept>

namespace {
unsigned inline_attempts = 0;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
} // namespace

namespace neura::nvm::tier3 {
std::string tier3_retry_test_inline_calls(const nvr::Image& image, Function& function,
                                         const InlineSource& source, bool) {
    ++inline_attempts;
    require(inline_attempts == 1, "the retry must not run the inliner again");
    const auto pending = source.program(1);
    require(!pending.settled, "the test callee must still be pending");

    // The same deliberately invalid graph as the publication verifier probe:
    // Const does not terminate its block. The actual verifier must reject it.
    function.blocks.clear();
    function.blocks.emplace_back().insts.push_back(0);
    function.entries = {{0, 0}};
    function.insts.clear();
    Inst inst;
    inst.op = Op::Const;
    inst.type = {Type::Int, 1};
    function.insts.push_back(inst);
    function.inlined_calls = 1;
    const auto violations = verify(image, function);
    require(!violations.empty() && violations.front().starts_with("V1:"),
            "the injected graph must fail V1");
    return violations.front();
}
} // namespace neura::nvm::tier3

int main() {
    using namespace neura::nvm;
    try {
        neura::nvr::Image image;
        image.functions.resize(2);
        for (auto& function : image.functions) function.blocks.resize(1);
        tier2::Region region;
        region.program = true;
        region.blocks = {0};
        region.block_labels = {0};
        region.entries.push_back({0, 0, {}, {}});
        region.code.emplace_back().code = tier2::Code::Deopt;

        TierOptions options;
        options.tier3 = TierOptions::Tier3::Interp;
        options.programs = TierOptions::Programs::On;
        TierCache cache(image, options);
        const auto* published = cache.compile_native(region, tier3::Variant::Standard);
        require(published != nullptr, "the rejected first graph must still publish a Tier 3 program");
        require(inline_attempts == 1, "only one inlining attempt is allowed");
        require(cache.native_code(region, tier3::Variant::Standard) == published,
                "the fallback must be stored in the native program slot");
        require(published->pending_callees == std::vector<std::uint32_t>{1},
                "the fallback must retain pending callees for settlement");
        require(published->ir.inlined_calls == 0, "the fallback must discard the mutated graph");
        require(tier3::verify(image, published->ir).empty(), "the fallback graph must verify");
        const auto statistics = cache.statistics();
        require(statistics.native_programs == 1 && statistics.native_failed == 0,
                "a successful fallback must count as published, not failed");
        require(cache.compile_native(region, tier3::Variant::Standard) == published,
                "an unsettled callee must retain the fallback without retrying");
        std::cout << "Tier 3 rejected-inline retry: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
