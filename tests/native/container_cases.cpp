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

struct Outcome {
    bool exited;
    int status;
    std::uintmax_t stdout_size;
    std::string stderr_text;
};

Outcome execute(const fs::path& file, const fs::path& directory) {
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
    check(waited == child, "waitpid failed");
    return {static_cast<bool>(WIFEXITED(status)), WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status),
            fs::file_size(output), std::string(errors.begin(), errors.end())};
}

void rejects(const fs::path& file, const fs::path& directory, std::string_view label,
             std::string_view diagnostic = "error:") {
    const auto outcome = execute(file, directory);
    check(outcome.exited && outcome.status == 1 && outcome.stdout_size == 0 &&
          outcome.stderr_text.find(diagnostic) != std::string::npos,
          std::string(label) + " did not reject with status 1 before the Hello World host request (exited " +
          std::to_string(outcome.exited) + ", status " + std::to_string(outcome.status) + "): " + outcome.stderr_text);
}

void put(Bytes& bytes, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

// The task settings a version 2 policy records after the three run limits.
struct TaskSettings {
    std::uint64_t max_tasks = 64;
    std::uint64_t quantum = 1024;
    std::uint64_t queued_messages = 4096;
    std::uint64_t pending_requests = 16;
    std::uint32_t workers = 1;
};

// A policy and image as the container stores them, with every length word
// under the caller's control so that a case can make one of them lie. A
// version 1 policy has no task settings.
Bytes policy_payload(std::uint32_t capabilities, std::uint32_t prefix_count, const std::vector<Bytes>& prefixes,
                     const Bytes& image, std::uint32_t image_length, std::uint32_t version = 2,
                     const TaskSettings& tasks = {}) {
    Bytes payload;
    put(payload, 0, 4);
    put(payload, capabilities, 4);
    put(payload, prefix_count, 4);
    for (const auto& prefix : prefixes) {
        put(payload, prefix.size(), 4);
        payload.insert(payload.end(), prefix.begin(), prefix.end());
    }
    put(payload, 1000, 8);
    put(payload, 100, 4);
    put(payload, 100000, 8);
    if (version >= 2) {
        put(payload, tasks.max_tasks, 8);
        put(payload, tasks.quantum, 8);
        put(payload, tasks.queued_messages, 8);
        put(payload, tasks.pending_requests, 8);
        put(payload, tasks.workers, 4);
    }
    put(payload, image_length, 4);
    payload.insert(payload.end(), image.begin(), image.end());
    return payload;
}

// A file made of the launcher, the payload, and a footer. A shift moves the
// footer's offset or size word away from the truth; the checksum covers the
// bytes the footer states.
Bytes assemble(const Bytes& launcher, const Bytes& payload, std::int64_t offset_shift = 0,
               std::int64_t size_shift = 0, std::uint32_t version = 2) {
    Bytes file = launcher;
    file.insert(file.end(), payload.begin(), payload.end());
    const auto offset = static_cast<std::uint64_t>(static_cast<std::int64_t>(launcher.size()) + offset_shift);
    const auto stated_size =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(payload.size()) + size_shift - offset_shift);
    const auto begin = std::min<std::uint64_t>(offset, file.size());
    const auto end = std::min<std::uint64_t>(offset + stated_size, file.size());
    put(file, offset, 8);
    put(file, stated_size, 8);
    put(file, crc(std::span(file).subspan(begin, end - begin)), 4);
    put(file, version, 4);
    for (const char letter : std::string_view("NEURAEXE")) file.push_back(static_cast<std::uint8_t>(letter));
    return file;
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
    check(number(bytes, footer + 20, 4) == 2, "the toolchain writes container version 2");
    for (const std::uint8_t version : {0, 3}) {
        changed = bytes;
        changed[footer + 20] = version;
        write(altered, changed);
        rejects(altered, directory, "container version " + std::to_string(version),
                "unsupported container version " + std::to_string(version));
    }
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

    // Crafted containers. The image is the good executable's own. The two
    // controls must run, and each lie about the policy must be refused.
    const auto launcher_bytes = read(launcher);
    constexpr std::size_t task_settings_size = 36;
    constexpr std::size_t default_policy_size = 36 + task_settings_size;
    const Bytes image(bytes.begin() + static_cast<std::ptrdiff_t>(offset + default_policy_size),
                      bytes.begin() + static_cast<std::ptrdiff_t>(footer));
    const auto image_size = static_cast<std::uint32_t>(image.size());
    constexpr std::uint32_t files_capability = 1;
    const Bytes slash{'/'};
    const auto craft = [&](const Bytes& file) { write(altered, file); };

    craft(assemble(launcher_bytes, policy_payload(0, 0, {}, image, image_size)));
    const auto control = execute(altered, directory);
    check(control.exited && control.status == 0 && control.stdout_size != 0,
          "the rebuilt default container must run: " + control.stderr_text);
    craft(assemble(launcher_bytes, policy_payload(files_capability, 1, {slash}, image, image_size)));
    const auto granted = execute(altered, directory);
    check(granted.exited && granted.status == 0 && granted.stdout_size != 0,
          "a container with one valid prefix must run: " + granted.stderr_text);

    // A version 1 container, which has no task settings, still runs: the
    // launcher uses the defaults for them. Its bytes under a version 2
    // footer, or the reverse, are refused, because the fields do not line up.
    craft(assemble(launcher_bytes, policy_payload(0, 0, {}, image, image_size, 1), 0, 0, 1));
    const auto older = execute(altered, directory);
    check(older.exited && older.status == 0 && older.stdout_size != 0,
          "a version 1 container must run: " + older.stderr_text);
    craft(assemble(launcher_bytes, policy_payload(0, 0, {}, image, image_size, 1), 0, 0, 2));
    rejects(altered, directory, "version 1 policy under a version 2 footer");
    craft(assemble(launcher_bytes, policy_payload(0, 0, {}, image, image_size, 2), 0, 0, 1));
    rejects(altered, directory, "version 2 policy under a version 1 footer");

    for (const auto& settings : {TaskSettings{.max_tasks = 0}, TaskSettings{.quantum = 0},
                                 TaskSettings{.queued_messages = 0}, TaskSettings{.pending_requests = 0},
                                 TaskSettings{.workers = 0}, TaskSettings{.workers = 1025}}) {
        craft(assemble(launcher_bytes, policy_payload(0, 0, {}, image, image_size, 2, settings)));
        rejects(altered, directory, "invalid task settings", "invalid task settings");
    }
    craft(assemble(launcher_bytes, policy_payload(0, 0, {}, image, image_size, 2, TaskSettings{.workers = 1024})));
    const auto widest = execute(altered, directory);
    check(widest.exited && widest.status == 0 && widest.stdout_size != 0,
          "a container with 1024 parallel workers must run: " + widest.stderr_text);

    craft(assemble(launcher_bytes, policy_payload(files_capability, 1, {Bytes{}}, image, image_size)));
    rejects(altered, directory, "empty file-system prefix", "file-system prefix is empty");
    craft(assemble(launcher_bytes, policy_payload(files_capability, 2, {slash, Bytes{}}, image, image_size)));
    rejects(altered, directory, "empty second file-system prefix", "file-system prefix is empty");
    for (const auto& invalid : {Bytes{0xff}, Bytes{'a', 0xc0, 0x80}, Bytes{0xe2, 0x82}, Bytes{0xed, 0xa0, 0x80}}) {
        craft(assemble(launcher_bytes, policy_payload(files_capability, 1, {invalid}, image, image_size)));
        rejects(altered, directory, "invalid UTF-8 prefix", "not valid UTF-8");
    }
    craft(assemble(launcher_bytes, policy_payload(0, 1, {slash}, image, image_size)));
    rejects(altered, directory, "prefix without the files capability", "require the files capability");

    const auto remaining_after_count = policy_payload(files_capability, 0, {}, image, image_size).size() - 12;
    const auto room = static_cast<std::uint32_t>((remaining_after_count - 24 - task_settings_size) / 4);
    for (const std::uint32_t count : {room + 1, 0x40000000U, 0xffffffffU}) {
        craft(assemble(launcher_bytes, policy_payload(files_capability, count, {}, image, image_size)));
        rejects(altered, directory, "prefix count " + std::to_string(count), "invalid file-system prefix count");
    }
    {
        auto lying = policy_payload(files_capability, 1, {slash}, image, image_size);
        for (unsigned i = 0; i < 4; ++i) lying[12 + i] = 0xff;
        craft(assemble(launcher_bytes, lying));
        rejects(altered, directory, "prefix length beyond the payload", "invalid file-system prefix length");
    }

    craft(assemble(launcher_bytes, policy_payload(0, 0, {}, image, image_size + 1)));
    rejects(altered, directory, "image length one too long", "invalid image length");
    craft(assemble(launcher_bytes, policy_payload(0, 0, {}, image, 0xffffffffU)));
    rejects(altered, directory, "image length of 4 GiB", "invalid image length");
    craft(assemble(launcher_bytes, policy_payload(0, 0, {}, image, image_size - 1)));
    rejects(altered, directory, "image length one too short", "trailing bytes after image");
    craft(assemble(launcher_bytes, policy_payload(0, 0, {}, image, 0)));
    rejects(altered, directory, "image length zero", "trailing bytes after image");

    craft(assemble(launcher_bytes, {}));
    rejects(altered, directory, "zero-length payload", "invalid payload bounds");
    craft(assemble(launcher_bytes, Bytes(71, 0)));
    rejects(altered, directory, "payload shorter than the fixed policy fields", "invalid payload bounds");
    craft(assemble(launcher_bytes, Bytes(35, 0), 0, 0, 1));
    rejects(altered, directory, "version 1 payload shorter than its fixed policy fields", "invalid payload bounds");
    {
        // The footer places the payload inside the launcher, so the policy
        // would be read from launcher code.
        const auto payload = policy_payload(0, 0, {}, image, image_size);
        for (const std::int64_t inside : {-1, -4, -64, -1000}) {
            craft(assemble(launcher_bytes, payload, inside));
            rejects(altered, directory, "payload offset " + std::to_string(inside) + " bytes inside the launcher");
        }
        craft(assemble(launcher_bytes, payload, -static_cast<std::int64_t>(launcher_bytes.size())));
        rejects(altered, directory, "payload offset zero", "invalid payload bounds");
        craft(assemble(launcher_bytes, payload, 0, 1));
        rejects(altered, directory, "payload size one too large", "invalid payload bounds");
        craft(assemble(launcher_bytes, payload, 0, -1));
        rejects(altered, directory, "payload size one too small", "invalid payload bounds");
        craft(assemble(launcher_bytes, payload, 1, 0));
        rejects(altered, directory, "payload offset one byte late");
    }

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
