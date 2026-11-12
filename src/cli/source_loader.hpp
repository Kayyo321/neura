#pragma once

#include "c-import/c_import.hpp"
#include "parser/syntax.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace neura {

struct IrProgram;

// An editor compilation reads open documents before disk, including new files.
// Scope is thread-local; ordinary CLI invocations retain strict disk behavior.
class SourceOverlayScope {
public:
    explicit SourceOverlayScope(const std::map<std::string, std::string, std::less<>>& sources,
                                bool allow_c_imports = false,
                                const std::set<std::string>* hidden_sources = nullptr);
    ~SourceOverlayScope();
    SourceOverlayScope(const SourceOverlayScope&) = delete;
    SourceOverlayScope& operator=(const SourceOverlayScope&) = delete;
private:
    const std::map<std::string, std::string, std::less<>>* previous_;
    bool previous_c_imports_;
    const std::set<std::string>* previous_hidden_;
};

// Lexes and parses `files` and the project modules they import. An import is
// searched for in the importing file's directory and then in each of `roots`.
// Std imports resolve only within the trusted standard library root.
std::vector<ParsedFile> read_sources(const std::vector<std::filesystem::path>& files,
                                     const std::vector<std::filesystem::path>& roots,
                                     const std::filesystem::path& library = {});

struct CImportOptions;

// Options for every later C import: include directories, definitions, and
// the libclang library.
void set_c_import_options(CImportOptions options);

// The module generated for the C header `header` (found as an import from
// the current directory would find it), for `neura c-bindings`.
std::string c_import_source(const std::filesystem::path& header, const std::vector<std::filesystem::path>& roots,
                            bool linked);

IrProgram lower_sources(const std::vector<std::filesystem::path>& files,
                        const std::vector<std::filesystem::path>& roots,
                        const std::filesystem::path& library);

} // namespace neura
