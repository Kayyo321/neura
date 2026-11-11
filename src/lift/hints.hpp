#pragma once

#include "expression.hpp"
#include "nvr/image.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace neura::lift {

// The optional LIFT section (docs/runtime/NVR.md, "Lift hints"): the lifted
// form the converter recorded for each block it could describe. A hint is a
// claim, never evidence. The runtime proves a hinted form exactly as it
// proves one found by search (lift_and_prove with the hint), so a wrong,
// stale, or malicious hint can only leave its block on the reference
// evaluator. A loader that does not know the section skips it: it is
// optional and raises no format version.
inline constexpr std::array<char, 4> hint_tag{'L', 'I', 'F', 'T'};
inline constexpr std::uint32_t hint_version = 1;

struct BlockHint {
    std::uint32_t function = 0;
    std::uint32_t block = 0;
    LiftedForm form;
};

// Encodes hints as a LIFT section. Identical forms are stored once. The
// encoding is a pure function of the hints in (function, block) order.
nvr::RawSection encode_hints(std::vector<BlockHint> hints);

// The decoded forms of a LIFT section, by (function, block).
using HintTable = std::map<std::pair<std::uint32_t, std::uint32_t>, std::shared_ptr<const LiftedForm>>;

// Decodes the image's LIFT section. A section that is absent, of another
// version, or malformed in any way yields an empty table: hints are only an
// optimization. Entries naming a function or block outside the image are
// dropped. Nothing here establishes that a form is well typed or describes
// its block; the proofs do.
HintTable decode_hints(const nvr::Image& image);
HintTable decode_hints(std::span<const std::uint8_t> section);

// Removes the LIFT section from an image (`--no-lift-hints`). Returns
// whether there was one.
bool strip_hints(nvr::Image& image);

} // namespace neura::lift
