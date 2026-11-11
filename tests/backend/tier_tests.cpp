// Tier 1 in the executor (docs/runtime/NVM.md, "Execution tiers"): a run
// that evaluates proven lifted blocks natively is observably the same as one
// that evaluates every graph by reference, whatever the image's LIFT hints
// claim, and fleets and parallel items use the lifted blocks too.
#include "graph_test_support.hpp"

#include "lift/hints.hpp"
#include "nvm/fleet.hpp"
#include "nvm/tier1.hpp"

#include <algorithm>

namespace neura::testing {
namespace {

nvm::RunLimits tier_limits(std::uint8_t tier, nvm::TierCache* cache = nullptr) {
    nvm::RunLimits limits;
    limits.max_steps = 20'000'000;
    limits.tier = tier;
    limits.tier_cache = cache;
    return limits;
}

void check_same_capture(const Captured& actual, const Captured& expected, const std::string& label) {
    check_same_run(actual.result, expected.result, label);
    check_equal(actual.output, expected.output, label + " output");
    check_equal(actual.requests.size(), expected.requests.size(), label + " request count");
    for (std::size_t i = 0; i < std::min(actual.requests.size(), expected.requests.size()); ++i) {
        check_same_request(actual.requests[i], expected.requests[i], label);
    }
}

// Runs `image` at tier 0 and at tier 1 with a fresh cache, checks the two
// runs agree, and returns the cache's statistics.
nvm::TierStatistics compare_tiers(const nvr::Image& image, const std::string& label,
                                  const std::vector<std::string>& arguments = {}) {
    const auto reference = run_captured(image, tier_limits(0), std::nullopt, arguments);
    nvm::TierCache cache(image);
    const auto lifted = run_captured(image, tier_limits(1, &cache), std::nullopt, arguments);
    check_same_capture(lifted, reference, label);
    return cache.statistics();
}

nvr::Image computation(const Environment& environment) {
    return build_program({program_path(environment, "computation")});
}

// Replaces the image's LIFT section with `section`.
nvr::Image with_section(nvr::Image image, std::vector<std::uint8_t> section) {
    lift::strip_hints(image);
    nvr::RawSection raw;
    raw.tag = lift::hint_tag;
    raw.data = std::move(section);
    image.other_sections.push_back(std::move(raw));
    std::sort(image.other_sections.begin(), image.other_sections.end(),
              [](const nvr::RawSection& a, const nvr::RawSection& b) { return a.tag < b.tag; });
    return nvr::load(nvr::encode(image));
}

const std::vector<std::uint8_t>& hint_bytes(const nvr::Image& image) {
    for (const auto& section : image.other_sections) {
        if (section.tag == lift::hint_tag) return section.data;
    }
    throw std::runtime_error("the image has no LIFT section");
}

// Flips the operation of every binary expression of a form: + and -, * and
// /, < and >=, == and !=, & and |. The structural proof fails for each, so
// no tampered hint can lift its block.
lift::ExpressionPtr tampered(const lift::ExpressionPtr& e, std::map<const lift::Expression*, lift::ExpressionPtr>& done) {
    if (!e) return e;
    if (const auto it = done.find(e.get()); it != done.end()) return it->second;
    auto copy = std::make_shared<lift::Expression>(*e);
    for (auto& operand : copy->operands) operand = tampered(operand, done);
    static const std::map<std::string, std::string> swaps{
        {"+", "-"}, {"-", "+"}, {"*", "/"}, {"/", "*"}, {"<", ">="}, {">=", "<"}, {">", "<="}, {"<=", ">"},
        {"==", "!="}, {"!=", "=="}, {"&", "|"}, {"|", "&"}, {"&&", "||"}, {"||", "&&"}};
    if (const auto it = swaps.find(copy->op); copy->kind == lift::Expression::Kind::Binary && it != swaps.end()) {
        copy->op = it->second;
    }
    return done.emplace(e.get(), copy).first->second;
}

} // namespace

NEURA_TEST(tier, programs_match_the_reference_tier) {
    std::uint64_t lifted = 0;
    std::uint64_t hinted = 0;
    for (const auto* name : {"computation", "collections", "integers", "floats", "objects", "interfaces",
                             "control-and-updates", "closures", "generics"}) {
        const auto image = build_program({program_path(environment, name)});
        const auto statistics = compare_tiers(image, name);
        lifted += statistics.steps.lifted;
        hinted += statistics.lifted_by_hint;
        check_equal(statistics.steps.declined + statistics.steps.lifted + statistics.steps.wired +
                        statistics.steps.empty + statistics.steps.reference,
                    run_captured(image, tier_limits(0)).result.steps, std::string(name) + " steps by tier");
    }
    // NEURA_TIER=0 in the environment keeps every run on tier 0.
    if (nvm::tier_ceiling() >= 1) {
        check(lifted > 0, "tier 1 ran lifted blocks");
        check(hinted > 0, "hinted blocks were lifted");
    }
}

NEURA_TEST(tier, traps_and_limits_match_the_reference_tier) {
    for (const auto* name : {"overflow", "index-out-of-bounds", "deep-recursion", "endless-loop", "message-trap",
                             "nil-receiver", "use-after-destroy", "uninitialized-field"}) {
        const auto path = backend_dir(environment) / "traps" / (std::string(name) + ".nra");
        const auto image = build_program({path});
        for (const std::uint64_t steps : {1ULL, 7ULL, 100ULL, 1'000ULL, 20'000'000ULL}) {
            auto limits0 = tier_limits(0);
            limits0.max_steps = steps;
            const auto reference = run_captured(image, limits0);
            nvm::TierCache cache(image);
            auto limits1 = tier_limits(1, &cache);
            limits1.max_steps = steps;
            check_same_capture(run_captured(image, limits1), reference,
                               std::string(name) + " with " + std::to_string(steps) + " steps");
        }
    }
}

NEURA_TEST(tier, a_wrong_hint_falls_back_to_the_reference) {
    const auto image = computation(environment);
    auto hints = lift::decode_hints(image);
    check(!hints.empty(), "the program has hints");
    std::vector<lift::BlockHint> wrong;
    for (const auto& [key, form] : hints) {
        std::map<const lift::Expression*, lift::ExpressionPtr> done;
        auto changed = *form;
        for (auto& [lane, value] : changed.values) value = tampered(value, done);
        for (auto& [lane, value] : changed.order) value = tampered(value, done);
        for (auto& value : changed.schedule) value = tampered(value, done);
        changed.condition = tampered(changed.condition, done);
        wrong.push_back({key.first, key.second, std::move(changed)});
    }
    const auto forged = with_section(image, lift::encode_hints(std::move(wrong)).data);
    const auto statistics = compare_tiers(forged, "forged hints");
    check(statistics.hints_rejected > 0 || nvm::tier_ceiling() == 0, "the forged hints were rejected");
    check_equal(statistics.lifted_by_hint, std::uint64_t{0}, "no block lifted by a forged hint");
}

NEURA_TEST(tier, a_corrupted_section_is_ignored) {
    const auto image = computation(environment);
    const auto& bytes = hint_bytes(image);
    std::vector<std::vector<std::uint8_t>> variants;
    variants.push_back({});
    variants.emplace_back(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2));
    auto version = bytes;
    version[0] = 2;
    variants.push_back(version);
    auto trailing = bytes;
    trailing.push_back(0);
    variants.push_back(trailing);
    auto huge = bytes;
    huge[4] = huge[5] = huge[6] = huge[7] = 0xff;
    variants.push_back(huge);
    for (std::size_t at = 8; at < bytes.size(); at += std::max<std::size_t>(1, bytes.size() / 97)) {
        auto flipped = bytes;
        flipped[at] ^= 0x5a;
        variants.push_back(std::move(flipped));
    }
    std::size_t ignored = 0;
    for (std::size_t i = 0; i < variants.size(); ++i) {
        const auto corrupted = with_section(image, variants[i]);
        ignored += lift::decode_hints(corrupted).empty() ? 1 : 0;
        compare_tiers(corrupted, "corrupted section " + std::to_string(i));
    }
    check(ignored >= 5, "malformed sections decode to no hints");
}

NEURA_TEST(tier, fleets_and_parallel_items_use_tier_1) {
    const auto image = computation(environment);
    const std::vector<std::vector<std::string>> arguments{{}, {"one"}, {"two", "three"}, {"four"}};
    std::vector<Captured> reference;
    for (const auto& member : arguments) reference.push_back(run_captured(image, tier_limits(0), std::nullopt, member));
    nvm::TierCache cache(image);
    nvm::Fleet fleet(image, {tier_limits(1, &cache), nullptr, 2});
    for (const auto& member : arguments) fleet.add(image.entry, member);
    CapturingConsole console;
    nvm::HostRegistry registry;
    registry.add(console);
    std::size_t members = 0;
    nvm::run_fleet(fleet, nvm::bind(image, registry), [&](std::size_t member, const nvm::RunResult& result) {
        check_same_run(result, reference[member].result, "fleet member " + std::to_string(member));
        ++members;
    });
    check_equal(members, arguments.size(), "fleet members");
    check(cache.statistics().steps.lifted > 0 || nvm::tier_ceiling() == 0, "fleet members ran lifted blocks");

    // A fleet without a cache of its own shares one among its members.
    nvm::Fleet unshared(image, {tier_limits(1), nullptr, 2});
    for (const auto& member : arguments) unshared.add(image.entry, member);
    nvm::run_fleet(unshared, nvm::bind(image, registry), [&](std::size_t member, const nvm::RunResult& result) {
        check_same_run(result, reference[member].result, "unshared fleet member " + std::to_string(member));
    });

    const auto parallel = build_program({environment.source_dir / "tests" / "concurrency" / "parallel" / "map.nra"});
    const auto expected = run_captured(parallel, tier_limits(0));
    for (const std::uint32_t workers : {1U, 4U}) {
        nvm::TierCache shared(parallel);
        auto limits = tier_limits(1, &shared);
        limits.parallel_workers = workers;
        check_same_capture(run_captured(parallel, limits), expected,
                           "parallel map with " + std::to_string(workers) + " workers");
        check(shared.statistics().steps.lifted > 0 || nvm::tier_ceiling() == 0, "parallel items ran lifted blocks");
    }
}

NEURA_TEST(tier, tier_0_lifts_nothing) {
    const auto image = computation(environment);
    nvm::TierCache cache(image);
    run_captured(image, tier_limits(0, &cache));
    const auto statistics = cache.statistics();
    check_equal(statistics.attempted + statistics.steps.lifted + statistics.steps.reference, std::uint64_t{0},
                "a tier 0 run leaves the cache untouched");
}

} // namespace neura::testing
