#pragma once

// The C import stage (docs/lang/ForeignInterop.md, C imports). It reads a C
// header through libclang, loaded at run time by the neura-libclang
// submodule, and generates the source of a Neura module whose functions call
// the header's functions through Std.DynamicForeign. The generated module
// then goes through the ordinary parser and lowering. The stage runs before
// parsing and owns no language rules of its own: everything it emits is
// ordinary Neura source.

#include "util/diagnostic.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace neura {

struct CImportOptions {
    // Extra include directories (--c-include) and macro definitions
    // (--c-define NAME[=VALUE]) for parsing headers.
    std::vector<std::string> include_dirs;
    std::vector<std::string> defines;
    // The libclang library to load (--libclang); empty searches as the
    // loader documents, after the NEURA_LIBCLANG environment variable.
    std::string libclang;
};

struct CImportResult {
    // The generated module's name and Neura source.
    std::string module_name;
    std::string source;
    // C source of exported wrappers for the header's static inline
    // functions; empty when it has none.
    std::string shim;
    // The native symbols the generated module looks up.
    std::vector<std::string> symbols;
    // Declarations the importer could not map, by their Neura name, with the
    // reason; using one is a compile error that names the reason.
    std::map<std::string, std::string> unsupported;
};

// What `neura doctor` reports about the libclang that C imports would use:
// the same loader and builtin-header search as an import, without parsing
// anything.
struct LibclangStatus {
    bool loaded = false;
    // Why loading failed, when it did.
    std::string error;
    // The library that was opened, and its LLVM major version (0 if unknown).
    std::string path;
    int major = 0;
    // The directory of libclang's builtin headers; empty when none was found.
    std::string resource_dir;
};

// The directories searched, in order, for a C header that no source root or
// --c-include directory holds. On macOS they are in the SDK: the one named by
// SDKROOT, else the developer directory's, the Command Line Tools', or Xcode's,
// else the one `xcrun` reports. Imports parse with that SDK as -isysroot.
//
// On Windows they are the C headers of the MSVC tools and the Windows SDK,
// found once per process: the directories of the INCLUDE variable followed by
// those of EXTERNAL_INCLUDE, when INCLUDE names any (a Visual Studio developer
// environment); otherwise the include directory of the default MSVC tools
// version of the newest Visual Studio 2017 or later that has the tools (as
// vswhere reports it), followed by the ucrt, shared, um, winrt and cppwinrt
// directories of the newest Windows 10 or 11 SDK with a ucrt (below the
// KitsRoot10 that the SDK records in the registry). Entries that are not
// absolute are left out. Imports parse with -nostdlibinc and these
// directories as -idirafter, so libclang reads the headers the search found,
// after its builtin headers.
std::vector<std::filesystem::path> system_header_directories();

// Whether `directory` is one of those, so that a header found there needs no
// -I. Always false outside macOS and Windows.
bool is_system_header_directory(const std::filesystem::path& directory);

LibclangStatus probe_libclang(const CImportOptions& options);

// Imports `header`. `linked` says whether the import names libraries; the
// generated module then looks symbols up in the image's embedded native
// library before the process's default lookup. Failures are diagnostics at
// `site`.
CImportResult import_c_header(const std::filesystem::path& header, bool linked, const std::string& module_name,
                              const CImportOptions& options, const Span& site);

} // namespace neura
