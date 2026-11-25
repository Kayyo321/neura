#include "windows_c_import.hpp"

#include "options.hpp"
#include "platform/dirs.hpp"
#include "platform/environment.hpp"
#include "platform/process.hpp"
#include "platform/text.hpp"
#include "runner/runner.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace neura::cli {
namespace {

using Bytes = std::span<const std::uint8_t>;

std::uint32_t read_u16(Bytes bytes, std::size_t offset) {
    return bytes[offset] | (std::uint32_t{bytes[offset + 1]} << 8);
}

std::uint32_t read_u32(Bytes bytes, std::size_t offset) {
    return read_u16(bytes, offset) | (read_u16(bytes, offset + 2) << 16);
}

std::string_view text(Bytes bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Look at member contents. A .lib is an archive in both forms, and a long
// import record is itself a COFF object with import directory contributions.
enum class CoffInput { invalid, object, archive, dll_import };

CoffInput coff_object(Bytes bytes) {
    constexpr std::uint32_t x64 = 0x8664;
    constexpr std::size_t header_size = 20;
    constexpr std::size_t big_header_size = 56;
    constexpr std::size_t section_size = 40;
    constexpr std::array<std::uint8_t, 16> big_object_id{
        0xc7, 0xa1, 0xba, 0xd1, 0xee, 0xba, 0xa9, 0x4b, 0xaf, 0x20, 0xfa, 0xf6, 0x6a, 0xa4, 0xdc, 0xb8};

    if (bytes.size() < header_size) return CoffInput::invalid;
    std::size_t section_offset = header_size;
    std::size_t sections = read_u16(bytes, 2);
    if (read_u16(bytes, 0) == 0 && sections == 0xffff) {
        const auto version = read_u16(bytes, 4);
        if (version == 0) return CoffInput::dll_import;
        if (version < 2 || bytes.size() < big_header_size || read_u16(bytes, 6) != x64 ||
            !std::ranges::equal(bytes.subspan(12, big_object_id.size()), big_object_id))
            return CoffInput::invalid;
        section_offset = big_header_size;
        sections = read_u32(bytes, 44);
    } else if (read_u16(bytes, 0) != x64 || read_u16(bytes, 16) != 0) {
        return CoffInput::invalid;
    }
    if (sections > (bytes.size() - section_offset) / section_size) return CoffInput::invalid;
    for (std::size_t i = 0; i < sections; ++i) {
        if (text(bytes.subspan(section_offset + i * section_size, 8)).starts_with(".idata$"))
            return CoffInput::dll_import;
    }
    return CoffInput::object;
}

std::string_view trim_padding(std::string_view field) {
    return field.substr(0, field.find_last_not_of(' ') + 1);
}

CoffInput coff_input(Bytes bytes) {
    constexpr std::string_view archive_magic = "!<arch>\n";
    constexpr std::size_t member_header_size = 60;
    if (bytes.size() < archive_magic.size() || !text(bytes).starts_with(archive_magic)) return coff_object(bytes);
    bytes = bytes.subspan(archive_magic.size());
    while (!bytes.empty()) {
        if (bytes.size() < member_header_size) return CoffInput::invalid;
        const auto header = text(bytes.first(member_header_size));
        const auto size = trim_padding(header.substr(48, 10));
        std::size_t length = 0;
        const auto [end, error] = std::from_chars(size.data(), size.data() + size.size(), length);
        bytes = bytes.subspan(member_header_size);
        if (header.substr(58) != "\x60\n" || error != std::errc{} || end != size.data() + size.size() ||
            length > bytes.size() || length % 2 > bytes.size() - length)
            return CoffInput::invalid;
        const auto name = trim_padding(header.substr(0, 16));
        if (name != "/" && name != "//" && name != "/SYM64/") {
            const auto kind = coff_object(bytes.first(length));
            if (kind != CoffInput::object) return kind;
        }
        bytes = bytes.subspan(length + length % 2);
    }
    return CoffInput::archive;
}

class LinkDirectory {
public:
    LinkDirectory() {
        std::error_code error;
        const auto temp = platform::temp_directory(error);
        if (error) throw std::system_error(error, "cannot find the temporary directory for C imports");
        path = temp / platform::path_from_utf8(platform::unique_name("neura-link-"));
        if (!platform::make_private_directory(path, error))
            throw std::system_error(error, "cannot create the temporary directory for C imports");
    }

    ~LinkDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    LinkDirectory(const LinkDirectory&) = delete;
    LinkDirectory& operator=(const LinkDirectory&) = delete;

    fs::path path;
};

fs::path find_tool(std::string_view name) {
    auto file = platform::path_from_utf8(name);
    if (file.extension().empty()) file += ".exe";
    const auto regular = [](const fs::path& path) {
        std::error_code error;
        return fs::is_regular_file(path, error);
    };
    if (file.has_parent_path() || file.has_root_name()) return regular(file) ? fs::absolute(file) : fs::path{};

    const auto path = platform::get_env("PATH").value_or("");
    std::string_view directories(path);
    while (!directories.empty()) {
        const auto separator = directories.find(';');
        auto directory = directories.substr(0, separator);
        if (directory.size() >= 2 && directory.front() == '"' && directory.back() == '"')
            directory = directory.substr(1, directory.size() - 2);
        if (!directory.empty()) {
            const auto candidate = platform::path_from_utf8(directory) / file;
            if (regular(candidate)) return fs::absolute(candidate);
        }
        if (separator == std::string_view::npos) break;
        directories.remove_prefix(separator + 1);
    }
    return {};
}

fs::path compiler_path(const Options& options) {
    const auto requested = options.cc.empty() ? platform::get_env("CC").value_or("") : options.cc;
    if (!requested.empty()) {
        if (const auto found = find_tool(requested); !found.empty()) return found;
        throw std::runtime_error("cannot find C compiler '" + requested + "' for Windows C imports");
    }
    if (const auto found = find_tool("cl.exe"); !found.empty()) return found;
    if (const auto found = find_tool("clang-cl.exe"); !found.empty()) return found;
    throw std::runtime_error("Windows C imports need cl.exe or clang-cl.exe. Run from a Visual Studio developer prompt");
}

fs::path linker_path(const fs::path& compiler) {
    auto name = platform::utf8_path(compiler.stem());
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto names = name.starts_with("clang-cl") ? std::array{"lld-link.exe", "link.exe"}
                                                    : std::array{"link.exe", "lld-link.exe"};
    for (const auto* tool : names) {
        const auto sibling = compiler.parent_path() / tool;
        if (const auto found = find_tool(platform::utf8_path(sibling)); !found.empty()) return found;
        if (const auto found = find_tool(tool); !found.empty()) return found;
    }
    throw std::runtime_error("Windows C imports need link.exe or lld-link.exe. Run from a Visual Studio developer prompt");
}

void run_tool(const fs::path& program, std::vector<std::string> arguments, std::string_view action) {
    platform::Spawn spawn;
    spawn.program = program;
    spawn.arguments = std::move(arguments);
    spawn.input = platform::Stdio::null();
    spawn.job = platform::JobMode::kill_on_close;
    const auto result = platform::run_capture(std::move(spawn), std::chrono::milliseconds::zero(),
                                              std::numeric_limits<std::size_t>::max());
    if (result.started && result.status.success() && !result.error) return;
    std::string message = std::string(action) + " failed with " + platform::utf8_path(program);
    if (result.error) message += ": " + result.error.message();
    else message += ": " + result.status.describe();
    throw std::runtime_error(message + "\n" + result.out + result.err);
}

void write_contents(const fs::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary);
    output << contents;
    output.close();
    if (!output) throw std::runtime_error("cannot write " + platform::utf8_path(path));
}

std::vector<std::string> stage_inputs(const fs::path& directory, const std::vector<std::string>& libraries) {
    std::vector<std::string> arguments;
    for (const auto& name : libraries) {
        const auto bytes = runner::read_file(platform::path_from_utf8(name));
        const auto kind = coff_input(bytes);
        if (kind == CoffInput::dll_import)
            throw std::runtime_error("C import input '" + name + "' is a DLL import library or object. "
                                     "Use Std.DynamicForeign.SymbolLookup to load a DLL");
        if (kind == CoffInput::invalid)
            throw std::runtime_error("C import input '" + name + "': expected an x64 COFF object or static archive");
        // Give each input the spelling LINK uses to distinguish an archive
        // from an object. Stage the same bytes that were classified above.
        const auto stem = "input-" + std::to_string(arguments.size());
        const auto staged = directory / (stem + (kind == CoffInput::archive ? ".lib" : ".obj"));
        write_contents(staged, text(bytes));
        const std::string path = platform::native_utf8_path(staged);
        arguments.push_back(kind == CoffInput::archive ? "/WHOLEARCHIVE:" + path : path);
    }
    return arguments;
}

} // namespace

std::vector<std::uint8_t> link_windows_c_import(const Options& options, std::string_view shim,
                                                const std::vector<std::string>& libraries,
                                                const std::set<std::string>& include_dirs,
                                                const std::set<std::string>& symbols) {
    const LinkDirectory temporary;
    const auto libraries_to_link = stage_inputs(temporary.path, libraries);
    const auto compiler = compiler_path(options);
    const auto linker = linker_path(compiler);
    const auto source = temporary.path / "neura-native.c";
    const auto object = temporary.path / "neura-native.obj";
    const auto exports = temporary.path / "neura-native.def";
    const auto dll = temporary.path / "neura-native.dll";
    write_contents(source, shim);
    std::string definitions = "EXPORTS\n";
    for (const auto& symbol : symbols) definitions += "    " + symbol + "\n";
    if (!symbols.empty()) write_contents(exports, definitions);

    std::vector<std::string> compile{platform::native_utf8_path(compiler), "/nologo", "/c", "/TC", "/std:c11", "/MT",
                                     "/O2", "/utf-8", "/Fo" + platform::native_utf8_path(object),
                                     platform::native_utf8_path(source)};
    for (const auto& dir : include_dirs) compile.push_back("/I" + dir);
    for (const auto& dir : options.c_import.include_dirs) compile.push_back("/I" + dir);
    for (const auto& define : options.c_import.defines) compile.push_back("/D" + define);
    run_tool(compiler, std::move(compile), "compiling the native wrappers of the C imports");

    std::vector<std::string> link{platform::native_utf8_path(linker), "/NOLOGO", "/DLL", "/MACHINE:X64", "/BREPRO",
                                  "/INCREMENTAL:NO", "/MANIFEST:NO", "/OUT:" + platform::native_utf8_path(dll),
                                  platform::native_utf8_path(object)};
    // Keep the DLL on the static CRT even when an input adds a CRT default
    // library directive. Inputs must be built for the same runtime.
    link.insert(link.end(), {"/NODEFAULTLIB:MSVCRT", "/NODEFAULTLIB:MSVCRTD", "/NODEFAULTLIB:MSVCPRT",
                             "/NODEFAULTLIB:MSVCPRTD", "/NODEFAULTLIB:VCRUNTIME", "/NODEFAULTLIB:VCRUNTIMED",
                             "/NODEFAULTLIB:UCRT", "/NODEFAULTLIB:UCRTD"});
    if (!symbols.empty()) link.push_back("/DEF:" + platform::native_utf8_path(exports));
    link.insert(link.end(), libraries_to_link.begin(), libraries_to_link.end());
    link.insert(link.end(), options.link_arguments.begin(), options.link_arguments.end());
    run_tool(linker, std::move(link), "linking the native library of the C imports");
    return runner::read_file(dll);
}

} // namespace neura::cli
