// neura-package: `neura package --project <manifest> --target <name>
// [--format <f>] [-o <file>]` (docs/design/Projects030.md, "Commands"). Wraps
// the built bundle of a server target with neura-run, neura-server-host and
// the core library (which holds the providers) in a reproducible .tar.gz, and
// a desktop target's bundle in a .tar.gz, .deb or .zip through the desktop
// packager (scripts/package-desktop.py). On Windows (W31, W32) a server
// package is a reproducible .zip with neura-run.exe, neura-server-host.exe,
// the core DLL and run.cmd, and a desktop package is a .zip.

#include "app/project.hpp"
#include "cli/tool.hpp"
#include "project/files.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include "platform/environment.hpp"
#include "platform/file.hpp"
#include "platform/text.hpp"
#endif

namespace {

namespace fs = std::filesystem;
using namespace neura;
using namespace neura::project;

constexpr std::string_view usage =
    "usage: neura package [--project <manifest>] --target <name> [--format <format>] [--bundle <directory>]\n"
    "                     [-o <file>] [--sign]\n"
    "\n"
    "Wraps the target's built bundle (dist/<name> unless --bundle) for delivery. A server target gives a .tar.gz\n"
    "holding the bundle, neura-run, neura-server-host and the core library; `run` in it serves the bundle. A\n"
    "desktop target gives a .tar.gz, .deb or .zip through the desktop packager (--format, from the manifest's\n"
    "packaging.formats by default). Build the bundle first with `neura build --target <name>`.\n";

[[noreturn]] void cannot_package(const Span& site, std::string message) {
    throw Diagnostic(site, std::move(message)).with_code("E1033");
}

[[noreturn]] void dev_bundle(const Span& site, std::string message) {
    throw Diagnostic(site, std::move(message)).with_code("E1029");
}

// --- A reproducible .tar.gz: ustar entries in order, owned by 0:0, dated at
// SOURCE_DATE_EPOCH (default 0), in a gzip stream of stored deflate blocks
// with no name and no time.

std::uint32_t crc32(std::string_view data) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char byte : data) crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

#ifndef _WIN32
std::string octal(std::uint64_t value, std::size_t width) {
    std::string digits;
    do {
        digits.insert(digits.begin(), static_cast<char>('0' + (value & 7)));
        value >>= 3;
    } while (value);
    if (digits.size() + 1 > width) throw std::runtime_error("value too large for a tar header");
    return std::string(width - 1 - digits.size(), '0') + digits + '\0';
}

class Tar {
public:
    explicit Tar(std::uint64_t mtime) : mtime_(mtime) {}

    void directory(const std::string& path) { entry(path + "/", {}, 0755, '5'); }
    void file(const std::string& path, std::string_view data, bool executable) {
        entry(path, data, executable ? 0755 : 0644, '0');
    }
    std::string finish() {
        out_.append(1024, '\0');
        return out_;
    }

private:
    void entry(const std::string& path, std::string_view data, unsigned mode, char type) {
        std::string header(512, '\0');
        std::string name = path, prefix;
        if (name.size() > 100) {
            const auto split = name.rfind('/', name.size() - 2 > 155 ? 155 : name.size() - 2);
            if (split == std::string::npos || name.size() - split - 1 > 100 || split > 155)
                throw std::runtime_error("path too long for a tar entry: " + path);
            prefix = name.substr(0, split);
            name = name.substr(split + 1);
        }
        const auto put = [&](std::size_t at, std::string_view text) { std::memcpy(header.data() + at, text.data(), text.size()); };
        put(0, name);
        put(100, octal(mode, 8));
        put(108, octal(0, 8));
        put(116, octal(0, 8));
        put(124, octal(data.size(), 12));
        put(136, octal(mtime_, 12));
        put(148, "        ");
        header[156] = type;
        put(257, std::string_view("ustar\0", 6));
        put(263, "00");
        put(265, "root");
        put(297, "root");
        put(345, prefix);
        unsigned sum = 0;
        for (const unsigned char c : header) sum += c;
        put(148, octal(sum, 7));
        header[155] = ' ';
        out_ += header;
        out_ += data;
        out_.append((512 - data.size() % 512) % 512, '\0');
    }

    std::uint64_t mtime_;
    std::string out_;
};

std::string gzip_stored(std::string_view data) {
    std::string out("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff", 10);
    std::size_t at = 0;
    do {
        const auto size = std::min<std::size_t>(65535, data.size() - at);
        const bool last = at + size == data.size();
        out += static_cast<char>(last ? 1 : 0);
        out += static_cast<char>(size & 0xFF);
        out += static_cast<char>(size >> 8);
        out += static_cast<char>(~size & 0xFF);
        out += static_cast<char>((~size >> 8) & 0xFF);
        out.append(data.substr(at, size));
        at += size;
    } while (at < data.size());
    const auto crc = crc32(data);
    for (int i = 0; i < 4; ++i) out += static_cast<char>((crc >> (8 * i)) & 0xFF);
    const auto size = static_cast<std::uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) out += static_cast<char>((size >> (8 * i)) & 0xFF);
    return out;
}
#endif

std::uint64_t source_date_epoch() {
    if (const char* value = std::getenv("SOURCE_DATE_EPOCH"); value && *value) return std::strtoull(value, nullptr, 10);
    return 0;
}

#ifdef _WIN32
// --- A reproducible .zip: entries in order, stored (not compressed), dated
// at SOURCE_DATE_EPOCH in UTC as an MS-DOS time, which cannot be earlier than
// 1980-01-01 (where SOURCE_DATE_EPOCH 0 lands) or later than 2107, with UTF-8
// names and no extra fields, attributes or comments.

class Zip {
public:
    explicit Zip(std::uint64_t epoch) {
        constexpr std::uint64_t first = 315532800;  // 1980-01-01T00:00:00Z
        constexpr std::uint64_t last = 4354819198;  // 2107-12-31T23:59:58Z
        epoch = std::clamp(epoch, first, last);
        // Days since 1970-01-01 to a civil date (H. Hinnant's algorithm).
        const auto days = static_cast<std::int64_t>(epoch / 86400) + 719468;
        const auto era = days / 146097;
        const auto day_of_era = days - era * 146097;
        const auto year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
        const auto day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
        const auto shifted_month = (5 * day_of_year + 2) / 153;
        const auto day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
        const auto month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
        const auto year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
        const auto seconds = epoch % 86400;
        date_ = static_cast<std::uint16_t>(((year - 1980) << 9) | (month << 5) | day);
        time_ = static_cast<std::uint16_t>(((seconds / 3600) << 11) | (((seconds / 60) % 60) << 5) | ((seconds % 60) / 2));
    }

    void directory(const std::string& path) { entry(path + "/", {}, true); }
    void file(const std::string& path, std::string_view data) { entry(path, data, false); }

    std::string finish() {
        if (entries_ > 0xFFFF || out_.size() > 0xFFFFFFFFu || central_.size() > 0xFFFFFFFFu - out_.size())
            throw std::runtime_error("the package is too large for a .zip without ZIP64");
        const auto offset = out_.size();
        out_ += central_;
        put32(out_, 0x06054b50);
        put16(out_, 0);
        put16(out_, 0);
        put16(out_, entries_);
        put16(out_, entries_);
        put32(out_, central_.size());
        put32(out_, offset);
        put16(out_, 0);
        return out_;
    }

private:
    static void put16(std::string& out, std::uint64_t value) {
        for (int i = 0; i < 2; ++i) out += static_cast<char>((value >> (8 * i)) & 0xFF);
    }
    static void put32(std::string& out, std::uint64_t value) {
        for (int i = 0; i < 4; ++i) out += static_cast<char>((value >> (8 * i)) & 0xFF);
    }

    void entry(const std::string& path, std::string_view data, bool is_directory) {
        if (path.size() > 0xFFFF || data.size() > 0xFFFFFFFEu || out_.size() > 0xFFFFFFFEu)
            throw std::runtime_error("the package is too large for a .zip without ZIP64: " + path);
        const auto crc = crc32(data);
        const auto offset = out_.size();
        // Version 2.0 (MS-DOS attributes), bit 11: the name is UTF-8.
        const auto header = [&](std::string& out, bool central) {
            put32(out, central ? 0x02014b50 : 0x04034b50);
            if (central) put16(out, 20);
            put16(out, 20);
            put16(out, 0x0800);
            put16(out, 0);
            put16(out, time_);
            put16(out, date_);
            put32(out, crc);
            put32(out, data.size());
            put32(out, data.size());
            put16(out, path.size());
            put16(out, 0);
            if (central) {
                put16(out, 0);
                put16(out, 0);
                put16(out, 0);
                put32(out, is_directory ? 0x10 : 0);
                put32(out, offset);
            }
            out += path;
        };
        header(out_, false);
        out_ += data;
        header(central_, true);
        ++entries_;
    }

    std::uint16_t date_ = 0;
    std::uint16_t time_ = 0;
    std::uint64_t entries_ = 0;
    std::string out_;
    std::string central_;
};

// The core DLL the running tools load, beside them in bin/ (W04).
std::vector<fs::path> core_libraries() {
    std::vector<fs::path> found;
    std::error_code error;
    for (fs::directory_iterator it(app::tool_directory(), error), end; !error && it != end; it.increment(error)) {
        const auto name = project::detail::path_text(it->path().filename());
        if (it->is_regular_file(error) && name.starts_with("neura-core-") && name.ends_with(".dll"))
            found.push_back(it->path());
    }
    std::ranges::sort(found);
    return found;
}
#else
// The core library the running tools load: lib/neura beside a build-tree
// tool, ../lib/neura in an installation.
std::vector<fs::path> core_libraries() {
    std::vector<fs::path> found;
    for (const auto& directory : {app::tool_directory() / "lib/neura", app::install_prefix() / "lib/neura"}) {
        std::error_code error;
        if (!fs::is_directory(directory, error)) continue;
        for (const auto& entry : fs::directory_iterator(directory)) {
            const auto name = entry.path().filename().string();
            if (entry.is_regular_file() && name.starts_with("libneura-core")) found.push_back(entry.path());
        }
        if (!found.empty()) break;
    }
    std::ranges::sort(found);
    return found;
}
#endif

std::vector<fs::path> sorted_files(const fs::path& root) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::ranges::sort(files);
    return files;
}

#ifndef _WIN32
fs::path package_server(const app::Project& project, const Target& target, const fs::path& bundle,
                        const fs::path& output) {
    const auto& identity = project.manifest.project;
    const auto top = identity.name.value + "-" + identity.version.value;
    const auto run_tool = app::tool_directory() / "neura-run";
    const auto host = app::tool_directory() / "neura-server-host";
    const auto libraries = core_libraries();
    if (!fs::exists(run_tool) || !fs::exists(host) || libraries.empty()) {
        cannot_package(target.kind.span, "a server package needs neura-run, neura-server-host and the core library "
                                         "beside this tool; they are part of the core component");
    }
    std::vector<std::pair<std::string, fs::path>> entries;
    for (const auto& file : sorted_files(bundle))
        entries.emplace_back(top + "/bundle/" + detail::path_text(file.lexically_relative(bundle)), file);
    entries.emplace_back(top + "/bin/neura-run", run_tool);
    entries.emplace_back(top + "/bin/neura-server-host", host);
    for (const auto& library : libraries) entries.emplace_back(top + "/lib/neura/" + library.filename().string(), library);
    std::ranges::sort(entries);
    Tar tar(source_date_epoch());
    std::vector<std::string> directories;
    const auto ensure = [&](const std::string& path) {
        for (auto slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1)) {
            const auto parent = path.substr(0, slash);
            if (std::ranges::find(directories, parent) == directories.end()) {
                directories.push_back(parent);
                tar.directory(parent);
            }
        }
    };
    const std::string run_script =
        "#!/bin/sh\n"
        "# Serves the bundle of " + identity.name.value + " " + identity.version.value + " in the foreground.\n"
        "# Arguments go to neura-server-host: run --sql-pool main=sqlite:data.db --config key=value\n"
        "here=$(dirname \"$0\")\n"
        "exec \"$here/bin/neura-run\" run \"$here/bundle\" -- \"$@\"\n";
    ensure(top + "/run");
    for (const auto& [path, file] : entries) {
        ensure(path);
        const bool executable = path.starts_with(top + "/bin/") ||
                                (path.starts_with(top + "/bundle/bin/") && !path.ends_with(".json"));
        tar.file(path, app::read_file(file), executable);
    }
    tar.file(top + "/run", run_script, true);
    app::write_file(output, gzip_stored(tar.finish()));
    return output;
}
#else
// The Windows server package (W32): the same tree as the .tar.gz, with the
// executables and the core DLL together in bin/, where Windows finds a DLL,
// and run.cmd in place of the shell script.
fs::path package_server_zip(const app::Project& project, const Target& target, const fs::path& bundle,
                            const fs::path& output) {
    const auto& identity = project.manifest.project;
    const auto top = identity.name.value + "-" + identity.version.value;
    const auto run_tool = app::tool_directory() / tool::executable_file("neura-run");
    const auto host = app::tool_directory() / tool::executable_file("neura-server-host");
    const auto libraries = core_libraries();
    if (!fs::exists(run_tool) || !fs::exists(host) || libraries.empty()) {
        cannot_package(target.kind.span, "a server package needs neura-run.exe, neura-server-host.exe and the core "
                                         "DLL (neura-core-<version>.dll) beside this tool; they are part of the core "
                                         "component");
    }
    std::vector<std::pair<std::string, fs::path>> entries;
    for (const auto& file : sorted_files(bundle))
        entries.emplace_back(top + "/bundle/" + detail::path_text(file.lexically_relative(bundle)), file);
    entries.emplace_back(top + "/bin/" + tool::executable_file("neura-run"), run_tool);
    entries.emplace_back(top + "/bin/" + tool::executable_file("neura-server-host"), host);
    for (const auto& library : libraries) entries.emplace_back(top + "/bin/" + detail::path_text(library.filename()), library);
    std::ranges::sort(entries);
    Zip zip(source_date_epoch());
    std::vector<std::string> directories;
    const auto ensure = [&](const std::string& path) {
        for (auto slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1)) {
            const auto parent = path.substr(0, slash);
            if (std::ranges::find(directories, parent) == directories.end()) {
                directories.push_back(parent);
                zip.directory(parent);
            }
        }
    };
    // cmd.exe reads a batch file with CRLF line endings.
    const std::string run_script =
        "@echo off\r\n"
        "rem Serves the bundle of " + identity.name.value + " " + identity.version.value + " in the foreground.\r\n"
        "rem Arguments go to neura-server-host: run --sql-pool main=sqlite:data.db --config key=value\r\n"
        "\"%~dp0bin\\neura-run.exe\" run \"%~dp0bundle\" -- %*\r\n"
        "exit /b %ERRORLEVEL%\r\n";
    ensure(top + "/run.cmd");
    for (const auto& [path, file] : entries) {
        ensure(path);
        zip.file(path, app::read_file(file));
    }
    zip.file(top + "/run.cmd", run_script);
    app::write_file(output, zip.finish());
    return output;
}

// The Python that runs the desktop packager: python.exe on PATH (an app
// execution alias of the Microsoft Store, a reparse point, is not one), else
// the Python launcher as `py -3`.
std::optional<std::vector<std::string>> python_command() {
    std::error_code error;
    const auto path = platform::get_env("PATH", error);
    if (!path) return std::nullopt;
    const auto find = [&](std::string_view name) -> std::optional<fs::path> {
        for (std::string_view rest(*path); !rest.empty();) {
            const auto semicolon = rest.find(';');
            auto entry = rest.substr(0, semicolon);
            if (entry.size() >= 2 && entry.front() == '"' && entry.back() == '"') entry = entry.substr(1, entry.size() - 2);
            if (!entry.empty() && platform::is_valid_utf8(entry)) {
                const auto candidate = platform::path_from_utf8(entry) / name;
                std::error_code status;
                if (platform::link_kind(candidate, status) == platform::LinkKind::none && !status &&
                    fs::is_regular_file(candidate, status))
                    return candidate;
            }
            if (semicolon == std::string_view::npos) break;
            rest.remove_prefix(semicolon + 1);
        }
        return std::nullopt;
    };
    if (const auto python = find("python.exe")) return std::vector<std::string>{app::argument_path(*python)};
    if (const auto launcher = find("py.exe")) return std::vector<std::string>{app::argument_path(*launcher), "-3"};
    return std::nullopt;
}
#endif

// E1033 for a package format that this system does not build.
[[noreturn]] void foreign_format(const Span& site, const std::string& format) {
#ifdef _WIN32
    if (format == "msix") {
        cannot_package(site, ".msix packages are not built in Neura 0.3.0: an MSIX cannot be installed unsigned, and "
                             "this SDK signs no package; this system builds zip");
    }
    if (format == "dmg") cannot_package(site, ".dmg packages are built on macOS; this system builds zip");
    cannot_package(site, "a desktop bundle built on Windows holds a Windows child, whose package is a .zip; --format " +
                             format + " is not one");
#else
    cannot_package(site, "." + format + " packages are built on " +
                             (format == "dmg" ? std::string("macOS") : std::string("Windows")) +
                             "; this system builds tar.gz, deb and zip");
#endif
}

// Whether the desktop packager of this system builds `format`.
bool foreign(const std::string& format) {
#ifdef _WIN32
    return format != "zip";
#else
    return format == "dmg" || format == "msix";
#endif
}

std::optional<fs::path> desktop_packager() {
    for (const auto& candidate : {app::install_prefix() / "share/neura/scripts/package-desktop.py",
                                  app::install_prefix() / "scripts/package-desktop.py"}) {
        if (fs::exists(candidate)) return candidate;
    }
    return std::nullopt;
}

fs::path package_desktop(const app::Project& project, const Target& target, const std::string& name,
                         const fs::path& bundle, const std::string& format, const fs::path& output, bool test_hooks) {
    const auto& settings = std::get<DesktopSettings>(target.settings);
    if (foreign(format)) foreign_format(target.kind.span, format);
    const auto& paths = project.resolved.targets.at(name);
    if (!paths.icon) {
        cannot_package(target.kind.span, "a desktop package needs [targets." + name +
                                             ".packaging] icon, a 1,024-pixel PNG");
    }
#ifdef _WIN32
    std::error_code error;
    const auto engine_text = platform::get_env("NEURA_DESKTOP_ENGINE", error);
    const auto engine = engine_text && !engine_text->empty() && platform::is_valid_utf8(*engine_text)
                            ? platform::path_from_utf8(*engine_text)
                            : fs::path();
    if (engine.empty() || !fs::exists(engine, error)) {
        cannot_package(target.kind.span, "a desktop package needs the pinned Electron 45.3.3 engine: set "
                                         "NEURA_DESKTOP_ENGINE to its executable (electron.exe)");
    }
    const auto script = desktop_packager();
    if (!script) cannot_package(target.kind.span, "the desktop packager (package-desktop.py) is not installed");
    (void)settings;
    auto command = python_command();
    if (!command) {
        cannot_package(target.kind.span, "the desktop packager needs Python 3: neither python.exe on PATH nor the "
                                         "Python launcher (py.exe) was found");
    }
    for (auto argument : {app::argument_path(*script), std::string("--engine"), app::argument_path(engine.parent_path()),
                          std::string("--package"), app::argument_path(bundle), std::string("--name"),
                          project.manifest.project.name.value, std::string("--icon"), app::argument_path(paths.icon->value),
                          std::string("--format"), format, std::string("-o"), app::argument_path(output)})
        command->push_back(std::move(argument));
    if (test_hooks) {
        command->emplace_back("--test-hooks");
        command->push_back(app::argument_path(bundle.parent_path() / "test-dialogs.json"));
    }
    if (const int status = app::spawn_and_wait(*command); status != 0) {
        cannot_package(target.kind.span, "the desktop packager failed (status " + std::to_string(status) + ")");
    }
    return output;
#else
    const char* engine = std::getenv("NEURA_DESKTOP_ENGINE");
    if (!engine || !*engine || !fs::exists(engine)) {
        cannot_package(target.kind.span, "a desktop package needs the pinned Electron 45.3.3 engine: set "
                                         "NEURA_DESKTOP_ENGINE to its executable");
    }
    const auto script = desktop_packager();
    if (!script) cannot_package(target.kind.span, "the desktop packager (package-desktop.py) is not installed");
    (void)settings;
    std::vector<std::string> command{"/usr/bin/env", "python3", script->string(), "--engine",
                                     fs::path(engine).parent_path().string(), "--package", bundle.string(), "--name",
                                     project.manifest.project.name.value, "--icon", paths.icon->value.string(),
                                     "--format", format, "-o", output.string()};
    if (test_hooks) {
        command.emplace_back("--test-hooks");
        command.push_back((bundle.parent_path() / "test-dialogs.json").string());
    }
    if (const int status = app::spawn_and_wait(command); status != 0) {
        cannot_package(target.kind.span, "the desktop packager failed (status " + std::to_string(status) + ")");
    }
    return output;
#endif
}

int package(const std::vector<std::string>& arguments) {
    std::optional<std::string> project, target_name, format, bundle_option, output;
    bool sign = false;
    for (std::size_t i = 2; i < arguments.size(); ++i) {
        const auto& arg = arguments[i];
        const auto value = [&]() -> std::optional<std::string> {
            if (i + 1 >= arguments.size()) {
                std::cerr << "neura package: " << arg << " needs a value\n";
                return std::nullopt;
            }
            return arguments[++i];
        };
        std::optional<std::string>* slot = nullptr;
        if (arg == "--help" || arg == "-h") {
            std::cout << usage;
            return 0;
        } else if (arg == "--project") slot = &project;
        else if (arg == "--target") slot = &target_name;
        else if (arg == "--format") slot = &format;
        else if (arg == "--bundle") slot = &bundle_option;
        else if (arg == "-o" || arg == "--output") slot = &output;
        else if (arg == "--sign") sign = true;
        else if (arg == "--report") {
            std::cerr << "neura package: --report is not implemented in Neura 0.3.0\n";
            return 1;
        } else {
            std::cerr << "neura package: unexpected argument " << arg << '\n' << usage;
            return 1;
        }
        if (slot) {
            *slot = value();
            if (!*slot) return 1;
        }
    }
    if (!target_name) {
        std::cerr << usage;
        return 1;
    }
    const auto manifest = app::find_manifest(project, std::cerr);
    if (!manifest) return 1;
    try {
        auto loaded = app::load_project(*manifest);
        app::check_target(loaded, *target_name);
        app::check_lock(loaded, false);
        const auto& target = loaded.manifest.targets.at(*target_name);
        if (sign) {
#ifdef _WIN32
            cannot_package(target.kind.span, "--sign runs the signing tools of the target system; this SDK signs no "
                                             "package on Windows in Neura 0.3.0");
#else
            cannot_package(target.kind.span, "--sign runs the signing tools of the target system; this SDK signs no "
                                             "package on Linux in Neura 0.3.0");
#endif
        }
        if (target.kind.value == TargetKind::Web) {
            cannot_package(target.kind.span, "a web bundle is delivered as its directory: copy dist/" + *target_name +
                                                 " to a static file host");
        }
        if (format && (*format == "dmg" || *format == "msix" ||
                       (target.kind.value == TargetKind::Desktop && foreign(*format))))
            foreign_format(target.kind.span, *format);
#ifdef _WIN32
        if (target.kind.value == TargetKind::Desktop) cannot_package(target.kind.span,
            "the desktop host is unavailable on Windows in Neura 0.3.0");
#endif
        const auto bundle = bundle_option ? fs::absolute(*bundle_option) : loaded.resolved.root / "dist" / *target_name;
        if (!app::is_bundle_directory(bundle)) {
            cannot_package(target.kind.span, "no built bundle in " + app::display_path(bundle) + "; build it with `neura build "
                                             "--target " + *target_name + "`");
        }
        // Checked as a host would, before anything is copied; a `neura dev`
        // build is refused (case 38).
        BundleVerification verification;
        verification.supported_requirements = app::supported_requirements();
        verification.allow_dev = true;
        const auto verified = verify_bundle_directory(bundle, verification);
        if (verified.manifest().dev.value_or(false)) {
            dev_bundle(Span{detail::path_text(bundle / "bundle.json")},
                       "the bundle was built by `neura dev`; build it with `neura build --target " + *target_name +
                           "` to package it");
        }
        if (verified.manifest().id != loaded.manifest.project.id.value ||
            verified.manifest().kind != target.kind.value) {
            cannot_package(target.kind.span, "the bundle in " + app::display_path(bundle) + " is not this target's");
        }
        const auto& identity = loaded.manifest.project;
        fs::path result;
        if (target.kind.value == TargetKind::Server) {
#ifdef _WIN32
            if (format && *format != "zip") {
                cannot_package(target.kind.span, "a server package on Windows is a .zip; --format " + *format +
                                                     " is not one");
            }
            const auto out = output ? fs::absolute(*output)
                                    : loaded.resolved.root / "dist" / (identity.name.value + "-" + identity.version.value +
                                                                       "-" + *target_name + ".zip");
            result = package_server_zip(loaded, target, bundle, out);
#else
            if (format && *format != "tar.gz") {
                cannot_package(target.kind.span, "a server package is a .tar.gz; --format " + *format + " is not one");
            }
            const auto out = output ? fs::absolute(*output)
                                    : loaded.resolved.root / "dist" / (identity.name.value + "-" + identity.version.value +
                                                                       "-" + *target_name + ".tar.gz");
            result = package_server(loaded, target, bundle, out);
#endif
        } else {
#ifdef _WIN32
            // Windows builds one desktop format, whatever the manifest lists
            // for the other systems (W31).
            std::string chosen = format ? *format : "zip";
#else
            const auto& settings = std::get<DesktopSettings>(target.settings);
            std::string chosen = format ? *format : settings.packaging.formats.empty() ? "tar.gz"
                                                                                      : settings.packaging.formats.front().value;
#endif
            const auto out = output ? fs::absolute(*output)
                                    : loaded.resolved.root / "dist" / (identity.name.value + "-" + identity.version.value +
                                                                       "-" + *target_name + "." + chosen);
            result = package_desktop(loaded, target, *target_name, bundle, chosen, out, false);
        }
        std::cout << app::display_path(result) << ": package " << app::sha256_of(app::read_file(result)).hex()
                  << " of bundle " << verified.digest().hex() << " (unsigned)\n";
        return 0;
    } catch (const Diagnostic& diagnostic) {
        app::print_diagnostic(std::cerr, diagnostic);
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-package"), argc, argv);
    if (start.status) return *start.status;
    try {
        return package(start.arguments);
    } catch (const std::exception& error) {
        std::cerr << "neura package: " << error.what() << '\n';
        return 1;
    }
}
