#pragma once

#include "image.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace neura::nvr {

// Encodes `image` as NVR 1.x bytes. The encoding is a pure function of the
// model: sections appear in ascending tag order and nothing depends on time,
// addresses, or the host. Throws std::invalid_argument when a count or index
// cannot be represented, for example a table that would need the reserved
// index 0xffffffff. The writer does not check the image semantically; the
// loader does.
std::vector<std::uint8_t> encode(const Image& image);

// Bounds applied while decoding, before the loader allocates storage for a
// record. The defaults accept every image the compiler emits for the
// repository's programs with a wide margin.
struct LoadLimits {
    std::size_t max_image_bytes = std::size_t{256} << 20;
    // Elements in any one vector: a table, a graph's nodes, a node's edges,
    // a frame, a function's blocks, and so on.
    std::uint32_t max_vector_length = 1U << 24;
    // Input lanes plus nodes in one graph.
    std::uint32_t max_graph_wires = 1U << 24;
};

class LoadError : public std::runtime_error {
public:
    enum class Kind {
        // The bytes do not form a well-encoded image: truncation, bad offsets,
        // invalid tags, trailing bytes, invalid UTF-8.
        Malformed,
        // A well-encoded image that this loader cannot run: another version,
        // graph profile, or an unknown required section.
        Unsupported,
        // A count or size exceeds LoadLimits.
        LimitExceeded,
        // Decoded records that violate the image contract: bad references,
        // ill-typed bindings, invalid graphs, layouts, or continuations.
        Invalid,
    };

    LoadError(Kind error_kind, std::string message) : std::runtime_error(std::move(message)), kind(error_kind) {}

    Kind kind;
};

std::string_view describe(LoadError::Kind kind);

// Decodes and validates an NVR image. Every check in docs/runtime/NVR.md
// "Writer and loader requirements" runs here, before any part of the program
// executes. Throws LoadError.
Image load(std::span<const std::uint8_t> bytes, const LoadLimits& limits = {});

// True when `bytes` is well-formed UTF-8 without overlong forms, surrogates,
// or code points above U+10FFFF.
bool valid_utf8(std::string_view bytes);

// Static transferability in an image with validated types and TRNS marking.
bool is_transferable(const Image& image, std::uint32_t type);

// Structural type identity and the subtype relation of an image whose type
// and layout tables have been validated.
class TypeSystem {
public:
    explicit TypeSystem(const Image& image);

    // Canonical representative of a type index: equal for structurally
    // equal types.
    std::uint32_t canonical(std::uint32_t type) const { return canonical_[type]; }
    bool same(std::uint32_t left, std::uint32_t right) const {
        return canonical_[left] == canonical_[right];
    }
    // True when a value of type `actual` can be stored where `expected` is
    // declared: structural equality, or an object layout that has
    // `expected`'s layout among its ancestors.
    bool assignable(std::uint32_t actual, std::uint32_t expected) const;
    // True when layout `derived` is `base` or has it among its ancestors.
    bool derives(std::uint32_t derived, std::uint32_t base) const;

    const Type& type(std::uint32_t index) const { return image_->types[index]; }

private:
    const Image* image_;
    std::vector<std::uint32_t> canonical_;
};

} // namespace neura::nvr
