#include "tool.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <system_error>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif

namespace neura::tool {

namespace fs = std::filesystem;

namespace {

int lease = -1;

void tool_help(const Tool& tool, std::ostream& out) {
    out << tool.name << ": the direct form of";
    bool first = true;
    for (const auto& command : commands()) {
        if (command.tool != tool.name || command.command.starts_with("-")) continue;
        out << (first ? " " : ", ") << "neura " << command.command;
        first = false;
    }
    out << " (" << component_name(tool.component) << ")\n";
    for (std::size_t i = 0; i < tool.words.size(); ++i) {
        out << (i == 0 ? "usage: " : "       ") << tool.name << ' ' << (i == 0 ? "[" : "") << tool.words[i]
            << (i == 0 ? "]" : "") << " ...\n";
    }
    out << "The first argument selects one of these commands; without one, the tool runs "
        << tool.words.front() << ".\n"
           "`neura help` lists the options of every command, which mean the same here.\n";
}

} // namespace

void usage(std::ostream& out) {
    out << usage_text();
}

fs::path executable_path(const fs::path& launch) {
    std::error_code error;
#ifdef __linux__
    if (const auto actual = fs::canonical("/proc/self/exe", error); !actual.empty()) return actual;
#endif
#ifdef __APPLE__
    {
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            buffer.resize(std::strlen(buffer.c_str()));
            if (const auto actual = fs::canonical(buffer, error); !actual.empty()) return actual;
        }
    }
#endif
    if (launch.has_parent_path()) return fs::canonical(launch, error);
    const auto* path = std::getenv("PATH");
    if (!path) return {};
#ifdef _WIN32
    constexpr char separator = ';';
    auto file = launch;
    if (!file.has_extension()) file += ".exe";
#else
    constexpr char separator = ':';
    const auto& file = launch;
#endif
    std::string_view remaining(path);
    for (;;) {
        const auto end = remaining.find(separator);
        const auto candidate = fs::path(remaining.substr(0, end)) / file;
        if (fs::is_regular_file(candidate, error)) return fs::canonical(candidate, error);
        if (end == std::string_view::npos) return {};
        remaining.remove_prefix(end + 1);
    }
}

Start start(const Tool& tool, int argc, char** argv) {
    Start result;
    // The lease comes first: before the tool opens any other file of its
    // generation (docs/design/Lifecycle030.md).
#ifdef __linux__
    {
        char self[4096];
        const auto length = ::readlink("/proc/self/exe", self, sizeof self - 1);
        if (length > 0) {
            self[length] = '\0';
            bool removed = false;
            lease = take_lease(self, removed);
            if (removed) {
                std::cerr << removed_generation_message;
                result.status = 1;
                return result;
            }
        }
    }
#elif !defined(_WIN32)
    {
        const auto self = executable_path(argc > 0 ? argv[0] : "");
        bool removed = false;
        lease = take_lease(self.c_str(), removed);
        if (removed) {
            std::cerr << removed_generation_message;
            result.status = 1;
            return result;
        }
    }
#endif
    int first = 1;
    if (argc > 1 && std::string_view(argv[1]).starts_with(dispatch_prefix)) {
        const auto claimed = std::string_view(argv[1]).substr(dispatch_prefix.size());
        const auto own = version_line();
        if (claimed != own) {
            const auto self = executable_path(argv[0]);
            std::cerr << "neura: error: " << (self.empty() ? std::string(tool.name) : self.string()) << " is from "
                      << own << ", but the neura that started it is " << claimed << "; every tool in "
                      << (self.empty() ? std::string("its directory") : self.parent_path().string())
                      << " must come from one build. Reinstall Neura\n";
            result.status = 1;
            return result;
        }
        first = 2;
    } else if (argc == 2 && std::string_view(argv[1]) == "--help") {
        tool_help(tool, std::cout);
        result.status = 0;
        return result;
    } else if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << version_line() << '\n';
        result.status = 0;
        return result;
    }
    result.arguments.emplace_back(argc > 0 ? argv[0] : tool.name);
    bool named = false;
    if (first < argc) {
        for (const auto& word : tool.words) named = named || word == argv[first];
    }
    if (!named) result.arguments.emplace_back(tool.words.front());
    for (int i = first; i < argc; ++i) result.arguments.emplace_back(argv[i]);
    return result;
}

int lease_descriptor() { return lease; }

std::vector<char*> argv_of(std::vector<std::string>& arguments) {
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    return pointers;
}

} // namespace neura::tool
