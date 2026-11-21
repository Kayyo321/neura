#include "source_loader.hpp"

#include "c-import/c_import.hpp"
#include "lexer/lexer.hpp"
#include "module_path.hpp"
#include "neura-ir/ir.hpp"
#include "neura-ir/linker.hpp"
#include "neura-ir/nir_format.hpp"
#include "neura-ir/storage.hpp"
#include "nvr/format.hpp"
#include "parser/html_template.hpp"
#include "util/line_endings.hpp"
#include "util/sha256.hpp"
#include "util/source_map.hpp"
#include "util/suggest.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace neura {
namespace {

namespace fs = std::filesystem;

thread_local CImportOptions c_import_options;
thread_local const std::map<std::string, std::string, std::less<>>* source_overlays = nullptr;
thread_local bool overlay_c_imports = true;
thread_local const std::set<std::string>* overlay_hidden_sources = nullptr;

// The text of a path as source names, import targets and messages hold it:
// UTF-8 in generic form, with '/' separators, on every host (W09). On
// Windows generic_string() would use the process code page.
std::string path_name(const fs::path& path) {
#ifdef _WIN32
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
#else
    return path.generic_string();
#endif
}

// A path from UTF-8 text, such as the header and library names of a C
// import. On Windows a std::string would be read in the process code page.
fs::path utf8_path(std::string_view text) {
#ifdef _WIN32
    return fs::path(std::u8string(text.begin(), text.end()));
#else
    return fs::path(text);
#endif
}

// `path` relative to the source root `root`. Both are canonical, so that two
// spellings of the root (c:/app and C:/app on Windows) agree.
fs::path relative_to_root(const fs::path& path, const fs::path& root) {
    std::error_code error;
    const auto canonical_root = fs::weakly_canonical(root, error);
    return path.lexically_relative(error ? root : canonical_root);
}

// Source text as the lexer reads it (W08): each CR LF is LF, so a file
// checked out with CR LF line endings has the spans, the diagnostics and the
// image of its LF original. Every source, template and stylesheet read from
// the disk passes through here before it is recorded and parsed; the text of
// an overlay is the language server's, which normalizes it itself.
std::string lf_source(std::string text) {
    if (text.find('\r') == std::string::npos) return text;
    return util::normalize_crlf(text);
}

fs::path source_canonical(const fs::path& path) {
    if (overlay_hidden_sources && overlay_hidden_sources->contains(path_name(fs::weakly_canonical(fs::absolute(path)))))
        throw std::runtime_error("source file was moved: " + path_name(path));
    if (source_overlays) {
        const auto actual = fs::weakly_canonical(fs::absolute(path));
        if (source_overlays->contains(path_name(actual))) return actual;
    }
    return fs::canonical(path);
}
bool source_exists(const fs::path& path) {
    if (overlay_hidden_sources && overlay_hidden_sources->contains(path_name(fs::weakly_canonical(fs::absolute(path))))) return false;
    if (source_overlays && source_overlays->contains(path_name(fs::weakly_canonical(fs::absolute(path))))) return true;
    return fs::is_regular_file(path);
}

// The modules generated for C imports during one source load.
struct CImports {
    // Generated source by its virtual path: the header's path with the
    // module name and .nra appended.
    std::map<fs::path, std::string> sources;
    // The C header each generated module was made from.
    std::map<fs::path, std::string> headers;
    std::vector<IrNativeUnit> units;
    // Declarations each generated module could not import, by module name.
    std::map<std::string, std::map<std::string, std::string>> unsupported;
};

// Finds a C header: beside the importing file, below a source root, in an
// include directory, or in the system include directories.
fs::path find_c_header(const std::string& header, const fs::path& base, const std::vector<fs::path>& roots,
                       const Span& span) {
    const auto name = utf8_path(header);
    std::vector<fs::path> directories{base};
    directories.insert(directories.end(), roots.begin(), roots.end());
    for (const auto& dir : c_import_options.include_dirs) directories.push_back(utf8_path(dir));
    for (auto& dir : system_header_directories()) directories.push_back(std::move(dir));
    if (name.is_absolute() && fs::is_regular_file(name)) return fs::canonical(name);
    for (const auto& dir : directories) {
        std::error_code ignored;
        if (fs::is_regular_file(dir / name, ignored)) return fs::canonical(dir / name);
    }
    throw Diagnostic(span, "cannot find C header '" + header + "'").with_code("E0709");
}

struct SourceDependencies {
    fs::path root;
    std::vector<const Expr*> imports;
    std::set<fs::path> active_templates;
    // Imported stylesheets resolved so far, by file, and the chain of
    // stylesheet files being resolved, to report a cycle.
    std::map<fs::path, std::pair<BundleFileSyntax, std::vector<BundleFileSyntax>>> imported_styles{};
    std::vector<fs::path> active_styles{};
};

void collect_imports(const Declaration& declaration, SourceDependencies& dependencies);
void collect_imports(const Statement& statement, SourceDependencies& dependencies);
void collect_imports(const Expr& expression, SourceDependencies& dependencies);

[[noreturn]] void template_file_error(const Token& path, const fs::path& root, std::string reason) {
    throw Diagnostic(path.span, "template or stylesheet file cannot be used").with_code("E1011")
        .label(std::move(reason)).note("allowed source root: " + path_name(root) + '/');
}

ResolvedTemplate read_template(const Token& path, const fs::path& root) {
    constexpr std::size_t maximum = 1024 * 1024;
    const auto written = decode_string_literal(path.text);
    const auto relative = utf8_path(written);
    if (relative.empty() || relative.has_root_path() || written.find(':') != std::string::npos ||
        written.find('\\') != std::string::npos)
        template_file_error(path, root, "write a relative path using '/' separators");
    const auto requested = utf8_path(path.span.file).parent_path() / relative;
    std::error_code error;
    const auto normalized = fs::weakly_canonical(requested, error);
    if (error) template_file_error(path, root, "cannot resolve this path");
    const auto within = relative_to_root(normalized, root);
    if (within.empty() || *within.begin() == "..") template_file_error(path, root, "this path leaves the source root");
    const auto name = path_name(normalized);
    std::string source;
    if (source_overlays && source_overlays->contains(name) &&
        (!overlay_hidden_sources || !overlay_hidden_sources->contains(name))) {
        source = source_overlays->at(name);
    } else {
        if (!source_exists(normalized)) template_file_error(path, root, "no template file exists at this path");
        std::ifstream input(normalized, std::ios::binary);
        if (!input) template_file_error(path, root, "cannot open this template");
        // The limit applies to the text with LF line endings, which CR LF
        // endings at most double.
        source.resize(2 * maximum + 1);
        input.read(source.data(), static_cast<std::streamsize>(source.size()));
        source.resize(static_cast<std::size_t>(input.gcount()));
        if (input.bad()) template_file_error(path, root, "cannot read this template");
        source = lf_source(std::move(source));
    }
    if (source.size() > maximum) template_file_error(path, root, "the template exceeds 1 MiB");
    if (!nvr::valid_utf8(source)) template_file_error(path, root, "the template must contain valid UTF-8");
    const auto bom = source.starts_with("\xEF\xBB\xBF") ? 3u : 0u;
    SourceMap::global().add(name, source, SourceKind::User);
    return {parse_template(std::string_view(source).substr(bom), {name, 1, 1, bom, source.size() - bom}),
            util::sha256_hex(source)};
}

// The bytes of a file the build reads, from an overlay or the disk, or
// nullopt when there is none. `too_large` is set when it exceeds `maximum`.
// Source text (`text`) read from the disk has its CR LF line endings as LF,
// and the limit applies to that text; an overlay holds an editor's text,
// which the language server has normalized already (W08).
std::optional<std::string> read_input_file(const fs::path& normalized, std::size_t maximum, bool& too_large,
                                           bool text = false) {
    const auto name = path_name(normalized);
    std::string source;
    too_large = false;
    if (source_overlays && source_overlays->contains(name) &&
        (!overlay_hidden_sources || !overlay_hidden_sources->contains(name))) {
        source = source_overlays->at(name);
    } else {
        if (!source_exists(normalized)) return std::nullopt;
        std::ifstream input(normalized, std::ios::binary);
        if (!input) return std::nullopt;
        // CR LF endings at most double the length of the text.
        source.resize((text ? 2 * maximum : maximum) + 1);
        input.read(source.data(), static_cast<std::streamsize>(source.size()));
        source.resize(static_cast<std::size_t>(input.gcount()));
        if (input.bad()) return std::nullopt;
        if (text) source = lf_source(std::move(source));
    }
    too_large = source.size() > maximum;
    return source;
}

[[noreturn]] void style_reference_error(const Span& span, std::string reason) {
    throw Diagnostic(span, "stylesheet import or asset cannot be resolved").with_code("E1013")
        .label(std::move(reason));
}

std::string lowercase_ascii(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// The scheme of a URL reference, lowercased, or empty for a relative one.
std::string url_scheme(std::string_view target) {
    const auto colon = target.find(':');
    if (colon == std::string_view::npos || colon == 0) return {};
    if (target.find_first_of("/?#") < colon) return {};
    for (std::size_t i = 0; i < colon; ++i) {
        const char c = target[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter && (i == 0 || !((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'))) return {};
    }
    return lowercase_ascii(std::string(target.substr(0, colon)));
}

// A local file named by a relative reference, inside the source root. The
// query and fragment are returned in `suffix` and are not part of the file.
fs::path local_reference(std::string_view target, const fs::path& directory, const fs::path& root, const Span& span,
                         std::string& suffix) {
    const auto end = target.find_first_of("?#");
    suffix = end == std::string_view::npos ? std::string{} : std::string(target.substr(end));
    const auto written = target.substr(0, end == std::string_view::npos ? target.size() : end);
    if (written.empty()) style_reference_error(span, "this reference names no file");
    if (written.front() == '/' || written.find('\\') != std::string_view::npos)
        style_reference_error(span, "write a path relative to this file, using '/' separators");
    std::error_code error;
    const auto normalized = fs::weakly_canonical(directory / utf8_path(written), error);
    if (error) style_reference_error(span, "cannot resolve this path");
    const auto within = relative_to_root(normalized, root);
    if (within.empty() || *within.begin() == "..")
        style_reference_error(span, "this path leaves the source root " + path_name(root) + '/');
    return normalized;
}

// The extension of a bundled asset: the file's own, lowercased, when it is
// short and alphanumeric; otherwise none.
std::string asset_extension(const fs::path& file) {
    auto extension = lowercase_ascii(path_name(file.extension()));
    if (extension.size() < 2 || extension.size() > 11) return {};
    for (std::size_t i = 1; i < extension.size(); ++i) {
        const char c = extension[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return {};
    }
    return extension;
}

BundleFileSyntax read_asset(const fs::path& file, const Span& span) {
    constexpr std::size_t maximum = 64u * 1024 * 1024; // W-1
    bool too_large = false;
    auto bytes = read_input_file(file, maximum, too_large);
    if (!bytes) style_reference_error(span, "no file exists at this path");
    if (too_large) style_reference_error(span, "the file exceeds the bundle file limit W-1 of 64 MiB");
    auto digest = util::sha256_hex(*bytes);
    return {"assets/" + digest + asset_extension(file), std::move(digest), std::move(*bytes)};
}

void add_bundle_file(std::vector<BundleFileSyntax>& files, BundleFileSyntax file) {
    if (std::ranges::none_of(files, [&](const BundleFileSyntax& item) { return item.path == file.path; }))
        files.push_back(std::move(file));
}

std::pair<BundleFileSyntax, std::vector<BundleFileSyntax>> import_stylesheet(const fs::path& file, const Span& span,
                                                                            SourceDependencies& dependencies);

// Resolve every url(...) and @import of a stylesheet whose text came from a
// file in `directory`.
void resolve_style_references(ResolvedStylesheet& sheet, const fs::path& directory, SourceDependencies& dependencies) {
    sheet.targets.assign(sheet.body.references.size(), {});
    for (std::size_t i = 0; i < sheet.body.references.size(); ++i) {
        const auto& reference = sheet.body.references[i];
        const auto& target = reference.target;
        const auto scheme = url_scheme(target);
        const bool import = reference.kind == CssReference::Kind::Import;
        if (!import && (target.empty() || target.front() == '#' || scheme == "data")) continue;
        if (!scheme.empty() || target.starts_with("//"))
            style_reference_error(reference.span, import
                ? "a remote stylesheet needs its origin listed under 'style' in a project manifest"
                : "a remote resource needs its origin listed under 'style' in a project manifest");
        std::string suffix;
        const auto local = local_reference(target, directory, dependencies.root, reference.span, suffix);
        if (import) {
            auto [imported, files] = import_stylesheet(local, reference.span, dependencies);
            sheet.targets[i] = imported.path.substr(std::string_view("styles/").size()) + suffix;
            for (auto& item : files) add_bundle_file(sheet.files, std::move(item));
            add_bundle_file(sheet.files, std::move(imported));
        } else {
            auto asset = read_asset(local, reference.span);
            sheet.targets[i] = "../" + asset.path + suffix;
            add_bundle_file(sheet.files, std::move(asset));
        }
    }
}

std::pair<BundleFileSyntax, std::vector<BundleFileSyntax>> import_stylesheet(const fs::path& file, const Span& span,
                                                                            SourceDependencies& dependencies) {
    if (const auto found = std::ranges::find(dependencies.active_styles, file);
        found != dependencies.active_styles.end()) {
        std::string chain;
        for (auto it = found; it != dependencies.active_styles.end(); ++it)
            chain += path_name(it->lexically_relative(dependencies.root)) + " -> ";
        chain += path_name(file.lexically_relative(dependencies.root));
        style_reference_error(span, "these stylesheets import each other: " + chain);
    }
    if (const auto cached = dependencies.imported_styles.find(file); cached != dependencies.imported_styles.end())
        return cached->second;
    constexpr std::size_t maximum = 1024 * 1024;
    bool too_large = false;
    auto source = read_input_file(file, maximum, too_large, true);
    if (!source) style_reference_error(span, "no stylesheet exists at this path");
    if (too_large) style_reference_error(span, "the stylesheet exceeds 1 MiB");
    if (!nvr::valid_utf8(*source)) style_reference_error(span, "the stylesheet must contain valid UTF-8");
    const auto name = path_name(file);
    SourceMap::global().add(name, *source, SourceKind::User);
    const auto bom = source->starts_with("\xEF\xBB\xBF") ? 3u : 0u;
    ResolvedStylesheet sheet;
    sheet.text = source->substr(bom);
    sheet.body = parse_stylesheet(sheet.text, {name, 1, 1, bom, sheet.text.size()}, false);
    sheet.path = path_name(relative_to_root(file, dependencies.root));
    sheet.external = true;
    dependencies.active_styles.push_back(file);
    resolve_style_references(sheet, file.parent_path(), dependencies);
    dependencies.active_styles.pop_back();
    auto text = rewrite_stylesheet(sheet.text, sheet.body, {}, {}, sheet.targets).text;
    auto digest = util::sha256_hex(text);
    std::pair<BundleFileSyntax, std::vector<BundleFileSyntax>> result{
        {"styles/" + digest + ".css", std::move(digest), std::move(text)}, std::move(sheet.files)};
    dependencies.imported_styles.emplace(file, result);
    return result;
}

void resolve_stylesheet(StylesheetSyntax& syntax, SourceDependencies& dependencies) {
    ResolvedStylesheet sheet;
    fs::path directory;
    if (syntax.source.kind == TokenKind::CssIsland) {
        if (!syntax.body) return;
        sheet.text = syntax.source.text;
        sheet.body = *syntax.body;
        sheet.external = false;
        const auto declaring = utf8_path(syntax.source.span.file);
        directory = declaring.parent_path();
        sheet.path = path_name(relative_to_root(declaring, dependencies.root));
    } else {
        const auto& path = syntax.source;
        const auto& root = dependencies.root;
        constexpr std::size_t maximum = 1024 * 1024;
        const auto written = decode_string_literal(path.text);
        const auto relative = utf8_path(written);
        if (relative.empty() || relative.has_root_path() || written.find(':') != std::string::npos ||
            written.find('\\') != std::string::npos)
            template_file_error(path, root, "write a relative path using '/' separators");
        std::error_code error;
        const auto normalized = fs::weakly_canonical(utf8_path(path.span.file).parent_path() / relative, error);
        if (error) template_file_error(path, root, "cannot resolve this path");
        const auto within = relative_to_root(normalized, root);
        if (within.empty() || *within.begin() == "..") template_file_error(path, root, "this path leaves the source root");
        bool too_large = false;
        auto source = read_input_file(normalized, maximum, too_large, true);
        if (!source) template_file_error(path, root, "no stylesheet file exists at this path");
        if (too_large) template_file_error(path, root, "the stylesheet exceeds 1 MiB");
        if (!nvr::valid_utf8(*source)) template_file_error(path, root, "the stylesheet must contain valid UTF-8");
        const auto name = path_name(normalized);
        SourceMap::global().add(name, *source, SourceKind::User);
        const auto bom = source->starts_with("\xEF\xBB\xBF") ? 3u : 0u;
        sheet.text = source->substr(bom);
        sheet.body = parse_stylesheet(sheet.text, {name, 1, 1, bom, sheet.text.size()}, syntax.module);
        sheet.external = true;
        sheet.path = path_name(within);
        directory = normalized.parent_path();
        dependencies.active_styles.push_back(normalized);
    }
    resolve_style_references(sheet, directory, dependencies);
    if (sheet.external) dependencies.active_styles.pop_back();
    syntax.resolved = std::move(sheet);
}

bool media_source(const TemplateElement& element, const TemplateAttribute& attribute) {
    if (attribute.kind != TemplateAttribute::Kind::Attribute) return false;
    // An SVG <use> names a symbol, in this document or in a file.
    if (element.svg) return attribute.name == "href" && element.name == "use";
    if (attribute.name == "poster") return element.name == "video";
    if (attribute.name == "srcset") return element.name == "img" || element.name == "source";
    return attribute.name == "src" &&
           (element.name == "img" || element.name == "source" || element.name == "video" ||
            element.name == "audio" || element.name == "track");
}

// A media reference names a local file unless it is empty, a fragment, a
// root-relative or protocol-relative path, or has a scheme.
bool local_media(std::string_view target) {
    return !target.empty() && target.front() != '#' && target.front() != '/' && url_scheme(target).empty();
}

// The value a template carries for a bundled file: its path in the bundle,
// which the page resolves against the bundle's base (decision 24).
std::string bundled_reference(const BundleFileSyntax& asset, std::string_view suffix) {
    return asset.path + std::string(suffix);
}

// A static media reference that names local files: each file is hashed into
// the bundle and the value rewritten to name it there.
void resolve_media(const TemplateElement& element, TemplateAttribute& attribute, SourceDependencies& dependencies) {
    const auto* value = std::get_if<std::string>(&attribute.value);
    if (!value || !attribute.value_span || !media_source(element, attribute)) return;
    const auto& span = *attribute.value_span;
    std::vector<std::pair<std::size_t, std::size_t>> urls;
    if (attribute.name == "srcset") urls = srcset_urls(*value);
    else urls.emplace_back(0, value->size());
    std::string rewritten;
    std::size_t copied = 0;
    try {
        for (const auto& [begin, end] : urls) {
            const auto target = std::string_view(*value).substr(begin, end - begin);
            if (!local_media(target)) continue;
            std::string suffix;
            const auto local = local_reference(target, utf8_path(span.file).parent_path(), dependencies.root, span, suffix);
            auto asset = std::make_shared<BundleFileSyntax>(read_asset(local, span));
            rewritten += value->substr(copied, begin - copied);
            rewritten += bundled_reference(*asset, suffix);
            copied = end;
            if (std::ranges::none_of(attribute.assets, [&](const auto& item) { return item->path == asset->path; }))
                attribute.assets.push_back(std::move(asset));
        }
    } catch (const Diagnostic& error) {
        attribute.assets.clear();
        attribute.asset_error = std::make_shared<Diagnostic>(error);
        return;
    }
    if (copied == 0 && attribute.assets.empty()) return;
    rewritten += value->substr(copied);
    attribute.asset_target = std::move(rewritten);
}

void collect_template_imports(std::vector<TemplateNode>& nodes, SourceDependencies& dependencies) {
    for (const auto& node : nodes) {
        if (const auto* hole = std::get_if<TemplateHole>(&node.value)) {
            collect_imports(hole->expression, dependencies);
        } else if (const auto* element = std::get_if<std::shared_ptr<TemplateElement>>(&node.value)) {
            for (auto& attribute : (*element)->attributes) {
                if (const auto* value = std::get_if<Expr>(&attribute.value)) collect_imports(*value, dependencies);
                resolve_media(**element, attribute, dependencies);
            }
            collect_template_imports((*element)->children, dependencies);
        } else if (const auto* choice = std::get_if<std::shared_ptr<TemplateIf>>(&node.value)) {
            for (auto& branch : (*choice)->branches) {
                if (branch.condition) collect_imports(*branch.condition, dependencies);
                collect_template_imports(branch.children, dependencies);
            }
        } else if (const auto* loop = std::get_if<std::shared_ptr<TemplateFor>>(&node.value)) {
            collect_imports((*loop)->head.input, dependencies);
            collect_imports((*loop)->head.key, dependencies);
            collect_template_imports((*loop)->children, dependencies);
        }
    }
}

void collect_imports(const Expr& expression, SourceDependencies& dependencies) {
    if (expression.kind == Expr::Kind::Import) {
        dependencies.imports.push_back(&expression);
    }
    for (const auto& operand : expression.operands) {
        collect_imports(operand, dependencies);
    }
    if (expression.lambda) {
        collect_imports(*expression.lambda, dependencies);
    }
    if (expression.match) collect_imports(*expression.match, dependencies);
    if (expression.html_factory) {
        auto& factory = *expression.html_factory;
        if (auto* body = std::get_if<TemplateSyntax>(&factory.source)) {
            collect_template_imports(body->nodes, dependencies);
        } else {
            const auto& path = std::get<Token>(factory.source);
            factory.external = read_template(path, dependencies.root);
            const auto actual = utf8_path(factory.external->body.span.file);
            if (!dependencies.active_templates.insert(actual).second)
                template_file_error(path, dependencies.root, "this template is already being loaded");
            collect_template_imports(factory.external->body.nodes, dependencies);
            dependencies.active_templates.erase(actual);
        }
        for (const auto& alternative : factory.overloads) collect_imports(alternative, dependencies);
    }
    if (expression.stylesheet) resolve_stylesheet(*expression.stylesheet, dependencies);
}

void collect_imports(const Declaration& declaration, SourceDependencies& dependencies) {
    if (declaration.initializer) {
        collect_imports(*declaration.initializer, dependencies);
    }
    for (const auto& member : declaration.members) {
        collect_imports(member, dependencies);
    }
    for (const auto& overload : declaration.overloads) {
        collect_imports(overload, dependencies);
    }
    for (const auto& statement : declaration.body) {
        collect_imports(statement, dependencies);
    }
}

void collect_imports(const Statement& statement, SourceDependencies& dependencies) {
    if (statement.declaration) {
        collect_imports(*statement.declaration, dependencies);
    }
    for (const auto& expression : statement.expressions) {
        collect_imports(expression, dependencies);
    }
    for (const auto& child : statement.body) {
        collect_imports(child, dependencies);
    }
    for (const auto& child : statement.alternative) {
        collect_imports(child, dependencies);
    }
    for (const auto& arm : statement.arms) {
        for (const auto& child : arm.body) collect_imports(child, dependencies);
        if (arm.value) collect_imports(*arm.value, dependencies);
    }
}

bool std_namespace(std::string_view name) {
    return name == "Std" || name.starts_with("Std.");
}

std::string library_path(const fs::path& path, const fs::path& library) {
    if (library.empty()) return {};
    const auto relative = path.lexically_relative(library);
    if (relative.empty() || *relative.begin() != "std") return {};
    return path_name(relative);
}

bool internal_module(std::string_view name) {
    return name.ends_with(".Internal") || name.find(".Internal.") != std::string_view::npos;
}

// The names of the public standard library modules, from the files under
// `library`: std/io/utf8-reader.nra is Std.Io.Utf8Reader. A file whose name
// does not map back to its path (module_path) is left out.
std::vector<std::string> library_modules(const fs::path& library) {
    std::vector<std::string> result;
    std::error_code error;
    const auto root = library / "std";
    if (library.empty() || !fs::is_directory(root, error)) return result;
    const auto pascal = [](std::string_view text) {
        std::string name;
        bool upper = true;
        for (const char c : text) {
            if (c == '-') {
                upper = true;
                continue;
            }
            name += upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
            upper = false;
        }
        return name;
    };
    for (auto it = fs::recursive_directory_iterator(root, error); !error && it != fs::recursive_directory_iterator();
         it.increment(error)) {
        if (!it->is_regular_file(error) || it->path().extension() != ".nra") continue;
        const auto relative = it->path().lexically_relative(library);
        std::string name;
        for (auto part = relative.begin(); part != relative.end(); ++part) {
            auto text = part->string();
            if (std::next(part) == relative.end()) text = part->stem().string();
            name += (name.empty() ? "" : ".") + pascal(text);
        }
        if (!internal_module(name) && module_path(name) == relative) result.push_back(std::move(name));
    }
    std::ranges::sort(result);
    return result;
}

fs::path find_module(const Expr& import, const std::vector<fs::path>& roots, const fs::path& library,
                     bool from_library, bool artifact = false) {
    auto relative = module_path(import.text);
    if (artifact) relative.replace_extension(".nir");
    if (std_namespace(import.text)) {
        if (internal_module(import.text) && !from_library) {
            throw Diagnostic(import.span, "cannot import internal standard library module '" + import.text + "'")
                .with_code("E0702");
        }
        const auto path = library / relative;
        if (!library.empty() && fs::is_regular_file(path)) {
            const auto actual = fs::canonical(path);
            // Resolve within the trusted tree without allowing an alias
            // to bypass an Internal component of the actual module path.
            if (library_path(actual, library) == path_name(relative)) return actual;
        }
        auto error = Diagnostic(import.span, "cannot resolve standard library import '" + import.text + "'")
            .with_code("E0701").label("no standard library module has this name");
        if (const auto close = closest_name(import.text, library_modules(library))) {
            const auto at = import.span.offset + import.span.length - import.text.size();
            const auto span = import.span.length >= import.text.size()
                ? source_span(import.span.file, at, import.text.size()) : std::nullopt;
            if (span && source_text(*span) == import.text)
                error.suggest("there is a module with a similar name: '" + *close + "'", *span, *close,
                              Applicability::MaybeIncorrect);
            else
                error.help("there is a module with a similar name: '" + *close + "'");
        }
        throw error;
    }
    std::set<fs::path> candidates;
    auto search = [&](const fs::path& root) {
        const auto path = root / relative;
        if (source_exists(path)) {
            const auto actual = source_canonical(path);
            if (library_path(actual, library).empty()) {
                candidates.insert(actual);
            }
        }
    };
    search(utf8_path(import.span.file).parent_path());
    for (const auto& root : roots) {
        search(root);
    }
    if (candidates.size() > 1) {
        throw Diagnostic(import.span, "ambiguous import '" + import.text + "'").with_code("E0703");
    }
    return candidates.empty() ? fs::path{} : *candidates.begin();
}

fs::path find_artifact(const Expr& import, const std::vector<fs::path>& roots) {
    const auto path = utf8_path(*import.import_path);
    if (path.extension() != ".nir")
        throw Diagnostic(import.span, "an artifact import needs a .nir file").with_code("E0706");
    std::set<fs::path> candidates;
    const auto search = [&](const fs::path& root) {
        if (fs::is_regular_file(root / path)) candidates.insert(fs::canonical(root / path));
    };
    search(utf8_path(import.span.file).parent_path());
    if (path.is_relative()) {
        for (const auto& root : roots) search(root);
    }
    if (candidates.empty())
        throw Diagnostic(import.span, "cannot find NeuraIR artifact '" + *import.import_path + "'").with_code("E0701");
    if (candidates.size() > 1)
        throw Diagnostic(import.span, "ambiguous NeuraIR artifact '" + *import.import_path + "'").with_code("E0703");
    return *candidates.begin();
}

std::vector<fs::path> find_wildcard_modules(const Expr& import, const std::vector<fs::path>& roots,
                                           const fs::path& library, bool from_library) {
    const auto prefix = import.text.substr(0, import.text.size() - 2);
    const auto standard = std_namespace(prefix);
    auto relative = module_path(prefix);
    relative.replace_extension();
    std::map<fs::path, fs::path> candidates;
    auto search = [&](const fs::path& root) {
        const auto directory = root / relative;
        std::set<fs::path> entries;
        if (fs::is_directory(directory)) {
            for (const auto& item : fs::directory_iterator(directory))
                if (item.is_regular_file() && source_exists(item.path())) entries.insert(source_canonical(item.path()));
        }
        if (source_overlays) {
            const auto actual_directory = fs::weakly_canonical(fs::absolute(directory));
            for (const auto& [name, source] : *source_overlays) {
                const auto candidate = utf8_path(name);
                if (candidate.parent_path() == actual_directory && source_exists(candidate)) entries.insert(candidate);
            }
        }
        for (const auto& actual : entries) {
            const auto filename = actual.filename();
            if (filename.extension() != ".nra") continue;
            const auto trusted_path = library_path(actual, library);
            if (standard) {
                if (trusted_path != path_name(relative / filename) ||
                    (!from_library && filename == "internal.nra")) continue;
            } else if (!trusted_path.empty()) {
                continue;
            }
            const auto [previous, added] = candidates.emplace(filename, actual);
            if (!added && previous->second != actual)
                throw Diagnostic(import.span, "ambiguous wildcard import '" + import.text + "'").with_code("E0703");
        }
    };
    if (standard) {
        if (internal_module(prefix) && !from_library)
            throw Diagnostic(import.span, "cannot import internal standard library module '" + prefix + "'")
                .with_code("E0702");
        if (!library.empty()) search(library);
    } else {
        search(utf8_path(import.span.file).parent_path());
        for (const auto& root : roots) search(root);
    }
    std::vector<fs::path> modules;
    for (const auto& [filename, actual] : candidates) modules.push_back(actual);
    if (modules.empty())
        throw Diagnostic(import.span, "cannot resolve wildcard import '" + import.text + "'").with_code("E0701");
    return modules;
}

struct ArtifactInput {
    fs::path path;
    Span span;
    IrProgram program;
    std::uint16_t minor_version{};
    std::map<std::string, Span> selectors{};
};

ArtifactInput read_artifact(const fs::path& path, const Span& span) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Diagnostic(span, "cannot read NeuraIR artifact '" + path_name(path) + "'")
        .with_code("E0706");
    const std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
    if (input.bad()) throw Diagnostic(span, "error reading NeuraIR artifact '" + path_name(path) + "'")
        .with_code("E0706");
    try {
        ArtifactInput artifact{path, span, {}};
        artifact.program = read_nir(bytes, path_name(path), &artifact.minor_version);
        return artifact;
    } catch (const NirError& error) {
        throw Diagnostic(span, error.what()).with_code("E0706");
    }
}

std::vector<ParsedFile> read_sources_impl(const std::vector<fs::path>& files, const std::vector<fs::path>& roots,
                                          const fs::path& library, std::vector<ArtifactInput>* artifacts,
                                          CImports* c_imports = nullptr) {
    const auto trusted = library.empty() ? fs::path{} : fs::canonical(library);
    std::vector<fs::path> pending;
    for (const auto& file : files) pending.push_back(source_canonical(file));
    std::set<fs::path> visited;
    std::vector<ParsedFile> parsed;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto file = pending[i];
        if (!visited.insert(file).second) {
            continue;
        }
        std::string source;
        const auto name = path_name(file);
        const auto in_library = !library_path(file, trusted).empty();
        if (c_imports && c_imports->sources.contains(file)) {
            source = c_imports->sources.at(file);
            SourceMap::global().add(name, source, SourceKind::Generated, c_imports->headers[file]);
        } else if (source_overlays && source_overlays->contains(name)) {
            source = source_overlays->at(name);
            SourceMap::global().add(name, source, in_library ? SourceKind::Library : SourceKind::User);
        } else {
            if (!fs::is_regular_file(file)) {
                throw std::runtime_error("not a regular input file: " + path_name(file));
            }
            std::ifstream input(file, std::ios::binary);
            if (!input) {
                throw std::runtime_error("cannot read " + path_name(file));
            }
            source.assign(std::istreambuf_iterator<char>{input}, {});
            if (input.bad()) {
                throw std::runtime_error("error reading " + path_name(file));
            }
            source = lf_source(std::move(source));
            SourceMap::global().add(name, source, in_library ? SourceKind::Library : SourceKind::User);
        }
        auto syntax = parse(lex(source, name));
        syntax.library_path = library_path(file, trusted);
        SourceDependencies dependencies{pending.front().parent_path(), {}, {}};
        for (const auto& statement : syntax.statements) {
            collect_imports(statement, dependencies);
        }
        std::set<std::pair<std::string, std::string>> seen_imports;
        if (syntax.untyped_variadic) {
            const Expr dependency{Expr::Kind::Import, *syntax.untyped_variadic,
                                  "Std.Core.Variadic", {}, {}, std::nullopt};
            auto path = find_module(dependency, roots, trusted, !syntax.library_path.empty());
            syntax.imports.push_back({dependency.text, path_name(path)});
            seen_imports.emplace(dependency.text, std::string{});
            pending.push_back(std::move(path));
        }
        for (const auto* import : dependencies.imports) {
            const auto requested_path = import->import_path.value_or("");
            if (!seen_imports.emplace(import->text, requested_path).second) {
                continue;
            }
            if (import->text.ends_with(".*")) {
                for (auto& dependency : find_wildcard_modules(*import, roots, trusted, !syntax.library_path.empty())) {
                    syntax.imports.push_back({import->text, path_name(dependency)});
                    pending.push_back(std::move(dependency));
                }
                continue;
            }
            const bool from_library = !syntax.library_path.empty();
            if (!import->c_import.empty()) {
                if (source_overlays && !overlay_c_imports)
                    throw Diagnostic(import->span, "C header analysis requires trusted workspace configuration").with_code("E0708");
                if (from_library || !c_imports)
                    throw Diagnostic(import->span, "a C import is not available here").with_code("E0708");
                const auto header = find_c_header(import->c_import.front(), file.parent_path(), roots, import->span);
                std::vector<std::string> libraries;
                for (std::size_t l = 1; l < import->c_import.size(); ++l) {
                    const auto path = file.parent_path() / utf8_path(import->c_import[l]);
                    if (!fs::is_regular_file(path))
                        throw Diagnostic(import->span, "cannot find C library '" + import->c_import[l] + "'")
                            .with_code("E0709");
                    const auto extension = path_name(path.extension());
                    if (extension != ".a" && extension != ".o" && extension != ".lib" && extension != ".obj")
                        throw Diagnostic(import->span, "a C import links static libraries (.a, .lib) and object "
                                                       "files (.o, .obj); load shared libraries with "
                                                       "Std.DynamicForeign.SymbolLookup").with_code("E0708");
                    libraries.push_back(path_name(fs::canonical(path)));
                }
                const auto module = import->text.substr(2);
                auto generated = header;
                generated += "." + module + ".nra";
                if (!c_imports->sources.contains(generated)) {
                    auto imported = import_c_header(header, !libraries.empty(), module, c_import_options, import->span);
                    c_imports->sources.emplace(generated, std::move(imported.source));
                    c_imports->headers.emplace(generated, path_name(header));
                    c_imports->units.push_back({path_name(header), std::move(libraries), std::move(imported.shim),
                                                std::move(imported.symbols)});
                    c_imports->unsupported[module] = std::move(imported.unsupported);
                } else if (!libraries.empty()) {
                    auto& unit = *std::ranges::find(c_imports->units, path_name(header), &IrNativeUnit::header);
                    for (auto& added : libraries)
                        if (std::ranges::find(unit.libraries, added) == unit.libraries.end())
                            unit.libraries.push_back(std::move(added));
                }
                syntax.imports.push_back({import->text, path_name(generated)});
                pending.push_back(generated);
                continue;
            }
            bool artifact = import->import_path.has_value();
            auto dependency = artifact ? find_artifact(*import, roots)
                                       : find_module(*import, roots, trusted, from_library);
            if (!artifact && dependency.empty() && !from_library && !std_namespace(import->text)) {
                dependency = find_module(*import, roots, trusted, false, true);
                artifact = !dependency.empty();
            }
            if (dependency.empty()) continue;
            if (artifact) {
                if (!artifacts || from_library)
                    throw Diagnostic(import->span, "this source load cannot import a NeuraIR artifact")
                        .with_code("E0706");
                auto found = std::ranges::find(*artifacts, dependency, &ArtifactInput::path);
                if (found == artifacts->end()) {
                    artifacts->push_back(read_artifact(dependency, import->span));
                    found = artifacts->end() - 1;
                }
                auto selector = import->import_path ? import->text
                    : import->text.substr(import->text.find_last_of('.') + 1);
                found->selectors.try_emplace(selector, import->span);
                syntax.imports.push_back({import->text, path_name(dependency), std::move(selector), true,
                                          requested_path});
            } else {
                syntax.imports.push_back({import->text, path_name(dependency)});
                pending.push_back(std::move(dependency));
            }
        }
        parsed.push_back(std::move(syntax));
    }
    std::map<std::string_view, const ParsedFile*, std::less<>> by_path;
    for (const auto& source : parsed) by_path.emplace(source.file, &source);
    for (auto& source : parsed) {
        for (auto& import : source.imports) {
            if (!import.name.ends_with(".*")) continue;
            const auto filename = utf8_path(import.file).filename();
            for (const auto& statement : by_path.at(import.file)->statements) {
                const auto& decl = statement.declaration;
                if (decl && decl->kind == Declaration::Kind::Module && module_path(decl->name) == filename) {
                    import.target_module = decl->name;
                    break;
                }
            }
        }
    }
    return parsed;
}

SymbolId artifact_module(const ArtifactInput& artifact, std::string_view selector, const Span& span) {
    std::vector<SymbolId> candidates;
    std::vector<SymbolId> matching;
    const auto& symbols = artifact.program.symbols;
    for (SymbolId id = 0; id < symbols.size(); ++id) {
        const auto& symbol = symbols[id];
        if (symbol.kind != SymbolKind::Module || symbol.owner == no_symbol ||
            symbols[symbol.owner].kind != SymbolKind::File ||
            !symbols[symbol.owner].library_path.empty() || std_namespace(symbol.name)) continue;
        candidates.push_back(id);
        auto filename = module_path(symbol.name).filename();
        filename.replace_extension(".nir");
        if (selector.empty() ? filename == artifact.path.filename() : symbol.name == selector)
            matching.push_back(id);
    }
    if (matching.size() == 1) return matching.front();
    if (matching.empty() && selector.empty() && candidates.size() == 1) return candidates.front();
    const auto path = path_name(artifact.path);
    if (!selector.empty() && matching.empty())
        throw Diagnostic(span, "artifact '" + path + "' does not declare module '" + std::string(selector) + "'")
            .with_code("E0705");
    if (selector.empty() && candidates.empty())
        throw Diagnostic(span, "artifact '" + path + "' has no project module to import").with_code("E0705");
    throw Diagnostic(span, "ambiguous module in artifact '" + path + "'").with_code("E0703");
}

IrProgram lower_with_artifacts(std::vector<ParsedFile> parsed, std::vector<ArtifactInput> artifacts,
                                const fs::path& library) {
    // Rebuild embedded standard library files from the trusted tree. Linker
    // comparisons make sure an artifact cannot replace the installed definitions.
    std::vector<fs::path> trusted_files;
    std::set<fs::path> trusted_paths;
    const auto add_trusted = [&](const std::string& relative, const Span& span) {
        const auto name = utf8_path(relative);
        if (!relative.starts_with("std/") || name.extension() != ".nra" ||
            path_name(name.lexically_normal()) != relative)
            throw Diagnostic(span, "invalid standard library identity '" + relative + "' in NeuraIR artifact")
                .with_code("E0706");
        if (library.empty() || !fs::is_regular_file(library / name))
            throw Diagnostic(span, "artifact needs installed standard library file '" + relative + "'")
                .with_code("E0707");
        const auto actual = fs::canonical(library / name);
        if (library_path(actual, library) != relative)
            throw Diagnostic(span, "invalid standard library identity '" + relative + "' in NeuraIR artifact")
                .with_code("E0706");
        if (trusted_paths.insert(actual).second) trusted_files.push_back(actual);
    };
    for (const auto& source : parsed) {
        if (!source.library_path.empty()) add_trusted(source.library_path, Span{source.file});
    }
    for (const auto& artifact : artifacts) {
        for (const auto& symbol : artifact.program.symbols) {
            if (symbol.kind == SymbolKind::File && !symbol.library_path.empty())
                add_trusted(symbol.library_path, artifact.span);
        }
    }
    LinkedImports linked;
    if (!trusted_files.empty()) linked.program = lower_sources(trusted_files, {}, library);
    const auto has_umbrella = [](const IrProgram& program) {
        return std::ranges::any_of(program.symbols, [](const IrSymbol& symbol) {
            return symbol.kind == SymbolKind::Class && symbol.owner == no_symbol && symbol.name == "$class";
        });
    };
    if (!has_umbrella(linked.program)) {
        const auto artifact = std::ranges::find_if(artifacts, [&](const ArtifactInput& input) {
            return has_umbrella(input.program);
        });
        if (artifact != artifacts.end()) link_ir(linked.program, builtin_class_ir(), artifact->span);
    }
    std::set<std::string> references;
    for (const auto& artifact : artifacts) {
        for (const auto& symbol : artifact.program.symbols) {
            if (symbol.kind == SymbolKind::Class && symbol.type.kind == TypeKind::Object &&
                symbol.type.name.starts_with('&')) references.insert(symbol.type.name.substr(1));
        }
    }
    for (const auto& name : references) {
        const auto& span = artifacts.front().span;
        link_ir(linked.program, builtin_reference_ir(name, span), span);
    }
    for (auto& artifact : artifacts) {
        const auto path = path_name(artifact.path);
        std::map<std::string, SymbolId> selectors;
        for (const auto& [selector, site] : artifact.selectors)
            selectors.emplace(selector, artifact_module(artifact, selector, site));
        const auto remap = link_ir(linked.program, std::move(artifact.program), artifact.span,
                                   artifact.minor_version < 6);
        for (const auto mapped : remap) {
            if (mapped == no_symbol) continue;
            const auto& item = linked.program.symbols[mapped];
            if (item.kind != SymbolKind::Module || item.owner == no_symbol) continue;
            const auto& owner = linked.program.symbols[item.owner];
            if (owner.kind == SymbolKind::File && owner.library_path.empty())
                linked.artifact_origins.try_emplace(mapped, path);
        }
        for (const auto& [selector, id] : selectors)
            linked.modules.emplace(std::pair{path, selector}, remap[id]);
    }
    std::set<std::string_view> embedded;
    for (const auto& symbol : linked.program.symbols) {
        if (symbol.kind == SymbolKind::File && symbol.library_path.empty()) embedded.insert(symbol.name);
    }
    for (const auto& source : parsed) {
        if (source.library_path.empty() && embedded.contains(source.file))
            throw Diagnostic(Span{source.file}, "source is also included in an imported NeuraIR artifact")
                .with_code("E0707");
    }
    std::erase_if(parsed, [](const ParsedFile& source) { return !source.library_path.empty(); });
    return lower(parsed, std::move(linked));
}

} // namespace

SourceOverlayScope::SourceOverlayScope(const std::map<std::string, std::string, std::less<>>& sources,
                                       bool allow_c_imports, const std::set<std::string>* hidden_sources)
    : previous_(source_overlays), previous_c_imports_(overlay_c_imports), previous_hidden_(overlay_hidden_sources) {
    source_overlays = &sources;
    overlay_c_imports = allow_c_imports;
    overlay_hidden_sources = hidden_sources;
}
SourceOverlayScope::~SourceOverlayScope() {
    source_overlays = previous_;
    overlay_c_imports = previous_c_imports_;
    overlay_hidden_sources = previous_hidden_;
}

std::vector<ParsedFile> read_sources(const std::vector<fs::path>& files,
                                     const std::vector<fs::path>& roots, const fs::path& library) {
    return read_sources_impl(files, roots, library, nullptr);
}

void set_c_import_options(CImportOptions options) { c_import_options = std::move(options); }

std::string c_import_source(const fs::path& header, const std::vector<fs::path>& roots, bool linked) {
    const auto found = find_c_header(path_name(header), fs::current_path(), roots, Span{path_name(header)});
    std::string stem = path_name(found.stem());
    std::string name;
    bool upper = true;
    for (const char c : stem) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !digit) {
            upper = true;
            continue;
        }
        if (name.empty() && digit) name += 'H';
        name += upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        upper = false;
    }
    if (name.empty()) name = "Header";
    auto imported = import_c_header(found, linked, name, c_import_options, Span{path_name(found)});
    if (!imported.shim.empty()) {
        imported.source += "\n// Native wrappers compiled into the image's library:\n";
        std::istringstream lines(imported.shim);
        for (std::string line; std::getline(lines, line);) imported.source += "// " + line + "\n";
    }
    return imported.source;
}

namespace {

// One load and lowering of `inputs`. A use of a C declaration that the
// importer could not map gets a note that names the reason.
IrProgram lower_once(const std::vector<fs::path>& inputs, const std::vector<fs::path>& roots,
                     const fs::path& trusted) {
    CImports c_imports;
    try {
        std::vector<ArtifactInput> artifacts;
        auto parsed = read_sources_impl(inputs, roots, trusted, &artifacts, &c_imports);
        auto program = artifacts.empty() ? lower(parsed)
                                         : lower_with_artifacts(std::move(parsed), std::move(artifacts), trusted);
        for (auto& unit : c_imports.units) program.native_units.push_back(std::move(unit));
        return program;
    } catch (const MissingPrimitiveWrapper&) {
        throw;
    } catch (const Diagnostic& error) {
        // Lowering records what it could not resolve: the kind of
        // reference, the name, and the namespace it was looked up in.
        const auto* kind = error.argument("kind");
        const auto* name = error.argument("name");
        const auto* owner = error.argument("owner");
        if (!kind || !name || (*kind != "name" && *kind != "member")) throw;
        const std::string* reason = nullptr;
        if (owner) {
            if (const auto found = c_imports.unsupported.find(*owner); found != c_imports.unsupported.end()) {
                if (const auto at = found->second.find(*name); at != found->second.end()) reason = &at->second;
            }
        }
        // A qualified type name such as m.flags names its module through an
        // import binding, so look in every generated module.
        if (!reason && *kind == "name") {
            for (const auto& [module, reasons] : c_imports.unsupported) {
                if (const auto at = reasons.find(*name); at != reasons.end()) {
                    reason = &at->second;
                    break;
                }
            }
        }
        if (!reason) throw;
        auto explained = error;
        if (!error.data() || error.data()->primary_label.empty()) explained.label("not imported from the C header");
        explained.note("the C declaration '" + *name + "' was not imported: it is " + *reason);
        throw explained;
    }
}

} // namespace

IrProgram lower_sources(const std::vector<fs::path>& files, const std::vector<fs::path>& roots,
                        const fs::path& library) {
    auto inputs = files;
    const auto trusted = library.empty() ? fs::path{} : fs::canonical(library);
    std::set<std::string> wrappers;
    for (;;) {
        // A load that is repeated with another wrapper reports its warnings
        // again, so drop those of the abandoned attempt.
        SpeculativeDiagnostics attempt;
        try {
            auto program = lower_once(inputs, roots, trusted);
            check_storage(program, StorageStage::Lowered);
            attempt.commit();
            return program;
        } catch (const MissingPrimitiveWrapper& missing) {
            const auto retry = [&] {
                if (library.empty() || !wrappers.insert(missing.path).second) return false;
                const auto path = trusted / missing.path;
                if (!fs::is_regular_file(path)) return false;
                auto actual = fs::canonical(path);
                if (library_path(actual, trusted) != missing.path) return false;
                inputs.push_back(std::move(actual));
                return true;
            };
            if (!retry()) {
                attempt.commit();
                throw;
            }
        } catch (...) {
            // Warnings found before an error are reported with it.
            attempt.commit();
            throw;
        }
    }
}

} // namespace neura
