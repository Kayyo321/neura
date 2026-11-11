#include "container.hpp"

#include "nvr/format.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#else
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace neura::launcher {
namespace {

constexpr std::string_view magic = "NEURAEXE";
constexpr std::size_t footer_size = 32;
// The fixed fields that follow the file-system prefixes: three limits and the
// image byte count, with the five task settings between them since version 2.
constexpr std::uint64_t task_settings_size = 4 * sizeof(std::uint64_t) + sizeof(std::uint32_t);
constexpr std::uint64_t policy_tail_size(std::uint32_t version) {
    return 24 + (version >= 2 ? task_settings_size : 0);
}
constexpr std::uint64_t minimum_payload_size(std::uint32_t version) { return 12 + policy_tail_size(version); }
constexpr std::size_t io_chunk = 64U << 10;

constexpr std::uint32_t sealed = 1U;
constexpr std::uint32_t files = 1U << 0;
constexpr std::uint32_t network = 1U << 1;
constexpr std::uint32_t environment = 1U << 2;
constexpr std::uint32_t clock = 1U << 3;
constexpr std::uint32_t entropy = 1U << 4;
constexpr std::uint32_t native = 1U << 5;
constexpr std::uint32_t capabilities_mask = files | network | environment | clock | entropy | native;

constexpr auto crc_table = [] {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        auto crc = i;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0U);
        }
        table[i] = crc;
    }
    return table;
}();

constexpr std::uint32_t crc_seed = 0xffffffffU;

std::uint32_t update_crc(std::uint32_t crc, std::span<const std::uint8_t> bytes) {
    for (const auto byte : bytes) crc = (crc >> 8) ^ crc_table[(crc ^ byte) & 0xff];
    return crc;
}

[[noreturn]] void malformed(std::string_view reason) {
    throw std::runtime_error("native executable: " + std::string(reason));
}

[[noreturn]] void no_payload() {
    throw std::runtime_error("this is a Neura launcher with no program; build one with neura build --native");
}

template <class Integer>
Integer little_endian(std::span<const std::uint8_t> bytes) {
    Integer value = 0;
    for (unsigned i = 0; i < sizeof(Integer); ++i) {
        value |= static_cast<Integer>(bytes[i]) << (i * 8);
    }
    return value;
}

template <class Integer>
void append_integer(std::vector<std::uint8_t>& bytes, Integer value) {
    for (unsigned i = 0; i < sizeof(Integer); ++i) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
}

struct Footer {
    std::uint64_t payload_offset;
    std::uint64_t payload_size;
    std::uint32_t checksum;
    std::uint32_t version;
};

Footer read_footer(std::span<const std::uint8_t> tail, std::uint64_t file_size) {
    if (!is_container(tail)) no_payload();
    if (tail.size() < footer_size) malformed("truncated footer");
    tail = tail.last(footer_size);
    const auto version = little_endian<std::uint32_t>(tail.subspan(20));
    if (version == 0 || version > container_version) {
        malformed("unsupported container version " + std::to_string(version));
    }
    const Footer footer{
        little_endian<std::uint64_t>(tail),
        little_endian<std::uint64_t>(tail.subspan(8)),
        little_endian<std::uint32_t>(tail.subspan(16)),
        version,
    };
    const auto footer_offset = file_size - footer_size;
    if (footer.payload_offset == 0 || footer.payload_offset > footer_offset ||
        footer.payload_size != footer_offset - footer.payload_offset ||
        footer.payload_size < minimum_payload_size(version)) {
        malformed("invalid payload bounds");
    }
    return footer;
}

void read_file_bytes(std::ifstream& input, std::span<std::uint8_t> bytes) {
    while (!bytes.empty()) {
        const auto length = std::min(bytes.size(), io_chunk);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length))) {
            malformed("could not read the footer or payload");
        }
        bytes = bytes.subspan(length);
    }
}

class PayloadReader {
public:
    explicit PayloadReader(std::span<const std::uint8_t> bytes) : bytes_(bytes), remaining_(bytes.size()) {}
    PayloadReader(std::ifstream& file, std::uint64_t size) : file_(&file), remaining_(size) {}

    std::uint64_t remaining() const { return remaining_; }
    std::uint32_t checksum() const { return ~crc_; }

    template <class Integer>
    Integer integer() {
        std::array<std::uint8_t, sizeof(Integer)> bytes{};
        read(bytes);
        return little_endian<Integer>(bytes);
    }

    void read(std::span<std::uint8_t> target) {
        if (target.size() > remaining_) malformed("truncated payload");
        if (target.empty()) return;
        if (file_) {
            read_file_bytes(*file_, target);
            crc_ = update_crc(crc_, target);
        } else {
            std::copy_n(bytes_.begin(), target.size(), target.begin());
            bytes_ = bytes_.subspan(target.size());
        }
        remaining_ -= target.size();
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::ifstream* file_ = nullptr;
    std::uint64_t remaining_;
    std::uint32_t crc_ = crc_seed;
};

Container read_payload(PayloadReader& input, const Footer& footer) {
    Container result{.version = footer.version, .policy = {}, .image = {}, .launcher_size = footer.payload_offset};
    auto& policy = result.policy;
    const auto tail_size = policy_tail_size(footer.version);
    const auto flags = input.integer<std::uint32_t>();
    if ((flags & ~sealed) != 0) malformed("reserved policy flags");
    policy.sealed = (flags & sealed) != 0;
    const auto caps = input.integer<std::uint32_t>();
    if ((caps & ~capabilities_mask) != 0) malformed("reserved capability bits");
    policy.capabilities.fs = (caps & files) != 0;
    policy.capabilities.net = (caps & network) != 0;
    policy.capabilities.env = (caps & environment) != 0;
    policy.capabilities.clock = (caps & clock) != 0;
    policy.capabilities.entropy = (caps & entropy) != 0;
    policy.capabilities.native = (caps & native) != 0;

    const auto prefixes = input.integer<std::uint32_t>();
    if (!policy.capabilities.fs && prefixes != 0) malformed("file-system prefixes require the files capability");
    // The minimum payload size guarantees space for the fixed fields here.
    // Each prefix leaves those fields and the remaining length words intact.
    if (prefixes > (input.remaining() - tail_size) / sizeof(std::uint32_t)) {
        malformed("invalid file-system prefix count");
    }
    for (std::uint32_t i = 0; i < prefixes; ++i) {
        const auto size = input.integer<std::uint32_t>();
        const auto trailing = tail_size + std::uint64_t{prefixes - i - 1} * sizeof(std::uint32_t);
        std::string prefix;
        if (size > input.remaining() - trailing || static_cast<std::uint64_t>(size) > prefix.max_size()) {
            malformed("invalid file-system prefix length");
        }
        prefix.resize(size);
        input.read({reinterpret_cast<std::uint8_t*>(prefix.data()), prefix.size()});
        if (prefix.empty()) malformed("file-system prefix is empty");
        if (!nvr::valid_utf8(prefix)) malformed("file-system prefix is not valid UTF-8");
        policy.capabilities.fs_prefixes.push_back(std::move(prefix));
    }

    policy.limits.max_steps = input.integer<std::uint64_t>();
    policy.limits.max_frames = input.integer<std::uint32_t>();
    policy.limits.max_heap_cells = input.integer<std::uint64_t>();
    if (footer.version >= 2) {
        policy.limits.max_tasks = input.integer<std::uint64_t>();
        policy.limits.task_quantum = input.integer<std::uint64_t>();
        policy.limits.max_queued_messages = input.integer<std::uint64_t>();
        policy.limits.max_pending_requests = input.integer<std::uint64_t>();
        policy.limits.parallel_workers = input.integer<std::uint32_t>();
        // A zero here would stop the first task, turn, send, or request, or
        // leave a parallel map with nothing to run on.
        if (policy.limits.max_tasks == 0 || policy.limits.task_quantum == 0 ||
            policy.limits.max_queued_messages == 0 || policy.limits.max_pending_requests == 0 ||
            policy.limits.parallel_workers == 0 || policy.limits.parallel_workers > max_parallel_workers) {
            malformed("invalid task settings");
        }
    }
    const auto image_size = input.integer<std::uint32_t>();
    if (image_size > input.remaining() || static_cast<std::uint64_t>(image_size) > result.image.max_size()) {
        malformed("invalid image length");
    }
    if (image_size < input.remaining()) malformed("trailing bytes after image");
    result.image.resize(image_size);
    input.read(result.image);
    return result;
}

std::vector<std::uint8_t> encode_policy(const Policy& policy, std::uint32_t image_size) {
    const auto& caps = policy.capabilities;
    if (!caps.fs && !caps.fs_prefixes.empty()) malformed("file-system prefixes require the files capability");
    if (caps.fs_prefixes.size() > std::numeric_limits<std::uint32_t>::max()) {
        malformed("too many file-system prefixes");
    }

    std::vector<std::uint8_t> bytes;
    append_integer(bytes, policy.sealed ? sealed : 0U);
    append_integer(bytes, (caps.fs ? files : 0U) | (caps.net ? network : 0U) |
                          (caps.env ? environment : 0U) | (caps.clock ? clock : 0U) |
                          (caps.entropy ? entropy : 0U) | (caps.native ? native : 0U));
    append_integer(bytes, static_cast<std::uint32_t>(caps.fs_prefixes.size()));
    for (const auto& prefix : caps.fs_prefixes) {
        if (prefix.empty()) malformed("file-system prefix is empty");
        if (prefix.size() > std::numeric_limits<std::uint32_t>::max()) malformed("file-system prefix is too long");
        if (!nvr::valid_utf8(prefix)) malformed("file-system prefix is not valid UTF-8");
        append_integer(bytes, static_cast<std::uint32_t>(prefix.size()));
        bytes.insert(bytes.end(), prefix.begin(), prefix.end());
    }
    append_integer(bytes, policy.limits.max_steps);
    append_integer(bytes, policy.limits.max_frames);
    append_integer(bytes, policy.limits.max_heap_cells);
    const auto& limits = policy.limits;
    if (limits.max_tasks == 0 || limits.task_quantum == 0 || limits.max_queued_messages == 0 ||
        limits.max_pending_requests == 0 || limits.parallel_workers == 0 ||
        limits.parallel_workers > max_parallel_workers) {
        malformed("invalid task settings");
    }
    append_integer(bytes, limits.max_tasks);
    append_integer(bytes, limits.task_quantum);
    append_integer(bytes, limits.max_queued_messages);
    append_integer(bytes, limits.max_pending_requests);
    append_integer(bytes, limits.parallel_workers);
    append_integer(bytes, image_size);
    return bytes;
}

class AtomicOutput {
public:
    explicit AtomicOutput(const std::filesystem::path& destination) : destination_(destination) {
#ifdef _WIN32
        static std::atomic<unsigned long long> sequence{0};
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            const auto name = L".neura-native-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
            temporary_ = (destination.parent_path() / name).native();
            file_ = CreateFileW(temporary_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file_ != INVALID_HANDLE_VALUE) return;
            if (GetLastError() != ERROR_FILE_EXISTS) fail("create temporary output");
        }
        fail("create temporary output");
#else
        temporary_ = (destination.parent_path() / ".neura-native-XXXXXX").native();
        file_ = ::mkstemp(temporary_.data());
        if (file_ < 0) fail("create temporary output");
#endif
    }

    ~AtomicOutput() {
#ifdef _WIN32
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
        if (!temporary_.empty()) DeleteFileW(temporary_.c_str());
#else
        if (file_ >= 0) ::close(file_);
        if (!temporary_.empty()) ::unlink(temporary_.c_str());
#endif
    }

    AtomicOutput(const AtomicOutput&) = delete;
    AtomicOutput& operator=(const AtomicOutput&) = delete;

    void write(std::span<const std::uint8_t> bytes) {
        while (!bytes.empty()) {
            const auto length = std::min(bytes.size(), io_chunk);
#ifdef _WIN32
            DWORD written = 0;
            if (!WriteFile(file_, bytes.data(), static_cast<DWORD>(length), &written, nullptr)) fail("write output");
#else
            const auto written = ::write(file_, bytes.data(), length);
            if (written < 0) {
                if (errno == EINTR) continue;
                fail("write output");
            }
#endif
            if (written == 0) throw std::runtime_error("could not write " + destination_.string());
            bytes = bytes.subspan(static_cast<std::size_t>(written));
        }
    }

    void publish() {
#ifdef _WIN32
        if (!FlushFileBuffers(file_)) fail("flush output");
        const auto closed = CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
        if (!closed) fail("close output");
        if (!MoveFileExW(temporary_.c_str(), destination_.c_str(), MOVEFILE_REPLACE_EXISTING)) fail("replace output");
#else
        if (::fchmod(file_, 0755) != 0) fail("set output mode");
        while (::fsync(file_) != 0) {
            if (errno != EINTR) fail("flush output");
        }
        const auto closed = ::close(file_);
        file_ = -1;
        if (closed != 0) fail("close output");
        if (::rename(temporary_.c_str(), destination_.c_str()) != 0) fail("replace output");
#endif
        temporary_.clear();
    }

private:
    [[noreturn]] void fail(std::string_view operation) const {
#ifdef _WIN32
        const std::error_code error{static_cast<int>(GetLastError()), std::system_category()};
#else
        const std::error_code error{errno, std::generic_category()};
#endif
        throw std::system_error(error, std::string(operation) + " " + destination_.string());
    }

    const std::filesystem::path& destination_;
    std::filesystem::path::string_type temporary_;
#ifdef _WIN32
    HANDLE file_ = INVALID_HANDLE_VALUE;
#else
    int file_ = -1;
#endif
};

} // namespace

bool is_container(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < magic.size()) return false;
    return std::ranges::equal(bytes.last(magic.size()), magic);
}

Container read_container(std::span<const std::uint8_t> bytes) {
    const auto footer = read_footer(bytes, bytes.size());
    const auto payload = bytes.subspan(static_cast<std::size_t>(footer.payload_offset),
                                       static_cast<std::size_t>(footer.payload_size));
    if (~update_crc(crc_seed, payload) != footer.checksum) malformed("payload checksum mismatch");
    PayloadReader reader(payload);
    return read_payload(reader, footer);
}

Container read_container(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("could not open " + path.string());
    const auto end = static_cast<std::streamoff>(input.tellg());
    if (end < 0) throw std::runtime_error("could not determine file size of " + path.string());
    const auto size = static_cast<std::uint64_t>(end);

    std::array<std::uint8_t, footer_size> tail{};
    const auto tail_length = static_cast<std::size_t>(std::min<std::uint64_t>(size, tail.size()));
    input.seekg(static_cast<std::streamoff>(size - tail_length));
    read_file_bytes(input, std::span(tail).first(tail_length));
    const auto footer = read_footer(std::span(tail).first(tail_length), size);

    const auto payload_offset = static_cast<std::streamoff>(footer.payload_offset);
    input.seekg(payload_offset);
    std::array<std::uint8_t, io_chunk> chunk{};
    auto crc = crc_seed;
    auto remaining = footer.payload_size;
    while (remaining != 0) {
        const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, chunk.size()));
        const auto part = std::span(chunk).first(length);
        read_file_bytes(input, part);
        crc = update_crc(crc, part);
        remaining -= length;
    }
    if (~crc != footer.checksum) malformed("payload checksum mismatch");

    input.seekg(payload_offset);
    PayloadReader reader(input, footer.payload_size);
    auto result = read_payload(reader, footer);
    // Check the bytes used for decoding too, in case the open file was rewritten.
    if (reader.checksum() != footer.checksum) malformed("payload checksum mismatch");
    return result;
}

void write_executable(const std::filesystem::path& path, std::span<const std::uint8_t> launcher,
                      std::span<const std::uint8_t> image, const Policy& policy) {
    if (launcher.empty()) malformed("empty launcher");
    if (image.size() > std::numeric_limits<std::uint32_t>::max()) malformed("image must be smaller than 4 GiB");
    const auto policy_bytes = encode_policy(policy, static_cast<std::uint32_t>(image.size()));
    const auto checksum = ~update_crc(update_crc(crc_seed, policy_bytes), image);

    std::vector<std::uint8_t> footer;
    footer.reserve(footer_size);
    append_integer(footer, static_cast<std::uint64_t>(launcher.size()));
    append_integer(footer, std::uint64_t{policy_bytes.size()} + image.size());
    append_integer(footer, checksum);
    append_integer(footer, container_version);
    footer.insert(footer.end(), magic.begin(), magic.end());

    AtomicOutput output(path);
    output.write(launcher);
    output.write(policy_bytes);
    output.write(image);
    output.write(footer);
    output.publish();
}

} // namespace neura::launcher
