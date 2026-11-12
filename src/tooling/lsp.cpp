#include "lsp.hpp"
#include "analysis.hpp"
#include "cli/module_path.hpp"
#include "cli/source_loader.hpp"
#include "formatter.hpp"
#include "neura-ir/tags.hpp"
#include "neura/version.hpp"
#include "protocol.hpp"
#include "refactor.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace neura::tooling {
namespace {
namespace fs = std::filesystem;

struct RequestError : std::runtime_error {
    int code;
    RequestError(int number, std::string message) : std::runtime_error(std::move(message)), code(number) {}
};
Json strings(const std::vector<std::string> &values) {
    Json::Array result;
    for (const auto &value : values)
        result.emplace_back(value);
    return result;
}

class Server {
  public:
    int run() {
        // Keep reading cancellation while compiler work is in progress. Analysis
        // stays serialized so snapshots and compiler session state never race.
        std::deque<Json> pending;
        std::condition_variable ready;
        bool finished = false;
        std::exception_ptr reader_error;
        std::thread reader([&] {
            try {
                while (auto incoming = read_message(std::cin)) {
                    const auto method = (*incoming)["method"].string();
                    std::unique_lock lock(input_mutex_);
                    if (method == "$/cancelRequest") {
                        if (cancelled_.size() >= 256)
                            cancelled_.erase(cancelled_.begin());
                        cancelled_.insert((*incoming)["params"]["id"].dump());
                        continue;
                    }
                    ready.wait(lock, [&] { return pending.size() < 32; });
                    pending.push_back(std::move(*incoming));
                    ready.notify_all();
                    if (method == "exit")
                        break;
                }
            } catch (...) {
                reader_error = std::current_exception();
            }
            const std::lock_guard lock(input_mutex_);
            finished = true;
            ready.notify_all();
        });
        int status = 1;
        for (;;) {
            Json message;
            {
                std::unique_lock lock(input_mutex_);
                ready.wait(lock, [&] { return finished || !pending.empty(); });
                if (pending.empty())
                    break;
                message = std::move(pending.front());
                pending.pop_front();
                ready.notify_all();
            }
            const auto id = message["id"];
            const auto method = message["method"].string();
            const auto params = message["params"];
            try {
                if (method == "exit") {
                    status = shutdown_ ? 0 : 1;
                    break;
                }
                if (!id.is_null() && take_cancel(id)) {
                    reply_error(id, -32800, "Request cancelled");
                    continue;
                }
                if (shutdown_) {
                    if (!id.is_null())
                        reply_error(id, -32600, "Server is shutting down");
                    continue;
                }
                auto result = dispatch(method, params);
                if (!id.is_null()) {
                    if (take_cancel(id))
                        reply_error(id, -32800, "Request cancelled");
                    else
                        write_message(std::cout, Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
                }
            } catch (const RequestError &error) {
                if (!id.is_null())
                    reply_error(id, error.code, error.what());
            } catch (const Diagnostic &error) {
                if (!id.is_null())
                    reply_error(id, -32803, error.what());
                else
                    log(error.what());
            } catch (const std::exception &error) {
                if (!id.is_null())
                    reply_error(id, -32603, error.what());
                else
                    log(error.what());
            }
        }
        reader.join();
        if (reader_error)
            std::rethrow_exception(reader_error);
        return status;
    }

  private:
    AnalysisSession session_;
    bool initialized_{}, shutdown_{};
    bool code_lens_enabled_{true};
    InlayOptions inlay_options_;
    std::set<std::string> cancelled_;
    std::mutex input_mutex_;
    bool take_cancel(const Json &id) {
        const std::lock_guard lock(input_mutex_);
        return cancelled_.erase(id.dump()) != 0;
    }
    std::map<std::string, std::int64_t> published_;
    std::map<std::string, std::set<std::string>> dependencies_;
    std::map<std::string, std::map<std::string, Json>> diagnostic_reports_;
    static void reply_error(const Json &id, int code, std::string message) {
        write_message(
            std::cout,
            Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", Json{{"code", code}, {"message", std::move(message)}}}});
    }
    static void log(std::string message) {
        write_message(std::cout, Json{{"jsonrpc", "2.0"},
                                      {"method", "window/logMessage"},
                                      {"params", Json{{"type", 2}, {"message", std::move(message)}}}});
    }
    void publish(const std::string &uri) {
        const auto changed = uri_path(uri);
        std::set<std::string> roots{uri};
        for (const auto &[root_uri, dependencies] : dependencies_)
            if (dependencies.contains(changed))
                roots.insert(root_uri);
        for (const auto &root_uri : roots) {
            session_.analyze(root_uri);
            auto &dependencies = dependencies_[root_uri];
            dependencies.clear();
            for (const auto &source : session_.sources.files())
                dependencies.insert(source->name());
            auto &reports = diagnostic_reports_[root_uri];
            reports.clear();
            for (const auto &[path, doc] : session_.documents)
                if (doc.open && (dependencies.contains(path) || doc.uri == root_uri))
                    reports[path] = session_.diagnostics_for(doc);
        }
        for (const auto &[path, doc] : session_.documents) {
            if (!doc.open)
                continue;
            Json::Array combined;
            std::set<std::string> seen;
            for (const auto &[root_uri, reports] : diagnostic_reports_) {
                const auto report = reports.find(path);
                if (report == reports.end())
                    continue;
                for (const auto &item : report->second.items())
                    if (seen.insert(item.dump()).second)
                        combined.push_back(item);
            }
            write_message(
                std::cout,
                Json{{"jsonrpc", "2.0"},
                     {"method", "textDocument/publishDiagnostics"},
                     {"params", Json{{"uri", doc.uri}, {"version", doc.version}, {"diagnostics", combined}}}});
            published_[doc.uri] = doc.version;
        }
    }

    Json initialize(const Json &params) {
        if (initialized_)
            throw RequestError(-32600, "Server already initialized");
        initialized_ = true;
        auto root_uri = params["rootUri"].string();
        if (root_uri.empty() && !params["workspaceFolders"].items().empty())
            root_uri = params["workspaceFolders"][std::size_t{0}]["uri"].string();
        auto root = uri_path(root_uri);
        if (root.empty())
            root = params["initializationOptions"]["workspaceRoot"].string();
        session_.root = root.empty() ? fs::current_path() : fs::path(root);
        const auto &options = params["initializationOptions"];
        session_.trusted = options["trusted"].boolean(false);
        code_lens_enabled_ = options["codeLens"]["enabled"].boolean(true);
        inlay_options_.types = options["inlayHints"]["types"].boolean(true);
        inlay_options_.parameter_names = options["inlayHints"]["parameterNames"].boolean(true);
        inlay_options_.accessors = options["inlayHints"]["accessors"].boolean(false);
        inlay_options_.captures = options["inlayHints"]["captures"].boolean(false);
        for (const auto &path : options["sourceRoots"].items()) {
            fs::path candidate(path.string());
            if (candidate.is_relative())
                candidate = session_.root / candidate;
            session_.source_roots.push_back(fs::weakly_canonical(candidate));
        }
        if (session_.source_roots.empty())
            session_.source_roots.push_back(session_.root);
        if (options["stdlibPath"].is_string())
            session_.library = options["stdlibPath"].string();
        if (session_.library.empty()) {
            std::vector<fs::path> candidates{session_.root / "stdlib", fs::current_path() / "stdlib"};
            if (const char *executable = std::getenv("NEURA_EXECUTABLE")) {
                auto prefix = fs::weakly_canonical(fs::path(executable)).parent_path().parent_path();
                candidates.push_back(prefix / "share/neura/stdlib");
                candidates.push_back(prefix / "stdlib");
            }
            if (options["executable"].is_string()) {
                auto prefix =
                    fs::weakly_canonical(fs::path(options["executable"].string())).parent_path().parent_path();
                candidates.push_back(prefix / "share/neura/stdlib");
                candidates.push_back(prefix / "stdlib");
            }
            for (const auto &candidate : candidates)
                if (fs::is_directory(candidate / "std")) {
                    session_.library = fs::weakly_canonical(candidate);
                    break;
                }
        }
        if (!session_.library.empty() && !fs::is_directory(session_.library))
            throw RequestError(-32602, "Configured standard library directory does not exist");
        Json capabilities{
            {"positionEncoding", "utf-16"},
            {"textDocumentSync", Json{{"openClose", true}, {"change", 2}, {"save", Json{{"includeText", false}}}}},
            {"completionProvider", Json{{"triggerCharacters", Json::Array{".", "#", ":", " "}}, {"resolveProvider", false}}},
            {"hoverProvider", true},
            {"definitionProvider", true},
            {"declarationProvider", true},
            {"typeDefinitionProvider", true},
            {"implementationProvider", true},
            {"referencesProvider", true},
            {"callHierarchyProvider", true},
            {"typeHierarchyProvider", true},
            {"documentHighlightProvider", true},
            {"documentSymbolProvider", true},
            {"workspaceSymbolProvider", true},
            {"renameProvider", Json{{"prepareProvider", true}}},
            {"foldingRangeProvider", true},
            {"selectionRangeProvider", true},
            {"signatureHelpProvider",
             Json{{"triggerCharacters", Json::Array{"(", ","}}, {"retriggerCharacters", Json::Array{","}}}},
            {"codeLensProvider", Json{{"resolveProvider", false}}},
            {"codeActionProvider",
             Json{{"codeActionKinds", Json::Array{"quickfix", "source.fixAll.neura", "source.organizeImports",
                                                  "refactor.rewrite", "refactor.extract", "refactor.inline"}},
                  {"resolveProvider", false}}},
            {"documentFormattingProvider", true},
            {"documentRangeFormattingProvider", true},
            {"inlayHintProvider", true},
            {"semanticTokensProvider", Json{{"legend", Json{{"tokenTypes", strings(semantic_types)},
                                                            {"tokenModifiers", strings(semantic_modifiers)}}},
                                            {"full", true},
                                            {"range", false}}},
            {"workspace", Json{{"workspaceFolders", Json{{"supported", false}, {"changeNotifications", false}}}}},
            {"experimental", Json{{"neuraEntryPoints", true}, {"neuraSourceOverlays", true}}}};
        return Json{{"capabilities", capabilities},
                    {"serverInfo", Json{{"name", "Neura Language Server"}, {"version", neura::version::full}}}};
    }
    static bool valid_identifier(std::string_view name) {
        if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_'))
            return false;
        for (char c : name)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
                return false;
        try {
            auto tokens = lex(name, "<rename>");
            return tokens.size() == 2 && tokens.front().kind == TokenKind::Identifier;
        } catch (const Diagnostic &) {
            return false;
        }
    }
    std::vector<Span> all_references(const EditorSymbol &selected, bool declarations) {
        const auto key_file = selected.file;
        const auto key_offset = selected.selection.offset;
        std::vector<Span> spans;
        if (declarations)
            spans.push_back(selected.selection);
        session_.index_workspace();
        std::vector<std::string> roots;
        for (const auto &[path, doc] : session_.documents)
            if (!doc.library && doc.source.find(selected.name) != std::string::npos)
                roots.push_back(doc.uri);
        for (const auto &uri : roots) {
            session_.analyze(uri);
            std::optional<std::size_t> found;
            for (std::size_t i = 0; i < session_.symbols.size(); ++i)
                if (session_.symbols[i].file == key_file && session_.symbols[i].selection.offset == key_offset) {
                    found = i;
                    break;
                }
            if (!found)
                continue;
            std::set<std::size_t> family{*found};
            if (session_.program && session_.symbols[*found].compiler_id != no_symbol) {
                const auto root_of = [&](SymbolId id) {
                    std::set<SymbolId> visited;
                    while (id < session_.program->symbols.size() &&
                           session_.program->symbols[id].overrides != no_symbol && visited.insert(id).second)
                        id = session_.program->symbols[id].overrides;
                    return id;
                };
                const auto target = session_.symbols[*found].compiler_id;
                if (session_.program->symbols[target].kind == SymbolKind::Function) {
                    const auto root = root_of(target);
                    for (std::size_t i = 0; i < session_.symbols.size(); ++i) {
                        const auto id = session_.symbols[i].compiler_id;
                        if (id != no_symbol && session_.program->symbols[id].kind == SymbolKind::Function &&
                            root_of(id) == root) {
                            family.insert(i);
                            if (declarations)
                                spans.push_back(session_.symbols[i].selection);
                        }
                    }
                }
            }
            for (const auto &reference : session_.references)
                if (family.contains(reference.symbol))
                    spans.push_back(reference.span);
        }
        std::sort(spans.begin(), spans.end(),
                  [](const auto &a, const auto &b) { return std::tie(a.file, a.offset) < std::tie(b.file, b.offset); });
        spans.erase(std::unique(spans.begin(), spans.end(),
                                [](const auto &a, const auto &b) { return a.file == b.file && a.offset == b.offset; }),
                    spans.end());
        return spans;
    }
    Json rename(EditorDocument &doc, const Json &params) {
        const auto offset = source_offset(doc.source, params["position"]);
        const auto *found = session_.symbol_at(doc, offset);
        if (!found)
            throw RequestError(-32602, "No resolved symbol at this position");
        const auto selected = *found;
        if (doc.library || selected.file.empty() || selected.name == "main" || selected.name == "init" ||
            selected.name == "destroy")
            throw RequestError(-32602, "This language-defined or library symbol cannot be renamed");
        const auto name = params["newName"].string();
        if (!valid_identifier(name))
            throw RequestError(-32602, "The new name must be a Neura identifier");
        if (!session_.program || selected.compiler_id == no_symbol)
            throw RequestError(-32803, "Resolve source errors before renaming this symbol");
        auto references = all_references(selected, true);
        std::vector<DiagnosticEdit> edits;
        std::map<std::string, std::string, std::less<>> overlays;
        for (const auto &[path, item] : session_.documents)
            if (item.open)
                overlays[path] = item.source;
        for (auto it = references.rbegin(); it != references.rend(); ++it) {
            const auto at = session_.documents.find(it->file);
            if (at == session_.documents.end() || at->second.library)
                throw RequestError(-32803, "Rename would modify a library declaration");
            if (!overlays.contains(it->file))
                overlays[it->file] = at->second.source;
            overlays[it->file].replace(it->offset, it->length, name);
            edits.push_back({*it, name});
        }
        // A missed reference or capture must never produce an apparently
        // successful rename. Validate every affected input before returning.
        std::optional<std::pair<std::string, std::string>> moved;
        std::set<std::string> hidden;
        if (selected.kind == 2 && selected.parent == static_cast<std::size_t>(-1) &&
            fs::path(selected.file).filename() == module_path(selected.name).filename()) {
            const auto destination =
                (fs::path(selected.file).parent_path() / module_path(name).filename()).generic_string();
            if (destination != selected.file) {
                if (fs::exists(destination) || session_.documents.contains(destination))
                    throw RequestError(-32803, "A file already exists at the renamed module path");
                overlays[destination] = overlays.at(selected.file);
                overlays.erase(selected.file);
                hidden.insert(selected.file);
                moved = std::pair{selected.file, destination};
            }
        }
        const SourceOverlayScope overlay_scope(overlays, session_.trusted, &hidden);
        SourceMap validation_sources;
        const SourceMapScope source_scope(validation_sources);
        DiagnosticSink warnings;
        const DiagnosticSinkScope warning_scope(warnings);
        std::set<std::string> inputs;
        for (const auto &span : references)
            inputs.insert(span.file);
        for (const auto &input : inputs) {
            try {
                const auto source = moved && input == moved->first ? moved->second : input;
                (void)lower_sources({source}, session_.source_roots, session_.library);
            } catch (const Diagnostic &error) {
                throw RequestError(-32803, "Rename would not preserve a valid program: " + std::string(error.what()));
            }
        }
        auto result = session_.workspace_edit(edits);
        if (moved)
            result["documentChanges"].push_back(
                Json{{"kind", "rename"},
                     {"oldUri", path_uri(moved->first)},
                     {"newUri", path_uri(moved->second)},
                     {"options", Json{{"overwrite", false}, {"ignoreIfExists", false}}}});
        return result;
    }
    Json dispatch(const std::string &method, const Json &params) {
        if (method == "initialize")
            return initialize(params);
        if (method == "shutdown") {
            shutdown_ = true;
            return Json{};
        }
        if (!initialized_)
            throw RequestError(-32002, "Server not initialized");
        if (method == "initialized" || method == "$/setTrace" || method == "textDocument/willSave" ||
            method == "workspace/didChangeWatchedFiles") {
            if (method == "workspace/didChangeWatchedFiles") {
                for (const auto &change : params["changes"].items()) {
                    const auto path = uri_path(change["uri"].string());
                    const auto found = session_.documents.find(path);
                    if (found != session_.documents.end() && !found->second.open)
                        session_.documents.erase(found);
                }
                session_.invalidate();
                std::vector<std::string> open;
                for (const auto &[path, doc] : session_.documents)
                    if (doc.open)
                        open.push_back(doc.uri);
                for (const auto &uri : open)
                    publish(uri);
            }
            return Json{};
        }
        if (method == "workspace/didChangeConfiguration") {
            const auto &config = params["settings"]["neura"];
            if (config["sourceRoots"].is_array()) {
                session_.source_roots.clear();
                for (const auto &value : config["sourceRoots"].items()) {
                    fs::path path(value.string());
                    session_.source_roots.push_back(path.is_absolute() ? path : session_.root / path);
                }
            }
            code_lens_enabled_ =
                config["codeLens"]["enabled"].boolean(config["codeLens.enabled"].boolean(code_lens_enabled_));
            inlay_options_.types =
                config["inlayHints"]["types"].boolean(config["inlayHints.types"].boolean(inlay_options_.types));
            inlay_options_.parameter_names = config["inlayHints"]["parameterNames"].boolean(
                config["inlayHints.parameterNames"].boolean(inlay_options_.parameter_names));
            inlay_options_.accessors = config["inlayHints"]["accessors"].boolean(
                config["inlayHints.accessors"].boolean(inlay_options_.accessors));
            inlay_options_.captures = config["inlayHints"]["captures"].boolean(
                config["inlayHints.captures"].boolean(inlay_options_.captures));
            session_.invalidate();
            return Json{};
        }
        if (method == "textDocument/didOpen") {
            const auto &item = params["textDocument"];
            auto &doc = session_.open(item["uri"].string(), item["text"].string(), item["version"].integer());
            publish(doc.uri);
            return Json{};
        }
        if (method == "textDocument/didChange") {
            session_.change(params);
            publish(params["textDocument"]["uri"].string());
            return Json{};
        }
        if (method == "textDocument/didClose") {
            const auto uri = params["textDocument"]["uri"].string();
            session_.close(uri);
            dependencies_.erase(uri);
            diagnostic_reports_.erase(uri);
            Json clear{{"uri", uri}, {"diagnostics", Json::Array{}}};
            write_message(std::cout,
                          Json{{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"}, {"params", clear}});
            return Json{};
        }
        if (method == "textDocument/didSave") {
            session_.invalidate();
            publish(params["textDocument"]["uri"].string());
            return Json{};
        }
        if (method == "workspace/symbol") {
            session_.index_workspace();
            if (!session_.documents.empty())
                session_.analyze(session_.documents.begin()->second.uri);
            const auto query = params["query"].string();
            Json::Array result;
            for (const auto &symbol : session_.symbols) {
                if (symbol.file.empty() || symbol.modifiers & (1u << 9) ||
                    symbol.name.find(query) == std::string::npos || symbol.token_type == 6)
                    continue;
                result.emplace_back(Json{{"name", symbol.name},
                                         {"kind", symbol.kind},
                                         {"location", session_.location(symbol.selection)},
                                         {"containerName", symbol.parent == static_cast<std::size_t>(-1)
                                                               ? ""
                                                               : session_.symbols[symbol.parent].qualified}});
            }
            return result;
        }
        auto uri = params["textDocument"]["uri"].string(params["uri"].string(params["item"]["uri"].string()));
        auto *doc = session_.document(uri);
        if (!doc)
            throw RequestError(-32602, "Document not found or unsupported URI");
        session_.analyze(uri);
        const auto offset = source_offset(doc->source, params["position"]);
        if (method == "neura/declarationDocumentation") {
            const EditorSymbol *selected = session_.symbol_at(*doc, offset);
            if (!selected || !selected->declaration) {
                for (const auto &candidate : session_.symbols)
                    if (candidate.file == doc->path && candidate.declaration && offset >= candidate.span.offset &&
                        offset <= candidate.span.offset + candidate.span.length &&
                        (!selected || candidate.span.length < selected->span.length))
                        selected = &candidate;
            }
            if (!selected || !selected->declaration)
                return Json{};
            const auto &declaration = *selected->declaration;
            Json::Array parameters, type_parameters;
            for (const auto &parameter : declaration.parameters)
                parameters.emplace_back(parameter.name);
            for (const auto &parameter : declaration.generics)
                type_parameters.emplace_back(parameter.name);
            Json result{{"name", selected->name},
                        {"signature", selected->detail},
                        {"kind", selected->kind},
                        {"range", source_range(doc->source, declaration.span)},
                        {"selectionRange", source_range(doc->source, selected->selection)},
                        {"parameters", parameters},
                        {"typeParameters", type_parameters}};
            if (declaration.type)
                result["returnType"] = declaration.type->name;
            if (declaration.documentation)
                result["documentation"] = Json{{"text", declaration.documentation->text},
                                               {"range", source_range(doc->source, declaration.documentation->span)}};
            return result;
        }
        if (method == "neura/entryPoints") {
            Json::Array entries;
            const auto discovered = session_.entries();
            for (const auto &entry : discovered.items())
                if (entry["uri"].string() == doc->uri)
                    entries.push_back(entry);
            return entries;
        }
        if (method == "textDocument/semanticTokens/full")
            return session_.semantic_tokens(*doc);
        if (method == "textDocument/documentSymbol")
            return session_.document_symbols(*doc);
        if (method == "textDocument/completion")
            return session_.completion(*doc, offset, params["context"]["triggerKind"].integer() == 2 &&
                                                     params["context"]["triggerCharacter"].string() == " ");
        if (method == "textDocument/codeLens")
            return code_lens_enabled_ ? session_.code_lenses(*doc) : Json{Json::Array{}};
        if (method == "textDocument/inlayHint")
            return session_.inlay_hints(*doc, params["range"], inlay_options_);
        if (method == "textDocument/signatureHelp")
            return session_.signature_help(*doc, offset);
        if (method == "textDocument/codeAction") {
            auto result = session_.code_actions(*doc, params);
            auto refactorings = refactor_actions(session_, *doc, params);
            for (auto &action : refactorings.items())
                result.push_back(std::move(action));
            if (!params["context"]["only"].items().empty()) {
                Json::Array filtered;
                for (const auto &action : result.items())
                    for (const auto &requested : params["context"]["only"].items()) {
                        const auto kind = action["kind"].string(), parent = requested.string();
                        if (kind == parent || kind.starts_with(parent + ".")) {
                            filtered.push_back(action);
                            break;
                        }
                    }
                return filtered;
            }
            return result;
        }
        if (method == "textDocument/prepareCallHierarchy" || method == "textDocument/prepareTypeHierarchy" ||
            method.starts_with("callHierarchy/") || method.starts_with("typeHierarchy/"))
            return hierarchy_request(session_, *doc, method, params);
        if (method == "textDocument/formatting" || method == "textDocument/rangeFormatting") {
            FormatOptions options;
            options.tab_size =
                static_cast<unsigned>(std::clamp<std::int64_t>(params["options"]["tabSize"].integer(4), 1, 16));
            options.insert_spaces = params["options"]["insertSpaces"].boolean(true);
            std::string formatted;
            try {
                formatted = format_source(doc->source, doc->path, options);
            } catch (const Diagnostic &) {
                // An editor snapshot can be between keystrokes. Diagnostics
                // already describe invalid source; formatting should leave it
                // untouched without turning format-on-save into a popup.
                return Json::Array{};
            }
            if (formatted == doc->source)
                return Json::Array{};
            // Full-document edits preserve formatter context even when the
            // requested selection cuts through a declaration or comment.
            return Json::Array{Json{{"range", source_range(doc->source, Span{doc->path, 1, 1, 0, doc->source.size()})},
                                    {"newText", formatted}}};
        }
        if (method == "textDocument/rename")
            return rename(*doc, params);
        if (method == "textDocument/prepareRename") {
            const auto *symbol = session_.symbol_at(*doc, offset);
            if (!symbol || doc->library || symbol->name == "main" || symbol->name == "init" ||
                symbol->name == "destroy" || symbol->compiler_id == no_symbol)
                return Json{};
            for (const auto &token : doc->tokens)
                if (token.text == symbol->name && offset >= token.span.offset &&
                    offset <= token.span.offset + token.span.length)
                    return Json{{"range", source_range(doc->source, token.span)}, {"placeholder", symbol->name}};
            return Json{};
        }
        if (method == "textDocument/hover") {
            const auto *symbol = session_.symbol_at(*doc, offset);
            if (!symbol) {
                for (const auto &token : doc->tokens)
                    if (offset >= token.span.offset && offset <= token.span.offset + token.span.length) {
                        if (const auto *tag = find_tag(token.text))
                            return Json{{"contents", Json{{"kind", "markdown"},
                                                          {"value", "```neura\n#" + tag_signature(*tag) + "\n```\n\n" +
                                                                        std::string(tag->meaning) + "\n\nStatus: " +
                                                                        std::string(tag_status_name(tag->status))}}},
                                        {"range", source_range(doc->source, token.span)}};
                    }
                return Json{};
            }
            std::string markdown = "```neura\n" + symbol->detail + "\n```";
            if (!symbol->documentation.empty())
                markdown += "\n\n" + symbol->documentation;
            if (symbol->declaration && !symbol->declaration->accessors.empty())
                markdown += "\n\nAccessor-backed property. Reads and writes through external "
                            "receivers use its declared getter and setter; storage inside "
                            "the declaring class follows Neura's receiver rules.";
            return Json{{"contents", Json{{"kind", "markdown"}, {"value", markdown}}}};
        }
        if (method == "textDocument/definition" || method == "textDocument/declaration") {
            const auto *symbol = session_.symbol_at(*doc, offset);
            return symbol ? session_.location(symbol->selection) : Json{};
        }
        if (method == "textDocument/typeDefinition") {
            const auto *symbol = session_.symbol_at(*doc, offset);
            if (!symbol || !session_.program || symbol->compiler_id == no_symbol)
                return Json{};
            auto target = session_.program->symbols[symbol->compiler_id].type.symbol;
            if (target == no_symbol)
                return Json{};
            for (const auto &candidate : session_.symbols)
                if (candidate.compiler_id == target)
                    return session_.location(candidate.selection);
            return Json{};
        }
        if (method == "textDocument/implementation") {
            const auto *symbol = session_.symbol_at(*doc, offset);
            Json::Array result;
            if (!symbol || !session_.program || symbol->compiler_id == no_symbol)
                return result;
            for (const auto &candidate : session_.symbols) {
                if (candidate.compiler_id == no_symbol)
                    continue;
                const auto &original = session_.program->symbols[candidate.compiler_id];
                bool match = original.overrides == symbol->compiler_id || original.base == symbol->compiler_id;
                for (const auto &interface : original.interfaces)
                    if (interface.symbol == symbol->compiler_id)
                        match = true;
                if (match)
                    result.push_back(session_.location(candidate.selection));
            }
            return result;
        }
        if (method == "textDocument/references") {
            const auto *symbol = session_.symbol_at(*doc, offset);
            Json::Array result;
            if (!symbol)
                return result;
            const auto selected = *symbol;
            for (const auto &span : all_references(selected, params["context"]["includeDeclaration"].boolean()))
                result.push_back(session_.location(span));
            return result;
        }
        if (method == "textDocument/documentHighlight") {
            auto selected = session_.symbol_index_at(*doc, offset);
            Json::Array result;
            if (!selected)
                return result;
            const auto &symbol = session_.symbols[*selected];
            if (symbol.file == doc->path)
                result.emplace_back(Json{{"range", source_range(doc->source, symbol.selection)}, {"kind", 3}});
            for (const auto &r : session_.references)
                if (r.symbol == *selected && r.span.file == doc->path)
                    result.emplace_back(Json{{"range", source_range(doc->source, r.span)}, {"kind", r.write ? 3 : 2}});
            return result;
        }
        if (method == "textDocument/foldingRange") {
            Json::Array result;
            std::vector<Span> stack;
            for (const auto &t : doc->tokens) {
                if (t.text == "{" || t.text == "(")
                    stack.push_back(t.span);
                else if ((t.text == "}" || t.text == ")") && !stack.empty()) {
                    auto start = stack.back();
                    stack.pop_back();
                    const auto a = source_position(doc->source, start.offset),
                               b = source_position(doc->source, t.span.offset);
                    if (b["line"].integer() > a["line"].integer())
                        result.emplace_back(Json{{"startLine", a["line"]},
                                                 {"startCharacter", a["character"]},
                                                 {"endLine", b["line"]},
                                                 {"endCharacter", b["character"]},
                                                 {"kind", "region"}});
                }
            }
            return result;
        }
        if (method == "textDocument/selectionRange") {
            Json::Array result;
            for (const auto &position : params["positions"].items()) {
                const auto at = source_offset(doc->source, position);
                Json current{{"range", source_range(doc->source, Span{doc->path, 1, 1, 0, doc->source.size()})}};
                std::vector<Span> ranges;
                for (const auto &s : session_.symbols)
                    if (s.file == doc->path && at >= s.span.offset && at <= s.span.offset + s.span.length)
                        ranges.push_back(s.span);
                std::sort(ranges.begin(), ranges.end(),
                          [](const auto &a, const auto &b) { return a.length > b.length; });
                for (const auto &range : ranges)
                    current = Json{{"range", source_range(doc->source, range)}, {"parent", current}};
                for (const auto &token : doc->tokens)
                    if (at >= token.span.offset && at <= token.span.offset + token.span.length) {
                        current = Json{{"range", source_range(doc->source, token.span)}, {"parent", current}};
                        break;
                    }
                result.push_back(current);
            }
            return result;
        }
        throw RequestError(-32601, "Method not supported: " + method);
    }
};
} // namespace
int run_lsp(const std::vector<std::string> &arguments) {
    for (const auto &argument : arguments)
        if (argument != "--stdio")
            throw std::runtime_error("neura lsp accepts only --stdio");
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    return Server{}.run();
}
} // namespace neura::tooling
