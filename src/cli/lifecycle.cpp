#include "lifecycle.hpp"

#include "install_layout.hpp"
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
#include "c-import/c_import.hpp"
#ifdef NEURA_HAS_GPU
#include "gpu/device.hpp"
#endif
#include "nvm/providers.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace neura::lifecycle {

namespace fs = std::filesystem;

std::string version_line() {
    return std::string("neura ") + version::full + " (" + version::triple + ", commit " + version::commit + ")";
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

fs::path version_directory(const fs::path& root, const std::string& version) { return root / "versions" / version; }

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

struct Fault {
    static bool at(std::string_view step) {
        const char* value = std::getenv("NEURA_TEST_UPDATE_FAULT");
        return value != nullptr && step == value;
    }
};

void inject(std::string_view step) {
    if (Fault::at(step)) throw Failure{"injected fault at step " + std::string(step) + " (NEURA_TEST_UPDATE_FAULT)"};
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

class UpdateLock {
public:
    explicit UpdateLock(const fs::path& root) : path_(root / lock_name) {
        const int descriptor = ::open(path_.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (descriptor < 0) {
            if (errno == EEXIST) {
                const auto modified = modified_no_follow(path_);
                if (modified && std::time(nullptr) - *modified > 3600) {
                    throw Failure{"the update lock " + path_.string() +
                                  " is more than an hour old. No update appears to be running; delete the file and run \"neura update\" again"};
                }
                throw Failure{"another update appears to be running (lock " + path_.string() +
                              "). If none is, delete that file and run \"neura update\" again"};
            }
            throw Failure{"cannot create the update lock " + path_.string() + ": " + std::strerror(errno)};
        }
        const auto pid = std::to_string(::getpid()) + "\n";
        const auto written = ::write(descriptor, pid.data(), pid.size());
        (void)written;
        ::close(descriptor);
        held_ = true;
    }
    ~UpdateLock() {
        if (held_) ::unlink(path_.c_str());
    }
    UpdateLock(const UpdateLock&) = delete;
    UpdateLock& operator=(const UpdateLock&) = delete;

private:
    fs::path path_;
    bool held_ = false;
};

// Removes `.staging-*` directories that a crashed update left more than a day ago.
void remove_stale_staging(const fs::path& root) {
    std::error_code error;
    for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (!name.starts_with(".staging-") || !is_directory_no_follow(it->path())) continue;
        const auto modified = modified_no_follow(it->path());
        if (modified && std::time(nullptr) - *modified > 24 * 3600) {
            std::error_code ignored;
            remove_tree(it->path(), ignored);
        }
    }
}

// Whether `name` is a safe member of the archive of `top`: relative, without
// "." or ".." components, with no backslash or control character, inside `top`.
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
    inject("download");
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
    inject("checksum");
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
    inject("extract");
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

void verify_staged_tree(const fs::path& tree, const std::string& version) {
    inject("verify");
    const auto binary = tree / "bin/neura";
    struct stat info {};
    if (::lstat(binary.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || (info.st_mode & S_IXUSR) == 0) {
        throw Failure{"the archive has no executable bin/neura"};
    }
    if (!fs::is_directory(tree / "share/neura/stdlib/std")) throw Failure{"the archive has no share/neura/stdlib/std"};
    const auto result = run_child({binary.string(), "--version"}, 30);
    if (!result.started || result.status != 0) throw Failure{"the new neura does not run: " + failure_detail(result)};
    const auto line = first_line(result.out);
    if (!line.starts_with("neura " + version + " ")) {
        throw Failure{"the new neura reports \"" + line + "\"; expected a line starting \"neura " + version + " \""};
    }
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

int run_update(const fs::path& executable, const Detection& detection, const UpdateOptions& options) {
    const auto& root = detection.root;
    const std::string current = version::full;
    auto fetcher = make_fetcher(options.source);
    std::optional<UpdateLock> lock;
    if (!options.check) {
        if (options.require_attestation && find_in_path("gh").empty()) {
            throw Failure{"--require-attestation needs gh (the GitHub CLI), which is not on PATH. Nothing was changed"};
        }
        lock.emplace(root);
        remove_stale_staging(root);
    }
    std::optional<PublishedRelease> selected;
    if (options.nightly && options.source == default_release_source) {
        selected = newest_github_release(root);
        fetcher = make_fetcher(std::string("https://github.com/") + repository + "/releases/download/" + selected->tag);
    }
    const auto metadata = fetch_metadata(fetcher, root, 30);
    if (selected && metadata.version != selected->version) {
        throw Failure{"the release metadata version " + metadata.version + " does not match selected GitHub tag " + selected->tag};
    }
    if (!is_newer(metadata.version, current)) {
        std::cout << "neura " << current << " is up to date\n";
        return exit_ok;
    }
    if (options.check) {
        std::cout << "neura " << current << " is installed; neura " << metadata.version << " is available\n"
                  << "run \"neura update" << (options.nightly ? " --nightly" : "") << "\" to install it\n";
        return exit_update_available;
    }
    const auto* asset = metadata.find(version::triple);
    if (asset == nullptr) {
        throw Failure{"neura " + metadata.version + " has no build for " + version::triple + ". Nothing was changed"};
    }
    const auto new_dir = version_directory(root, metadata.version);
    if (exists_no_follow(new_dir)) throw Failure{new_dir.string() + " already exists. Nothing was changed"};
    std::cout << "updating neura " << current << " to " << metadata.version << "\n";

    std::string staging_name = (root / (".staging-" + std::to_string(::getpid()) + "-XXXXXX")).string();
    if (::mkdtemp(staging_name.data()) == nullptr) {
        throw Failure{"cannot create a staging directory in " + root.string() + ": " + std::strerror(errno)};
    }
    const fs::path staging = staging_name;
    RemoveOnExit staging_cleanup(staging);
    ::chmod(staging.c_str(), 0700);

    const auto archive = fetch_asset(fetcher, *asset, staging);
    check_size_and_digest(archive, *asset);
    std::cout << "verified: size and SHA-256 of " << asset->file << " against the release metadata\n";
    std::cout << verify_provenance(archive, !fetcher.remote, options.require_attestation) << "\n";
    const auto extract_dir = staging / "tree";
    fs::create_directory(extract_dir);
    ::chmod(extract_dir.c_str(), 0700);
    const std::string top = "neura-" + metadata.version + "-" + version::triple;
    extract(archive, extract_dir, top);
    const auto staged = extract_dir / top;
    verify_staged_tree(staged, metadata.version);

    // Activate. Everything from here on is undone if a step fails.
    Marker marker = detection.marker;
    std::vector<std::string> old_versions = marker.versions;
    if (std::ranges::find(old_versions, detection.running_version) == old_versions.end()) old_versions.push_back(detection.running_version);
    marker.versions = {metadata.version};
    std::string old_target = "versions/" + detection.running_version;
    {
        char buffer[4096];
        const auto length = ::readlink((root / "current").c_str(), buffer, sizeof buffer - 1);
        if (length > 0) old_target.assign(buffer, static_cast<std::size_t>(length));
    }
    fs::path marker_temp;
    bool tree_placed = false;
    bool link_switched = false;
    try {
        std::error_code created;
        fs::create_directories(root / "versions", created);
        if (created) throw Failure{"cannot create " + (root / "versions").string() + ": " + describe(created)};
        if (::rename(staged.c_str(), new_dir.c_str()) != 0) {
            throw Failure{"cannot move the new version to " + new_dir.string() + ": " + std::strerror(errno)};
        }
        tree_placed = true;
        marker_temp = write_temp_file(root, ".marker-", format_marker(marker));
        inject("activate-link");
        replace_symlink(root, "versions/" + metadata.version);
        link_switched = true;
        inject("activate-marker");
        if (::rename(marker_temp.c_str(), (root / marker_name).c_str()) != 0) {
            throw Failure{std::string("cannot write ") + (root / marker_name).string() + ": " + std::strerror(errno)};
        }
        marker_temp.clear();
    } catch (const Failure& failure) {
        std::vector<std::string> problems;
        if (link_switched) {
            try {
                replace_symlink(root, old_target);
            } catch (const Failure& again) {
                problems.push_back(again.message);
            }
        }
        if (!marker_temp.empty()) ::unlink(marker_temp.c_str());
        if (tree_placed) {
            std::error_code removal;
            remove_tree(new_dir, removal);
            if (removal) problems.push_back("cannot remove " + new_dir.string() + ": " + describe(removal));
        }
        if (!problems.empty()) {
            std::string text = "update failed (" + failure.message + ") and the rollback did not finish:";
            for (const auto& problem : problems) text += "\n  " + problem;
            text += "\n" + executable.string() + " still works. See docs/Install.md to repair " + root.string();
            throw Failure{text};
        }
        throw Failure{"update failed and was rolled back; neura " + detection.running_version + " is still installed (" + failure.message + ")"};
    }

    // The new version is active. Removing the old ones is cleanup.
    std::vector<std::string> kept;
    for (const auto& old : old_versions) {
        if (old == metadata.version) continue;
        std::error_code removal;
        remove_tree(version_directory(root, old), removal);
        if (removal) {
            std::cerr << "neura: warning: could not remove " << version_directory(root, old).string() << ": " << describe(removal) << "\n";
            kept.push_back(old);
        }
    }
    if (!kept.empty()) {
        // Keep uninstall able to find what is left.
        try {
            marker.versions.insert(marker.versions.end(), kept.begin(), kept.end());
            const auto replacement = write_temp_file(root, ".marker-", format_marker(marker));
            if (::rename(replacement.c_str(), (root / marker_name).c_str()) != 0) ::unlink(replacement.c_str());
        } catch (const Failure&) {
        }
    }
    std::cout << "updated neura " << current << " to " << metadata.version << "\n";
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

} // namespace

int uninstall(const fs::path& executable, const UninstallOptions& options) {
    const auto detection = detect_install(executable);
    if (!detection.archive()) return refuse_unmanaged(detection, executable, "uninstall");
    const auto& root = detection.root;
    std::error_code error;
    const auto canonical_root = fs::canonical(root, error);
    if (error) {
        std::cerr << "neura: uninstall: cannot resolve " << root.string() << ": " << describe(error) << "\n";
        return exit_failure;
    }

    // Decide what goes before anything is removed: the bin link resolves
    // through `current`, which this removes.
    std::vector<Removal> plan;
    std::vector<std::string> left;
    const fs::path link(detection.marker.link);
    if (is_symlink_no_follow(link)) {
        std::error_code resolve;
        auto resolved = fs::canonical(link, resolve);
        if (resolve) {
            char buffer[4096];
            const auto length = ::readlink(link.c_str(), buffer, sizeof buffer - 1);
            if (length > 0) {
                const fs::path target(std::string(buffer, static_cast<std::size_t>(length)));
                if (target.is_absolute()) resolved = target.lexically_normal();
            }
        }
        if (!resolved.empty() && (path_inside(resolved, canonical_root) || path_inside(resolved, root))) {
            plan.push_back({link, false});
        } else {
            left.push_back(link.string() + ": it is a symbolic link that does not point into " + root.string());
        }
    } else if (exists_no_follow(link)) {
        left.push_back(link.string() + ": it is not a symbolic link");
    }
    if (is_symlink_no_follow(root / "current")) {
        plan.push_back({root / "current", false});
    } else if (exists_no_follow(root / "current")) {
        left.push_back((root / "current").string() + ": it is not a symbolic link");
    }
    for (const auto& version : detection.marker.versions) {
        const auto directory = version_directory(root, version);
        if (exists_no_follow(directory)) plan.push_back({directory, true});
    }
    std::vector<fs::path> leftovers;
    for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (name == lock_name || name == update_check_name || name.starts_with(".staging-") || name.starts_with(".meta-") ||
            name.starts_with(".marker-") || name.starts_with(".current-")) {
            leftovers.push_back(it->path());
        }
    }
    std::sort(leftovers.begin(), leftovers.end());
    for (const auto& leftover : leftovers) plan.push_back({leftover, true});
    const auto marker_path = root / marker_name;
    plan.push_back({marker_path, false});

    std::set<fs::path> planned;
    for (const auto& item : plan) planned.insert(item.path);

    if (options.dry_run) {
        for (const auto& item : plan) std::cout << "would remove " << item.path.string() << "\n";
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

    std::vector<std::string> failed;
    for (const auto& item : plan) {
        if (item.path == marker_path && !failed.empty()) {
            // Keep the marker so that "uninstall" can be run again.
            failed.push_back(marker_path.string() + ": kept because other removals failed");
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
    switch (detection.kind) {
    case Detection::Kind::Unmanaged:
        report.ok("install", "unmanaged (a source build or a copy; \"neura update\" and \"neura uninstall\" do not apply)");
        break;
    case Detection::Kind::Broken:
        report.fail("install", "the install marker is not valid: " + detection.problem);
        break;
    case Detection::Kind::Installed:
        report.ok("install", "method " + detection.marker.method + ", root " + detection.root.string());
        break;
    }

    if (options.library.empty()) {
        report.fail("stdlib", "the standard library directory (share/neura/stdlib/std) was not found next to this executable");
    } else {
        report.ok("stdlib", options.library.string());
    }

    {
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
    }

    {
        const auto status = probe_libclang(CImportOptions{});
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

    {
        const char* cc = std::getenv("CC");
        const std::string compiler = cc != nullptr && *cc != '\0' ? cc : "cc";
        if (find_in_path(compiler).empty()) {
            report.warn("c-compiler", compiler + " was not found on PATH; it is needed only to link the libraries of C imports");
        } else {
            report.ok("c-compiler", find_in_path(compiler).string());
        }
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

    if (detection.archive()) {
        const fs::path link(detection.marker.link);
        std::error_code error;
        if (!is_symlink_no_follow(link)) {
            report.warn("bin-link", link.string() + (exists_no_follow(link) ? " is not a symbolic link" : " does not exist"));
        } else {
            const auto resolved = fs::canonical(link, error);
            const auto root = fs::canonical(detection.root, error);
            if (resolved.empty() || !path_inside(resolved, root)) {
                report.warn("bin-link", link.string() + " does not point into " + detection.root.string());
            } else {
                report.ok("bin-link", link.string() + " -> " + resolved.string());
            }
        }
        if (path_lists(link.parent_path())) {
            report.ok("path", link.parent_path().string() + " is on PATH");
        } else {
            report.warn("path", link.parent_path().string() + " is not on PATH");
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
    const auto detection = detect_install(executable);
    if (!detection.archive()) return refuse_unmanaged(detection, executable, "update");
    try {
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
