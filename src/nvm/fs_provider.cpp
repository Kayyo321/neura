#include "provider_internal.hpp"

#include "nvr/format.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace neura::nvm::provider {

namespace {

// OpenMode ordinals in Std.Fs.File, and SeekOrigin ordinals.
enum OpenMode : std::int32_t { Read = 0, Write = 1, Append = 2, ReadWrite = 3, CreateNew = 4 };

// FileKind ordinals in Std.Fs.Fs.
enum FileKind : std::int32_t { RegularFile = 0, Directory = 1, OtherKind = 2 };

// Checks a path argument against the capability and returns the path to
// hand to the operating system, or an error code.
struct Checked {
    std::string path;
    std::uint32_t error = 0;
};

Checked check_path(const Capabilities& capabilities, const std::vector<ResolvedPath>& prefixes,
                   const std::string& path, bool follow_last) {
    if (!capabilities.fs) return {{}, host_error::capability_denied};
    if (!usable_text(path)) return {{}, host_error::invalid_input};
    if (prefixes.empty()) return {path, 0};
    const auto resolved = resolve_path(path, follow_last);
    if (resolved.error != 0) return {{}, resolved.error};
    for (const auto& prefix : prefixes) {
        if (resolved.within(prefix)) return {resolved.text(), 0};
    }
    return {{}, host_error::capability_denied};
}

} // namespace

#ifdef _WIN32

void add_fs_services(const Adder& add, StandardServices::Tables&, const Capabilities& capabilities,
                     const std::vector<ResolvedPath>&) {
    const auto unsupported = [&capabilities](const HostRequest&) {
        return capabilities.fs ? fail(host_error::unsupported) : denied();
    };
    add(signature("neura.fs.open", {text(), int32()}, int64()), unsupported);
    add(signature("neura.fs.read", {int64(), int32()}, byte_list()), unsupported);
    add(signature("neura.fs.write", {int64(), byte_list()}, int32()), unsupported);
    add(signature("neura.fs.seek", {int64(), int64(), int32()}, int64()), unsupported);
    add(signature("neura.fs.close", {int64()}, void_type()), unsupported);
    add(signature("neura.fs.stat", {text(), boolean()}, tuple({int32(), int64(), int64(), int32()})), unsupported);
    add(signature("neura.fs.list", {text()}, text_list()), unsupported);
    add(signature("neura.fs.make_directory", {text()}, void_type()), unsupported);
    add(signature("neura.fs.remove", {text(), boolean()}, void_type()), unsupported);
    add(signature("neura.fs.rename", {text(), text()}, void_type()), unsupported);
}

void close_files(StandardServices::Tables& tables) {
    tables.files.clear();
    tables.next_file = 1;
}

#else

namespace {

int retry_open(const char* path, int flags) {
    for (;;) {
        const int descriptor = ::open(path, flags | O_CLOEXEC, 0666);
        if (descriptor >= 0 || errno != EINTR) return descriptor;
    }
}

HostCompletion errno_failure() { return fail(host_error::from_errno(errno)); }

} // namespace

void add_fs_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities,
                     const std::vector<ResolvedPath>& prefixes) {
    auto* files = &tables;
    const auto* granted = &capabilities;
    const auto* allowed = &prefixes;

    add(signature("neura.fs.open", {text(), int32()}, int64()), [=](const HostRequest& request) {
        const auto mode = as_int32(request.arguments.at(1));
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, mode != CreateNew);
        if (checked.error != 0) return fail(checked.error);
        int flags = 0;
        bool readable = false;
        bool writable = false;
        switch (mode) {
        case Read:
            flags = O_RDONLY;
            readable = true;
            break;
        case Write:
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            writable = true;
            break;
        case Append:
            flags = O_WRONLY | O_CREAT | O_APPEND;
            writable = true;
            break;
        case ReadWrite:
            flags = O_RDWR;
            readable = writable = true;
            break;
        case CreateNew:
            flags = O_RDWR | O_CREAT | O_EXCL;
            readable = writable = true;
            break;
        default:
            return fail(host_error::invalid_input);
        }
        if (files->files.size() >= max_open_ids) return fail(host_error::other);
        const int descriptor = retry_open(checked.path.c_str(), flags);
        if (descriptor < 0) return errno_failure();
        struct stat info {};
        if (::fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode)) {
            ::close(descriptor);
            return fail(host_error::is_a_directory);
        }
        const auto id = files->next_file++;
        files->files[id] = {descriptor, readable, writable};
        return HostCompletion::ok(int64_value(id));
    });

    add(signature("neura.fs.read", {int64(), int32()}, byte_list()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        const auto max = as_int32(request.arguments.at(1));
        if (found == files->files.end() || max < 1) return fail(host_error::invalid_input);
        if (!found->second.readable) return fail(host_error::permission_denied);
        std::string buffer(std::min(static_cast<std::size_t>(max), max_read_bytes), '\0');
        for (;;) {
            const auto count = ::read(found->second.descriptor, buffer.data(), buffer.size());
            if (count >= 0) {
                buffer.resize(static_cast<std::size_t>(count));
                return HostCompletion::ok(bytes_value(std::move(buffer)));
            }
            if (errno != EINTR) return errno_failure();
        }
    });

    add(signature("neura.fs.write", {int64(), byte_list()}, int32()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        const auto& bytes = request.arguments.at(1).bytes;
        if (found == files->files.end()) return fail(host_error::invalid_input);
        if (!found->second.writable) return fail(host_error::permission_denied);
        if (bytes.empty()) return HostCompletion::ok(int32_value(0));
        const auto size = std::min<std::size_t>(bytes.size(), INT32_MAX);
        for (;;) {
            const auto count = ::write(found->second.descriptor, bytes.data(), size);
            if (count >= 0) return HostCompletion::ok(int32_value(static_cast<std::int32_t>(count)));
            if (errno != EINTR) return errno_failure();
        }
    });

    add(signature("neura.fs.seek", {int64(), int64(), int32()}, int64()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        const auto offset = as_int64(request.arguments.at(1));
        const auto origin = as_int32(request.arguments.at(2));
        if (found == files->files.end() || origin < 0 || origin > 2) return fail(host_error::invalid_input);
        const int whence = origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END;
        const auto position = ::lseek(found->second.descriptor, static_cast<off_t>(offset), whence);
        if (position < 0) return errno_failure();
        return HostCompletion::ok(int64_value(static_cast<std::int64_t>(position)));
    });

    add(signature("neura.fs.close", {int64()}, void_type()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        if (found == files->files.end()) return fail(host_error::invalid_input);
        const int descriptor = found->second.descriptor;
        // The ID is released even when close reports an error.
        files->files.erase(found);
        if (::close(descriptor) != 0 && errno != EINTR) return errno_failure();
        return HostCompletion::ok();
    });

    add(signature("neura.fs.stat", {text(), boolean()}, tuple({int32(), int64(), int64(), int32()})),
        [=](const HostRequest& request) {
            const bool follow = as_bool(request.arguments.at(1));
            const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, follow);
            if (checked.error != 0) return fail(checked.error);
            struct stat info {};
            const int status = follow ? ::stat(checked.path.c_str(), &info) : ::lstat(checked.path.c_str(), &info);
            if (status != 0) return errno_failure();
            const auto kind = S_ISREG(info.st_mode) ? RegularFile : S_ISDIR(info.st_mode) ? Directory : OtherKind;
#ifdef __APPLE__
            const auto modified = info.st_mtimespec;
#else
            const auto modified = info.st_mtim;
#endif
            return HostCompletion::ok(tuple_value({int32_value(kind), int64_value(static_cast<std::int64_t>(info.st_size)),
                                                   int64_value(static_cast<std::int64_t>(modified.tv_sec)),
                                                   int32_value(static_cast<std::int32_t>(modified.tv_nsec))}));
        });

    add(signature("neura.fs.list", {text()}, text_list()), [=](const HostRequest& request) {
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, true);
        if (checked.error != 0) return fail(checked.error);
        DIR* directory = ::opendir(checked.path.c_str());
        if (directory == nullptr) return errno_failure();
        std::vector<std::string> names;
        bool invalid = false;
        for (;;) {
            errno = 0;
            const auto* entry = ::readdir(directory);
            if (entry == nullptr) break;
            const std::string name(entry->d_name);
            if (name == "." || name == "..") continue;
            if (!nvr::valid_utf8(name)) invalid = true;
            names.push_back(name);
        }
        const int error = errno;
        ::closedir(directory);
        if (error != 0) return fail(host_error::from_errno(error));
        // A name that is not UTF-8 cannot become a Neura string.
        if (invalid) return fail(host_error::unsupported);
        // std::string compares as unsigned bytes, which is UTF-8 byte order.
        std::sort(names.begin(), names.end());
        HostValue list;
        for (auto& name : names) list.elements.push_back(bytes_value(std::move(name)));
        return HostCompletion::ok(std::move(list));
    });

    add(signature("neura.fs.make_directory", {text()}, void_type()), [=](const HostRequest& request) {
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, false);
        if (checked.error != 0) return fail(checked.error);
        if (::mkdir(checked.path.c_str(), 0777) != 0) return errno_failure();
        return HostCompletion::ok();
    });

    add(signature("neura.fs.remove", {text(), boolean()}, void_type()), [=](const HostRequest& request) {
        const bool directory = as_bool(request.arguments.at(1));
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, false);
        if (checked.error != 0) return fail(checked.error);
        if (directory) {
            if (::rmdir(checked.path.c_str()) == 0) return HostCompletion::ok();
            // Some systems report a non-empty directory as EEXIST.
            return fail(errno == EEXIST ? host_error::directory_not_empty : host_error::from_errno(errno));
        }
        if (::unlink(checked.path.c_str()) == 0) return HostCompletion::ok();
        const int error = errno;
        struct stat info {};
        if (error == EPERM && ::lstat(checked.path.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
            return fail(host_error::is_a_directory);
        }
        return fail(host_error::from_errno(error));
    });

    add(signature("neura.fs.rename", {text(), text()}, void_type()), [=](const HostRequest& request) {
        const auto from = check_path(*granted, *allowed, request.arguments.at(0).bytes, false);
        if (from.error != 0) return fail(from.error);
        const auto to = check_path(*granted, *allowed, request.arguments.at(1).bytes, false);
        if (to.error != 0) return fail(to.error);
        if (::rename(from.path.c_str(), to.path.c_str()) == 0) return HostCompletion::ok();
        return fail(errno == EEXIST ? host_error::directory_not_empty : host_error::from_errno(errno));
    });
}

void close_files(StandardServices::Tables& tables) {
    for (const auto& [id, file] : tables.files) ::close(file.descriptor);
    tables.files.clear();
    tables.next_file = 1;
}

#endif

} // namespace neura::nvm::provider
