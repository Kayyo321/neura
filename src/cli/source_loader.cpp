#include "source_loader.hpp"

#include "c-import/c_import.hpp"
#include "lexer/lexer.hpp"
#include "module_path.hpp"
#include "neura-ir/ir.hpp"
#include "neura-ir/linker.hpp"
#include "neura-ir/nir_format.hpp"
#include "util/source_map.hpp"

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

CImportOptions c_import_options;

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
    const fs::path name(header);
    std::vector<fs::path> directories{base};
    directories.insert(directories.end(), roots.begin(), roots.end());
    for (const auto& dir : c_import_options.include_dirs) directories.emplace_back(dir);
    for (auto& dir : system_header_directories()) directories.push_back(std::move(dir));
    if (name.is_absolute() && fs::is_regular_file(name)) return fs::canonical(name);
    for (const auto& dir : directories) {
        std::error_code ignored;
        if (fs::is_regular_file(dir / name, ignored)) return fs::canonical(dir / name);
    }
    throw Diagnostic(span, "cannot find C header '" + header + "'").with_code("E0709");
}

void collect_imports(const Declaration& declaration, std::vector<const Expr*>& imports);
void collect_imports(const Statement& statement, std::vector<const Expr*>& imports);

void collect_imports(const Expr& expression, std::vector<const Expr*>& imports) {
    if (expression.kind == Expr::Kind::Import) {
        imports.push_back(&expression);
    }
    for (const auto& operand : expression.operands) {
        collect_imports(operand, imports);
    }
    if (expression.lambda) {
        collect_imports(*expression.lambda, imports);
    }
    if (expression.match) collect_imports(*expression.match, imports);
}

void collect_imports(const Declaration& declaration, std::vector<const Expr*>& imports) {
    if (declaration.initializer) {
        collect_imports(*declaration.initializer, imports);
    }
    for (const auto& member : declaration.members) {
        collect_imports(member, imports);
    }
    for (const auto& overload : declaration.overloads) {
        collect_imports(overload, imports);
    }
    for (const auto& statement : declaration.body) {
        collect_imports(statement, imports);
    }
}

void collect_imports(const Statement& statement, std::vector<const Expr*>& imports) {
    if (statement.declaration) {
        collect_imports(*statement.declaration, imports);
    }
    for (const auto& expression : statement.expressions) {
        collect_imports(expression, imports);
    }
    for (const auto& child : statement.body) {
        collect_imports(child, imports);
    }
    for (const auto& child : statement.alternative) {
        collect_imports(child, imports);
    }
    for (const auto& arm : statement.arms) {
        for (const auto& child : arm.body) collect_imports(child, imports);
        if (arm.value) collect_imports(*arm.value, imports);
    }
}

bool std_namespace(std::string_view name) {
    return name == "Std" || name.starts_with("Std.");
}

std::string library_path(const fs::path& path, const fs::path& library) {
    if (library.empty()) return {};
    const auto relative = path.lexically_relative(library);
    if (relative.empty() || *relative.begin() != "std") return {};
    return relative.generic_string();
}

bool internal_module(std::string_view name) {
    return name.ends_with(".Internal") || name.find(".Internal.") != std::string_view::npos;
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
            if (library_path(actual, library) == relative.generic_string()) return actual;
        }
        throw Diagnostic(import.span, "cannot resolve standard library import '" + import.text + "'")
            .with_code("E0701");
    }
    std::set<fs::path> candidates;
    auto search = [&](const fs::path& root) {
        const auto path = root / relative;
        if (fs::is_regular_file(path)) {
            const auto actual = fs::canonical(path);
            if (library_path(actual, library).empty()) {
                candidates.insert(actual);
            }
        }
    };
    search(fs::path(import.span.file).parent_path());
    for (const auto& root : roots) {
        search(root);
    }
    if (candidates.size() > 1) {
        throw Diagnostic(import.span, "ambiguous import '" + import.text + "'").with_code("E0703");
    }
    return candidates.empty() ? fs::path{} : *candidates.begin();
}

fs::path find_artifact(const Expr& import, const std::vector<fs::path>& roots) {
    const fs::path path(*import.import_path);
    if (path.extension() != ".nir")
        throw Diagnostic(import.span, "an artifact import needs a .nir file").with_code("E0706");
    std::set<fs::path> candidates;
    const auto search = [&](const fs::path& root) {
        if (fs::is_regular_file(root / path)) candidates.insert(fs::canonical(root / path));
    };
    search(fs::path(import.span.file).parent_path());
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
        if (!fs::is_directory(directory)) return;
        for (const auto& item : fs::directory_iterator(directory)) {
            const auto filename = item.path().filename();
            if (filename.extension() != ".nra" || !item.is_regular_file()) continue;
            const auto actual = fs::canonical(item.path());
            const auto trusted_path = library_path(actual, library);
            if (standard) {
                if (trusted_path != (relative / filename).generic_string() ||
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
        search(fs::path(import.span.file).parent_path());
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
    if (!input) throw Diagnostic(span, "cannot read NeuraIR artifact '" + path.generic_string() + "'")
        .with_code("E0706");
    const std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
    if (input.bad()) throw Diagnostic(span, "error reading NeuraIR artifact '" + path.generic_string() + "'")
        .with_code("E0706");
    try {
        ArtifactInput artifact{path, span, {}};
        artifact.program = read_nir(bytes, path.generic_string(), &artifact.minor_version);
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
    for (const auto& file : files) pending.push_back(fs::canonical(file));
    std::set<fs::path> visited;
    std::vector<ParsedFile> parsed;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto file = pending[i];
        if (!visited.insert(file).second) {
            continue;
        }
        std::string source;
        const auto name = file.generic_string();
        const auto in_library = !library_path(file, trusted).empty();
        if (c_imports && c_imports->sources.contains(file)) {
            source = c_imports->sources.at(file);
            SourceMap::global().add(name, source, SourceKind::Generated, c_imports->headers[file]);
        } else {
            if (!fs::is_regular_file(file)) {
                throw std::runtime_error("not a regular input file: " + file.string());
            }
            std::ifstream input(file, std::ios::binary);
            if (!input) {
                throw std::runtime_error("cannot read " + file.string());
            }
            source.assign(std::istreambuf_iterator<char>{input}, {});
            if (input.bad()) {
                throw std::runtime_error("error reading " + file.string());
            }
            SourceMap::global().add(name, source, in_library ? SourceKind::Library : SourceKind::User);
        }
        auto syntax = parse(lex(source, name));
        syntax.library_path = library_path(file, trusted);
        std::vector<const Expr*> imports;
        for (const auto& statement : syntax.statements) {
            collect_imports(statement, imports);
        }
        std::set<std::pair<std::string, std::string>> seen_imports;
        if (syntax.untyped_variadic) {
            const Expr dependency{Expr::Kind::Import, *syntax.untyped_variadic,
                                  "Std.Core.Variadic", {}, {}, std::nullopt};
            auto path = find_module(dependency, roots, trusted, !syntax.library_path.empty());
            syntax.imports.push_back({dependency.text, path.generic_string()});
            seen_imports.emplace(dependency.text, std::string{});
            pending.push_back(std::move(path));
        }
        for (const auto* import : imports) {
            const auto requested_path = import->import_path.value_or("");
            if (!seen_imports.emplace(import->text, requested_path).second) {
                continue;
            }
            if (import->text.ends_with(".*")) {
                for (auto& dependency : find_wildcard_modules(*import, roots, trusted, !syntax.library_path.empty())) {
                    syntax.imports.push_back({import->text, dependency.generic_string()});
                    pending.push_back(std::move(dependency));
                }
                continue;
            }
            const bool from_library = !syntax.library_path.empty();
            if (!import->c_import.empty()) {
                if (from_library || !c_imports)
                    throw Diagnostic(import->span, "a C import is not available here").with_code("E0708");
                const auto header = find_c_header(import->c_import.front(), file.parent_path(), roots, import->span);
                std::vector<std::string> libraries;
                for (std::size_t l = 1; l < import->c_import.size(); ++l) {
                    const auto path = file.parent_path() / import->c_import[l];
                    if (!fs::is_regular_file(path))
                        throw Diagnostic(import->span, "cannot find C library '" + import->c_import[l] + "'")
                            .with_code("E0709");
                    const auto extension = path.extension().string();
                    if (extension != ".a" && extension != ".o" && extension != ".lib" && extension != ".obj")
                        throw Diagnostic(import->span, "a C import links static libraries (.a, .lib) and object "
                                                       "files (.o, .obj); load shared libraries with "
                                                       "Std.DynamicForeign.SymbolLookup").with_code("E0708");
                    libraries.push_back(fs::canonical(path).string());
                }
                const auto module = import->text.substr(2);
                const fs::path generated(header.string() + "." + module + ".nra");
                if (!c_imports->sources.contains(generated)) {
                    auto imported = import_c_header(header, !libraries.empty(), module, c_import_options, import->span);
                    c_imports->sources.emplace(generated, std::move(imported.source));
                    c_imports->headers.emplace(generated, header.string());
                    c_imports->units.push_back({header.string(), std::move(libraries), std::move(imported.shim),
                                                std::move(imported.symbols)});
                    c_imports->unsupported[module] = std::move(imported.unsupported);
                } else if (!libraries.empty()) {
                    auto& unit = *std::ranges::find(c_imports->units, header.string(), &IrNativeUnit::header);
                    for (auto& added : libraries)
                        if (std::ranges::find(unit.libraries, added) == unit.libraries.end())
                            unit.libraries.push_back(std::move(added));
                }
                syntax.imports.push_back({import->text, generated.generic_string()});
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
                syntax.imports.push_back({import->text, dependency.generic_string(), std::move(selector), true,
                                          requested_path});
            } else {
                syntax.imports.push_back({import->text, dependency.generic_string()});
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
            const auto filename = fs::path(import.file).filename();
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
    const auto path = artifact.path.generic_string();
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
        const fs::path name(relative);
        if (!relative.starts_with("std/") || name.extension() != ".nra" ||
            name.lexically_normal().generic_string() != relative)
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
        const auto path = artifact.path.generic_string();
        std::map<std::string, SymbolId> selectors;
        for (const auto& [selector, site] : artifact.selectors)
            selectors.emplace(selector, artifact_module(artifact, selector, site));
        const auto remap = link_ir(linked.program, std::move(artifact.program), artifact.span,
                                   artifact.minor_version < 6);
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

std::vector<ParsedFile> read_sources(const std::vector<fs::path>& files,
                                     const std::vector<fs::path>& roots, const fs::path& library) {
    return read_sources_impl(files, roots, library, nullptr);
}

void set_c_import_options(CImportOptions options) { c_import_options = std::move(options); }

std::string c_import_source(const fs::path& header, const std::vector<fs::path>& roots, bool linked) {
    const auto found = find_c_header(header.string(), fs::current_path(), roots, Span{header.string()});
    std::string stem = found.stem().string();
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
    auto imported = import_c_header(found, linked, name, c_import_options, Span{found.string()});
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
