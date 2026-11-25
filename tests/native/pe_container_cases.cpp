#include "launcher/container.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace launcher = neura::launcher;
using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t pe_header = 0x80;
constexpr std::size_t optional_header = pe_header + 24;
constexpr std::size_t pe_checksum = optional_header + 64;
constexpr std::size_t security_directory = optional_header + 112 + 4 * 8;
constexpr std::size_t section_header = optional_header + 240;
constexpr std::size_t section_start = 0x200;
constexpr std::size_t section_size = 0x200;

void check(bool condition, std::string_view detail) {
    if (!condition) throw std::runtime_error(std::string(detail));
}

void set(Bytes& bytes, std::size_t offset, std::uint32_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i) bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8));
}

Bytes read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input), "could not read PE test file");
    return {std::istreambuf_iterator<char>(input), {}};
}

void write(const fs::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    check(static_cast<bool>(output), "could not write PE test file");
}

Bytes pe_launcher() {
    // A PE32+ header and one section with file-backed data. Only the container
    // reader consumes this fixture, so it needs no code or Windows toolchain.
    Bytes bytes(section_start + section_size, 0);
    set(bytes, 0, 0x5a4d, 2);                 // DOS signature, MZ.
    set(bytes, 0x3c, pe_header);              // e_lfanew.
    set(bytes, pe_header, 0x4550);            // PE signature.
    set(bytes, pe_header + 4, 0x8664, 2);      // AMD64.
    set(bytes, pe_header + 6, 1, 2);           // NumberOfSections.
    set(bytes, pe_header + 20, 240, 2);        // SizeOfOptionalHeader.
    set(bytes, optional_header, 0x20b, 2);     // PE32+.
    set(bytes, optional_header + 60, section_start); // SizeOfHeaders.
    set(bytes, pe_checksum, 0x12345678);             // CheckSum.
    set(bytes, optional_header + 108, 16);           // NumberOfRvaAndSizes.
    set(bytes, section_header + 16, section_size);   // SizeOfRawData.
    set(bytes, section_header + 20, section_start);  // PointerToRawData.
    return bytes;
}

std::size_t aligned_table_offset(std::size_t size) {
    return (size + 7) & ~std::size_t{7};
}

Bytes append_certificate(Bytes bytes, std::size_t extra_padding = 0) {
    const auto table_offset = aligned_table_offset(bytes.size()) + extra_padding;
    constexpr std::uint32_t table_size = 16;
    bytes.resize(table_offset + table_size, 0);
    set(bytes, security_directory, static_cast<std::uint32_t>(table_offset));
    set(bytes, security_directory + 4, table_size);
    set(bytes, table_offset, 9);            // dwLength before 8-byte rounding.
    set(bytes, table_offset + 4, 0x200, 2); // WIN_CERT_REVISION_2_0.
    set(bytes, table_offset + 6, 2, 2);     // WIN_CERT_TYPE_PKCS_SIGNED_DATA.
    bytes[table_offset + 8] = 0x5a;
    return bytes;
}

void check_container(const launcher::Container& result, const Bytes& image, std::size_t launcher_size) {
    check(result.image == image, "PE container changed image bytes");
    check(result.launcher_size == launcher_size, "PE container changed launcher size");
    check(result.policy.sealed, "PE container changed sealed policy");
}

template <class Read>
void expect_corrupt(const Read& read_case, std::string_view diagnostic) {
    try {
        read_case();
    } catch (const launcher::CorruptContainerError& error) {
        check(std::string_view(error.what()).find(diagnostic) != std::string_view::npos, error.what());
        return;
    }
    throw std::runtime_error("malformed PE container was accepted: " + std::string(diagnostic));
}

void reject_bad_table(const fs::path& path, const Bytes& bytes, std::string_view diagnostic) {
    write(path, bytes);
    expect_corrupt([&] { launcher::is_container(bytes); }, diagnostic);
    expect_corrupt([&] { launcher::read_container(bytes); }, diagnostic);
    expect_corrupt([&] { launcher::read_container(path); }, diagnostic);
}

void test_pe_container(const fs::path& path) {
    const auto stub = pe_launcher();
    launcher::Policy policy;
    policy.sealed = true;
    check(!launcher::is_container(stub), "bare PE launcher was recognized as a container");

    // These consecutive image lengths exercise every alignment gap from 0 to 7.
    for (std::size_t length = 1; length <= 8; ++length) {
        const Bytes image(length, static_cast<std::uint8_t>(length));
        launcher::write_executable(path, stub, image, policy);
        const auto plain = read(path);
        check(std::ranges::equal(std::span(plain).first(stub.size()), stub), "writer modified the PE launcher");
        check(launcher::is_container(plain), "unsigned PE container was not recognized");
        check_container(launcher::read_container(plain), image, stub.size());
        check_container(launcher::read_container(path), image, stub.size());

        const auto with_table = append_certificate(plain);
        check(launcher::is_container(with_table), "PE container with a certificate table was not recognized");
        write(path, with_table);
        check_container(launcher::read_container(with_table), image, stub.size());
        check_container(launcher::read_container(path), image, stub.size());
    }

    const Bytes image{1};
    launcher::write_executable(path, stub, image, policy);
    const auto plain = read(path);
    const auto with_table = append_certificate(plain);
    const auto table_offset = aligned_table_offset(plain.size());

    auto changed = with_table;
    changed.pop_back();
    reject_bad_table(path, changed, "certificate table bounds");
    changed = with_table;
    set(changed, security_directory, static_cast<std::uint32_t>(table_offset + 1));
    reject_bad_table(path, changed, "misaligned PE certificate table offset");
    changed = with_table;
    set(changed, security_directory, section_start + 8);
    reject_bad_table(path, changed, "overlaps executable headers or sections");
    changed = with_table;
    set(changed, security_directory + 4, 0);
    reject_bad_table(path, changed, "certificate table bounds");
    changed = with_table;
    set(changed, table_offset, 17);
    reject_bad_table(path, changed, "invalid PE certificate entry length");
    set(changed, table_offset, 0);
    reject_bad_table(path, changed, "invalid PE certificate entry length");

    changed = plain;
    changed.push_back(0);
    check(!launcher::is_container(changed), "unsigned PE container accepted trailing bytes");
    changed = with_table;
    changed.at(plain.size()) = 1;
    check(!launcher::is_container(changed), "signed PE container accepted nonzero padding");
    check(!launcher::is_container(append_certificate(plain, 8)), "PE container accepted more than seven padding bytes");

    changed = with_table;
    set(changed, section_header + 16, section_size + 1);
    write(path, changed);
    expect_corrupt([&] { launcher::read_container(changed); }, "invalid payload bounds");
    expect_corrupt([&] { launcher::read_container(path); }, "invalid payload bounds");

    // Preserve even the existing overlay and the signer's alignment bytes
    // before the certificate table.
    auto unaligned_stub = stub;
    unaligned_stub.push_back(0xa5);
    auto signed_stub = append_certificate(unaligned_stub);
    check(!launcher::is_container(signed_stub), "signed bare PE launcher was recognized as a container");
    launcher::write_executable(path, signed_stub, image, policy);
    const auto normalized = read(path);
    auto expected_stub = unaligned_stub;
    expected_stub.resize(aligned_table_offset(unaligned_stub.size()), 0);
    set(expected_stub, pe_checksum, 0);
    check(normalized.size() == expected_stub.size() + plain.size() - stub.size(),
          "output from a signed PE template retained certificate table bytes");
    check(std::ranges::equal(std::span(normalized).first(expected_stub.size()), expected_stub),
          "output from a signed PE template must clear the Security directory and PE checksum and preserve other bytes");
    check(launcher::is_container(normalized), "output from a signed PE template was not recognized");
    check_container(launcher::read_container(normalized), image, expected_stub.size());
    check_container(launcher::read_container(path), image, expected_stub.size());

    signed_stub.pop_back();
    expect_corrupt([&] { launcher::write_executable(path, signed_stub, image, policy); }, "certificate table bounds");
    check(read(path) == normalized, "a malformed signed PE template replaced the destination");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const fs::path path = argv[1];
    try {
        test_pe_container(path);
        fs::remove(path);
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        fs::remove(path, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
