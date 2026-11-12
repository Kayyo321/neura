#include "refactor.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace neura::tooling {
namespace {

constexpr std::size_t none = static_cast<std::size_t>(-1);

bool contains(const Span& span, std::size_t offset) {
    return span.offset <= offset && offset <= span.offset + span.length;
}

std::string spelling(const EditorDocument& document, const Span& span) {
    if (span.offset > document.source.size() || span.length > document.source.size() - span.offset) return {};
    return document.source.substr(span.offset, span.length);
}

std::string eol(const EditorDocument& document) {
    const auto at = document.source.find_first_of("\r\n");
    return at != std::string::npos && document.source[at] == '\r' ?
        at + 1 < document.source.size() && document.source[at + 1] == '\n' ? "\r\n" : "\r" : "\n";
}

std::string indentation(const EditorDocument& document, std::size_t offset) {
    auto start = document.source.find_last_of("\r\n", offset ? offset - 1 : 0);
    start = start == std::string::npos ? 0 : start + 1;
    auto end = start;
    while (end < document.source.size() && (document.source[end] == ' ' || document.source[end] == '\t')) ++end;
    return document.source.substr(start, end - start);
}

std::string type_source(const TypeSyntax& type, const std::map<std::string, std::string>& substitutions = {}) {
    if (type.anonymous) return {};
    std::string result = type.reference ? "&" : "";
    if (auto found = substitutions.find(type.name); found != substitutions.end()) result += found->second;
    else result += type.name;
    if (type.name == "[function]") {
        result = "func(";
        for (std::size_t i = 0; i + 1 < type.arguments.size(); ++i) {
            if (i) result += ", ";
            result += type_source(type.arguments[i], substitutions);
        }
        result += ") -> " + (type.arguments.empty() ? std::string("void") : type_source(type.arguments.back(), substitutions));
    } else if (type.name == "[tuple]") {
        result = "[";
        for (std::size_t i = 0; i < type.arguments.size(); ++i) {
            if (i) result += ", ";
            result += type_source(type.arguments[i], substitutions);
        }
        result += ']';
    } else if (!type.arguments.empty()) {
        result += '<';
        for (std::size_t i = 0; i < type.arguments.size(); ++i) {
            if (i) result += ", ";
            result += type_source(type.arguments[i], substitutions);
        }
        result += '>';
    }
    for (const auto kind : type.collections) result += kind == CollectionKind::Array ? "[]" : "[list]";
    return result;
}

std::string ir_type_source(const IrType& type) {
    if (type.kind == TypeKind::Array || type.kind == TypeKind::List) {
        return type.arguments.empty() ? std::string{} : ir_type_source(type.arguments.front()) +
            (type.kind == TypeKind::Array ? "[]" : "[list]");
    }
    if (type.kind == TypeKind::Callable || type.kind == TypeKind::Tuple) {
        if (type.arguments.empty()) return {};
        std::string result = type.kind == TypeKind::Callable ? "func(" : "[";
        const auto count = type.kind == TypeKind::Callable ? type.arguments.size() - 1 : type.arguments.size();
        for (std::size_t i = 0; i < count; ++i) {
            if (i) result += ", ";
            auto name = ir_type_source(type.arguments[i]);
            if (name.empty()) return {};
            result += name;
        }
        if (type.kind == TypeKind::Callable) result += ") -> " + ir_type_source(type.arguments.back());
        else result += ']';
        return result;
    }
    std::string result = type.name;
    if (result.empty()) {
        if (type.kind == TypeKind::Void) result = "void";
        else if (type.kind == TypeKind::Bool) result = "bool";
        else if (type.kind == TypeKind::String) result = "string";
        else return {};
    }
    if (result.find_first_of("$[]") != std::string::npos) return {};
    if (!type.arguments.empty()) {
        result += '<';
        for (std::size_t i = 0; i < type.arguments.size(); ++i) {
            if (i) result += ", ";
            auto name = ir_type_source(type.arguments[i]);
            if (name.empty()) return {};
            result += name;
        }
        result += '>';
    }
    return result;
}

void nodes(const IrNode& node, const std::function<void(const IrNode&)>& visit) {
    visit(node);
    for (const auto& child : node.operands) nodes(child, visit);
    for (const auto& child : node.body) nodes(child, visit);
    for (const auto& child : node.alternative) nodes(child, visit);
}

std::string expression_type(const AnalysisSession& session, const Expr& expression) {
    if (!session.program) return {};
    std::string result;
    for (const auto& body : session.program->bodies)
        for (const auto& node : body.instructions)
            nodes(node, [&](const IrNode& current) {
                if (current.span.file == expression.span.file && current.span.offset == expression.span.offset &&
                    current.span.length == expression.span.length && current.type.kind != TypeKind::Void)
                    result = ir_type_source(current.type);
            });
    return result;
}

bool requested(const Json& params, std::string_view kind) {
    const auto& only = params["context"]["only"];
    if (!only.is_array() || only.items().empty()) return true;
    for (const auto& item : only.items()) {
        const auto prefix = item.string();
        if (kind == prefix || (kind.size() > prefix.size() && kind.substr(0, prefix.size()) == prefix && kind[prefix.size()] == '.')) return true;
    }
    return false;
}

bool validates(const AnalysisSession& original, const EditorDocument& document, std::vector<DiagnosticEdit> edits) {
    if (document.library || edits.empty()) return false;
    auto changed = document.source;
    std::sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.span.offset > b.span.offset; });
    auto last = changed.size();
    for (const auto& edit : edits) {
        if (edit.span.file != document.path || edit.span.offset > changed.size() || edit.span.length > changed.size() - edit.span.offset ||
            edit.span.offset + edit.span.length > last) return false;
        changed.replace(edit.span.offset, edit.span.length, edit.replacement);
        last = edit.span.offset;
    }
    try {
        AnalysisSession trial;
        trial.root = original.root;
        trial.library = original.library;
        trial.source_roots = original.source_roots;
        trial.trusted = original.trusted;
        for (const auto& [path, doc] : original.documents)
            if (doc.open && path != document.path) trial.open(doc.uri, doc.source, doc.version);
        trial.open(document.uri, std::move(changed), document.version + 1);
        trial.analyze(document.uri);
        return trial.program.has_value();
    } catch (const std::exception&) {
        return false;
    }
}

void action(Json::Array& actions, AnalysisSession& session, const EditorDocument& document,
            std::string title, std::string kind, std::vector<DiagnosticEdit> edits) {
    if (validates(session, document, edits))
        actions.emplace_back(Json{{"title", std::move(title)}, {"kind", std::move(kind)},
                                  {"edit", session.workspace_edit(edits)}});
}

std::string unused_name(const AnalysisSession& session, const EditorDocument& document, std::string base) {
    auto name = base;
    for (unsigned suffix = 2;; ++suffix) {
        const bool exists = std::any_of(session.symbols.begin(), session.symbols.end(), [&](const auto& symbol) {
            return symbol.file == document.path && symbol.name == name;
        });
        if (!exists) return name;
        name = base + "_" + std::to_string(suffix);
    }
}

bool literal(const Expr& expression) {
    if (expression.kind == Expr::Kind::Integer || expression.kind == Expr::Kind::Float ||
        expression.kind == Expr::Kind::String || expression.kind == Expr::Kind::Boolean) return true;
    return expression.kind == Expr::Kind::Unary && expression.operands.size() == 1 && literal(expression.operands.front());
}

struct SelectedExpression {
    const Expr* expression{};
    const Statement* statement{};
    const Declaration* function{};
};

SelectedExpression selected_expression(const EditorDocument& document, std::size_t begin, std::size_t end) {
    SelectedExpression result;
    std::function<void(const Declaration&)> declaration;
    std::function<void(const Statement&, const Declaration*, bool)> statement;
    std::function<void(const Expr&)> expression;
    expression = [&](const Expr& item) {
        if (item.lambda) declaration(*item.lambda);
        for (const auto& operand : item.operands) expression(operand);
    };
    statement = [&](const Statement& item, const Declaration* function, bool can_insert) {
        const Expr* candidate = nullptr;
        if (item.kind == Statement::Kind::Return && item.expressions.size() == 1) candidate = &item.expressions.front();
        if (item.kind == Statement::Kind::Binding && item.declaration && item.declaration->initializer)
            candidate = &*item.declaration->initializer;
        if (can_insert && function && candidate && candidate->span.offset == begin && candidate->span.offset + candidate->span.length == end)
            result = {candidate, &item, function};
        if (item.declaration) declaration(*item.declaration);
        for (const auto& value : item.expressions) expression(value);
        for (const auto* body : {&item.body, &item.alternative}) for (const auto& child : *body) statement(child, function, true);
        for (const auto* body : {&item.initializer, &item.step}) for (const auto& child : *body) statement(child, function, false);
        for (const auto& arm : item.arms) for (const auto& child : arm.body) statement(child, function, true);
        for (const auto& arm : item.select_arms) for (const auto& child : arm.body) statement(child, function, true);
    };
    declaration = [&](const Declaration& item) {
        for (const auto& member : item.members) declaration(member);
        for (const auto& overload : item.overloads) declaration(overload);
        for (const auto& child : item.body) statement(child, &item, true);
        if (item.initializer) expression(*item.initializer);
    };
    for (const auto& item : document.syntax.statements) statement(item, nullptr, false);
    return result;
}

const Declaration* named_declaration(const AnalysisSession& session, const EditorDocument& document,
                                     std::string name, Declaration::Kind kind) {
    if (const auto dot = name.find_last_of('.'); dot != std::string::npos) name = name.substr(dot + 1);
    const Declaration* result = nullptr;
    for (const auto& symbol : session.symbols) {
        if (symbol.file != document.path || symbol.name != name || !symbol.declaration || symbol.declaration->kind != kind) continue;
        if (result && result != symbol.declaration) return nullptr;
        result = symbol.declaration;
    }
    return result;
}

// Imports can initialize a program. Only reorder modules whose complete
// source-import closure has no executable initializer or native/prebuilt import.
bool inert_import(const AnalysisSession& session, const std::string& path, std::set<std::string>& seen) {
    if (!seen.insert(path).second) return true;
    const auto found = session.documents.find(path);
    if (found == session.documents.end() || !found->second.syntax_diagnostics.empty()) return false;
    const auto& document = found->second;
    std::function<bool(const Declaration&)> declaration;
    declaration = [&](const Declaration& item) {
        if (item.singleton) return false;
        if (item.kind == Declaration::Kind::Binding && item.initializer && !literal(*item.initializer) &&
            item.initializer->kind != Expr::Kind::Import) return false;
        if (item.initializer && item.initializer->kind == Expr::Kind::Import &&
            (item.initializer->import_path || !item.initializer->c_import.empty())) return false;
        if (item.kind == Declaration::Kind::Module)
            for (const auto& member : item.members) if (!declaration(member)) return false;
        return true;
    };
    for (const auto& item : document.syntax.statements)
        if (!item.declaration || !declaration(*item.declaration)) return false;
    if (!session.program) return false;
    for (const auto& imported : session.program->symbols) {
        if (imported.kind != SymbolKind::Import || imported.span.file != path) continue;
        if (imported.type.symbol >= session.program->symbols.size()) return false;
        const auto& target = session.program->symbols[imported.type.symbol];
        if (target.span.file.empty() || !inert_import(session, target.span.file, seen)) return false;
    }
    return true;
}

void organize_imports(Json::Array& actions, AnalysisSession& session, const EditorDocument& document) {
    if (!session.program) return;
    std::vector<const Declaration*> imports;
    for (const auto& statement : document.syntax.statements) {
        const auto* d = statement.declaration.get();
        if (!d || !d->initializer || d->initializer->kind != Expr::Kind::Import) break;
        if (d->exported || d->promote_import || d->name == "_" || d->documentation || !d->tags.empty() ||
            d->initializer->import_path || !d->initializer->c_import.empty()) return;
        imports.push_back(d);
    }
    if (imports.size() < 2) return;
    const auto start = imports.front()->span.offset;
    const auto end = imports.back()->span.offset + imports.back()->span.length;
    if (document.source.substr(start, end - start).find("//") != std::string::npos) return;
    std::set<std::string> seen;
    for (const auto* declaration : imports) {
        const auto editor = std::find_if(session.symbols.begin(), session.symbols.end(), [&](const auto& symbol) {
            return symbol.file == document.path && symbol.declaration == declaration;
        });
        if (editor == session.symbols.end() || editor->compiler_id >= session.program->symbols.size()) return;
        const auto& alias = session.program->symbols[editor->compiler_id];
        if (alias.kind != SymbolKind::Import || alias.type.symbol >= session.program->symbols.size()) return;
        const auto& target = session.program->symbols[alias.type.symbol];
        if (target.span.file.empty() || !inert_import(session, target.span.file, seen)) return;
    }
    auto sorted = imports;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
        return std::tie(a->initializer->text, a->name) < std::tie(b->initializer->text, b->name);
    });
    if (sorted == imports) return;
    std::string replacement;
    for (const auto* item : sorted) {
        if (!replacement.empty()) replacement += eol(document);
        replacement += spelling(document, item->span);
    }
    action(actions, session, document, "Organize imports (preserve initialization effects)", "source.organizeImports",
           {{{document.path, 1, 1, start, end - start}, std::move(replacement)}});
}

void missing_members(Json::Array& actions, AnalysisSession& session, const EditorDocument& document,
                     const Declaration& target) {
    std::string generated;
    std::set<std::string> implemented;
    for (const auto& member : target.members) implemented.insert(member.name);
    const auto newline = eol(document);
    const auto indent = indentation(document, target.span.offset) + "    ";
    std::set<const Declaration*> seen;
    std::function<void(const Declaration&, const std::map<std::string, std::string>&, bool)> append;
    append = [&](const Declaration& owner, const std::map<std::string, std::string>& substitutions, bool overriding) {
        if (!seen.insert(&owner).second) return;
        for (const auto& member : owner.members) {
            if (member.kind != Declaration::Kind::Function || !member.exported || member.static_member ||
                implemented.contains(member.name) || !member.overloads.empty() ||
                member.name == "init" || member.name == "destroy" || member.name == "deinit" || member.cast_target ||
                std::any_of(member.parameters.begin(), member.parameters.end(), [](const auto& p) { return p.variadic; })) continue;
            if (!overriding && member.has_body) continue;
            std::string signature = indent + "export let" + (overriding ? std::string(".override ") : std::string(" ")) + member.name + " = func";
            if (!member.generics.empty()) {
                signature += '<';
                for (std::size_t i = 0; i < member.generics.size(); ++i) {
                    if (i) signature += ", ";
                    const auto& generic = member.generics[i];
                    signature += generic.name;
                    if (!generic.bounds.empty()) {
                        signature += ": [";
                        for (std::size_t b = 0; b < generic.bounds.size(); ++b) {
                            if (b) signature += ", ";
                            signature += type_source(generic.bounds[b], substitutions);
                        }
                        signature += ']';
                    }
                }
                signature += '>';
            }
            signature += '(';
            for (std::size_t i = 0; i < member.parameters.size(); ++i) {
                if (i) signature += ", ";
                signature += member.parameters[i].name + ": " + type_source(member.parameters[i].type, substitutions);
            }
            const auto result = member.type ? type_source(*member.type, substitutions) : "void";
            signature += ") -> " + result + " {" + newline;
            // Neura currently offers super.init only, not a base-method
            // call. Generate an explicit implementation placeholder rather
            // than an unsupported or recursively dispatched forwarding call.
            signature += indent + "    trap_with_message(\"TODO: implement " + member.name + "\");" + newline;
            signature += indent + "};" + newline;
            if (!generated.empty()) generated += newline;
            generated += signature;
            implemented.insert(member.name);
        }
    };
    for (const auto& interface : target.interfaces) {
        const auto* owner = named_declaration(session, document, interface.name, Declaration::Kind::Interface);
        if (!owner) continue;
        std::map<std::string, std::string> substitutions;
        for (std::size_t i = 0; i < owner->generics.size() && i < interface.arguments.size(); ++i)
            substitutions[owner->generics[i].name] = type_source(interface.arguments[i]);
        append(*owner, substitutions, false);
    }
    if (target.base) {
        if (const auto* owner = named_declaration(session, document, target.base->name, Declaration::Kind::Class)) append(*owner, {}, true);
        if (const auto* owner = named_declaration(session, document, target.base->name, Declaration::Kind::Interface)) append(*owner, {}, false);
    }
    if (generated.empty()) return;
    std::size_t close = none;
    for (const auto& token : document.tokens)
        if (token.span.offset >= target.span.offset && token.span.offset < target.span.offset + target.span.length && token.text == ")") close = token.span.offset;
    if (close == none) return;
    const auto prefix = close && document.source[close - 1] != '\n' && document.source[close - 1] != '\r' ? newline : std::string{};
    action(actions, session, document, "Implement interface and inherited members", "refactor.rewrite",
           {{{document.path, 1, 1, close, 0}, prefix + generated + indentation(document, target.span.offset)}});
}

void constructor(Json::Array& actions, AnalysisSession& session, const EditorDocument& document,
                 const Declaration& target) {
    if (target.singleton || target.base || std::any_of(target.members.begin(), target.members.end(),
        [](const auto& member) { return member.name == "init"; })) return;
    std::vector<const Declaration*> fields;
    std::set<std::string> names;
    for (const auto& member : target.members) {
        names.insert(member.name);
        if (member.kind == Declaration::Kind::Binding && !member.static_member && (!member.constant || member.last) &&
            !member.initializer && member.type && !member.type->anonymous) fields.push_back(&member);
    }
    if (fields.empty()) return;
    const auto newline = eol(document);
    const auto indent = indentation(document, target.span.offset) + "    ";
    std::string generated = indent + "export let init = func(";
    std::vector<std::string> parameters;
    for (const auto* field : fields) {
        auto name = "initial_" + field->name;
        while (names.contains(name)) name = "_" + name;
        names.insert(name);
        parameters.push_back(name);
        if (parameters.size() > 1) generated += ", ";
        generated += name + ": " + type_source(*field->type);
    }
    generated += ") {" + newline;
    for (std::size_t i = 0; i < fields.size(); ++i)
        generated += indent + "    " + fields[i]->name + " = " + parameters[i] + ";" + newline;
    generated += indent + "};" + newline;
    std::size_t close = none;
    for (const auto& token : document.tokens)
        if (token.span.offset >= target.span.offset && token.span.offset < target.span.offset + target.span.length && token.text == ")") close = token.span.offset;
    if (close == none) return;
    action(actions, session, document, "Generate constructor for uninitialized fields", "refactor.rewrite",
        {{{document.path, 1, 1, close, 0}, newline + generated + indentation(document, target.span.offset)}});
}

void exhaustive_matches(Json::Array& actions, AnalysisSession& session, const EditorDocument& document,
                        std::size_t offset) {
    const Statement* selected = nullptr;
    std::function<void(const Statement&)> statement;
    std::function<void(const Declaration&)> declaration;
    std::function<void(const Expr&)> expression;
    expression = [&](const Expr& item) {
        if (item.match) statement(*item.match);
        if (item.lambda) declaration(*item.lambda);
        for (const auto& operand : item.operands) expression(operand);
    };
    declaration = [&](const Declaration& item) {
        for (const auto& member : item.members) declaration(member);
        for (const auto& overload : item.overloads) declaration(overload);
        for (const auto& child : item.body) statement(child);
        if (item.initializer) expression(*item.initializer);
    };
    statement = [&](const Statement& item) {
        if (!contains(item.span, offset)) return;
        if (item.kind == Statement::Kind::Match && !item.arms.empty() && !item.arms.front().value &&
            (!selected || item.span.length < selected->span.length)) selected = &item;
        if (item.declaration) declaration(*item.declaration);
        for (const auto& value : item.expressions) expression(value);
        for (const auto* body : {&item.body, &item.alternative, &item.initializer, &item.step})
            for (const auto& child : *body) statement(child);
        for (const auto& arm : item.arms) for (const auto& child : arm.body) statement(child);
        for (const auto& arm : item.select_arms) for (const auto& child : arm.body) statement(child);
    };
    for (const auto& item : document.syntax.statements) statement(item);
    if (!selected || selected->expressions.empty() || spelling(document, selected->span).find("//") != std::string::npos) return;
    const auto& scrutinee = selected->expressions.front();
    const auto* value = session.symbol_at(document, scrutinee.span.offset);
    std::string type = value ? value->type_name : std::string{};
    if (const auto generic = type.find('<'); generic != std::string::npos) type.resize(generic);
    if (type.empty()) {
        const auto& variant = selected->arms.front().variant;
        if (const auto dot = variant.find_last_of('.'); dot != std::string::npos) type = variant.substr(0, dot);
    }
    const auto* definition = named_declaration(session, document, type, Declaration::Kind::Enum);
    if (!definition) return;
    std::set<std::string> present;
    for (const auto& arm : selected->arms) {
        const auto dot = arm.variant.find_last_of('.');
        present.insert(dot == std::string::npos ? arm.variant : arm.variant.substr(dot + 1));
    }
    const auto newline = eol(document);
    const auto indent = indentation(document, selected->span.offset) + "    ";
    std::string generated;
    for (const auto& variant : definition->members) {
        if (present.contains(variant.name)) continue;
        if (!generated.empty()) generated += ',' + newline;
        generated += indent + type + "." + variant.name;
        if (!variant.parameters.empty()) {
            generated += variant.named_payload ? " { " : "(";
            for (std::size_t i = 0; i < variant.parameters.size(); ++i) {
                if (i) generated += ", ";
                if (variant.named_payload) generated += variant.parameters[i].name + ": ";
                generated += '_';
            }
            generated += variant.named_payload ? " }" : ")";
        }
        generated += " => {" + newline + indent + "    trap_with_message(\"TODO: handle " + type + "." + variant.name + "\");" +
                     newline + indent + '}';
    }
    if (generated.empty()) return;
    const auto& last = selected->arms.back();
    std::size_t insertion = last.span.offset + last.span.length;
    std::size_t close = none;
    bool comma = false;
    for (const auto& token : document.tokens) {
        if (token.span.offset < insertion || token.span.offset >= selected->span.offset + selected->span.length) continue;
        if (token.text == ",") { insertion = token.span.offset + token.span.length; comma = true; }
        if (token.text == "}") close = token.span.offset;
    }
    if (close == none) return;
    const auto gap = document.source.substr(insertion, close - insertion);
    auto replacement = (comma ? std::string{} : std::string(",")) + newline + generated;
    if (gap.find_first_of("\r\n") == std::string::npos) replacement += newline + indentation(document, selected->span.offset);
    action(actions, session, document, "Add missing enum match arms", "quickfix",
           {{{document.path, 1, 1, insertion, 0}, std::move(replacement)}});
}

const EditorSymbol* symbol_for(const AnalysisSession& session, SymbolId id) {
    for (const auto& symbol : session.symbols) if (symbol.compiler_id == id) return &symbol;
    return nullptr;
}

Json hierarchy_item(const AnalysisSession& session, SymbolId id) {
    const auto* symbol = symbol_for(session, id);
    if (!symbol || symbol->name.empty() || symbol->name.starts_with('$')) return {};
    const auto document = session.documents.find(symbol->file);
    if (document == session.documents.end()) return {};
    return Json{{"name", symbol->name}, {"kind", symbol->kind}, {"detail", symbol->detail},
                {"uri", document->second.uri}, {"range", source_range(document->second.source, symbol->span)},
                {"selectionRange", source_range(document->second.source, symbol->selection)},
                {"data", Json{{"file", symbol->file}, {"name", symbol->name}, {"offset", symbol->selection.offset}}}};
}

SymbolId hierarchy_target(const AnalysisSession& session, const EditorDocument& document, const Json& params) {
    if (params["item"].is_object()) {
        const auto& data = params["item"]["data"];
        const auto file = data["file"].string();
        const auto name = data["name"].string();
        const auto offset = static_cast<std::size_t>(std::max<std::int64_t>(0, data["offset"].integer()));
        for (const auto& symbol : session.symbols)
            if (symbol.file == file && symbol.name == name && symbol.selection.offset == offset) return symbol.compiler_id;
        return no_symbol;
    }
    const auto* symbol = session.symbol_at(document, source_offset(document.source, params["position"]));
    return symbol ? symbol->compiler_id : no_symbol;
}

Span call_site(const AnalysisSession& session, const IrNode& node, SymbolId target) {
    auto span = node.span;
    const auto document = session.documents.find(span.file);
    if (document == session.documents.end() || !session.program || target >= session.program->symbols.size()) return span;
    const auto& name = session.program->symbols[target].name;
    for (const auto& token : document->second.tokens)
        if (token.span.offset >= span.offset && token.span.offset + token.span.length <= span.offset + span.length && token.text == name) return token.span;
    return span;
}

bool call_node(const IrProgram& program, const IrNode& node) {
    if (node.symbol >= program.symbols.size() || program.symbols[node.symbol].kind != SymbolKind::Function) return false;
    return node.op == IrOp::Call || node.op == IrOp::VirtualCall || node.op == IrOp::ExternalCall ||
           node.op == IrOp::BaseInitialize || node.op == IrOp::Allocate;
}

bool calls_symbol(const IrProgram& program, const IrNode& node, SymbolId target) {
    if (node.symbol == target) return true;
    if (node.op != IrOp::VirtualCall) return false;
    std::set<SymbolId> seen;
    for (auto parent = program.symbols[target].overrides; parent < program.symbols.size() && seen.insert(parent).second;
         parent = program.symbols[parent].overrides)
        if (node.symbol == parent) return true;
    return false;
}

Json project_incoming(AnalysisSession& session, const EditorDocument& document, SymbolId id, bool calls) {
    const auto* selected = symbol_for(session, id);
    if (!selected) return Json::Array{};
    const auto file = selected->file;
    const auto name = selected->name;
    const auto offset = selected->selection.offset;
    const auto original_uri = document.uri;
    session.index_workspace();
    std::vector<std::string> roots;
    for (const auto& [path, candidate] : session.documents)
        if (!candidate.library) roots.push_back(candidate.uri);
    std::map<std::string, Json> items;
    std::map<std::string, Json::Array> ranges;
    std::set<std::tuple<std::string, std::string, std::size_t>> seen;
    for (const auto& root : roots) {
        session.analyze(root);
        if (!session.program) continue;
        const auto target = std::find_if(session.symbols.begin(), session.symbols.end(), [&](const auto& candidate) {
            return candidate.file == file && candidate.name == name && candidate.selection.offset == offset;
        });
        if (target == session.symbols.end() || target->compiler_id >= session.program->symbols.size()) continue;
        const auto target_id = target->compiler_id;
        if (calls) {
            for (const auto& body : session.program->bodies)
                for (const auto& node : body.instructions) nodes(node, [&](const IrNode& current) {
                    if (!call_node(*session.program, current) || !calls_symbol(*session.program, current, target_id)) return;
                    auto item = hierarchy_item(session, body.owner);
                    if (item.is_null()) return;
                    const auto key = item["uri"].string() + ':' + item["selectionRange"].dump();
                    const auto span = call_site(session, current, current.symbol);
                    const auto source = session.documents.find(span.file);
                    if (source == session.documents.end() || !seen.emplace(key, span.file, span.offset).second) return;
                    items.insert_or_assign(key, std::move(item));
                    ranges[key].push_back(source_range(source->second.source, span));
                });
        } else {
            for (SymbolId child_id = 0; child_id < session.program->symbols.size(); ++child_id) {
                const auto& child = session.program->symbols[child_id];
                if (child.kind != SymbolKind::Class && child.kind != SymbolKind::Interface) continue;
                const bool implements = child.base == target_id || std::any_of(child.interfaces.begin(), child.interfaces.end(),
                    [&](const auto& interface) { return interface.symbol == target_id; });
                if (!implements) continue;
                auto item = hierarchy_item(session, child_id);
                if (item.is_null()) continue;
                const auto key = item["uri"].string() + ':' + item["selectionRange"].dump();
                items.insert_or_assign(key, std::move(item));
            }
        }
    }
    session.analyze(original_uri);
    Json::Array result;
    for (auto& [key, item] : items) {
        if (calls) result.emplace_back(Json{{"from", std::move(item)}, {"fromRanges", std::move(ranges[key])}});
        else result.push_back(std::move(item));
    }
    return result;
}

} // namespace

Json refactor_actions(AnalysisSession& session, const EditorDocument& document, const Json& params) {
    Json::Array result;
    if (document.library || !document.syntax_diagnostics.empty()) return result;
    auto begin = source_offset(document.source, params["range"]["start"]);
    auto end = source_offset(document.source, params["range"]["end"]);
    while (begin < end && std::string_view(" \t\r\n").find(document.source[begin]) != std::string_view::npos) ++begin;
    while (end > begin && std::string_view(" \t\r\n").find(document.source[end - 1]) != std::string_view::npos) --end;
    if (requested(params, "source.organizeImports")) organize_imports(result, session, document);
    if (requested(params, "quickfix")) exhaustive_matches(result, session, document, begin);

    if (session.program && end > begin) {
        const auto selection = selected_expression(document, begin, end);
        if (selection.expression && selection.statement && selection.function && selection.expression->kind != Expr::Kind::Lambda &&
            selection.expression->kind != Expr::Kind::New && selection.expression->kind != Expr::Kind::Spawn) {
            auto type = expression_type(session, *selection.expression);
            if (selection.statement->kind == Statement::Kind::Return && selection.function->type) type = type_source(*selection.function->type);
            if (selection.statement->declaration && selection.statement->declaration->type) type = type_source(*selection.statement->declaration->type);
            const auto value = spelling(document, selection.expression->span);
            const auto indent = indentation(document, selection.statement->span.offset);
            auto insertion = selection.statement->span;
            insertion.length = 0;
            if (requested(params, "refactor.extract.variable")) {
                const auto name = unused_name(session, document, "extracted_value");
                action(result, session, document, "Extract expression to assign-once local", "refactor.extract.variable",
                    {{insertion, "last " + name + (type.empty() ? "" : ": " + type) + " = " + value + ";" + eol(document) + indent},
                     {selection.expression->span, name}});
            }
            if (!type.empty() && type != "void" && requested(params, "refactor.extract.function")) {
                const auto name = unused_name(session, document, "extracted_function");
                action(result, session, document, "Extract expression to local function", "refactor.extract.function",
                    {{insertion, "last " + name + " = func() -> " + type + " {" + eol(document) + indent + "    return " + value + ";" +
                        eol(document) + indent + "};" + eol(document) + indent},
                     {selection.expression->span, name + "()"}});
            }
        }
    }

    const auto* selected = session.symbol_at(document, begin);
    if (selected && selected->declaration) {
        const auto& declaration = *selected->declaration;
        if (requested(params, "refactor.inline") && session.program && selected->compiler_id < session.program->symbols.size() &&
            session.program->symbols[selected->compiler_id].kind == SymbolKind::Local && declaration.initializer && literal(*declaration.initializer) &&
            declaration.tags.empty() && !declaration.documentation) {
            std::vector<const EditorReference*> uses;
            for (const auto& reference : session.references)
                if (reference.symbol < session.symbols.size() && session.symbols[reference.symbol].compiler_id == selected->compiler_id) uses.push_back(&reference);
            const auto type = ir_type_source(session.program->symbols[selected->compiler_id].type);
            if (uses.size() == 1 && !uses.front()->write && uses.front()->span.file == document.path && !type.empty())
                action(result, session, document, "Inline single-use literal local", "refactor.inline",
                    {{declaration.span, ""}, {uses.front()->span, "(" + spelling(document, declaration.initializer->span) + " as " + type + ")"}});
        }
        if (requested(params, "refactor.rewrite") && declaration.kind == Declaration::Kind::Binding && declaration.exported &&
            !declaration.constant && !declaration.static_member && declaration.accessors.empty() && selected->kind == 8) {
            auto insertion = declaration.span;
            insertion.offset += insertion.length;
            insertion.length = 0;
            const auto indent = indentation(document, declaration.span.offset) + "    ";
            const auto getter = eol(document) + indent + "| get: " + declaration.name + ";";
            action(result, session, document, "Generate getter and setter", "refactor.rewrite",
                   {{insertion, getter + eol(document) + indent + "| set: " + declaration.name + " = value;"}});
            action(result, session, document, "Generate read-only getter", "refactor.rewrite", {{insertion, getter}});
        }
    }

    if (requested(params, "refactor.rewrite")) {
        const Declaration* target = nullptr;
        for (const auto& symbol : session.symbols)
            if (symbol.file == document.path && symbol.declaration && symbol.declaration->kind == Declaration::Kind::Class &&
                contains(symbol.span, begin) && (!target || symbol.span.length < target->span.length)) target = symbol.declaration;
        if (target) {
            missing_members(result, session, document, *target);
            constructor(result, session, document, *target);
        }
    }
    return result;
}

Json hierarchy_request(AnalysisSession& session, const EditorDocument& document,
                       std::string_view method, const Json& params) {
    Json::Array result;
    if (!session.program) return result;
    const auto id = hierarchy_target(session, document, params);
    if (id >= session.program->symbols.size()) return result;
    if (method == "callHierarchy/incomingCalls") return project_incoming(session, document, id, true);
    if (method == "typeHierarchy/subtypes") return project_incoming(session, document, id, false);
    const auto& symbol = session.program->symbols[id];
    if (method == "textDocument/prepareCallHierarchy" || method == "textDocument/prepareTypeHierarchy") {
        const bool call = method == "textDocument/prepareCallHierarchy";
        if (call ? symbol.kind != SymbolKind::Function : symbol.kind != SymbolKind::Class && symbol.kind != SymbolKind::Interface) return result;
        auto item = hierarchy_item(session, id);
        if (!item.is_null()) result.push_back(std::move(item));
    } else if (method == "typeHierarchy/supertypes") {
        std::set<SymbolId> seen;
        if (symbol.base != no_symbol) seen.insert(symbol.base);
        for (const auto& interface : symbol.interfaces) if (interface.symbol != no_symbol) seen.insert(interface.symbol);
        for (const auto parent : seen) {
            auto item = hierarchy_item(session, parent);
            if (!item.is_null()) result.push_back(std::move(item));
        }
    } else if (method == "typeHierarchy/subtypes") {
        for (SymbolId candidate = 0; candidate < session.program->symbols.size(); ++candidate) {
            const auto& child = session.program->symbols[candidate];
            if (child.kind != SymbolKind::Class && child.kind != SymbolKind::Interface) continue;
            const bool implements = child.base == id || std::any_of(child.interfaces.begin(), child.interfaces.end(), [&](const auto& interface) { return interface.symbol == id; });
            if (!implements) continue;
            auto item = hierarchy_item(session, candidate);
            if (!item.is_null()) result.push_back(std::move(item));
        }
    } else if (method == "callHierarchy/incomingCalls" || method == "callHierarchy/outgoingCalls") {
        const bool incoming = method == "callHierarchy/incomingCalls";
        std::map<SymbolId, Json::Array> grouped;
        std::set<std::tuple<SymbolId, std::string, std::size_t>> seen;
        for (const auto& body : session.program->bodies) {
            if (!incoming && body.owner != id) continue;
            for (const auto& node : body.instructions) nodes(node, [&](const IrNode& current) {
                if (!call_node(*session.program, current)) return;
                if (incoming && !calls_symbol(*session.program, current, id)) return;
                const auto key = incoming ? body.owner : current.symbol;
                const auto span = call_site(session, current, current.symbol);
                if (!seen.emplace(key, span.file, span.offset).second) return;
                const auto source = session.documents.find(span.file);
                if (source != session.documents.end()) grouped[key].push_back(source_range(source->second.source, span));
            });
        }
        for (auto& [target, ranges] : grouped) {
            auto item = hierarchy_item(session, target);
            if (item.is_null()) continue;
            result.emplace_back(incoming ? Json{{"from", std::move(item)}, {"fromRanges", std::move(ranges)}} :
                                           Json{{"to", std::move(item)}, {"fromRanges", std::move(ranges)}});
        }
    }
    return result;
}

} // namespace neura::tooling
