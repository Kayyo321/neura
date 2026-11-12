#pragma once

#include "json.hpp"
#include "lexer/lexer.hpp"
#include "neura-ir/ir.hpp"
#include "parser/syntax.hpp"
#include "util/source_map.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace neura::tooling {

std::string path_uri(std::string_view path);
std::string uri_path(std::string_view uri);
Json source_position(std::string_view source, std::size_t offset);
std::size_t source_offset(std::string_view source, const Json &position);
Json source_range(std::string_view source, const Span &span);

inline const std::vector<std::string> semantic_types{"namespace",
                                                     "type",
                                                     "class",
                                                     "enum",
                                                     "interface",
                                                     "typeParameter",
                                                     "parameter",
                                                     "variable",
                                                     "property",
                                                     "enumMember",
                                                     "event",
                                                     "function",
                                                     "method",
                                                     "macro",
                                                     "keyword",
                                                     "modifier",
                                                     "comment",
                                                     "string",
                                                     "number",
                                                     "regexp",
                                                     "operator",
                                                     "decorator",
                                                     "neuraGetter",
                                                     "neuraSetter",
                                                     "neuraAccessorRead",
                                                     "neuraAccessorWrite",
                                                     "neuraStorage",
                                                     "neuraModule",
                                                     "neuraConstructor",
                                                     "neuraDestructor"};
inline const std::vector<std::string> semantic_modifiers{
    "declaration", "definition",   "readonly",      "static",         "deprecated",   "abstract",
    "async",       "modification", "documentation", "defaultLibrary", "exported",     "override",
    "captured",    "deferred",     "reference",     "scopeBound",     "transferable", "synchronized"};

struct EditorSymbol {
    std::string name, qualified, file, detail, documentation, type_name;
    Span span, selection, scope;
    int kind{13}, token_type{7};
    unsigned modifiers{};
    std::size_t parent{static_cast<std::size_t>(-1)};
    SymbolId compiler_id{no_symbol};
    const Declaration *declaration{};
};
struct EditorReference {
    Span span;
    std::size_t symbol{};
    bool write{};
    int token_type{-1};
};
struct EditorDocument {
    std::string uri, path, source;
    std::int64_t version{};
    bool open{}, library{};
    std::vector<Token> tokens;
    ParsedFile syntax;
    std::vector<Diagnostic> syntax_diagnostics;
    // Coordinates belong to this text snapshot, never to its buffer address.
    std::vector<std::size_t> line_starts;
    Json position(std::size_t offset) const;
    Json range(const Span &span) const;
};

struct InlayOptions {
    bool types{true};
    bool parameter_names{true};
    bool accessors{};
    bool captures{};
};

class AnalysisSession {
  public:
    std::filesystem::path root, library;
    std::vector<std::filesystem::path> source_roots;
    bool trusted{};
    std::map<std::string, EditorDocument, std::less<>> documents;
    std::vector<EditorSymbol> symbols;
    std::vector<EditorReference> references;
    std::vector<Diagnostic> diagnostics;
    std::optional<IrProgram> program;
    SourceMap sources;

    EditorDocument &open(std::string uri, std::string source, std::int64_t version);
    EditorDocument *document(std::string_view uri);
    void close(std::string_view uri);
    void change(const Json &params);
    void analyze(std::string_view uri);
    void invalidate() {
        analyzed_.clear();
        workspace_indexed_ = false;
    }
    void index_workspace();
    const EditorSymbol *symbol_at(const EditorDocument &document, std::size_t offset) const;
    std::optional<std::size_t> symbol_index_at(const EditorDocument &document, std::size_t offset) const;
    Json location(const Span &span) const;
    Json diagnostics_for(const EditorDocument &document) const;
    Json semantic_tokens(const EditorDocument &document) const;
    Json completion(const EditorDocument &document, std::size_t offset) const;
    Json document_symbols(const EditorDocument &document) const;
    Json code_lenses(const EditorDocument &document) const;
    Json inlay_hints(const EditorDocument &document, const Json &range, InlayOptions options = {}) const;
    Json signature_help(const EditorDocument &document, std::size_t offset) const;
    Json code_actions(const EditorDocument &document, const Json &params) const;
    Json workspace_edit(const std::vector<DiagnosticEdit> &edits) const;
    Json entries() const;

  private:
    std::string analyzed_;
    bool workspace_indexed_{};
    std::map<std::pair<std::string, std::string>, std::vector<std::size_t>> symbol_names_;
    void rebuild_symbol_index();
    void parse_document(EditorDocument &document);
    void index_syntax(EditorDocument &document);
    void index_compiler();
    std::optional<std::size_t> resolve_name(const EditorDocument &document, std::string_view name,
                                            std::size_t offset) const;
};
} // namespace neura::tooling
