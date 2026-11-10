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

// Imports `header`. `linked` says whether the import names libraries; the
// generated module then looks symbols up in the image's embedded native
// library before the process's default lookup. Failures are diagnostics at
// `site`.
CImportResult import_c_header(const std::filesystem::path& header, bool linked, const std::string& module_name,
                              const CImportOptions& options, const Span& site);

} // namespace neura
