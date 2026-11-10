#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

namespace {

constexpr std::size_t footer_size = 32;

void check(bool condition, const std::string& detail) {
    if (!condition) throw std::runtime_error(detail);
}

Bytes read(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    check(static_cast<bool>(input), "cannot read " + file.string());
    return {std::istreambuf_iterator<char>(input), {}};
}

void write(const fs::path& file, const Bytes& bytes) {
    std::ofstream output(file, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    output.close();
    check(static_cast<bool>(output), "cannot write " + file.string());
}

std::uint64_t number(const Bytes& bytes, std::size_t offset, unsigned width) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= std::uint64_t{bytes.at(offset + i)} << (i * 8);
    return value;
}

std::uint32_t crc(std::span<const std::uint8_t> bytes) {
    std::uint32_t checksum = 0xffffffffU;
    for (auto byte : bytes) {
        checksum ^= byte;
        for (int bit = 0; bit < 8; ++bit) checksum = (checksum >> 1) ^ (checksum & 1 ? 0xedb88320U : 0U);
    }
    return ~checksum;
}

void fix_checksum(Bytes& bytes, std::size_t offset) {
    const auto footer = bytes.size() - footer_size;
    const auto value = crc(std::span(bytes).subspan(offset, footer - offset));
    for (unsigned i = 0; i < 4; ++i) bytes[footer + 16 + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void rejects(const fs::path& file, const fs::path& directory, std::string_view label,
             std::string_view diagnostic = "error:") {
    const auto output = directory / "captured-output";
    const auto error = directory / "captured-error";
    const auto child = fork();
    check(child >= 0, "fork failed");
    if (child == 0) {
        const auto out_fd = open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        const auto err_fd = open(error.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (out_fd < 0 || err_fd < 0 || dup2(out_fd, STDOUT_FILENO) < 0 || dup2(err_fd, STDERR_FILENO) < 0) _exit(126);
        close(out_fd);
        close(err_fd);
        execl(file.c_str(), file.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    const auto errors = read(error);
    const std::string stderr_text(errors.begin(), errors.end());
    check(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 1 &&
          fs::file_size(output) == 0 && stderr_text.find(diagnostic) != std::string::npos,
          std::string(label) + " did not reject before the Hello World host request: " + stderr_text);
}

} // namespace

int main(int argc, char** argv) try {
    if (argc != 5) return 2;
    const fs::path good = argv[1];
    const fs::path launcher = argv[2];
    const fs::path strip = argv[3];
    const fs::path directory = argv[4];
    const auto altered = directory / "altered";
    const auto bytes = read(good);
    check(bytes.size() > footer_size, "missing test container footer");
    const auto footer = bytes.size() - footer_size;
    const auto offset = number(bytes, footer, 8);
    check(offset == fs::file_size(launcher) && number(bytes, footer + 8, 8) == footer - offset,
          "container footer extents");
    check(number(bytes, footer + 16, 4) == crc(std::span(bytes).subspan(offset, footer - offset)), "IEEE CRC-32");
    fs::copy_file(good, altered, fs::copy_options::overwrite_existing);
    for (auto size = bytes.size() - 1; size >= offset; --size) {
        fs::resize_file(altered, size);
        rejects(altered, directory, "truncation at byte " + std::to_string(size));
    }

    auto changed = bytes;
    changed[footer + 16] ^= 1;
    write(altered, changed);
    rejects(altered, directory, "wrong checksum", "checksum");
    changed = bytes;
    changed[footer + 20] = 2;
    write(altered, changed);
    rejects(altered, directory, "newer container", "version");
    changed = bytes;
    changed[offset + 3] = 0x80;
    fix_checksum(changed, offset);
    write(altered, changed);
    rejects(altered, directory, "reserved policy flag", "reserved");
    changed = bytes;
    changed[offset + 7] = 0x80;
    fix_checksum(changed, offset);
    write(altered, changed);
    rejects(altered, directory, "reserved capability bit", "reserved");
    changed = bytes;
    changed.push_back(0);
    write(altered, changed);
    rejects(altered, directory, "trailing file bytes");
    changed = bytes;
    changed.insert(changed.begin() + static_cast<std::ptrdiff_t>(footer), 0);
    const auto payload_size = footer - offset + 1;
    for (unsigned i = 0; i < 8; ++i) changed[footer + 1 + 8 + i] = static_cast<std::uint8_t>(payload_size >> (i * 8));
    fix_checksum(changed, offset);
    write(altered, changed);
    rejects(altered, directory, "trailing payload byte", "trailing bytes after image");

    constexpr std::string_view bare = "this is a Neura launcher with no program; build one with neura build --native";
    rejects(launcher, directory, "bare launcher", bare);
    fs::copy_file(good, altered, fs::copy_options::overwrite_existing);
    const auto child = fork();
    check(child >= 0, "could not start strip");
    if (child == 0) {
        execl(strip.c_str(), strip.c_str(), altered.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, "strip failed");
    rejects(altered, directory, "stripped executable", bare);
    std::cout << bytes.size() - offset << " payload and footer truncations checked\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
