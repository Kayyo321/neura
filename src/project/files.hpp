#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace neura::project::detail {
namespace fs = std::filesystem;

inline std::string path_text(const fs::path& path) {
    const auto utf8 = path.generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

inline fs::path path_from_text(std::string_view value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}

struct ReadFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#ifndef _WIN32

class FileDescriptor {
public:
    explicit FileDescriptor(int value) : value_(value) {}
    ~FileDescriptor() { if (value_ >= 0) ::close(value_); }
    FileDescriptor(FileDescriptor&& other) noexcept : value_(other.release()) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        std::swap(value_, other.value_);
        return *this;
    }
    int get() const { return value_; }
    int release() { return std::exchange(value_, -1); }
private:
    int value_;
};

constexpr int directory_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;

inline FileDescriptor subdirectory(FileDescriptor current, std::string_view path) {
    while (!path.empty()) {
        const auto end = path.find('/');
        const auto name = std::string(path.substr(0, end));
        FileDescriptor next(::openat(current.get(), name.c_str(), directory_flags));
        if (next.get() < 0) throw ReadFailure("cannot open directory '" + name + "' without following links");
        current = std::move(next);
        if (end == std::string_view::npos) break;
        path.remove_prefix(end + 1);
    }
    return current;
}

inline FileDescriptor open_root(const fs::path& path) {
    FileDescriptor root(::open(path.root_path().c_str(), directory_flags));
    if (root.get() < 0) throw ReadFailure("cannot open the filesystem root");
    return subdirectory(std::move(root), path_text(path.relative_path()));
}

inline bool unchanged(const struct stat& before, const struct stat& after) {
#ifdef __APPLE__
    const auto before_mtime = before.st_mtimespec, after_mtime = after.st_mtimespec;
    const auto before_ctime = before.st_ctimespec, after_ctime = after.st_ctimespec;
#else
    const auto before_mtime = before.st_mtim, after_mtime = after.st_mtim;
    const auto before_ctime = before.st_ctim, after_ctime = after.st_ctim;
#endif
    return before.st_size == after.st_size &&
           before_mtime.tv_sec == after_mtime.tv_sec && before_mtime.tv_nsec == after_mtime.tv_nsec &&
           before_ctime.tv_sec == after_ctime.tv_sec && before_ctime.tv_nsec == after_ctime.tv_nsec;
}

#endif

} // namespace neura::project::detail
