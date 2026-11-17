#include "lifecycle.hpp"

#include "install_layout.hpp"
#include "tool.hpp"
#include "release_channel.hpp"
#include "neura/version.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef _WIN32
#ifdef NEURA_HAS_GPU
#include "gpu/device.hpp"
#endif
#include "nvm/providers.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace neura::lifecycle {

namespace fs = std::filesystem;

std::string version_line() {
    return tool::version_line();
}

#ifdef _WIN32

int doctor(const fs::path&, const DoctorOptions&) {
    std::cerr << "neura: doctor is not supported on Windows; run neura under WSL2\n";
    return exit_failure;
}
int update(const fs::path&, const UpdateOptions&) {
    std::cerr << "neura: update is not supported on Windows; run neura under WSL2\n";
    return exit_failure;
}
int uninstall(const fs::path&, const UninstallOptions&) {
    std::cerr << "neura: uninstall is not supported on Windows; run neura under WSL2\n";
    return exit_failure;
}
void update_notice(const fs::path&) {}

#else

namespace {

constexpr const char* repository = "Kayyo321/neura";
constexpr std::uintmax_t max_metadata_bytes = std::uintmax_t{1} << 20;
constexpr std::size_t max_capture_bytes = std::size_t{16} << 20;

struct Failure {
    std::string message;
};

// ---------------------------------------------------------------------------
// Files and paths

std::string describe(const std::error_code& error) { return error ? error.message() : "unknown error"; }

bool exists_no_follow(const fs::path& path) {
    struct stat info {};
    return ::lstat(path.c_str(), &info) == 0;
}

bool is_symlink_no_follow(const fs::path& path) {
    struct stat info {};
    return ::lstat(path.c_str(), &info) == 0 && S_ISLNK(info.st_mode);
}

bool is_directory_no_follow(const fs::path& path) {
    struct stat info {};
    return ::lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

std::optional<std::time_t> modified_no_follow(const fs::path& path) {
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) return std::nullopt;
    return info.st_mtime;
}

// Removes a file, a symbolic link (never its target), or a directory tree.
// A link inside the tree is unlinked, not followed.
void remove_tree(const fs::path& path, std::error_code& error) {
    error.clear();
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) {
        if (errno != ENOENT) error = std::error_code(errno, std::generic_category());
        return;
    }
    if (S_ISDIR(info.st_mode)) {
        // A directory extracted without write permission could not be emptied.
        ::chmod(path.c_str(), info.st_mode | S_IRWXU);
        std::vector<fs::path> children;
        {
            std::error_code listing;
            for (fs::directory_iterator it(path, listing), end; !listing && it != end; it.increment(listing)) {
                children.push_back(it->path());
            }
            if (listing) {
                error = listing;
                return;
            }
        }
        for (const auto& child : children) {
            std::error_code child_error;
            remove_tree(child, child_error);
            if (child_error && !error) error = child_error;
        }
        if (::rmdir(path.c_str()) != 0 && !error) error = std::error_code(errno, std::generic_category());
    } else if (::unlink(path.c_str()) != 0 && !error) {
        error = std::error_code(errno, std::generic_category());
    }
}

bool path_inside(const fs::path& path, const fs::path& root) {
    auto p = path.begin();
    for (auto r = root.begin(); r != root.end(); ++r, ++p) {
        if (p == path.end() || *p != *r) return false;
    }
    return p != path.end();
}

// A new file next to nothing else: O_EXCL with a unique name, mode 0600.
fs::path make_temp_file(const fs::path& directory, const std::string& prefix, int& descriptor) {
    std::string name = (directory / (prefix + "XXXXXX")).string();
    descriptor = ::mkstemp(name.data());
    if (descriptor < 0) throw Failure{"cannot create a temporary file in " + directory.string() + ": " + std::strerror(errno)};
    return name;
}

void write_all(int descriptor, std::string_view text) {
    while (!text.empty()) {
        const auto count = ::write(descriptor, text.data(), text.size());
        if (count < 0) {
            if (errno == EINTR) continue;
            throw Failure{std::string("write failed: ") + std::strerror(errno)};
        }
        text.remove_prefix(static_cast<std::size_t>(count));
    }
}

// Writes `text` to a new temporary file in `directory` and returns its path.
fs::path write_temp_file(const fs::path& directory, const std::string& prefix, std::string_view text) {
    int descriptor = -1;
    const auto path = make_temp_file(directory, prefix, descriptor);
    try {
        write_all(descriptor, text);
        ::fsync(descriptor);
    } catch (...) {
        ::close(descriptor);
        ::unlink(path.c_str());
        throw;
    }
    ::close(descriptor);
    return path;
}

std::string read_small_file(const fs::path& path, const char* what) {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) throw Failure{std::string("cannot read ") + what + " " + path.string() + ": " + std::strerror(errno)};
    if (!S_ISREG(info.st_mode)) throw Failure{std::string(what) + " " + path.string() + " is not a regular file"};
    if (static_cast<std::uintmax_t>(info.st_size) > max_metadata_bytes) throw Failure{std::string(what) + " " + path.string() + " is larger than 1 MiB"};
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Failure{std::string("cannot read ") + what + " " + path.string()};
    return std::string(std::istreambuf_iterator<char>(input), {});
}

fs::path find_in_path(std::string_view name) {
    if (name.find('/') != std::string_view::npos) {
        std::error_code error;
        const fs::path candidate(name);
        return fs::is_regular_file(candidate, error) && ::access(candidate.c_str(), X_OK) == 0 ? candidate : fs::path{};
    }
    const char* path = std::getenv("PATH");
    if (path == nullptr) return {};
    std::string_view remaining(path);
    for (;;) {
        const auto end = remaining.find(':');
        const auto directory = remaining.substr(0, end);
        const fs::path candidate = (directory.empty() ? fs::path(".") : fs::path(directory)) / name;
        std::error_code error;
        if (fs::is_regular_file(candidate, error) && ::access(candidate.c_str(), X_OK) == 0) return candidate;
        if (end == std::string_view::npos) return {};
        remaining.remove_prefix(end + 1);
    }
}

bool env_set(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0';
}

// ---------------------------------------------------------------------------
// Child processes: an argument vector, never a shell.

struct ChildResult {
    bool started = false;
    bool timed_out = false;
    bool output_too_large = false;
    int status = -1;
    std::string out;
    std::string err;
};

ChildResult run_child(const std::vector<std::string>& arguments, int timeout_seconds) {
    ChildResult result;
    int out_pipe[2];
    int err_pipe[2];
    if (::pipe(out_pipe) != 0) return result;
    if (::pipe(err_pipe) != 0) {
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        return result;
    }
    for (const int descriptor : {out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) ::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
    std::vector<char*> argv;
    std::vector<std::string> copy = arguments;
    for (auto& argument : copy) argv.push_back(argument.data());
    argv.push_back(nullptr);
    const pid_t child = ::fork();
    if (child < 0) {
        for (const int descriptor : {out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) ::close(descriptor);
        return result;
    }
    if (child == 0) {
        const int null = ::open("/dev/null", O_RDONLY);
        if (null >= 0) ::dup2(null, 0);
        ::dup2(out_pipe[1], 1);
        ::dup2(err_pipe[1], 2);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }
    result.started = true;
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    int fds[2] = {out_pipe[0], err_pipe[0]};
    std::string* sinks[2] = {&result.out, &result.err};
    const auto deadline = std::time(nullptr) + timeout_seconds;
    while (fds[0] >= 0 || fds[1] >= 0) {
        pollfd polls[2];
        nfds_t count = 0;
        int which[2];
        for (int i = 0; i < 2; ++i) {
            if (fds[i] < 0) continue;
            polls[count] = {fds[i], POLLIN, 0};
            which[count++] = i;
        }
        int wait_ms = -1;
        if (timeout_seconds > 0) {
            const auto left = deadline - std::time(nullptr);
            if (left <= 0) {
                result.timed_out = true;
                ::kill(child, SIGKILL);
                break;
            }
            wait_ms = static_cast<int>(std::min<std::time_t>(left, 3600) * 1000);
        }
        const int ready = ::poll(polls, count, wait_ms);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (nfds_t i = 0; i < count; ++i) {
            if ((polls[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) continue;
            char buffer[4096];
            const auto got = ::read(polls[i].fd, buffer, sizeof buffer);
            if (got > 0) {
                auto& sink = *sinks[which[i]];
                if (sink.size() + static_cast<std::size_t>(got) > max_capture_bytes) {
                    result.output_too_large = true;
                    ::kill(child, SIGKILL);
                    ::close(fds[which[i]]);
                    fds[which[i]] = -1;
                } else {
                    sink.append(buffer, static_cast<std::size_t>(got));
                }
            } else if (got == 0 || errno != EINTR) {
                ::close(fds[which[i]]);
                fds[which[i]] = -1;
            }
        }
    }
    for (const int descriptor : fds) {
        if (descriptor >= 0) ::close(descriptor);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    result.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return result;
}

std::string first_line(const std::string& text) {
    std::string line = text.substr(0, text.find('\n'));
    if (line.size() > 300) line.resize(300);
    return line;
}

std::string failure_detail(const ChildResult& result) {
    if (result.timed_out) return "it did not finish in time";
    const auto text = result.err.empty() ? result.out : result.err;
    return text.empty() ? "exit status " + std::to_string(result.status) : first_line(text);
}

// The curl command line for fetching `url` into `output`.
std::vector<std::string> curl_command(const std::string& url, const fs::path& output, int max_seconds, std::uint64_t max_bytes) {
    // -q first: ignore any .curlrc. Only HTTPS, including after redirects.
    return {"curl", "-q", "--proto", "=https", "--proto-redir", "=https", "--tlsv1.2", "--fail", "--location", "--silent",
            "--show-error", "--max-time", std::to_string(max_seconds), "--max-filesize", std::to_string(max_bytes), "--output",
            output.string(), "--url", url};
}

// ---------------------------------------------------------------------------
// Install detection and messages

int refuse_unmanaged(const Detection& detection, const fs::path& executable, const char* command) {
    if (detection.kind == Detection::Kind::Broken) {
        std::cerr << "neura: " << command << ": the install marker is not valid: " << detection.problem << "\n";
        return exit_failure;
    }
    if (detection.kind == Detection::Kind::Installed) {
        const auto& method = detection.marker.method;
        std::cerr << "neura: " << command << ": this install is managed by " << method << ", not by neura (" << executable.string()
                  << ").\nUse " << method << " to " << (std::string_view(command) == "update" ? "update" : "remove") << " neura.\n";
        return exit_unmanaged;
    }
    std::cerr << "neura: " << command << ": " << executable.string()
              << " is not an archive install, so neura does not manage it.\n"
                 "It was probably built from a source checkout or copied from another install. "
              << (std::string_view(command) == "update" ? "Rebuild it from the checkout, or install a release archive "
                                                         "(see docs/Install.md) to use \"neura update\"."
                                                         : "Remove its files yourself; neura did not install them.")
              << "\n";
    return exit_unmanaged;
}

// ---------------------------------------------------------------------------
// neura update

// ---------------------------------------------------------------------------
// Test hooks. NEURA_TEST_UPDATE_FAULT=<step> makes the named step fail, and
// NEURA_TEST_UPDATE_CRASH=<step> ends the process there without cleanup
// (docs/design/Lifecycle030.md). Both exist for the tests only.

void step_point(std::string_view step) {
    const char* fault = std::getenv("NEURA_TEST_UPDATE_FAULT");
    if (fault != nullptr && step == fault) throw Failure{"injected fault at step " + std::string(step) + " (NEURA_TEST_UPDATE_FAULT)"};
    const char* crash = std::getenv("NEURA_TEST_UPDATE_CRASH");
    if (crash != nullptr && step == crash) {
        std::cout.flush();
        std::cerr << "neura: crashing at step " << step << " (NEURA_TEST_UPDATE_CRASH)\n";
        std::cerr.flush();
        ::_exit(70);
    }
}


struct RemoveOnExit {
    fs::path path;
    explicit RemoveOnExit(fs::path p) : path(std::move(p)) {}
    ~RemoveOnExit() {
        if (!path.empty()) {
            std::error_code ignored;
            remove_tree(path, ignored);
        }
    }
    RemoveOnExit(const RemoveOnExit&) = delete;
    RemoveOnExit& operator=(const RemoveOnExit&) = delete;
};

bool archive_member_ok(std::string_view name, std::string_view top) {
    if (name.empty() || name.front() == '/') return false;
    if (std::ranges::any_of(name, [](unsigned char c) { return c < 0x20 || c == 0x7f || c == '\\'; })) return false;
    if (name.back() == '/') name.remove_suffix(1);
    bool first = true;
    while (true) {
        const auto slash = name.find('/');
        const auto part = name.substr(0, slash);
        if (part.empty() || part == "." || part == "..") return false;
        if (first && part != top) return false;
        first = false;
        if (slash == std::string_view::npos) return true;
        name.remove_prefix(slash + 1);
    }
}

// After extraction the staging directory holds exactly `top`, and `top`
// holds only regular files and directories.
void check_extracted_tree(const fs::path& staging, const std::string& top) {
    std::size_t entries = 0;
    for (const auto& entry : fs::directory_iterator(staging)) {
        if (entry.path().filename() != top || !is_directory_no_follow(entry.path())) {
            throw Failure{"the archive extracted " + entry.path().filename().string() + ", not only the directory " + top};
        }
        ++entries;
    }
    if (entries != 1) throw Failure{"the archive did not extract the directory " + top};
    std::vector<fs::path> pending{staging / top};
    while (!pending.empty()) {
        const auto directory = pending.back();
        pending.pop_back();
        for (const auto& entry : fs::directory_iterator(directory)) {
            const auto status = entry.symlink_status();
            if (fs::is_directory(status)) {
                pending.push_back(entry.path());
            } else if (!fs::is_regular_file(status)) {
                throw Failure{"the archive holds " + entry.path().string() + ", which is not a regular file or a directory"};
            } else if (fs::hard_link_count(entry.path()) > 1) {
                // GNU tar lists a hard link as its own kind, which extract() rejects
                // before unpacking. bsdtar (macOS) lists it like a regular file, so
                // it is found here: a fresh tree has no file with two names.
                throw Failure{"the archive holds " + entry.path().string() + ", which is not a regular file or a directory"};
            }
        }
    }
}

struct Fetcher {
    std::string source;
    bool remote = false;

    fs::path local_file(const std::string& name) const { return fs::path(source) / name; }
};

std::string stripped_source(std::string source) {
    while (source.size() > 1 && source.back() == '/') source.pop_back();
    return source;
}

Fetcher make_fetcher(const std::string& given) {
    Fetcher fetcher;
    if (given.empty()) throw Failure{"--source is empty"};
    if (std::ranges::any_of(given, [](unsigned char c) { return c < 0x20 || c == 0x7f || c == ' '; })) {
        throw Failure{"--source holds a space or control character"};
    }
    if (given.find("://") != std::string::npos) {
        if (!given.starts_with("https://") || given.size() == 8) {
            throw Failure{"--source accepts an https:// URL or a local directory, not " + given};
        }
        fetcher.remote = true;
    } else if (!fs::is_directory(given)) {
        throw Failure{"--source " + given + " is not a directory"};
    }
    fetcher.source = stripped_source(given);
    return fetcher;
}

PublishedRelease newest_github_release(const fs::path& root) {
    if (find_in_path("curl").empty()) throw Failure{"curl is not installed; --nightly needs it to check GitHub releases"};
    constexpr std::size_t page_size = 20;
    constexpr std::size_t max_pages = 100;
    const auto deadline = std::time(nullptr) + 120;
    std::optional<PublishedRelease> selected;
    for (std::size_t page_number = 1; page_number <= max_pages; ++page_number) {
        const auto remaining = deadline - std::time(nullptr);
        if (remaining <= 0) throw Failure{"the --nightly GitHub release check exceeded its 120 second limit"};
        const int seconds = static_cast<int>(std::min<std::time_t>(remaining, 30));
        const std::string url = std::string("https://api.github.com/repos/") + repository + "/releases?per_page=" +
                                std::to_string(page_size) + "&page=" + std::to_string(page_number);
        int descriptor = -1;
        const auto temp = make_temp_file(root, ".meta-", descriptor);
        ::close(descriptor);
        RemoveOnExit cleanup(temp);
        auto command = curl_command(url, temp, seconds, max_metadata_bytes);
        command.insert(command.end(), {"--header", "Accept: application/vnd.github+json", "--header", "X-GitHub-Api-Version: 2022-11-28"});
        const auto result = run_child(command, seconds + 5);
        if (!result.started || result.status != 0) {
            throw Failure{"cannot check GitHub releases for --nightly: " + (result.started ? failure_detail(result) : "curl could not be run")};
        }
        std::string error;
        const auto page = parse_github_release_page(read_small_file(temp, "GitHub releases"), error);
        if (!page || page->count > page_size) throw Failure{"cannot check GitHub releases for --nightly: " + (page ? "oversized page" : error)};
        select_newest_release(selected, *page);
        if (page->count < page_size) {
            if (!selected) throw Failure{"GitHub has no published Neura release with a valid semantic-version tag"};
            return *selected;
        }
    }
    throw Failure{"the --nightly GitHub release list exceeded 100 pages; no update was selected"};
}

ReleaseMetadata fetch_metadata(const Fetcher& fetcher, const fs::path& root, int max_seconds) {
    std::string text;
    if (fetcher.remote) {
        if (find_in_path("curl").empty()) throw Failure{"curl is not installed; it is needed to download from " + fetcher.source};
        int descriptor = -1;
        const auto temp = make_temp_file(root, ".meta-", descriptor);
        ::close(descriptor);
        RemoveOnExit cleanup(temp);
        const auto result = run_child(curl_command(fetcher.source + "/neura-release.txt", temp, max_seconds, max_metadata_bytes), max_seconds + 10);
        if (!result.started || result.status != 0) {
            throw Failure{"cannot fetch " + fetcher.source + "/neura-release.txt: " + (result.started ? failure_detail(result) : "curl could not be run")};
        }
        text = read_small_file(temp, "release metadata");
    } else {
        text = read_small_file(fetcher.local_file("neura-release.txt"), "release metadata");
    }
    std::string error;
    auto metadata = parse_release_metadata(text, error);
    if (!metadata) throw Failure{"the release metadata is not valid: " + error};
    return *metadata;
}

// Copies or downloads the asset into `staging` and returns its path.
fs::path fetch_asset(const Fetcher& fetcher, const ReleaseAsset& asset, const fs::path& staging) {
    step_point("download");
    const auto destination = staging / asset.file;
    if (fetcher.remote) {
        if (find_in_path("curl").empty()) throw Failure{"curl is not installed; it is needed to download from " + fetcher.source};
        const auto result = run_child(curl_command(fetcher.source + "/" + asset.file, destination, 1800, asset.size), 1810);
        if (!result.started || result.status != 0) {
            throw Failure{"cannot download " + asset.file + ": " + (result.started ? failure_detail(result) : "curl could not be run")};
        }
    } else {
        const auto source = fetcher.local_file(asset.file);
        struct stat info {};
        if (::stat(source.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
            throw Failure{"cannot read " + source.string() + " (the file the release metadata names)"};
        }
        std::error_code error;
        fs::copy_file(source, destination, fs::copy_options::none, error);
        if (error) throw Failure{"cannot copy " + source.string() + ": " + describe(error)};
    }
    return destination;
}

void check_size_and_digest(const fs::path& file, const ReleaseAsset& asset) {
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error) throw Failure{"cannot read the downloaded file: " + describe(error)};
    if (size != asset.size) {
        throw Failure{"the downloaded file is " + std::to_string(size) + " bytes; the release metadata says " + std::to_string(asset.size)};
    }
    step_point("checksum");
    const auto digest = util::sha256_file(file);
    if (!digest) throw Failure{"cannot read the downloaded file to check its SHA-256"};
    if (digest->hex != asset.sha256) {
        throw Failure{"SHA-256 mismatch: the downloaded file is " + digest->hex + "; the release metadata says " + asset.sha256};
    }
}

std::string verify_provenance(const fs::path& file, bool local_source, bool require) {
    const auto gh = find_in_path("gh");
    if (gh.empty()) {
        if (require) throw Failure{"--require-attestation needs gh (the GitHub CLI), which is not on PATH"};
        return "NOT verified: build provenance. gh is not installed, so integrity rests on the SHA-256 in the release metadata" +
               std::string(local_source ? " read from the local source." : " fetched over HTTPS.");
    }
    if (local_source && !require) return "not checked: build provenance (a local source is not attested)";
    // gh can verify nothing until it is signed in. That is a missing tool,
    // not a failed verification: only a check that ran and failed says the
    // file is not what the release workflow built.
    const auto signed_in = run_child({gh.string(), "auth", "status"}, 30);
    if (!signed_in.started || signed_in.status != 0) {
        if (require) {
            throw Failure{"--require-attestation needs gh (the GitHub CLI) to be signed in; run \"gh auth login\" or set GH_TOKEN"};
        }
        return "NOT verified: build provenance. gh is installed but not signed in, so integrity rests on the SHA-256 in the "
               "release metadata" + std::string(local_source ? " read from the local source." : " fetched over HTTPS.");
    }
    const auto result = run_child({gh.string(), "attestation", "verify", file.string(), "--repo", repository}, 180);
    if (!result.started || result.status != 0) {
        throw Failure{"build provenance verification failed (gh attestation verify): " + (result.started ? failure_detail(result) : "gh could not be run")};
    }
    return std::string("verified: build provenance with gh attestation verify --repo ") + repository;
}

void extract(const fs::path& archive, const fs::path& staging, const std::string& top) {
    step_point("extract");
    if (find_in_path("tar").empty()) throw Failure{"tar is not installed; it is needed to unpack the release archive"};
    const auto names = run_child({"tar", "-tzf", archive.string()}, 300);
    if (!names.started || names.status != 0 || names.output_too_large) {
        throw Failure{"cannot list the archive: " + (names.output_too_large ? std::string("the listing is too large") : failure_detail(names))};
    }
    const auto details = run_child({"tar", "-tvzf", archive.string()}, 300);
    if (!details.started || details.status != 0 || details.output_too_large) {
        throw Failure{"cannot list the archive: " + (details.output_too_large ? std::string("the listing is too large") : failure_detail(details))};
    }
    std::vector<std::string> members;
    std::istringstream name_stream(names.out);
    for (std::string line; std::getline(name_stream, line);) members.push_back(line);
    std::vector<std::string> kinds;
    std::istringstream detail_stream(details.out);
    for (std::string line; std::getline(detail_stream, line);) kinds.push_back(line);
    if (members.empty() || members.size() != kinds.size()) throw Failure{"the archive listing is not readable"};
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (!archive_member_ok(members[i], top)) throw Failure{"the archive member \"" + members[i] + "\" is not inside " + top + "/"};
        if (kinds[i].empty() || (kinds[i][0] != '-' && kinds[i][0] != 'd')) {
            throw Failure{"the archive member \"" + members[i] + "\" is not a regular file or a directory"};
        }
    }
    const auto unpacked = run_child({"tar", "-xzf", archive.string(), "-C", staging.string(), "--no-same-owner"}, 600);
    if (!unpacked.started || unpacked.status != 0) throw Failure{"cannot extract the archive: " + failure_detail(unpacked)};
    check_extracted_tree(staging, top);
}

void replace_symlink(const fs::path& root, const std::string& target) {
    const auto temporary = root / (".current-" + std::to_string(::getpid()));
    ::unlink(temporary.c_str());
    if (::symlink(target.c_str(), temporary.c_str()) != 0) throw Failure{std::string("cannot create a symbolic link: ") + std::strerror(errno)};
    if (::rename(temporary.c_str(), (root / "current").c_str()) != 0) {
        const auto saved = errno;
        ::unlink(temporary.c_str());
        throw Failure{std::string("cannot replace ") + (root / "current").string() + ": " + std::strerror(saved)};
    }
}

void sync_directory(const fs::path& directory) {
    const int descriptor = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor >= 0) {
        ::fsync(descriptor);
        ::close(descriptor);
    }
}

// Writes `text` to `path` by a temporary file and one rename.
void replace_file(const fs::path& path, std::string_view text) {
    const auto temporary = write_temp_file(path.parent_path(), "." + path.filename().string() + "-", text);
    ::chmod(temporary.c_str(), 0644);
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        const auto saved = errno;
        ::unlink(temporary.c_str());
        throw Failure{"cannot write " + path.string() + ": " + std::strerror(saved)};
    }
    sync_directory(path.parent_path());
}

bool lock_unsupported(int error) { return error == ENOLCK || error == EOPNOTSUPP || error == ENOSYS || error == EINVAL; }

// ---------------------------------------------------------------------------
// The installation lock (step 1)

class InstallLock {
public:
    // `legacy` is true on a root whose marker has schema 1, where a 0.2
    // updater may run and the 0.2 lock file is honoured.
    InstallLock(const fs::path& root, bool legacy) : root_(root) {
        const auto path = root / install_lock_name;
        descriptor_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (descriptor_ < 0) throw Failure{"cannot open the installation lock " + path.string() + ": " + std::strerror(errno)};
        if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            const auto saved = errno;
            if (saved == EWOULDBLOCK) {
                ::close(descriptor_);
                descriptor_ = -1;
                throw Failure{"another install, update or uninstall is running (lock " + path.string() + ")"};
            }
            if (!lock_unsupported(saved)) {
                ::close(descriptor_);
                descriptor_ = -1;
                throw Failure{"cannot lock " + path.string() + ": " + std::strerror(saved)};
            }
            // A file system without flock: the command runs unlocked, and
            // nothing is pruned (docs/Install.md).
            unlocked_ = true;
        }
        const auto legacy_path = root / lock_name;
        if (legacy) {
            for (int attempt = 0; attempt < 2 && !legacy_held_; ++attempt) {
                const int file = ::open(legacy_path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
                if (file >= 0) {
                    write_all(file, "neura-lifecycle " + std::to_string(::getpid()) + "\n");
                    ::close(file);
                    legacy_held_ = true;
                    break;
                }
                if (errno != EEXIST) throw Failure{"cannot create the 0.2 update lock " + legacy_path.string() + ": " + std::strerror(errno)};
                // One this tool wrote is left over from an interrupted
                // command, since this process holds the installation lock.
                std::string text;
                try {
                    text = read_small_file(legacy_path, "the 0.2 update lock");
                } catch (const Failure&) {
                }
                if (text.starts_with("neura-lifecycle ") && !unlocked_) {
                    ::unlink(legacy_path.c_str());
                    continue;
                }
                const auto modified = modified_no_follow(legacy_path);
                if (modified && std::time(nullptr) - *modified > 3600) {
                    throw Failure{"the 0.2 update lock " + legacy_path.string() +
                                  " is more than an hour old. No update appears to be running; delete the file and run the command again"};
                }
                throw Failure{"a 0.2 update is running (lock " + legacy_path.string() + "); run the command again when it has finished"};
            }
        } else if (exists_no_follow(legacy_path)) {
            // No 0.2 client runs on a schema 2 root: the file is left over
            // from an interrupted migration.
            ::unlink(legacy_path.c_str());
        }
    }
    ~InstallLock() {
        if (legacy_held_) ::unlink((root_ / lock_name).c_str());
        if (descriptor_ >= 0) ::close(descriptor_);
    }
    InstallLock(const InstallLock&) = delete;
    InstallLock& operator=(const InstallLock&) = delete;

    bool unlocked() const { return unlocked_; }

private:
    fs::path root_;
    int descriptor_ = -1;
    bool legacy_held_ = false;
    bool unlocked_ = false;
};

// ---------------------------------------------------------------------------
// Generations and their manifests

struct Generation {
    // name -> (manifest, its bytes), in component order.
    std::vector<std::pair<ComponentManifest, std::string>> manifests;

    std::vector<std::string> components() const {
        std::vector<std::string> names;
        for (const auto& [manifest, text] : manifests) names.push_back(manifest.name);
        return names;
    }
    std::set<std::string> tools() const {
        std::set<std::string> names;
        for (const auto& [manifest, text] : manifests) names.insert(manifest.tools.begin(), manifest.tools.end());
        return names;
    }
    const ComponentManifest* find(std::string_view name) const {
        for (const auto& [manifest, text] : manifests) {
            if (manifest.name == name) return &manifest;
        }
        return nullptr;
    }
    const std::string* text(std::string_view name) const {
        for (const auto& [manifest, bytes] : manifests) {
            if (manifest.name == name) return &bytes;
        }
        return nullptr;
    }
    const std::string& version() const { return manifests.front().first.version; }
    const std::string& build() const { return manifests.front().first.build; }
    const std::string& target() const { return manifests.front().first.target; }
    std::string name() const {
        std::vector<std::pair<std::string, std::string>> parts;
        for (const auto& [manifest, text] : manifests) parts.emplace_back(manifest.name, text);
        return generation_name(version(), parts);
    }
};

std::string join_names(const std::vector<std::string>& names, std::string_view separator = " ") {
    std::string text;
    for (const auto& name : names) text += (text.empty() ? "" : std::string(separator)) + name;
    return text;
}

// Reads share/neura/components/*.txt of a tree. Throws a Failure naming the
// first problem. `unknown` collects component names this neura does not
// know; without it such a name is an error.
Generation read_generation(const fs::path& tree, std::vector<std::string>* unknown = nullptr) {
    const auto directory = tree / components_directory;
    if (!is_directory_no_follow(directory)) throw Failure{tree.string() + " has no component manifests (" + std::string(components_directory) + ")"};
    Generation generation;
    std::vector<fs::path> files;
    std::error_code error;
    for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) files.push_back(it->path());
    if (error) throw Failure{"cannot list " + directory.string() + ": " + describe(error)};
    for (const auto& file : files) {
        const auto name = file.filename().string();
        if (!name.ends_with(".txt") || !fs::is_regular_file(fs::symlink_status(file))) {
            throw Failure{file.string() + " is not a component manifest"};
        }
        const auto text = read_small_file(file, "the component manifest");
        std::string problem;
        auto manifest = parse_manifest(text, problem);
        if (!manifest) throw Failure{file.string() + ": " + problem};
        if (manifest->name + ".txt" != name) throw Failure{file.string() + " names the component " + manifest->name};
        if (!known_component(manifest->name)) {
            if (unknown == nullptr) throw Failure{file.string() + ": the component " + manifest->name + " is not one this neura knows"};
            unknown->push_back(manifest->name);
            continue;
        }
        if (const auto rule = check_known_manifest(*manifest); !rule.empty()) throw Failure{file.string() + ": " + rule};
        generation.manifests.emplace_back(std::move(*manifest), text);
    }
    std::ranges::sort(generation.manifests, [](const auto& a, const auto& b) { return component_before(a.first.name, b.first.name); });
    if (generation.manifests.empty() || generation.manifests.front().first.name != "core") {
        throw Failure{tree.string() + " has no core component manifest"};
    }
    std::set<std::string> paths;
    for (const auto& [manifest, text] : generation.manifests) {
        const auto& core = generation.manifests.front().first;
        if (manifest.version != core.version || manifest.build != core.build || manifest.target != core.target) {
            throw Failure{"the " + manifest.name + " component is version " + manifest.version + ", build " + manifest.build + " for " +
                          manifest.target + "; the core is version " + core.version + ", build " + core.build + " for " + core.target};
        }
        for (const auto& required : manifest.requirements) {
            if (!generation.find(required)) throw Failure{"the " + manifest.name + " component requires " + required + ", which is not there"};
        }
        for (const auto& file : manifest.files) {
            if (!paths.insert(file.path).second) throw Failure{file.path + " belongs to two components"};
        }
    }
    return generation;
}

// Checks a tree against its manifests: every listed file with its size,
// mode (the owner's execute bit when `exact_modes` is false) and digest,
// and nothing else but the manifests and `extras`. Returns the problems,
// each a line; `limit` stops early.
std::vector<std::string> verify_tree(const fs::path& tree, const Generation& generation, bool digests, bool exact_modes,
                                     const std::set<std::string>& extras, std::size_t limit = 64) {
    std::vector<std::string> problems;
    std::set<std::string> listed;
    for (const auto& [manifest, text] : generation.manifests) {
        listed.insert(std::string(components_directory) + "/" + manifest.name + ".txt");
        for (const auto& file : manifest.files) {
            if (problems.size() >= limit) return problems;
            listed.insert(file.path);
            const auto path = tree / file.path;
            struct stat info {};
            if (::lstat(path.c_str(), &info) != 0) {
                problems.push_back(path.string() + ": missing");
                continue;
            }
            if (!S_ISREG(info.st_mode)) {
                problems.push_back(path.string() + ": not a regular file");
                continue;
            }
            if (static_cast<std::uint64_t>(info.st_size) != file.size) {
                problems.push_back(path.string() + ": expected " + std::to_string(file.size) + " bytes, found " + std::to_string(info.st_size));
                continue;
            }
            const bool executable = (info.st_mode & S_IXUSR) != 0;
            if (exact_modes ? (info.st_mode & 0777) != file.mode : executable != (file.mode == 0755)) {
                char found[8];
                std::snprintf(found, sizeof found, "%o", static_cast<unsigned>(info.st_mode & 0777));
                problems.push_back(path.string() + ": expected mode " + (file.mode == 0755 ? "755" : "644") + ", found " + found);
                continue;
            }
            if (digests) {
                const auto digest = util::sha256_file(path);
                if (!digest) {
                    problems.push_back(path.string() + ": cannot be read");
                } else if (digest->hex != file.sha256) {
                    problems.push_back(path.string() + ": expected sha256 " + file.sha256 + ", found " + digest->hex);
                }
            }
        }
    }
    // Nothing else is there.
    std::vector<fs::path> pending{tree};
    while (!pending.empty() && problems.size() < limit) {
        const auto directory = pending.back();
        pending.pop_back();
        std::error_code error;
        for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
            const auto relative = it->path().lexically_relative(tree).string();
            if (is_directory_no_follow(it->path())) {
                pending.push_back(it->path());
            } else if (!listed.contains(relative) && !extras.contains(relative)) {
                problems.push_back(it->path().string() + ": not in any manifest");
                if (problems.size() >= limit) break;
            }
        }
    }
    return problems;
}

void require_verified(const std::vector<std::string>& problems, const std::string& what) {
    if (problems.empty()) return;
    std::string text = what + " does not match its manifests:";
    for (std::size_t i = 0; i < problems.size() && i < 10; ++i) text += "\n  " + problems[i];
    throw Failure{text};
}

// The executables' digests and every file's size: the integrity check of
// doctor and of the commands that need a complete installation.
std::vector<std::string> quick_check(const fs::path& tree, const Generation& generation, std::size_t limit) {
    std::vector<std::string> problems;
    for (const auto& [manifest, text] : generation.manifests) {
        for (const auto& file : manifest.files) {
            if (problems.size() >= limit) return problems;
            const auto path = tree / file.path;
            struct stat info {};
            if (::lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
                problems.push_back(path.string() + ": missing");
            } else if (static_cast<std::uint64_t>(info.st_size) != file.size) {
                problems.push_back(path.string() + ": expected " + std::to_string(file.size) + " bytes, found " + std::to_string(info.st_size));
            } else if (file.mode == 0755 || file.path.find("/lib") != std::string::npos || file.path.starts_with("lib/")) {
                const auto digest = util::sha256_file(path);
                if (!digest || digest->hex != file.sha256) {
                    problems.push_back(path.string() + ": expected sha256 " + file.sha256 + ", found " + (digest ? digest->hex : "nothing"));
                }
            }
        }
    }
    return problems;
}

// ---------------------------------------------------------------------------
// Leases (docs/design/Lifecycle030.md, "Generations in use")

fs::path lease_path(const fs::path& root, const std::string& generation) { return root / leases_name / generation; }

void touch_lease(const fs::path& root, const std::string& generation) {
    const auto directory = root / leases_name;
    ::mkdir(directory.c_str(), 0755);
    const auto path = lease_path(root, generation);
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (descriptor < 0) throw Failure{"cannot create the lease " + path.string() + ": " + std::strerror(errno)};
    // The time of the lease is the time of the generation's last
    // activation, which orders the generations for --rollback.
    ::futimens(descriptor, nullptr);
    ::close(descriptor);
}

std::optional<std::time_t> lease_time(const fs::path& root, const std::string& generation) {
    struct stat info {};
    if (::stat(lease_path(root, generation).c_str(), &info) != 0) return std::nullopt;
    return info.st_mtime;
}

// An exclusive lock on a generation's lease, taken without waiting. The
// running tool's own shared lease is converted rather than locked again.
class ExclusiveLease {
public:
    enum class Result { Held, InUse, Unsupported, Missing };

    ExclusiveLease(const fs::path& root, const std::string& generation, const std::string& own_generation) {
        if (generation == own_generation && tool::lease_descriptor() >= 0) {
            own_ = tool::lease_descriptor();
            if (::flock(own_, LOCK_EX | LOCK_NB) == 0) {
                result_ = Result::Held;
            } else {
                result_ = errno == EWOULDBLOCK ? Result::InUse : Result::Unsupported;
                own_ = -1;
            }
            return;
        }
        const auto path = lease_path(root, generation);
        descriptor_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (descriptor_ < 0) {
            result_ = Result::Missing;
            return;
        }
        if (::flock(descriptor_, LOCK_EX | LOCK_NB) == 0) {
            result_ = Result::Held;
        } else {
            result_ = errno == EWOULDBLOCK ? Result::InUse : Result::Unsupported;
        }
    }
    ~ExclusiveLease() {
        if (own_ >= 0) ::flock(own_, LOCK_SH);
        if (descriptor_ >= 0) ::close(descriptor_);
    }
    ExclusiveLease(const ExclusiveLease&) = delete;
    ExclusiveLease& operator=(const ExclusiveLease&) = delete;

    Result result() const { return result_; }

private:
    int descriptor_ = -1;
    int own_ = -1;
    Result result_ = Result::Missing;
};

// Whether a process other than this one holds the generation's lease.
bool generation_in_use(const fs::path& root, const std::string& generation, const std::string& own_generation) {
    ExclusiveLease lease(root, generation, own_generation);
    return lease.result() == ExclusiveLease::Result::InUse;
}

std::vector<std::string> generations_on_disk(const fs::path& root) {
    std::vector<std::string> names;
    std::error_code error;
    for (fs::directory_iterator it(root / "versions", error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (is_directory_no_follow(it->path()) && parse_generation_name(name)) names.push_back(name);
    }
    std::ranges::sort(names);
    return names;
}

std::string current_generation(const fs::path& root) {
    char buffer[4096];
    const auto length = ::readlink((root / "current").c_str(), buffer, sizeof buffer - 1);
    if (length <= 0) return {};
    const std::string target(buffer, static_cast<std::size_t>(length));
    if (!target.starts_with("versions/")) return {};
    const auto name = target.substr(9);
    return parse_generation_name(name) ? name : std::string();
}

// The generation to return to: the most recently activated other one.
std::string previous_generation(const fs::path& root, const std::string& active, bool require_manifests) {
    std::string best;
    std::time_t best_time = 0;
    for (const auto& name : generations_on_disk(root)) {
        if (name == active) continue;
        if (require_manifests && (parse_generation_name(name)->id.empty() ||
                                  !is_directory_no_follow(root / "versions" / name / components_directory))) {
            continue;
        }
        const auto time = lease_time(root, name).value_or(0);
        if (best.empty() || time > best_time || (time == best_time && name > best)) {
            best = name;
            best_time = time;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Links

std::string link_target(const fs::path& root, std::string_view tool) { return (root / "current/bin" / tool).string(); }

enum class LinkState { Absent, Correct, OursOther, Conflict };

LinkState link_state(const fs::path& path, const fs::path& root) {
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) return LinkState::Absent;
    if (!S_ISLNK(info.st_mode)) return LinkState::Conflict;
    char buffer[4096];
    const auto length = ::readlink(path.c_str(), buffer, sizeof buffer - 1);
    if (length <= 0) return LinkState::Conflict;
    const std::string target(buffer, static_cast<std::size_t>(length));
    if (target == link_target(root, path.filename().string())) return LinkState::Correct;
    const fs::path target_path(target);
    if (target_path.is_absolute() && path_inside(target_path.lexically_normal(), root)) return LinkState::OursOther;
    std::error_code error;
    const auto resolved = fs::weakly_canonical(target_path.is_absolute() ? target_path : path.parent_path() / target_path, error);
    if (!error && path_inside(resolved, root)) return LinkState::OursOther;
    return LinkState::Conflict;
}

void create_link(const fs::path& path, const fs::path& root) {
    const auto temporary = path.parent_path() / (".neura-link-" + std::to_string(::getpid()));
    ::unlink(temporary.c_str());
    if (::symlink(link_target(root, path.filename().string()).c_str(), temporary.c_str()) != 0) {
        throw Failure{"cannot create a link in " + path.parent_path().string() + ": " + std::strerror(errno)};
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        const auto saved = errno;
        ::unlink(temporary.c_str());
        throw Failure{"cannot create " + path.string() + ": " + std::strerror(saved)};
    }
}

struct LinkPlan {
    std::vector<std::string> remove;
    std::vector<std::string> add;
    std::vector<std::string> conflicts;
};

// The links a change removes and adds: the owned links of tools the new set
// lacks (or of the old bin directory, when it moves), and each new tool's
// link that is absent or points elsewhere in this root. A path that exists
// and is not ours is a conflict.
LinkPlan plan_links(const fs::path& root, const fs::path& old_bin, const std::vector<std::string>& old_links, const fs::path& new_bin,
                    const std::set<std::string>& new_tools) {
    LinkPlan plan;
    for (const auto& name : old_links) {
        if (old_bin == new_bin && new_tools.contains(name)) continue;
        const auto path = old_bin / name;
        const auto state = link_state(path, root);
        if (state == LinkState::Correct || state == LinkState::OursOther) plan.remove.push_back(path.string());
    }
    for (const auto& name : new_tools) {
        const auto path = new_bin / name;
        switch (link_state(path, root)) {
        case LinkState::Correct:
            break;
        case LinkState::Absent:
        case LinkState::OursOther:
            plan.add.push_back(path.string());
            break;
        case LinkState::Conflict:
            plan.conflicts.push_back(path.string());
            break;
        }
    }
    return plan;
}

[[noreturn]] void refuse_conflicts(const std::vector<std::string>& conflicts) {
    std::string text;
    for (const auto& path : conflicts) text += (text.empty() ? "" : "\n") + path + " exists and is not a link made by neura";
    text += "\nnothing was changed";
    throw Failure{text};
}

// ---------------------------------------------------------------------------
// The journal, recovery, and the change itself

fs::path journal_path(const fs::path& root) { return root / journal_name; }

std::string read_marker_text(const fs::path& root) {
    const auto path = root / marker_name;
    if (!exists_no_follow(path)) return {};
    return read_small_file(path, "the install marker");
}

// Writes the marker with a generation line for each directory in versions/.
void write_marker(const fs::path& root, Marker marker) {
    marker.generations = generations_on_disk(root);
    replace_file(root / marker_name, format_marker(marker));
}

void remove_generation_directory(const fs::path& root, const std::string& generation) {
    const auto aside = root / (".removing-" + generation + "-" + std::to_string(::getpid()));
    if (::rename((root / "versions" / generation).c_str(), aside.c_str()) != 0) {
        throw Failure{"cannot remove " + (root / "versions" / generation).string() + ": " + std::strerror(errno)};
    }
    ::unlink(lease_path(root, generation).c_str());
    std::error_code error;
    remove_tree(aside, error);
}

// Undoes a change by its journal: links, `current`, the marker, and the
// new generation's directory when the change placed it.
void undo_change(const fs::path& root, const Journal& journal, const std::string& own_generation) {
    for (const auto& path : journal.add) {
        const auto state = link_state(path, root);
        if (state == LinkState::Correct || state == LinkState::OursOther) ::unlink(path.c_str());
    }
    for (const auto& path : journal.remove) {
        if (link_state(path, root) == LinkState::Absent) create_link(path, root);
    }
    if (journal.old_generation.empty()) {
        if (is_symlink_no_follow(root / "current")) ::unlink((root / "current").c_str());
    } else if (current_generation(root) != journal.old_generation) {
        replace_symlink(root, "versions/" + journal.old_generation);
    }
    if (journal.old_marker.empty()) {
        ::unlink((root / marker_name).c_str());
    } else {
        replace_file(root / marker_name, journal.old_marker);
    }
    if (journal.placed && journal.new_generation != journal.old_generation && is_directory_no_follow(root / "versions" / journal.new_generation)) {
        ExclusiveLease lease(root, journal.new_generation, own_generation);
        if (lease.result() != ExclusiveLease::Result::InUse) remove_generation_directory(root, journal.new_generation);
    }
    if (!journal.aside.empty() && is_directory_no_follow(journal.aside) && !exists_no_follow(root / "versions" / journal.new_generation)) {
        if (::rename(journal.aside.c_str(), (root / "versions" / journal.new_generation).c_str()) != 0) {
            throw Failure{"cannot restore " + journal.aside + ": " + std::strerror(errno)};
        }
    }
    ::unlink(journal_path(root).c_str());
    sync_directory(root);
}

// Completes a change whose `current` already names the new generation.
void complete_change(const fs::path& root, const Journal& journal) {
    for (const auto& path : journal.remove) {
        const auto state = link_state(path, root);
        if (state == LinkState::Correct || state == LinkState::OursOther) ::unlink(path.c_str());
    }
    for (const auto& path : journal.add) {
        if (link_state(path, root) != LinkState::Conflict && link_state(path, root) != LinkState::Correct) create_link(path, root);
    }
    std::string problem;
    auto marker = parse_marker(journal.new_marker, problem);
    if (!marker) throw Failure{"the journal's new marker is not valid: " + problem};
    write_marker(root, *marker);
    if (!journal.aside.empty()) {
        std::error_code ignored;
        remove_tree(journal.aside, ignored);
    }
    ::unlink(journal_path(root).c_str());
    sync_directory(root);
}

// Step 2: a journal that is present is from a command that ended early.
void recover(const fs::path& root, const std::string& own_generation, const char* prefix) {
    const auto path = journal_path(root);
    if (!exists_no_follow(path)) return;
    std::string problem;
    const auto journal = parse_journal(read_small_file(path, "the journal"), problem);
    if (!journal) throw Failure{"the journal " + path.string() + " is not valid (" + problem + "); see docs/Install.md to repair " + root.string()};
    if (current_generation(root) == journal->new_generation) {
        complete_change(root, *journal);
        std::cout << prefix << "completed an interrupted " << journal->operation << " to " << journal->new_generation << "\n";
    } else {
        undo_change(root, *journal, own_generation);
        std::cout << prefix << "undid an interrupted " << journal->operation << "; "
                  << (journal->old_generation.empty() ? std::string("nothing") : journal->old_generation) << " is active\n";
    }
}

// Removes what crashed commands left: staging, set-aside and half-removed
// directories, and temporary files. Called with the lock held, so every
// one of them is stale.
void remove_leftovers(const fs::path& root) {
    std::error_code error;
    std::vector<fs::path> found;
    for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (name.starts_with(".staging-") || name.starts_with(".removing-") || name.starts_with(".meta-") || name.starts_with(".marker-") ||
            name.starts_with(".current-") || name.starts_with(".install-") || name.starts_with(".neura-install.txt-") ||
            name.starts_with(".journal-")) {
            found.push_back(it->path());
        }
    }
    for (const auto& path : found) {
        std::error_code ignored;
        remove_tree(path, ignored);
    }
}

struct Change {
    std::string operation;
    fs::path root;
    // The generation `current` names before the change; empty for none.
    std::string old_generation;
    std::string old_marker_text;
    fs::path old_bin;
    std::vector<std::string> old_links;
    // The new generation, and the staged tree to place, or empty when the
    // generation is already on disk.
    std::string new_generation;
    fs::path new_tree;
    // A damaged generation of the same name to set aside first.
    bool set_aside = false;
    Marker new_marker;
    std::set<std::string> new_tools;
    // The generation of the running tool, whose own lease it holds.
    std::string own_generation;
    // Generations to keep besides the new and the old one.
    std::set<std::string> keep;
    bool prune = true;
    const char* prefix = "neura: update: ";
};

// Generations other than the active and the previous one, when no process
// holds their lease. Returns the generations kept because they are in use.
std::vector<std::string> prune_generations(const fs::path& root, const std::set<std::string>& keep, const std::string& own_generation,
                                           const char* prefix) {
    std::vector<std::string> in_use;
    for (const auto& name : generations_on_disk(root)) {
        if (keep.contains(name)) continue;
        if (name == own_generation) {
            // This process runs from it.
            in_use.push_back(name);
            continue;
        }
        if (!exists_no_follow(lease_path(root, name))) {
            try {
                touch_lease(root, name);
            } catch (const Failure&) {
                continue;
            }
        }
        ExclusiveLease lease(root, name, own_generation);
        switch (lease.result()) {
        case ExclusiveLease::Result::Held:
            try {
                remove_generation_directory(root, name);
                std::cout << "removed generation " << name << "\n";
            } catch (const Failure& failure) {
                std::cerr << prefix << "warning: " << failure.message << "\n";
            }
            break;
        case ExclusiveLease::Result::InUse:
            in_use.push_back(name);
            break;
        case ExclusiveLease::Result::Unsupported:
        case ExclusiveLease::Result::Missing:
            break;
        }
    }
    return in_use;
}

ServiceHooks& service_hooks();

void run_change(Change& change, const InstallLock& lock) {
    const auto& root = change.root;
    // Step 6: every public link.
    const auto plan = plan_links(root, change.old_bin, change.old_links, fs::path(change.new_marker.bin), change.new_tools);
    if (!plan.conflicts.empty()) refuse_conflicts(plan.conflicts);
    change.new_marker.links.assign(change.new_tools.begin(), change.new_tools.end());

    Journal journal;
    journal.operation = change.operation;
    journal.old_generation = change.old_generation;
    journal.new_generation = change.new_generation;
    journal.placed = !change.new_tree.empty();
    journal.remove = plan.remove;
    journal.add = plan.add;
    journal.old_marker = change.old_marker_text;
    {
        auto marker = change.new_marker;
        marker.generations = generations_on_disk(root);
        if (std::ranges::find(marker.generations, change.new_generation) == marker.generations.end()) {
            marker.generations.push_back(change.new_generation);
        }
        journal.new_marker = format_marker(marker);
    }
    const auto new_dir = root / "versions" / change.new_generation;
    if (change.set_aside) journal.aside = (root / (".removing-" + change.new_generation + "-" + std::to_string(::getpid()) + "-damaged")).string();

    bool journal_written = false;
    try {
        // Step 7.
        step_point("journal");
        {
            const auto temporary = write_temp_file(root, ".journal-", format_journal(journal));
            if (::rename(temporary.c_str(), journal_path(root).c_str()) != 0) {
                const auto saved = errno;
                ::unlink(temporary.c_str());
                throw Failure{"cannot write the journal " + journal_path(root).string() + ": " + std::strerror(saved)};
            }
            sync_directory(root);
            journal_written = true;
        }
        step_point("place");
        std::error_code created;
        fs::create_directories(root / "versions", created);
        if (created) throw Failure{"cannot create " + (root / "versions").string() + ": " + describe(created)};
        if (change.set_aside && ::rename(new_dir.c_str(), journal.aside.c_str()) != 0) {
            throw Failure{"cannot set " + new_dir.string() + " aside: " + std::strerror(errno)};
        }
        if (journal.placed && ::rename(change.new_tree.c_str(), new_dir.c_str()) != 0) {
            throw Failure{"cannot move the new generation to " + new_dir.string() + ": " + std::strerror(errno)};
        }
        // Every generation in a schema 2 root has a lease file; the
        // previous one's is created here if a 0.2 client placed it.
        if (!change.old_generation.empty() && !exists_no_follow(lease_path(root, change.old_generation))) {
            touch_lease(root, change.old_generation);
        }
        touch_lease(root, change.new_generation);
        sync_directory(root / "versions");
        // Step 8.
        step_point("links-remove");
        for (const auto& path : plan.remove) {
            if (::unlink(path.c_str()) != 0 && errno != ENOENT) throw Failure{"cannot remove " + path + ": " + std::strerror(errno)};
        }
        // Step 9.
        step_point("activate-link");
        replace_symlink(root, "versions/" + change.new_generation);
        // Step 10.
        step_point("links-add");
        if (!plan.add.empty()) {
            std::error_code made;
            fs::create_directories(change.new_marker.bin, made);
        }
        for (std::size_t i = 0; i < plan.add.size(); ++i) {
            create_link(plan.add[i], root);
            // A crash after some links (the tests' "links-add-<n>").
            step_point("links-add-" + std::to_string(i + 1));
        }
        // Step 11.
        step_point("activate-marker");
        write_marker(root, change.new_marker);
        if (!journal.aside.empty()) {
            std::error_code ignored;
            remove_tree(journal.aside, ignored);
        }
        ::unlink(journal_path(root).c_str());
        sync_directory(root);
    } catch (const Failure& failure) {
        try {
            if (journal_written) {
                undo_change(root, journal, change.own_generation);
            } else if (journal.placed && exists_no_follow(change.new_tree)) {
                // Nothing was placed; the staging directory goes with its owner.
            }
        } catch (const Failure& again) {
            throw Failure{"the change failed (" + failure.message + ") and could not be undone (" + again.message +
                          "); run \"neura update --repair\""};
        }
        throw Failure{"update failed and was rolled back; " + (change.old_generation.empty() ? std::string("nothing") : change.old_generation) +
                      " is still active (" + failure.message + ")"};
    }

    // Step 12: a failure here is a warning; the change has succeeded.
    try {
        step_point("retire");
        if (!change.old_generation.empty() && change.old_generation != change.new_generation && service_hooks().retire) {
            service_hooks().retire(root, change.old_generation);
        }
        step_point("prune");
        if (change.prune && !lock.unlocked()) {
            auto keep = change.keep;
            keep.insert(change.new_generation);
            if (!change.old_generation.empty()) keep.insert(change.old_generation);
            const auto in_use = prune_generations(root, keep, change.own_generation, change.prefix);
            for (const auto& name : in_use) std::cout << "kept generation " << name << ": it is in use\n";
            write_marker(root, change.new_marker);
        }
    } catch (const Failure& failure) {
        std::cerr << change.prefix << "warning: " << failure.message << "\n";
    }
}

// ---------------------------------------------------------------------------
// Staging and composing

fs::path make_staging(const fs::path& root) {
    std::string name = (root / (".staging-" + std::to_string(::getpid()) + "-XXXXXX")).string();
    if (::mkdtemp(name.data()) == nullptr) throw Failure{"cannot create a staging directory in " + root.string() + ": " + std::strerror(errno)};
    ::chmod(name.c_str(), 0700);
    return name;
}

// A source of components: an unpacked archive's top directory, or a
// generation on disk.
struct Source {
    fs::path tree;
    Generation generation;
    std::string what;
};

// Copies the files and manifests of `components` from the sources into
// `destination`, each with its manifest's mode. The first source that has a
// component supplies it.
void compose(const fs::path& destination, const std::vector<std::string>& components, const std::vector<const Source*>& sources) {
    step_point("compose");
    for (const auto& component : components) {
        const Source* source = nullptr;
        for (const auto* each : sources) {
            if (each->generation.find(component)) {
                source = each;
                break;
            }
        }
        if (source == nullptr) throw Failure{"no archive brings the " + component + " component"};
        const auto& manifest = *source->generation.find(component);
        for (const auto& file : manifest.files) {
            const auto target = destination / file.path;
            std::error_code error;
            fs::create_directories(target.parent_path(), error);
            if (error) throw Failure{"cannot create " + target.parent_path().string() + ": " + describe(error)};
            fs::copy_file(source->tree / file.path, target, fs::copy_options::none, error);
            if (error) throw Failure{"cannot copy " + (source->tree / file.path).string() + ": " + describe(error)};
            ::chmod(target.c_str(), file.mode);
        }
        const auto manifests = destination / components_directory;
        std::error_code error;
        fs::create_directories(manifests, error);
        replace_file(manifests / (component + ".txt"), *source->generation.text(component));
    }
    // Directories as the archives make them.
    std::vector<fs::path> pending{destination};
    while (!pending.empty()) {
        const auto directory = pending.back();
        pending.pop_back();
        ::chmod(directory.c_str(), 0755);
        std::error_code error;
        for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
            if (is_directory_no_follow(it->path())) pending.push_back(it->path());
        }
    }
}

// Reads and checks an unpacked archive: its manifests, every file against
// them, and the version line of its bin/neura (step 5).
Source verify_unpacked(const fs::path& tree, const std::string& expected_version) {
    step_point("verify");
    Source source;
    source.tree = tree;
    source.what = tree.string();
    source.generation = read_generation(tree);
    require_verified(verify_tree(tree, source.generation, true, false, {"install.sh", "VERSION"}), tree.string());
    if (source.generation.target() != version::triple) {
        throw Failure{tree.string() + " is for " + source.generation.target() + "; this machine is " + version::triple};
    }
    if (!expected_version.empty() && source.generation.version() != expected_version) {
        throw Failure{tree.string() + " holds version " + source.generation.version() + "; expected " + expected_version};
    }
    const auto binary = tree / "bin/neura";
    const auto result = run_child({binary.string(), "--version"}, 30);
    if (!result.started || result.status != 0) throw Failure{"the new neura does not run: " + failure_detail(result)};
    const auto line = first_line(result.out);
    if (!line.starts_with("neura " + source.generation.version() + " ")) {
        throw Failure{"the new neura reports \"" + line + "\"; expected a line starting \"neura " + source.generation.version() + " \""};
    }
    return source;
}

// The core of two sources is the same bytes.
void require_same_core(const Source& a, const Source& b) {
    if (a.generation.build() != b.generation.build() || a.generation.version() != b.generation.version()) {
        throw Failure{b.what + " is build " + b.generation.build() + " of " + b.generation.version() + "; " + a.what + " is build " +
                      a.generation.build() + " of " + a.generation.version() + ". Both must come from one build"};
    }
    if (*a.generation.text("core") != *b.generation.text("core")) {
        throw Failure{"the core of " + b.what + " is not the core of " + a.what};
    }
}

std::vector<std::string> sorted_components(std::vector<std::string> components) {
    std::ranges::sort(components, [](const std::string& a, const std::string& b) { return component_before(a, b); });
    components.erase(std::unique(components.begin(), components.end()), components.end());
    return components;
}

// ---------------------------------------------------------------------------
// The service hooks

std::string service_id(const fs::path& root) {
    std::error_code error;
    const auto real = fs::canonical(root, error);
    return util::sha256_hex((error ? root : real).string()).substr(0, 16);
}

// <run> of docs/design/Service030.md, without creating it.
fs::path service_run_directory() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime != nullptr && *runtime == '/') return runtime;
    const char* temporary = std::getenv("TMPDIR");
    return fs::path(temporary != nullptr && *temporary != '\0' ? temporary : "/tmp") / ("neura-" + std::to_string(::getuid()));
}

fs::path supervisor_directory(const fs::path& root, const std::string& generation) {
    std::error_code error;
    const auto real = fs::canonical(root, error);
    std::string key = (error ? root : real).string();
    key.push_back('\0');
    key += generation;
    return service_run_directory() / "neura" / util::sha256_hex(key).substr(0, 16);
}

fs::path service_state_directory(const fs::path& root) {
    const char* state = std::getenv("XDG_STATE_HOME");
    fs::path base;
    if (state != nullptr && *state == '/') {
        base = state;
    } else {
        const char* home = std::getenv("HOME");
        if (home == nullptr || *home == '\0') return {};
        base = fs::path(home) / ".local/state";
    }
    return base / "neura" / service_id(root);
}

std::vector<fs::path> service_unit_files(const fs::path& root) {
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return {};
    const auto id = service_id(root);
    return {fs::path(home) / ".config/systemd/user" / ("neurad-" + id + ".service"),
            fs::path(home) / "Library/LaunchAgents" / ("net.neura.neurad-" + id + ".plist")};
}

// The generations of this installation whose supervisor holds its lock.
std::vector<std::string> running_supervisors(const fs::path& root) {
    std::vector<std::string> running;
    for (const auto& generation : generations_on_disk(root)) {
        const auto lock = supervisor_directory(root, generation) / "lock";
        const int descriptor = ::open(lock.c_str(), O_RDONLY | O_CLOEXEC);
        if (descriptor < 0) continue;
        if (::flock(descriptor, LOCK_SH | LOCK_NB) != 0 && errno == EWOULDBLOCK) running.push_back(generation);
        ::close(descriptor);
    }
    return running;
}

ServiceHooks& service_hooks() {
    static ServiceHooks hooks = [] {
        ServiceHooks defaults;
        // Without a control client the number of applications is not
        // known; a running supervisor counts as applications that may run.
        defaults.applications = [](const fs::path& root) -> std::optional<std::size_t> {
            if (running_supervisors(root).empty()) return std::size_t{0};
            return std::nullopt;
        };
        defaults.terminate = [](const fs::path& root) -> std::string {
            // NEURA_TEST_SERVICE_CONTROL names a stand-in for the service's
            // own command, for the tests only.
            const char* stand_in = std::getenv("NEURA_TEST_SERVICE_CONTROL");
            std::vector<std::string> command;
            if (stand_in != nullptr && *stand_in != '\0') {
                command = {stand_in, "shutdown", "--terminate", "--root", root.string()};
            } else {
                const auto neura = root / "current/bin/neura";
                if (!fs::exists(root / "current/bin/neurad")) return "the service component (neurad) is not in the active generation";
                command = {neura.string(), "runtime", "shutdown", "--terminate"};
            }
            const auto result = run_child(command, 120);
            if (!result.started || result.status != 0) return "the service did not stop: " + failure_detail(result);
            return {};
        };
        defaults.retire = [](const fs::path&, const std::string&) {};
        return defaults;
    }();
    return hooks;
}

// Removes the login registration and the state of this installation's
// service, and nothing of another installation's. Returns what it removed.
std::vector<std::string> remove_service_state(const fs::path& root, bool dry_run) {
    std::vector<std::string> removed;
    for (const auto& unit : service_unit_files(root)) {
        if (!exists_no_follow(unit)) continue;
        if (!dry_run) {
            if (unit.extension() == ".service" && !find_in_path("systemctl").empty()) {
                run_child({"systemctl", "--user", "disable", unit.filename().string()}, 30);
            }
            ::unlink(unit.c_str());
            if (unit.extension() == ".service" && !find_in_path("systemctl").empty()) run_child({"systemctl", "--user", "daemon-reload"}, 30);
        }
        removed.push_back(unit.string());
    }
    const auto state = service_state_directory(root);
    if (!state.empty() && exists_no_follow(state)) {
        if (!dry_run) {
            std::error_code ignored;
            remove_tree(state, ignored);
        }
        removed.push_back(state.string());
    }
    return removed;
}

// Refuses a removal that affects running applications, unless --terminate
// stops them first.
void check_applications(const fs::path& root, bool terminate, const char* prefix) {
    const auto count = service_hooks().applications(root);
    if (count && *count == 0) return;
    if (!terminate) {
        if (count) {
            throw Failure{std::to_string(*count) + " applications of this installation are running (neura runtime ps); pass --terminate to stop them"};
        }
        throw Failure{"a supervisor of this installation is running (neura runtime status); pass --terminate to stop it and its applications"};
    }
    std::cout << prefix << "stopping the applications of this installation's service\n";
    if (const auto problem = service_hooks().terminate(root); !problem.empty()) throw Failure{problem + ". Nothing was changed"};
}

// ---------------------------------------------------------------------------
// Installation state

struct State {
    fs::path root;
    std::optional<Marker> marker;
    std::string marker_text;
    std::string active;
    std::string own_generation;
};

bool unmigrated(const Detection& detection) {
    return detection.marker.schema == 1 &&
           exists_no_follow(detection.root / "versions" / detection.running_generation / components_directory / "core.txt");
}

constexpr const char* migrate_instruction =
    "this installation was updated by a 0.2 client; run \"neura update --migrate-install\" first";

// The components of a set name: sdk, runtime, or sdk,service.
std::vector<std::string> parse_component_set(const std::string& text) {
    if (text == "sdk") return {"core", "sdk"};
    if (text == "runtime") return {"core", "service"};
    if (text == "sdk,service" || text == "service,sdk") return {"core", "sdk", "service"};
    throw Failure{"--components takes sdk, runtime or sdk,service, not \"" + text + "\""};
}

// Reads the active generation and requires a complete installation.
Generation active_generation(const fs::path& root, const std::string& active) {
    if (active.empty()) throw Failure{(root / "current").string() + " does not name a generation; run \"neura update --repair\""};
    const auto tree = root / "versions" / active;
    Generation generation = read_generation(tree);
    const auto problems = quick_check(tree, generation, 3);
    if (!problems.empty()) {
        throw Failure{"the installation is damaged (" + join_names(problems, "; ") +
                      "); reinstall this version with install.sh --force, then run the command again"};
    }
    return generation;
}

// ---------------------------------------------------------------------------
// Fetching archives for an update or an added component

ComponentsMetadata fetch_components_metadata(const Fetcher& fetcher, const fs::path& root, int max_seconds, bool& absent) {
    absent = false;
    std::string text;
    if (fetcher.remote) {
        if (find_in_path("curl").empty()) throw Failure{"curl is not installed; it is needed to download from " + fetcher.source};
        int descriptor = -1;
        const auto temp = make_temp_file(root, ".meta-", descriptor);
        ::close(descriptor);
        RemoveOnExit cleanup(temp);
        const auto result = run_child(curl_command(fetcher.source + "/neura-components.txt", temp, max_seconds, max_metadata_bytes), max_seconds + 10);
        if (!result.started || result.status != 0) {
            // curl --fail exits 22 for an HTTP error such as 404.
            if (result.started && result.status == 22) {
                absent = true;
                return {};
            }
            throw Failure{"cannot fetch " + fetcher.source + "/neura-components.txt: " + (result.started ? failure_detail(result) : "curl could not be run")};
        }
        text = read_small_file(temp, "component metadata");
    } else {
        const auto path = fetcher.local_file("neura-components.txt");
        if (!exists_no_follow(path)) {
            absent = true;
            return {};
        }
        text = read_small_file(path, "component metadata");
    }
    std::string error;
    auto metadata = parse_components_metadata(text, error);
    if (!metadata) throw Failure{"the component metadata is not valid: " + error};
    return *metadata;
}

// Downloads, checks and unpacks one archive of the metadata into `staging`
// (steps 3 to 5) and returns its tree.
Source fetch_product(const Fetcher& fetcher, const ComponentsMetadata& metadata, const std::string& product, const fs::path& staging,
                     bool require_attestation) {
    const auto* archive = metadata.find(product, version::triple);
    if (archive == nullptr) throw Failure{"neura " + metadata.version + " has no " + product + " archive for " + version::triple + ". Nothing was changed"};
    ReleaseAsset asset{archive->triple, archive->file, archive->sha256, archive->size};
    const auto file = fetch_asset(fetcher, asset, staging);
    check_size_and_digest(file, asset);
    std::cout << "verified: size and SHA-256 of " << asset.file << " against the component metadata\n";
    std::cout << verify_provenance(file, !fetcher.remote, require_attestation) << "\n";
    const auto directory = staging / ("unpacked-" + product);
    fs::create_directory(directory);
    ::chmod(directory.c_str(), 0700);
    const std::string top = "neura-" + product + "-" + metadata.version + "-" + version::triple;
    extract(file, directory, top);
    auto source = verify_unpacked(directory / top, metadata.version);
    if (source.generation.build() != metadata.build) {
        throw Failure{asset.file + " is build " + source.generation.build() + "; the component metadata says " + metadata.build};
    }
    source.what = asset.file;
    return source;
}

// ---------------------------------------------------------------------------
// The commands

// Prints what the links are and how to reach them.
void print_installed(const fs::path& root, const Marker& marker, const std::string& generation) {
    std::cout << "  root:        " << root.string() << "\n"
              << "  generation:  " << root.string() << "/versions/" << generation << "\n"
              << "  components:  " << join_names(marker.components) << "\n"
              << "  links:       " << marker.links.size() << " in " << marker.bin << "\n";
    const char* path = std::getenv("PATH");
    const std::string entries = std::string(":") + (path ? path : "") + ":";
    if (entries.find(":" + marker.bin + ":") == std::string::npos) {
        std::cout << "\n" << marker.bin << " is not on your PATH. To use neura, add this line to your shell startup file:\n\n"
                  << "    export PATH=\"" << marker.bin << ":$PATH\"\n\n";
    }
}

fs::path environment_directory(const char* name, const fs::path& fallback) {
    const char* value = std::getenv(name);
    if (value != nullptr && *value != '\0') {
        const fs::path path(value);
        if (!path.is_absolute()) throw Failure{std::string(name) + " must be an absolute path, not " + value};
        return path;
    }
    return fallback;
}

fs::path real_directory(const fs::path& path, const std::string& what) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error) throw Failure{"cannot create " + what + " " + path.string() + ": " + describe(error)};
    const auto real = fs::canonical(path, error);
    if (error) throw Failure{"cannot resolve " + what + " " + path.string() + ": " + describe(error)};
    return real;
}

int install_from(const UpdateOptions& options) {
    const char* home = std::getenv("HOME");
    const char* data = std::getenv("XDG_DATA_HOME");
    fs::path default_root;
    if (data != nullptr && *data == '/') default_root = fs::path(data) / "neura";
    else if (home != nullptr && *home != '\0') default_root = fs::path(home) / ".local/share/neura";
    const auto root_request = environment_directory("NEURA_INSTALL_ROOT", default_root);
    if (root_request.empty()) throw Failure{"HOME is not set; set NEURA_INSTALL_ROOT"};

    // The marker of an installation that is already there.
    std::optional<Marker> old_marker;
    std::string old_text;
    if (exists_no_follow(root_request / marker_name)) {
        old_text = read_small_file(root_request / marker_name, "the install marker");
        std::string problem;
        old_marker = parse_marker(old_text, problem);
        if (!old_marker) throw Failure{(root_request / marker_name).string() + " is not a neura install marker: " + problem};
        if (old_marker->method != "archive") {
            std::cerr << "install.sh: " << root_request.string() << " is owned by " << old_marker->method << "; use " << old_marker->method
                      << " to manage neura\n";
            return exit_unmanaged;
        }
    }
    const char* bin_value = std::getenv("NEURA_BIN_DIR");
    fs::path bin_request;
    if (bin_value != nullptr && *bin_value != '\0') bin_request = environment_directory("NEURA_BIN_DIR", {});
    else if (old_marker) bin_request = old_marker->bin;
    else if (home != nullptr && *home != '\0') bin_request = fs::path(home) / ".local/bin";
    else throw Failure{"HOME is not set; set NEURA_BIN_DIR"};

    // Steps 4 and 5 for an unpacked archive need no lock.
    std::vector<Source> sources;
    std::string version_text;
    {
        const fs::path from = fs::absolute(options.install_from);
        const auto version_file = from / "VERSION";
        version_text = read_small_file(version_file, "VERSION");
        if (!version_text.empty() && version_text.back() == '\n') version_text.pop_back();
        if (!parse_semver(version_text)) throw Failure{version_file.string() + " does not hold a semantic version"};
        sources.push_back(verify_unpacked(from, version_text));
        sources.back().what = from.string();
    }
    if (!options.add.empty()) {
        const fs::path add = fs::absolute(options.add);
        auto other = verify_unpacked(add, "");
        other.what = add.string();
        require_same_core(sources.front(), other);
        sources.push_back(std::move(other));
    }
    std::vector<std::string> components;
    for (const auto& source : sources) {
        for (const auto& name : source.generation.components()) components.push_back(name);
    }
    components = sorted_components(components);
    if (!valid_component_set(components)) throw Failure{"the archives hold the components " + join_names(components) + ", which is not a product"};

    const auto root = real_directory(root_request, "the install root");
    const auto bin = real_directory(bin_request, "the bin directory");
    InstallLock lock(root, old_marker && old_marker->schema == 1);
    recover(root, {}, "install.sh: ");
    remove_leftovers(root);
    // The marker may have changed while this waited for nothing: read it again.
    old_text = read_marker_text(root);
    if (!old_text.empty()) {
        std::string problem;
        old_marker = parse_marker(old_text, problem);
        if (!old_marker) throw Failure{(root / marker_name).string() + " is not a neura install marker: " + problem};
    }

    const auto staging = make_staging(root);
    RemoveOnExit cleanup(staging);
    const auto tree = staging / "tree";
    fs::create_directory(tree);
    std::vector<const Source*> pointers;
    for (const auto& source : sources) pointers.push_back(&source);
    compose(tree, components, pointers);
    Source composed;
    composed.tree = tree;
    composed.generation = read_generation(tree);
    const auto generation = composed.generation.name();

    Change change;
    change.operation = "install";
    change.root = root;
    change.old_generation = current_generation(root);
    change.old_marker_text = old_text;
    change.new_generation = generation;
    change.new_tree = tree;
    change.prefix = "install.sh: ";
    if (old_marker) {
        change.old_bin = old_marker->bin;
        change.old_links = old_marker->links;
    } else {
        change.old_bin = bin;
    }
    const auto existing = root / "versions" / generation;
    if (exists_no_follow(existing)) {
        if (!options.force) {
            throw Failure{"neura " + version_text + " is already installed in " + existing.string() + "; pass --force to replace it"};
        }
        std::vector<std::string> problems;
        try {
            const auto on_disk = read_generation(existing);
            problems = verify_tree(existing, on_disk, true, true, {"install.sh", "VERSION"}, 3);
        } catch (const Failure& failure) {
            problems.push_back(failure.message);
        }
        if (problems.empty()) {
            // Whole: nothing is copied; the installation switches to it and
            // its links are repaired (case 10).
            change.new_tree.clear();
            std::cout << "install.sh: " << existing.string() << " is whole; switching to it and repairing its links\n";
        } else if (generation_in_use(root, generation, {})) {
            throw Failure{"generation " + generation + " is damaged (" + problems.front() + ") and in use by a running neura process; end it and run the command again"};
        } else {
            std::cout << "install.sh: " << existing.string() << " is damaged (" << problems.front() << "); replacing it\n";
            change.set_aside = true;
        }
    }
    change.new_marker.method = "archive";
    change.new_marker.bin = bin.string();
    change.new_marker.components = components;
    change.new_tools = composed.generation.tools();
    run_change(change, lock);
    std::cout << "Installed neura " << version_text << " (" << generation << ")\n";
    print_installed(root, change.new_marker, generation);
    std::cout << "Run \"neura doctor\" to check the install.\n";
    return exit_ok;
}

int run_rollback(const Detection& detection) {
    const auto& root = detection.root;
    InstallLock lock(root, detection.marker.schema == 1);
    recover(root, detection.running_generation, "neura: update: ");
    remove_leftovers(root);
    const auto text = read_marker_text(root);
    std::string problem;
    const auto marker = parse_marker(text, problem);
    if (!marker) throw Failure{"the install marker is not valid: " + problem};
    const auto active = current_generation(root);
    const auto previous = previous_generation(root, active, true);
    if (previous.empty()) throw Failure{"no previous 0.3 generation is installed"};
    const auto tree = root / "versions" / previous;
    const auto generation = read_generation(tree);
    require_verified(verify_tree(tree, generation, true, true, {"install.sh", "VERSION"}), tree.string());
    Change change;
    change.operation = "rollback";
    change.root = root;
    change.old_generation = active;
    change.old_marker_text = text;
    change.old_bin = marker->bin;
    change.old_links = marker->links;
    change.new_generation = previous;
    change.new_marker = *marker;
    change.new_marker.components = generation.components();
    change.new_tools = generation.tools();
    change.own_generation = detection.running_generation;
    run_change(change, lock);
    std::cout << "rolled back to " << previous << "; " << active << " is the previous generation\n";
    return exit_ok;
}

int run_repair(const Detection& detection) {
    const auto& root = detection.root;
    InstallLock lock(root, detection.marker.schema == 1);
    recover(root, detection.running_generation, "neura: update: ");
    remove_leftovers(root);
    const auto text = read_marker_text(root);
    std::string problem;
    auto marker = parse_marker(text, problem);
    if (!marker) throw Failure{"the install marker is not valid: " + problem};
    if (marker->schema == 1) throw Failure{migrate_instruction};
    const auto active = current_generation(root);
    if (active.empty()) throw Failure{(root / "current").string() + " does not name a generation; reinstall with install.sh --force"};
    // Owned links that are missing are restored.
    std::size_t restored = 0;
    for (const auto& name : marker->links) {
        const auto path = fs::path(marker->bin) / name;
        const auto state = link_state(path, root);
        if (state == LinkState::Absent || state == LinkState::OursOther) {
            create_link(path, root);
            ++restored;
        } else if (state == LinkState::Conflict) {
            std::cerr << "neura: update: warning: " << path.string() << " exists and is not a link made by neura; left as it is\n";
        }
    }
    std::set<std::string> keep{active};
    if (const auto previous = previous_generation(root, active, false); !previous.empty()) keep.insert(previous);
    if (!lock.unlocked()) {
        for (const auto& name : prune_generations(root, keep, detection.running_generation, "neura: update: ")) {
            std::cout << "kept generation " << name << ": it is in use\n";
        }
    }
    write_marker(root, *marker);
    std::cout << "repaired " << root.string() << ": " << restored << " link" << (restored == 1 ? "" : "s") << " restored\n";
    return exit_ok;
}

int run_migrate(const Detection& detection) {
    const auto& root = detection.root;
    const bool legacy = detection.marker.schema == 1;
    InstallLock lock(root, legacy);
    recover(root, detection.running_generation, "neura: update: ");
    remove_leftovers(root);
    const auto text = read_marker_text(root);
    std::string problem;
    const auto marker = parse_marker(text, problem);
    if (!marker) throw Failure{"the install marker is not valid: " + problem};
    const auto active = current_generation(root);
    if (active.empty()) throw Failure{(root / "current").string() + " does not name a generation"};
    const auto tree = root / "versions" / active;
    const auto generation = read_generation(tree);
    if (marker->schema == 2) {
        // Already migrated: verify and restore the links.
        std::size_t restored = 0;
        for (const auto& name : marker->links) {
            const auto path = fs::path(marker->bin) / name;
            const auto state = link_state(path, root);
            if (state == LinkState::Absent || state == LinkState::OursOther) {
                create_link(path, root);
                ++restored;
            }
        }
        std::cout << "already migrated" << (restored ? "; restored " + std::to_string(restored) + " links" : std::string()) << "\n";
        return exit_ok;
    }
    require_verified(verify_tree(tree, generation, true, false, {"install.sh", "VERSION"}), tree.string());
    const fs::path link(marker->link);
    if (link.filename() != "neura") throw Failure{"the marker's link " + marker->link + " is not named neura; reinstall with install.sh"};
    Change change;
    change.operation = "migrate";
    change.root = root;
    change.old_generation = active;
    change.old_marker_text = text;
    change.old_bin = marker->bin;
    change.old_links = marker->links;
    change.new_generation = active;
    change.new_marker.method = "archive";
    change.new_marker.bin = link.parent_path().string();
    change.new_marker.components = generation.components();
    change.new_tools = generation.tools();
    change.own_generation = detection.running_generation;
    change.prune = false;
    for (const auto& name : generations_on_disk(root)) {
        if (!exists_no_follow(lease_path(root, name))) touch_lease(root, name);
    }
    run_change(change, lock);
    std::cout << "migrated " << root.string() << ": " << change.new_tools.size() << " tools linked in " << change.new_marker.bin << "\n";
    return exit_ok;
}

int run_components(const Detection& detection, const UpdateOptions& options, std::vector<std::string> wanted, const char* prefix,
                   const std::string& operation) {
    const auto& root = detection.root;
    InstallLock lock(root, detection.marker.schema == 1);
    recover(root, detection.running_generation, prefix);
    remove_leftovers(root);
    const auto text = read_marker_text(root);
    std::string problem;
    const auto marker = parse_marker(text, problem);
    if (!marker) throw Failure{"the install marker is not valid: " + problem};
    const auto active = current_generation(root);
    const auto installed = active_generation(root, active);
    wanted = sorted_components(wanted);
    const auto have = installed.components();
    if (wanted == have) {
        std::cout << prefix << "the installation already has the components " << join_names(have) << "; nothing to change\n";
        return exit_ok;
    }
    std::vector<std::string> removed;
    std::vector<std::string> added;
    for (const auto& name : have) {
        if (std::ranges::find(wanted, name) == wanted.end()) removed.push_back(name);
    }
    for (const auto& name : wanted) {
        if (std::ranges::find(have, name) == have.end()) added.push_back(name);
    }
    if (std::ranges::find(removed, "service") != removed.end()) check_applications(root, options.terminate, prefix);

    const auto staging = make_staging(root);
    RemoveOnExit cleanup(staging);
    Source current;
    current.tree = root / "versions" / active;
    current.generation = installed;
    current.what = "the installed generation " + active;
    std::vector<Source> fetched;
    if (!added.empty()) {
        // The archive of the installed version, from that version's own
        // release, of the installed build.
        const auto version_text = installed.version();
        const std::string source = options.source_given ? options.source
                                                        : std::string("https://github.com/") + repository + "/releases/download/v" + version_text;
        const auto fetcher = make_fetcher(source);
        bool absent = false;
        const auto metadata = fetch_components_metadata(fetcher, root, 30, absent);
        if (absent) throw Failure{"release " + version_text + " has no component metadata"};
        const std::string product = std::ranges::find(added, "sdk") != added.end() ? "sdk" : "runtime";
        const auto* archive = metadata.find(product, version::triple);
        if (metadata.version != version_text || metadata.build != installed.build()) {
            throw Failure{(archive ? archive->file : product_archive_name(product, metadata.version, version::triple)) + " is build " +
                          metadata.build + "; the installation is build " + installed.build() + ". Update first, then add the component"};
        }
        fetched.push_back(fetch_product(fetcher, metadata, product, staging, options.require_attestation));
        require_same_core(current, fetched.back());
    }
    const auto tree = staging / "tree";
    fs::create_directory(tree);
    std::vector<const Source*> pointers{&current};
    for (const auto& source : fetched) pointers.push_back(&source);
    compose(tree, wanted, pointers);
    const auto generation = read_generation(tree);
    const auto name = generation.name();
    Change change;
    change.operation = operation;
    change.root = root;
    change.old_generation = active;
    change.old_marker_text = text;
    change.old_bin = marker->bin;
    change.old_links = marker->links;
    change.new_generation = name;
    change.new_tree = tree;
    change.new_marker = *marker;
    change.new_marker.components = wanted;
    change.new_tools = generation.tools();
    change.own_generation = detection.running_generation;
    change.prefix = prefix;
    if (exists_no_follow(root / "versions" / name)) {
        // The same set again: switch to the generation on disk, verified.
        require_verified(verify_tree(root / "versions" / name, read_generation(root / "versions" / name), true, true, {}), name);
        change.new_tree.clear();
    }
    run_change(change, lock);
    if (std::ranges::find(removed, "service") != removed.end()) {
        for (const auto& path : remove_service_state(root, false)) std::cout << "removed " << path << "\n";
    }
    std::cout << prefix << "the installation now has the components " << join_names(wanted) << " (" << name << ")";
    if (!removed.empty()) std::cout << "; removed " << join_names(removed);
    if (!added.empty()) std::cout << "; added " << join_names(added);
    std::cout << "\n";
    return exit_ok;
}

int run_update(const fs::path&, const Detection& detection, const UpdateOptions& options) {
    const auto& root = detection.root;
    auto fetcher = make_fetcher(options.source);
    std::optional<InstallLock> lock;
    if (!options.check) {
        if (options.require_attestation && find_in_path("gh").empty()) {
            throw Failure{"--require-attestation needs gh (the GitHub CLI), which is not on PATH. Nothing was changed"};
        }
        lock.emplace(root, detection.marker.schema == 1);
        recover(root, detection.running_generation, "neura: update: ");
        remove_leftovers(root);
    }
    const auto text = read_marker_text(root);
    std::string problem;
    const auto marker = parse_marker(text, problem);
    if (!marker) throw Failure{"the install marker is not valid: " + problem};
    const auto active = current_generation(root);
    std::string installed_version = version::full;
    std::optional<Generation> installed;
    if (!options.check) {
        installed = active_generation(root, active);
        installed_version = installed->version();
    } else if (!active.empty()) {
        try {
            installed_version = read_generation(root / "versions" / active).version();
        } catch (const Failure&) {
        }
    }
    std::optional<PublishedRelease> selected;
    if (options.nightly && options.source == default_release_source) {
        selected = newest_github_release(root);
        fetcher = make_fetcher(std::string("https://github.com/") + repository + "/releases/download/" + selected->tag);
    }
    bool absent = false;
    const auto metadata = fetch_components_metadata(fetcher, root, 30, absent);
    std::string latest = metadata.version;
    if (absent) {
        // A source of 0.2 releases: neura-release.txt alone.
        latest = fetch_metadata(fetcher, root, 30).version;
    }
    if (selected && latest != selected->version) {
        throw Failure{"the release metadata version " + latest + " does not match selected GitHub tag " + selected->tag};
    }
    if (!is_newer(latest, installed_version)) {
        std::cout << "neura " << installed_version << " is up to date\n";
        return exit_ok;
    }
    if (absent) throw Failure{"release " + latest + " has no component metadata"};
    if (options.check) {
        std::cout << "neura " << installed_version << " is installed; neura " << latest << " is available\n"
                  << "run \"neura update" << (options.nightly ? " --nightly" : "") << "\" to install it\n";
        return exit_update_available;
    }
    const auto components = installed->components();
    std::vector<std::string> products;
    if (std::ranges::find(components, "sdk") != components.end()) products.push_back("sdk");
    if (std::ranges::find(components, "service") != components.end()) products.push_back("runtime");
    for (const auto& product : products) {
        if (!metadata.find(product, version::triple)) {
            throw Failure{"neura " + latest + " has no " + product + " archive for " + version::triple + ". Nothing was changed"};
        }
    }
    std::cout << "updating neura " << installed_version << " to " << latest << "\n";
    const auto staging = make_staging(root);
    RemoveOnExit cleanup(staging);
    std::vector<Source> sources;
    for (const auto& product : products) {
        sources.push_back(fetch_product(fetcher, metadata, product, staging, options.require_attestation));
        if (sources.size() > 1) require_same_core(sources.front(), sources.back());
    }
    const auto tree = staging / "tree";
    fs::create_directory(tree);
    std::vector<const Source*> pointers;
    for (const auto& source : sources) pointers.push_back(&source);
    compose(tree, components, pointers);
    const auto generation = read_generation(tree);
    const auto name = generation.name();
    if (exists_no_follow(root / "versions" / name)) throw Failure{(root / "versions" / name).string() + " already exists. Nothing was changed"};
    Change change;
    change.operation = "update";
    change.root = root;
    change.old_generation = active;
    change.old_marker_text = text;
    change.old_bin = marker->bin;
    change.old_links = marker->links;
    change.new_generation = name;
    change.new_tree = tree;
    change.new_marker = *marker;
    change.new_marker.components = components;
    change.new_tools = generation.tools();
    change.own_generation = detection.running_generation;
    run_change(change, *lock);
    std::cout << "updated neura " << installed_version << " to " << latest << " (" << name << ")\n";
    return exit_ok;
}

// ---------------------------------------------------------------------------
// neura uninstall

struct Removal {
    fs::path path;
    bool tree = false;
};

std::vector<std::string> remaining_entries(const fs::path& directory, const std::set<fs::path>& planned) {
    std::vector<std::string> remaining;
    std::error_code error;
    for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        const auto& path = it->path();
        if (planned.contains(path)) continue;
        const bool contains_planned = is_directory_no_follow(path) &&
                                      std::ranges::any_of(planned, [&](const fs::path& p) { return path_inside(p, path); });
        if (contains_planned) {
            for (const auto& inner : remaining_entries(path, planned)) remaining.push_back(inner);
        } else {
            remaining.push_back(path.string());
        }
    }
    std::sort(remaining.begin(), remaining.end());
    return remaining;
}

int run_uninstall(const fs::path& executable, const Detection& detection, const UninstallOptions& options) {
    const auto& root = detection.root;
    std::error_code error;
    const auto canonical_root = fs::canonical(root, error);
    if (error) throw Failure{"cannot resolve " + root.string() + ": " + describe(error)};
    const auto& marker = detection.marker;

    std::optional<InstallLock> lock;
    if (!options.dry_run) {
        lock.emplace(root, marker.schema == 1);
        recover(root, detection.running_generation, "neura: uninstall: ");
    }
    // A removal that affects running applications needs --terminate, and
    // no other process may hold a lease (docs/design/Lifecycle030.md).
    std::vector<std::string> in_use;
    for (const auto& name : generations_on_disk(root)) {
        if (exists_no_follow(lease_path(root, name)) && generation_in_use(root, name, detection.running_generation)) in_use.push_back(name);
    }
    const auto applications = service_hooks().applications(root);
    const bool service_running = !applications || *applications > 0;

    // Decide what goes before anything is removed: the links resolve
    // through `current`, which this removes.
    std::vector<Removal> plan;
    std::vector<std::string> left;
    for (const auto& name : marker.links) {
        const auto link = fs::path(marker.bin) / name;
        switch (link_state(link, canonical_root)) {
        case LinkState::Correct:
        case LinkState::OursOther:
            plan.push_back({link, false});
            break;
        case LinkState::Absent:
            break;
        case LinkState::Conflict:
            if (link_state(link, root) == LinkState::Correct || link_state(link, root) == LinkState::OursOther) {
                plan.push_back({link, false});
            } else {
                left.push_back(link.string() + (is_symlink_no_follow(link) ? ": it is a symbolic link that does not point into " + root.string()
                                                                           : std::string(": it is not a symbolic link")));
            }
            break;
        }
    }
    if (is_symlink_no_follow(root / "current")) {
        plan.push_back({root / "current", false});
    } else if (exists_no_follow(root / "current")) {
        left.push_back((root / "current").string() + ": it is not a symbolic link");
    }
    for (const auto& name : generations_on_disk(root)) plan.push_back({root / "versions" / name, true});
    for (const auto& name : marker.generations) {
        const auto directory = root / "versions" / name;
        if (exists_no_follow(directory) && std::ranges::none_of(plan, [&](const Removal& r) { return r.path == directory; })) {
            plan.push_back({directory, true});
        }
    }
    if (is_directory_no_follow(root / leases_name)) plan.push_back({root / leases_name, true});
    std::vector<fs::path> leftovers;
    for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (name == lock_name || name == update_check_name || name == journal_name || name.starts_with(".staging-") ||
            name.starts_with(".meta-") || name.starts_with(".marker-") || name.starts_with(".current-") || name.starts_with(".removing-") ||
            name.starts_with(".journal-") || name.starts_with(".neura-install.txt-")) {
            leftovers.push_back(it->path());
        }
    }
    std::sort(leftovers.begin(), leftovers.end());
    for (const auto& leftover : leftovers) plan.push_back({leftover, true});
    const auto marker_path = root / marker_name;
    const auto lock_path = root / install_lock_name;
    plan.push_back({marker_path, false});
    if (exists_no_follow(lock_path)) plan.push_back({lock_path, false});
    const auto service = remove_service_state(root, true);

    std::set<fs::path> planned;
    for (const auto& item : plan) planned.insert(item.path);

    if (options.dry_run) {
        for (const auto& item : plan) std::cout << "would remove " << item.path.string() << "\n";
        for (const auto& path : service) std::cout << "would remove " << path << " (the service's)\n";
        for (const auto& name : in_use) std::cout << "generation " << name << " is in use by a running neura process\n";
        if (service_running) {
            std::cout << (applications ? std::to_string(*applications) + " applications of this installation are running (neura runtime ps)"
                                       : std::string("a supervisor of this installation is running (neura runtime status)"))
                      << "\n";
        }
        for (const auto& note : left) std::cout << "would leave " << note << "\n";
        const auto versions_dir = root / "versions";
        const auto remaining = remaining_entries(root, planned);
        if (is_directory_no_follow(versions_dir) && remaining_entries(versions_dir, planned).empty()) {
            std::cout << "would remove " << versions_dir.string() << " (empty)\n";
        }
        if (remaining.empty()) {
            std::cout << "would remove " << root.string() << " (empty)\n";
        } else {
            std::cout << "would leave " << root.string() << " with:\n";
            for (const auto& entry : remaining) std::cout << "  " << entry << "\n";
        }
        std::cout << "nothing was removed (--dry-run)\n";
        return exit_ok;
    }
    if (service_running) check_applications(root, options.terminate, "neura: uninstall: ");
    // Holders other than the service's applications must end first.
    in_use.clear();
    for (const auto& name : generations_on_disk(root)) {
        if (exists_no_follow(lease_path(root, name)) && generation_in_use(root, name, detection.running_generation)) in_use.push_back(name);
    }
    if (!in_use.empty()) {
        std::string text;
        for (const auto& name : in_use) text += (text.empty() ? "" : "; ") + std::string("generation ") + name + " is in use by a running neura process";
        throw Failure{text + "; end it and run the command again"};
    }

    if (!options.yes) {
        if (!::isatty(0)) {
            std::cerr << "neura: uninstall: standard input is not a terminal; pass --yes to remove neura " << version::full << " from "
                      << root.string() << "\n";
            return exit_failure;
        }
        std::cout << "Remove neura " << version::full << " from " << root.string() << "? [y/N] " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        if (answer != "y" && answer != "Y" && answer != "yes" && answer != "Yes") {
            std::cout << "nothing was removed\n";
            return exit_failure;
        }
    }
    (void)executable;

    std::vector<std::string> failed;
    for (const auto& path : remove_service_state(root, false)) std::cout << "removed " << path << "\n";
    for (const auto& item : plan) {
        if ((item.path == marker_path || item.path == lock_path) && !failed.empty()) {
            // Keep the marker so that "uninstall" can be run again.
            if (item.path == marker_path) failed.push_back(marker_path.string() + ": kept because other removals failed");
            continue;
        }
        std::error_code removal;
        remove_tree(item.path, removal);
        if (removal) {
            failed.push_back(item.path.string() + ": " + describe(removal));
        } else {
            std::cout << "removed " << item.path.string() << "\n";
        }
    }
    for (const auto& note : left) std::cout << "left " << note << "\n";
    // `versions` and the root go only when nothing else is in them.
    const auto versions_dir = root / "versions";
    if (is_directory_no_follow(versions_dir)) {
        if (::rmdir(versions_dir.c_str()) == 0) std::cout << "removed " << versions_dir.string() << "\n";
    }
    const auto remaining = remaining_entries(root, {});
    if (remaining.empty()) {
        if (::rmdir(root.c_str()) == 0) {
            std::cout << "removed " << root.string() << "\n";
        } else {
            failed.push_back(root.string() + ": " + std::strerror(errno));
        }
    } else {
        std::cout << "left " << root.string() << ", which still holds:\n";
        for (const auto& entry : remaining) std::cout << "  " << entry << "\n";
    }
    if (!failed.empty()) {
        std::cerr << "neura: uninstall did not finish; these could not be removed:\n";
        for (const auto& entry : failed) std::cerr << "  " << entry << "\n";
        return exit_failure;
    }
    return exit_ok;
}

} // namespace

void set_service_hooks(ServiceHooks hooks) {
    auto& current = service_hooks();
    if (hooks.applications) current.applications = std::move(hooks.applications);
    if (hooks.terminate) current.terminate = std::move(hooks.terminate);
    if (hooks.retire) current.retire = std::move(hooks.retire);
}

int uninstall(const fs::path& executable, const UninstallOptions& options) {
    const auto detection = detect_install(executable);
    if (!detection.archive()) return refuse_unmanaged(detection, executable, "uninstall");
    try {
        if (!options.component.empty()) {
            if (options.component != "sdk" && options.component != "service") {
                throw Failure{"--component takes sdk or service, not \"" + options.component + "\""};
            }
            if (unmigrated(detection)) throw Failure{migrate_instruction};
            if (options.dry_run) throw Failure{"--dry-run applies to a full uninstall"};
            const auto active = current_generation(detection.root);
            const auto installed = read_generation(detection.root / "versions" / active);
            auto wanted = installed.components();
            if (std::ranges::find(wanted, options.component) == wanted.end()) {
                std::cout << "neura: uninstall: the " << options.component << " component is not installed\n";
                return exit_ok;
            }
            std::erase(wanted, options.component);
            if (!valid_component_set(wanted)) throw Failure{"removing " + options.component + " would leave no product; use \"neura uninstall\""};
            UpdateOptions update;
            update.terminate = options.terminate;
            return run_components(detection, update, wanted, "neura: uninstall: ", "uninstall-component");
        }
        return run_uninstall(executable, detection, options);
    } catch (const Failure& failure) {
        std::cerr << "neura: uninstall: " << failure.message << "\n";
        return exit_failure;
    }
}

// ---------------------------------------------------------------------------
// The update notice

namespace {

struct NoticeState {
    bool enabled = false;
    std::string reason;
};

NoticeState notice_state(const Detection& detection) {
    if (!detection.archive()) return {false, "not an archive install"};
    if (env_set("CI")) return {false, "CI is set"};
    if (env_set("NEURA_NO_UPDATE_CHECK")) return {false, "NEURA_NO_UPDATE_CHECK is set"};
    if (!::isatty(2) || !::isatty(0)) return {false, "standard input or standard error is not a terminal"};
    return {true, ""};
}

std::optional<UpdateCheck> read_update_check(const fs::path& root) {
    std::error_code error;
    const auto path = root / update_check_name;
    if (!fs::is_regular_file(path, error)) return std::nullopt;
    try {
        std::string reason;
        return parse_update_check(read_small_file(path, "update state"), reason);
    } catch (const Failure&) {
        return std::nullopt;
    }
}

} // namespace

void update_notice(const fs::path& executable) {
    try {
        const auto detection = detect_install(executable);
        if (!notice_state(detection).enabled) return;
        if (unmigrated(detection)) {
            // The instruction replaces the notice, and nothing is written.
            std::cerr << "neura: " << migrate_instruction << "\n";
            return;
        }
        const auto& root = detection.root;
        auto state = read_update_check(root);
        const auto now = static_cast<std::int64_t>(std::time(nullptr));
        if (update_check_due(state, now)) {
            UpdateCheck refreshed{now, state ? state->latest : std::string(version::full)};
            // A failure is silent and still records the time of the attempt.
            try {
                if (!find_in_path("curl").empty()) {
                    Fetcher fetcher;
                    fetcher.source = default_release_source;
                    fetcher.remote = true;
                    const auto metadata = fetch_metadata(fetcher, root, 2);
                    refreshed.latest = metadata.version;
                }
            } catch (const Failure&) {
            }
            try {
                const auto temporary = write_temp_file(root, ".meta-", format_update_check(refreshed));
                if (::rename(temporary.c_str(), (root / update_check_name).c_str()) != 0) ::unlink(temporary.c_str());
            } catch (const Failure&) {
            }
            state = refreshed;
        }
        if (state && is_newer(state->latest, version::full)) {
            std::cerr << "neura: version " << state->latest << " is available; run \"neura update\"\n";
        }
    } catch (...) {
    }
}

// ---------------------------------------------------------------------------
// neura doctor

namespace {

class Report {
public:
    void ok(const std::string& name, const std::string& detail) { line("ok", name, detail); }
    void warn(const std::string& name, const std::string& detail) { line("warn", name, detail); }
    void fail(const std::string& name, const std::string& detail) {
        line("FAIL", name, detail);
        failed_ = true;
    }
    bool failed() const { return failed_; }

private:
    void line(const char* level, const std::string& name, const std::string& detail) {
        std::cout << level << "  " << name << ": " << detail << "\n";
    }
    bool failed_ = false;
};

bool path_lists(const fs::path& directory) {
    const char* path = std::getenv("PATH");
    if (path == nullptr) return false;
    std::error_code error;
    const auto wanted = fs::weakly_canonical(directory, error);
    std::string_view remaining(path);
    for (;;) {
        const auto end = remaining.find(':');
        const auto entry = remaining.substr(0, end);
        if (!entry.empty()) {
            std::error_code entry_error;
            if (fs::weakly_canonical(fs::path(entry), entry_error) == wanted && !entry_error) return true;
        }
        if (end == std::string_view::npos) return false;
        remaining.remove_prefix(end + 1);
    }
}

} // namespace

int doctor(const fs::path& executable, const DoctorOptions& options) {
    Report report;
    report.ok("version", version_line());
    if (executable.empty()) {
        report.warn("executable", "the path of this executable could not be determined");
    } else {
        report.ok("executable", executable.string());
    }

    const auto detection = detect_install(executable);
    const auto& root = detection.root;
    const bool archive = detection.archive();
    const bool not_migrated = archive && unmigrated(detection);
    const bool interrupted = archive && exists_no_follow(journal_path(root));
    // The generation this executable is in, read from its manifests.
    std::optional<Generation> generation;
    std::vector<std::string> unknown;
    std::string manifest_problem;
    if (detection.kind == Detection::Kind::Installed) {
        try {
            generation = read_generation(root / "versions" / detection.running_generation, &unknown);
        } catch (const Failure& failure) {
            // A generation that a 0.2 client placed has no manifests.
            if (exists_no_follow(root / "versions" / detection.running_generation / components_directory)) manifest_problem = failure.message;
        }
    }
    std::vector<std::string> damage;
    if (generation) damage = quick_check(root / "versions" / detection.running_generation, *generation, 3);
    std::string state = "complete";
    if (not_migrated) state = "unmigrated";
    else if (interrupted) state = "interrupted";
    else if (!damage.empty() || !manifest_problem.empty()) state = "damaged";
    switch (detection.kind) {
    case Detection::Kind::Unmanaged:
        report.ok("install", "unmanaged (a source build or a copy; \"neura update\" and \"neura uninstall\" do not apply)");
        break;
    case Detection::Kind::Broken:
        report.fail("install", "the install marker is not valid: " + detection.problem);
        break;
    case Detection::Kind::Installed:
        report.ok("install", "method " + detection.marker.method + ", root " + root.string() + ", marker schema " +
                                 std::to_string(detection.marker.schema) + (archive ? ", state " + state : std::string()));
        break;
    }

    // The components: from the manifests of an installed generation, else
    // the executables next to this one (docs/design/Products030.md).
    bool sdk = true;
    bool service = false;
    if (generation) {
        const auto names = generation->components();
        sdk = std::ranges::find(names, "sdk") != names.end();
        service = std::ranges::find(names, "service") != names.end();
        report.ok("components", join_names(names, ", ") + " (version " + generation->version() + ", build " + generation->build() + ")");
        for (const auto& name : unknown) report.warn("components", "the component " + name + " is not one this neura knows; it is ignored");
        if (damage.empty()) {
            report.ok("integrity", "every file of the manifests has its size, and every executable and library its digest");
        } else {
            report.fail("integrity", join_names(damage, "; ") + ". Reinstall this version with install.sh --force");
        }
    } else if (!manifest_problem.empty()) {
        report.fail("integrity", manifest_problem);
    } else if (!executable.empty()) {
        const auto directory = executable.parent_path();
        std::vector<std::string> missing_core;
        std::vector<std::string> missing_sdk;
        std::size_t sdk_found = 0;
        std::error_code error;
        if (!fs::is_regular_file(directory / tool::executable_file("neura"), error)) missing_core.emplace_back("neura");
        for (const auto& each : tool::tools()) {
            const bool found = fs::is_regular_file(directory / tool::executable_file(each.name), error);
            if (each.component == tool::Component::Core && !found) missing_core.emplace_back(each.name);
            if (each.component != tool::Component::Sdk) continue;
            if (found) ++sdk_found;
            else missing_sdk.emplace_back(each.name);
        }
        sdk = sdk_found > 0;
        service = fs::is_regular_file(directory / tool::executable_file(tool::service_executable), error);
        if (!missing_core.empty()) {
            report.fail("components", "the Neura core is incomplete: " + join_names(missing_core, ", ") + " missing from " + directory.string() +
                                          ". Reinstall Neura");
        } else if (sdk && !missing_sdk.empty()) {
            report.fail("components", "the Neura-sdk component is incomplete: " + join_names(missing_sdk, ", ") + " missing from " +
                                          directory.string() + ". Reinstall Neura");
        } else {
            report.ok("components", sdk ? "core, sdk" : "core (runtime only: the Neura-sdk component is not installed)");
        }
    }

    if (!sdk) {
        report.ok("sdk", "not installed; source commands need Neura-sdk");
    } else {
        if (options.library.empty()) {
            report.fail("stdlib", "the standard library directory (share/neura/stdlib/std) was not found next to this executable");
        } else {
            report.ok("stdlib", options.library.string());
        }
        auto prefix = executable.parent_path();
        if (prefix.filename() == "bin") prefix = prefix.parent_path();
        const auto triple = nvm::native_target_triple();
        const auto directory = prefix / "lib/neura/launchers" / triple;
        std::error_code error;
        if (!executable.empty() && fs::is_regular_file(directory / "neura-launcher", error)) {
            report.ok("launchers", directory.string());
        } else {
            report.warn("launchers", "no native launchers for " + triple + " in " + directory.string() + "; build --native is unavailable");
        }
        if (!options.probe_libclang) {
            report.warn("libclang", "not checked: this doctor was built without the Neura-sdk component");
            report.warn("clang-headers", "not checked: libclang was not loaded");
        } else {
            const auto status = options.probe_libclang();
            if (status.loaded) {
                report.ok("libclang", status.path + (status.major != 0 ? " (LLVM " + std::to_string(status.major) + ")" : ""));
                if (status.resource_dir.empty()) {
#ifdef __APPLE__
                    report.warn("clang-headers", "libclang's builtin headers (stddef.h) were not found; C imports are unavailable. "
                                                 "Install the Xcode Command Line Tools (xcode-select --install) or Xcode");
#else
                    report.warn("clang-headers", "libclang's builtin headers (stddef.h) were not found; C imports are unavailable. On Debian and Ubuntu install libclang-common-" +
                                                     std::to_string(status.major != 0 ? status.major : 19) + "-dev");
#endif
                } else {
                    report.ok("clang-headers", status.resource_dir);
                }
            } else {
#ifdef __APPLE__
                report.warn("libclang", "C imports are unavailable: " + first_line(status.error) +
                                            ". Install the Xcode Command Line Tools (xcode-select --install) or Xcode, or name a libclang.dylib with NEURA_LIBCLANG");
#else
                report.warn("libclang", "C imports are unavailable: " + first_line(status.error) +
                                            ". On Debian and Ubuntu install libclang1-19 libclang-common-19-dev");
#endif
                report.warn("clang-headers", "not checked: libclang did not load");
            }
        }
        const char* cc = std::getenv("CC");
        const std::string compiler = cc != nullptr && *cc != '\0' ? cc : "cc";
        if (find_in_path(compiler).empty()) {
            report.warn("c-compiler", compiler + " was not found on PATH; it is needed only to link the libraries of C imports");
        } else {
            report.ok("c-compiler", find_in_path(compiler).string());
        }
#ifdef NEURA_HAS_GPU
        if (gpu::vulkan_loader_available()) {
            report.ok("gpu", "compiled in; the Vulkan loader opens");
        } else {
            report.warn("gpu", "compiled in; the Vulkan loader (libvulkan) cannot be opened, so graphs run on the CPU");
        }
#else
        report.ok("gpu", "not compiled in; graphs run on the CPU");
#endif
    }

    if (!service) {
        report.ok("service", "not installed (optional)");
    } else {
        // Offline and read-only: the supervisor is found by the lock it
        // holds, and login start by its file; nothing is started.
        std::string detail = "installed, protocol 1.0";
        if (generation) {
            if (const auto* manifest = generation->find("service"); manifest && manifest->protocol.starts_with("service ")) {
                detail = "installed, protocol " + manifest->protocol.substr(8);
            }
        }
        const bool daemon = !executable.empty() && fs::exists(executable.parent_path() / tool::executable_file(tool::service_executable));
        if (!daemon) detail += "; neurad is not in this build";
        if (archive) {
            const auto supervisors = running_supervisors(root);
            detail += supervisors.empty() ? "; no supervisor is running" : "; a supervisor runs for " + join_names(supervisors, ", ");
            bool registered = false;
            for (const auto& unit : service_unit_files(root)) registered = registered || exists_no_follow(unit);
            detail += registered ? "; login start is registered" : "; login start is not registered";
        }
        report.ok("service", detail);
    }

    const std::array<std::pair<const char*, const char*>, 3> tools{{
        {"curl", "needed by \"neura update\""},
        {"tar", "needed by \"neura update\""},
        {"gh", "needed to verify build provenance in \"neura update\""},
    }};
    for (const auto& [tool, purpose] : tools) {
        const auto found = find_in_path(tool);
        if (found.empty()) {
            report.warn(tool, std::string("not found on PATH; ") + purpose);
        } else {
            report.ok(tool, found.string());
        }
    }

    if (archive) {
        std::size_t good = 0;
        for (const auto& name : detection.marker.links) {
            const auto link = fs::path(detection.marker.bin) / name;
            switch (link_state(link, root)) {
            case LinkState::Correct:
                ++good;
                break;
            case LinkState::Absent:
                report.warn("links", link.string() + " does not exist; run \"neura update --repair\"");
                break;
            case LinkState::OursOther:
                report.warn("links", link.string() + " does not point through " + (root / "current").string());
                break;
            case LinkState::Conflict:
                report.warn("links", link.string() + (is_symlink_no_follow(link) ? " does not point into " + root.string()
                                                                                : std::string(" is not a symbolic link")));
                break;
            }
        }
        if (good == detection.marker.links.size()) {
            report.ok("links", std::to_string(good) + " in " + detection.marker.bin + ", each through " + (root / "current").string());
        }
        if (path_lists(detection.marker.bin)) {
            report.ok("path", detection.marker.bin + " is on PATH");
        } else {
            report.warn("path", detection.marker.bin + " is not on PATH");
        }
        if (not_migrated) {
            report.fail("migration", "run \"neura update --migrate-install\" to finish the install");
        } else {
            report.ok("migration", detection.marker.schema == 2 ? "not needed" : "not needed: a 0.2 installation");
        }
        if (interrupted) {
            std::string operation = "change";
            try {
                std::string problem;
                if (const auto journal = parse_journal(read_small_file(journal_path(root), "the journal"), problem)) operation = journal->operation;
            } catch (const Failure&) {
            }
            report.fail("transaction", "an interrupted " + operation + " left " + journal_path(root).string() +
                                           "; run \"neura update --repair\" to complete or undo it");
        } else {
            report.ok("transaction", "none interrupted");
        }
        // Generations: the active and the previous one, and any other that
        // a running process keeps.
        const auto active = current_generation(root);
        const auto previous = previous_generation(root, active, false);
        report.ok("generations", "active " + (active.empty() ? std::string("none") : active) +
                                     (previous.empty() ? std::string(", no previous") : ", previous " + previous));
        for (const auto& name : generations_on_disk(root)) {
            if (name == active || name == previous) continue;
            const bool used = exists_no_follow(lease_path(root, name)) && generation_in_use(root, name, detection.running_generation);
            report.warn("generations", name + " is on disk and " + (used ? "in use by a running neura process" :
                                                                         "not in use; \"neura update --repair\" removes it"));
        }
        if (detection.marker.schema == 2 && !active.empty()) {
            const int descriptor = ::open(lease_path(root, active).c_str(), O_RDONLY | O_CLOEXEC);
            if (descriptor >= 0) {
                if (::flock(descriptor, LOCK_SH | LOCK_NB) != 0 && lock_unsupported(errno)) {
                    report.warn("leases", "the file system of " + root.string() +
                                              " cannot lock, so tools run without leases and no generation is removed automatically");
                } else {
                    report.ok("leases", "each tool holds the lease of its generation while it runs");
                }
                ::close(descriptor);
            }
        }
    }

    {
        const auto notices = notice_state(detection);
        if (notices.enabled) {
            report.ok("update-notices", "enabled (at most one network check every 7 days; set NEURA_NO_UPDATE_CHECK=1 to turn it off)");
        } else {
            report.ok("update-notices", "off here: " + notices.reason);
        }
    }
    return report.failed() ? exit_failure : exit_ok;
}

int update(const fs::path& executable, const UpdateOptions& options) {
    const int modes = int(options.check) + int(options.migrate) + int(options.rollback) + int(options.repair) +
                      int(!options.components.empty()) + int(!options.install_from.empty());
    if (modes > 1) {
        std::cerr << "neura: update: --check, --components, --rollback, --repair, --migrate-install and --install-from exclude each other\n";
        return exit_failure;
    }
    if ((!options.add.empty() || options.force) && options.install_from.empty()) {
        std::cerr << "neura: update: --add and --force apply to --install-from, which install.sh runs\n";
        return exit_failure;
    }
    if (options.terminate && options.components.empty()) {
        std::cerr << "neura: update: --terminate applies to --components\n";
        return exit_failure;
    }
    if (!options.install_from.empty()) {
        // install.sh runs this from the unpacked archive, which belongs to
        // no installation; the root comes from NEURA_INSTALL_ROOT.
        try {
            return install_from(options);
        } catch (const Failure& failure) {
            std::cerr << "install.sh: " << failure.message << "\n";
            return exit_failure;
        } catch (const std::exception& error) {
            std::cerr << "install.sh: " << error.what() << "\n";
            return exit_failure;
        }
    }
    const auto detection = detect_install(executable);
    if (!detection.archive()) return refuse_unmanaged(detection, executable, "update");
    try {
        if (options.migrate) return run_migrate(detection);
        if (unmigrated(detection)) throw Failure{migrate_instruction};
        if (options.rollback) return run_rollback(detection);
        if (options.repair) return run_repair(detection);
        if (!options.components.empty()) {
            return run_components(detection, options, parse_component_set(options.components), "neura: update: ", "components");
        }
        return run_update(executable, detection, options);
    } catch (const Failure& failure) {
        std::cerr << "neura: update: " << failure.message << "\n";
        return exit_failure;
    } catch (const std::exception& error) {
        std::cerr << "neura: update: " << error.what() << "\n";
        return exit_failure;
    }
}

#endif

} // namespace neura::lifecycle

