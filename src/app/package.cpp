// neura-package: `neura package --project <manifest> --target <name>
// [--format <f>] [-o <file>]` (docs/design/Projects030.md, "Commands"). Wraps
// the built bundle of a server target with neura-run, neura-server-host and
// the core library (which holds the providers) in a reproducible .tar.gz, and
// a desktop target's bundle in a .tar.gz, .deb or .zip through the desktop
// packager (scripts/package-desktop.py).

#include "app/project.hpp"
#include "cli/tool.hpp"
#include "project/files.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>

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

std::uint64_t source_date_epoch() {
    if (const char* value = std::getenv("SOURCE_DATE_EPOCH"); value && *value) return std::strtoull(value, nullptr, 10);
    return 0;
}

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

std::vector<fs::path> sorted_files(const fs::path& root) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::ranges::sort(files);
    return files;
}

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
    if (format == "dmg" || format == "msix") {
        cannot_package(target.kind.span, "." + format + " packages are built on " +
                                             (format == "dmg" ? std::string("macOS") : std::string("Windows")) +
                                             "; this system builds tar.gz, deb and zip");
    }
    const auto& paths = project.resolved.targets.at(name);
    if (!paths.icon) {
        cannot_package(target.kind.span, "a desktop package needs [targets." + name +
                                             ".packaging] icon, a 1,024-pixel PNG");
    }
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
            cannot_package(target.kind.span, "--sign runs the signing tools of the target system; this SDK signs no "
                                             "package on Linux in Neura 0.3.0");
        }
        if (target.kind.value == TargetKind::Web) {
            cannot_package(target.kind.span, "a web bundle is delivered as its directory: copy dist/" + *target_name +
                                                 " to a static file host");
        }
        if (format && (*format == "dmg" || *format == "msix")) {
            cannot_package(target.kind.span, "." + *format + " packages are built on " +
                                                 (*format == "dmg" ? std::string("macOS") : std::string("Windows")) +
                                                 "; this system builds tar.gz, deb and zip");
        }
        const auto bundle = bundle_option ? fs::absolute(*bundle_option) : loaded.resolved.root / "dist" / *target_name;
        if (!app::is_bundle_directory(bundle)) {
            cannot_package(target.kind.span, "no built bundle in " + bundle.string() + "; build it with `neura build "
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
            cannot_package(target.kind.span, "the bundle in " + bundle.string() + " is not this target's");
        }
        const auto& identity = loaded.manifest.project;
        fs::path result;
        if (target.kind.value == TargetKind::Server) {
            if (format && *format != "tar.gz") {
                cannot_package(target.kind.span, "a server package is a .tar.gz; --format " + *format + " is not one");
            }
            const auto out = output ? fs::absolute(*output)
                                    : loaded.resolved.root / "dist" / (identity.name.value + "-" + identity.version.value +
                                                                       "-" + *target_name + ".tar.gz");
            result = package_server(loaded, target, bundle, out);
        } else {
            const auto& settings = std::get<DesktopSettings>(target.settings);
            std::string chosen = format ? *format : settings.packaging.formats.empty() ? "tar.gz"
                                                                                      : settings.packaging.formats.front().value;
            const auto out = output ? fs::absolute(*output)
                                    : loaded.resolved.root / "dist" / (identity.name.value + "-" + identity.version.value +
                                                                       "-" + *target_name + "." + chosen);
            result = package_desktop(loaded, target, *target_name, bundle, chosen, out, false);
        }
        std::cout << result.string() << ": package " << app::sha256_of(app::read_file(result)).hex()
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
