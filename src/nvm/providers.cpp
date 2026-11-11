#include "provider_internal.hpp"

#include "nvr/format.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <deque>
#include <random>
#include <thread>

#ifndef _WIN32
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/random.h>
#endif

namespace neura::nvm {

std::uint32_t host_error::from_errno(int error) {
    switch (error) {
    case ENOENT:
        return not_found;
    case EACCES:
    case EPERM:
    case EROFS:
        return permission_denied;
    case EEXIST:
    case EADDRINUSE:
        return already_exists;
    case EINVAL:
    case ENAMETOOLONG:
    case EADDRNOTAVAIL:
    case EAFNOSUPPORT:
        return invalid_input;
    case EINTR:
        return interrupted;
    case ENOTDIR:
        return not_a_directory;
    case EISDIR:
        return is_a_directory;
    case ENOTEMPTY:
        return directory_not_empty;
    case ECONNREFUSED:
        return connection_refused;
    case ECONNRESET:
    case ECONNABORTED:
    case EPIPE:
    case ENOTCONN:
        return connection_reset;
    case ETIMEDOUT:
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
        return timed_out;
    case ENOSYS:
    case EXDEV:
    case ESPIPE:
#ifdef ENOTSUP
    case ENOTSUP:
#endif
#if defined(EOPNOTSUPP) && (!defined(ENOTSUP) || EOPNOTSUPP != ENOTSUP)
    case EOPNOTSUPP:
#endif
        return unsupported;
    default:
        return other;
    }
}

namespace provider {

bool readable_now(int descriptor) {
#ifdef _WIN32
    (void)descriptor;
    return false;
#else
    pollfd entry{descriptor, POLLIN, 0};
    for (;;) {
        const int ready = ::poll(&entry, 1, 0);
        if (ready >= 0) return ready > 0;
        if (errno != EINTR) return false;
    }
#endif
}

int timeout_milliseconds(std::int64_t nanos) {
    if (nanos <= 0) return -1;
    const auto milliseconds = nanos / 1'000'000 + (nanos % 1'000'000 != 0 ? 1 : 0);
    return static_cast<int>(std::min<std::int64_t>(milliseconds, INT_MAX));
}

} // namespace provider

using namespace provider;

std::string ResolvedPath::text() const {
    if (components.empty()) return "/";
    std::string result;
    for (const auto& component : components) {
        result += '/';
        result += component;
    }
    return result;
}

bool ResolvedPath::within(const ResolvedPath& prefix) const {
    return prefix.components.size() <= components.size() &&
           std::equal(prefix.components.begin(), prefix.components.end(), components.begin());
}

namespace {

void split_components(std::string_view text, std::deque<std::string>& out, bool at_front) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = std::min(text.find('/', start), text.size());
        const auto part = text.substr(start, end - start);
        if (!part.empty() && part != ".") parts.emplace_back(part);
        start = end + 1;
    }
    if (at_front) {
        out.insert(out.begin(), parts.begin(), parts.end());
    } else {
        out.insert(out.end(), parts.begin(), parts.end());
    }
}

} // namespace

ResolvedPath resolve_path(std::string_view path, bool follow_last) {
    ResolvedPath result;
    if (path.empty() || path.find('\0') != std::string_view::npos) {
        result.error = host_error::invalid_input;
        return result;
    }
#ifdef _WIN32
    (void)follow_last;
    result.error = host_error::unsupported;
    return result;
#else
    std::deque<std::string> pending;
    if (path.front() != '/') {
        std::string directory(PATH_MAX, '\0');
        if (::getcwd(directory.data(), directory.size()) == nullptr) {
            result.error = host_error::from_errno(errno);
            return result;
        }
        directory.resize(std::char_traits<char>::length(directory.c_str()));
        std::deque<std::string> current;
        split_components(directory, current, false);
        result.components.assign(current.begin(), current.end());
    }
    split_components(path, pending, false);
    std::optional<std::string> last;
    if (!follow_last && !pending.empty() && pending.back() != "..") {
        last = pending.back();
        pending.pop_back();
    }
    int links = 0;
    bool exists = true;
    while (!pending.empty()) {
        auto component = std::move(pending.front());
        pending.pop_front();
        if (component == "..") {
            if (!result.components.empty()) result.components.pop_back();
            continue;
        }
        if (exists) {
            result.components.push_back(component);
            const auto candidate = result.text();
            struct stat info {};
            if (::lstat(candidate.c_str(), &info) != 0) {
                exists = false;
                continue;
            }
            if (!S_ISLNK(info.st_mode)) continue;
            if (++links > 40) {
                result.error = host_error::other;
                return result;
            }
            std::string target(static_cast<std::size_t>(info.st_size > 0 ? info.st_size : PATH_MAX) + 1, '\0');
            const auto length = ::readlink(candidate.c_str(), target.data(), target.size());
            if (length < 0) {
                result.error = host_error::from_errno(errno);
                return result;
            }
            target.resize(static_cast<std::size_t>(length));
            result.components.pop_back();
            if (!target.empty() && target.front() == '/') result.components.clear();
            split_components(target, pending, true);
            continue;
        }
        result.components.push_back(component);
    }
    if (last) result.components.push_back(*last);
    return result;
#endif
}

ConsoleSource::Read DescriptorSource::read(std::size_t max) {
    Read result;
#ifdef _WIN32
    (void)max;
    result.error = host_error::unsupported;
#else
    std::string buffer(max, '\0');
    for (;;) {
        const auto count = ::read(descriptor_, buffer.data(), buffer.size());
        if (count >= 0) {
            buffer.resize(static_cast<std::size_t>(count));
            result.bytes = std::move(buffer);
            return result;
        }
        if (errno != EINTR) break;
    }
    result.error = host_error::from_errno(errno);
#endif
    return result;
}

ConsoleSource::Read StringSource::read(std::size_t max) {
    const auto count = std::min({max, chunk_, input_.size() - position_});
    Read result;
    result.bytes = input_.substr(position_, count);
    position_ += count;
    return result;
}

void replay_console_output(const std::string& import, std::uint32_t version, const std::vector<HostValue>& arguments,
                           const HostCompletion& completion, ConsoleSink& output, ConsoleSink& error) {
    if (version != 1 || !completion.success) return;
    if (import == "neura.console.write_line" && arguments.size() == 1) {
        output.write(arguments[0].bytes + "\n");
    } else if (import == "neura.console.write" && arguments.size() == 2) {
        const auto stream = as_int32(arguments[0]);
        const auto count = std::clamp<std::int64_t>(as_int32(completion.value), 0,
                                                     static_cast<std::int64_t>(arguments[1].bytes.size()));
        const auto bytes = std::string_view(arguments[1].bytes).substr(0, static_cast<std::size_t>(count));
        if (stream == 1) output.write(bytes);
        if (stream == 2) error.write(bytes);
    }
}

StandardServices::StandardServices(ConsoleSink& output, ConsoleSink& error, ConsoleSource& input,
                                   Capabilities capabilities)
    : output_(output), error_(error), input_(input), capabilities_(std::move(capabilities)),
      tables_(std::make_unique<Tables>()) {
    for (const auto& prefix : capabilities_.fs_prefixes) {
        auto resolved = resolve_path(prefix, true);
        if (resolved.error != 0) {
            throw std::invalid_argument("cannot resolve the file system prefix '" + prefix + "'");
        }
        prefixes_.push_back(std::move(resolved));
    }

    // Console: always allowed.
    add(signature("neura.console.write", {int32(), byte_list()}, int32()), [this](const HostRequest& request) {
        const auto stream = as_int32(request.arguments.at(0));
        const auto& bytes = request.arguments.at(1).bytes;
        if (stream != 1 && stream != 2) return fail(host_error::invalid_input);
        if (bytes.size() > static_cast<std::size_t>(INT32_MAX)) return fail(host_error::invalid_input);
        if (bytes.empty()) return HostCompletion::ok(int32_value(0));
        const auto written = (stream == 1 ? output_ : error_).write_some(bytes);
        if (written.written == 0 && written.failed) return fail(host_error::other);
        return HostCompletion::ok(int32_value(static_cast<std::int32_t>(written.written)));
    });
    const FunctionService::Handler read_input = [this](const HostRequest& request) {
        const auto max = as_int32(request.arguments.at(0));
        if (max < 1) return fail(host_error::invalid_input);
        auto read = input_.read(std::min(static_cast<std::size_t>(max), max_read_bytes));
        if (read.error != 0) return fail(read.error);
        return HostCompletion::ok(bytes_value(std::move(read.bytes)));
    };
    // Overlapping: input that is not a descriptor (in memory) completes at
    // once. A descriptor is waited for, and a read that finds it readable
    // does not block.
    add_overlapping(signature("neura.console.read", {int32()}, byte_list()), read_input,
                    [this, read_input](const HostRequest& request) -> std::variant<HostCompletion, PendingOperation> {
        const auto max = as_int32(request.arguments.at(0));
        const auto descriptor = input_.descriptor();
        if (max < 1 || descriptor < 0) return read_input(request);
        PendingOperation operation;
        operation.descriptor = descriptor;
        operation.readable = true;
        const auto count = std::min(static_cast<std::size_t>(max), max_read_bytes);
        operation.attempt = [this, descriptor, count](bool cancelled) -> std::optional<HostCompletion> {
            if (!readable_now(descriptor)) {
                if (cancelled) return cancelled_answer();
                return std::nullopt;
            }
            auto read = input_.read(count);
            if (read.error != 0) return fail(read.error);
            return HostCompletion::ok(bytes_value(std::move(read.bytes)));
        };
        return operation;
    });

    // Process exit: not an external capability, so always allowed.
    add(signature("neura.process.exit", {int32()}, void_type()), [](const HostRequest& request) {
        const auto status = as_int32(request.arguments.at(0));
        if (status < 0 || status > 255) return fail(host_error::invalid_input);
        return HostCompletion::terminate(status);
    });

    add(signature("neura.env.get", {text()}, tuple({boolean(), text()})), [this](const HostRequest& request) {
        if (!capabilities_.env) return denied();
        const auto& name = request.arguments.at(0).bytes;
        if (!usable_text(name) || name.find('=') != std::string::npos) return fail(host_error::invalid_input);
        const char* value = std::getenv(name.c_str());
        if (value == nullptr) return HostCompletion::ok(tuple_value({scalar(0), bytes_value({})}));
        std::string text(value);
        if (!nvr::valid_utf8(text)) return fail(host_error::unsupported);
        return HostCompletion::ok(tuple_value({scalar(1), bytes_value(std::move(text))}));
    });

    add(signature("neura.clock.wall", {}, tuple({int64(), int32()})), [this](const HostRequest&) {
        if (!capabilities_.clock) return denied();
        // Under virtual time the wall clock starts at 2000-01-01T00:00:00Z.
        constexpr std::int64_t virtual_epoch = 946'684'800LL * 1'000'000'000LL;
        const auto now = clock_ ? virtual_epoch + clock_->now()
                                : std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
        auto seconds = now / 1'000'000'000;
        auto nanos = now % 1'000'000'000;
        if (nanos < 0) {
            nanos += 1'000'000'000;
            seconds -= 1;
        }
        return HostCompletion::ok(
            tuple_value({int64_value(seconds), int32_value(static_cast<std::int32_t>(nanos))}));
    });
    add(signature("neura.clock.monotonic", {}, int64()), [this](const HostRequest&) {
        if (!capabilities_.clock) return denied();
        const auto now = clock_ ? clock_->now()
                                : std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count();
        return HostCompletion::ok(int64_value(now));
    });
    const FunctionService::Handler sleep = [this](const HostRequest& request) {
        if (!capabilities_.clock) return denied();
        const auto nanos = as_int64(request.arguments.at(0));
        if (nanos < 0) return fail(host_error::invalid_input);
        if (clock_) {
            clock_->wait_until(deadline_after(clock_->now(), nanos));
        } else {
            std::this_thread::sleep_for(std::chrono::nanoseconds(nanos));
        }
        return HostCompletion::ok();
    };
    // Overlapping: a deadline on the task clock; no thread sleeps.
    add_overlapping(signature("neura.clock.sleep", {int64()}, void_type()), sleep,
                    [this](const HostRequest& request) -> std::variant<HostCompletion, PendingOperation> {
        if (!capabilities_.clock) return denied();
        const auto nanos = as_int64(request.arguments.at(0));
        if (nanos < 0) return fail(host_error::invalid_input);
        if (nanos == 0) return HostCompletion::ok();
        PendingOperation operation;
        operation.deadline = deadline_after(request.now, nanos);
        operation.attempt = [](bool cancelled) -> std::optional<HostCompletion> {
            if (cancelled) return cancelled_answer();
            return std::nullopt;
        };
        operation.expire = [] { return HostCompletion::ok(); };
        return operation;
    });

    add(signature("neura.entropy.fill", {int32()}, byte_list()), [this](const HostRequest& request) {
        if (!capabilities_.entropy) return denied();
        const auto count = as_int32(request.arguments.at(0));
        if (count < 0 || count > max_entropy_bytes) return fail(host_error::invalid_input);
        std::string bytes(static_cast<std::size_t>(count), '\0');
        std::size_t filled = 0;
#ifdef __linux__
        while (filled < bytes.size()) {
            const auto got = ::getrandom(bytes.data() + filled, bytes.size() - filled, 0);
            if (got < 0) {
                if (errno == EINTR) continue;
                break;
            }
            filled += static_cast<std::size_t>(got);
        }
#endif
        if (filled < bytes.size()) {
            std::random_device device;
            for (; filled < bytes.size(); ++filled) bytes[filled] = static_cast<char>(device() & 0xff);
        }
        return HostCompletion::ok(bytes_value(std::move(bytes)));
    });

    const Adder adder = [this](HostSignature signature, FunctionService::Handler handler) {
        add(std::move(signature), std::move(handler));
    };
    add_fs_services(adder, *tables_, capabilities_, prefixes_);
    const OverlapAdder overlap = [this](HostSignature signature, FunctionService::Handler handler,
                                        FunctionService::Begin begin) {
        add_overlapping(std::move(signature), std::move(handler), std::move(begin));
    };
    add_net_services(adder, overlap, *tables_, capabilities_);
    add_ffi_services(adder, *tables_, capabilities_);
}

void StandardServices::set_embedded_object(std::string object) {
    embedded_object_ = std::move(object);
    tables_->native.embedded_object = &embedded_object_;
}

StandardServices::~StandardServices() { end_run(); }

void StandardServices::add(HostSignature signature, FunctionService::Handler handler) {
    services_.push_back(std::make_unique<FunctionService>(std::move(signature), std::move(handler)));
}

void StandardServices::add_overlapping(HostSignature signature, FunctionService::Handler handler,
                                       FunctionService::Begin begin) {
    auto cancellable = signature;
    cancellable.version = 2;
    services_.push_back(std::make_unique<FunctionService>(std::move(signature), handler, begin, false));
    services_.push_back(
        std::make_unique<FunctionService>(std::move(cancellable), std::move(handler), std::move(begin), true));
}

void StandardServices::register_all(HostRegistry& registry) {
    for (auto& service : services_) registry.add(*service);
}

void StandardServices::end_run() {
    close_files(*tables_);
    close_sockets(*tables_);
    close_native(*tables_);
}

std::size_t StandardServices::open_files() const { return tables_->files.size(); }
std::size_t StandardServices::open_sockets() const { return tables_->sockets.size(); }

} // namespace neura::nvm
