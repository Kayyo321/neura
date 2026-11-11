#pragma once

// SHA-256 as FIPS 180-4 defines it. `neura update` uses it to check a
// downloaded archive against the digest in the release metadata.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace neura::util {

class Sha256 {
public:
    Sha256();
    void update(std::span<const std::uint8_t> bytes);
    void update(std::string_view text);
    // Finishes the digest. The object accepts no further input.
    std::array<std::uint8_t, 32> finish();

private:
    void block(const std::uint8_t* bytes);
    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0;
};

// The digest as 64 lowercase hexadecimal digits.
std::string sha256_hex(std::string_view text);

// The digest of a file's contents and its size, or nothing when the file
// cannot be read.
struct FileDigest {
    std::string hex;
    std::uint64_t size = 0;
};
std::optional<FileDigest> sha256_file(const std::filesystem::path& path);

} // namespace neura::util
