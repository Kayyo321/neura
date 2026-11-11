#include "sha256.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace neura::util {

namespace {

constexpr std::array<std::uint32_t, 64> round_constants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32 - count));
}

} // namespace

Sha256::Sha256()
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::block(const std::uint8_t* bytes) {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t i = 0; i < 16; ++i) {
        schedule[i] = (std::uint32_t{bytes[4 * i]} << 24) | (std::uint32_t{bytes[4 * i + 1]} << 16) |
                      (std::uint32_t{bytes[4 * i + 2]} << 8) | std::uint32_t{bytes[4 * i + 3]};
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const auto s0 = rotate_right(schedule[i - 15], 7) ^ rotate_right(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
        const auto s1 = rotate_right(schedule[i - 2], 17) ^ rotate_right(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
        schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }
    auto [a, b, c, d, e, f, g, h] = state_;
    for (std::size_t i = 0; i < 64; ++i) {
        const auto sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const auto choice = (e & f) ^ (~e & g);
        const auto first = h + sum1 + choice + round_constants[i] + schedule[i];
        const auto sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto second = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> bytes) {
    length_ += bytes.size();
    while (!bytes.empty()) {
        if (buffered_ == 0 && bytes.size() >= 64) {
            block(bytes.data());
            bytes = bytes.subspan(64);
            continue;
        }
        const auto take = std::min(bytes.size(), buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, bytes.data(), take);
        buffered_ += take;
        bytes = bytes.subspan(take);
        if (buffered_ == buffer_.size()) {
            block(buffer_.data());
            buffered_ = 0;
        }
    }
}

void Sha256::update(std::string_view text) {
    update(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

std::array<std::uint8_t, 32> Sha256::finish() {
    const std::uint64_t bit_length = length_ * 8;
    const std::uint8_t one = 0x80;
    update(std::span(&one, 1));
    const std::uint8_t zero = 0;
    while (buffered_ != 56) update(std::span(&zero, 1));
    std::array<std::uint8_t, 8> tail{};
    for (std::size_t i = 0; i < 8; ++i) tail[i] = static_cast<std::uint8_t>(bit_length >> (56 - 8 * i));
    update(tail);
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t i = 0; i < 8; ++i) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            digest[4 * i + byte] = static_cast<std::uint8_t>(state_[i] >> (24 - 8 * byte));
        }
    }
    return digest;
}

namespace {

std::string hex_of(const std::array<std::uint8_t, 32>& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (const auto byte : digest) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

} // namespace

std::string sha256_hex(std::string_view text) {
    Sha256 hash;
    hash.update(text);
    return hex_of(hash.finish());
}

std::optional<FileDigest> sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    Sha256 hash;
    FileDigest result;
    std::array<char, 1 << 16> chunk{};
    while (input) {
        input.read(chunk.data(), chunk.size());
        const auto count = static_cast<std::size_t>(input.gcount());
        if (count == 0) break;
        hash.update(std::span(reinterpret_cast<const std::uint8_t*>(chunk.data()), count));
        result.size += count;
    }
    if (input.bad()) return std::nullopt;
    result.hex = hex_of(hash.finish());
    return result;
}

} // namespace neura::util
