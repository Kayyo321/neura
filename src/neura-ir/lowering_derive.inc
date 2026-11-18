// JSON derivation runs once the whole linked program has declarations, and
// before any use of a derived module resolves its generated members.

std::size_t Lowering::codec_shape(const IrType& input, const Span& at, const std::string& path,
                                  std::vector<IrType>& active, std::vector<CodecShape>& shapes) {
    const auto invalid = [&](const std::string& reason) {
        return Diagnostic(at, "'Json.Codec' cannot encode " + path + ": " + reason).with_code("E0590");
    };
    const auto contains = [&](const auto& self, const IrType& parent) -> bool {
        return std::ranges::any_of(parent.arguments, [&](const auto& part) { return part == input || self(self, part); });
    };
    for (const auto& ancestor : active) {
        if (input == ancestor || (input.symbol != no_symbol && ancestor.symbol == input.symbol && !contains(contains, ancestor)))
            throw invalid(describe(input) + " reaches itself");
    }
    for (std::size_t i = 0; i < shapes.size(); ++i)
        if (shapes[i].type == input) return i;
    const auto index = shapes.size();
    CodecShape shape{};
    shape.type = input;
    shapes.push_back(shape);
    active.push_back(input);
    const auto slot = [&](SymbolId symbol, const std::string& name, IrType value, bool field) {
        const bool optional = field && library_enum_type(value, "std/core/optional.nra", "Optional");
        if (optional) value = IrType(value.arguments.at(0));
        return CodecSlot{symbol, name, name, codec_shape(value, at, path + "." + name, active, shapes), optional};
    };
    switch (input.kind) {
    case TypeKind::Bool: case TypeKind::Integer: case TypeKind::Float: case TypeKind::String: break;
    case TypeKind::Tuple:
        for (std::size_t i = 0; i < input.arguments.size(); ++i)
            shape.slots.push_back(slot(no_symbol, std::to_string(i), input.arguments[i], false));
        break;
    case TypeKind::Object:
        if (!record_class(input.symbol)) throw invalid(describe(input) + " is not a record");
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            if (symbol(id).owner != input.symbol || symbol(id).kind != SymbolKind::Field) continue;
            resolve(id);
            shape.slots.push_back(slot(id, std::string(symbol(id).name), IrType(symbol(id).type), true));
        }
        break;
    case TypeKind::Enum: {
        if (library_enum_type(input, "std/core/optional.nra", "Optional"))
            throw invalid("Optional is supported only as the type of a record field");
        const auto arguments = enum_arguments(input);
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            if (symbol(id).owner != input.symbol || symbol(id).kind != SymbolKind::Variant) continue;
            const auto variant = symbol(id);
            CodecVariant entry{id, variant.name, variant.name, !variant.payload_names.empty(), {}};
            for (std::size_t i = 0; i < variant.payload_types.size(); ++i) {
                const auto name = entry.named ? variant.payload_names[i] : std::to_string(i);
                const auto payload = substitute(variant.payload_types[i], arguments);
                const auto nested = codec_shape(payload, at, path + "." + variant.name + "." + name, active, shapes);
                entry.slots.push_back({id, name, name, nested, false});
            }
            shape.variants.push_back(std::move(entry));
        }
        break;
    }
    default: throw invalid(describe(input) + " is outside the supported JSON shapes");
    }
    active.pop_back();
    shapes[index] = std::move(shape);
    return index;
}

void Lowering::codec_mappings(std::vector<CodecShape>& shapes, const DeriveSyntax& syntax, const Context& ctx) {
    std::set<std::pair<SymbolId, std::string>> seen;
    for (const auto& mapping : syntax.mappings) {
        std::string path;
        for (const auto& part : mapping.parts) path += (path.empty() ? "" : ".") + part;
        const auto unknown = [&] {
            return Diagnostic(mapping.path, "'" + path + "' names no member reachable from the codec root")
                .with_code("E0318");
        };
        if (mapping.parts.size() < 2) throw unknown();
        std::string owner_name;
        for (std::size_t i = 0; i + 1 < mapping.parts.size(); ++i) {
            if (i) owner_name += ".";
            owner_name += mapping.parts[i];
        }
        SymbolId owner;
        try { owner = qualified(owner_name, mapping.path, ctx); }
        catch (const Diagnostic&) { throw unknown(); }
        if (symbol(owner).kind == SymbolKind::Alias) owner = symbol(owner).type.symbol;
        const auto& name = mapping.parts.back();
        const auto invalid = [&](const std::string& reason) {
            return Diagnostic(mapping.wire_span, reason).with_code("E0319");
        };
        bool matched = false;
        for (auto& shape : shapes) {
            if (shape.type.symbol == owner && !mapping.variant)
                for (auto& field : shape.slots) if (field.name == name) {
                    field.wire = mapping.wire;
                    matched = true;
                }
            for (auto& variant : shape.variants) {
                if (mapping.variant && shape.type.symbol == owner && variant.name == name) {
                    variant.wire = mapping.wire;
                    matched = true;
                }
                if (!mapping.variant && variant.symbol == owner && variant.named)
                    for (auto& field : variant.slots) if (field.name == name) {
                        field.wire = mapping.wire;
                        matched = true;
                    }
            }
        }
        if (!matched) {
            auto error = unknown();
            std::vector<std::string> candidates;
            for (const auto& shape : shapes) {
                if (shape.type.symbol == owner) {
                    if (mapping.variant) for (const auto& variant : shape.variants) candidates.push_back(variant.name);
                    else for (const auto& field : shape.slots) candidates.push_back(field.name);
                }
                for (const auto& variant : shape.variants) if (variant.symbol == owner && !mapping.variant) {
                    if (variant.named) for (const auto& field : variant.slots) candidates.push_back(field.name);
                    else if (!variant.slots.empty() && entries_[owner].declaration == nullptr)
                        error.help("if this dependency declared named payload fields, re-emit it from source to retain their names");
                }
            }
            if (const auto candidate = closest_name(name, candidates))
                error.suggest("use the declared member", mapping.part_spans.back(), *candidate,
                              Applicability::MaybeIncorrect);
            throw error;
        }
        if (mapping.wire.empty()) throw invalid("a wire name must not be empty");
        if (!seen.emplace(owner, name).second) throw invalid("this member already has a wire mapping");
    }
    const auto unique = [&](const auto& members) {
        std::set<std::string> names;
        for (const auto& member : members) if (!names.insert(member.wire).second) {
            const auto it = std::ranges::find_if(syntax.mappings, [&](const auto& map) { return map.wire == member.wire; });
            throw Diagnostic(it->wire_span, "duplicate wire name '" + member.wire + "'").with_code("E0319");
        }
    };
    for (const auto& shape : shapes) {
        unique(shape.slots);
        unique(shape.variants);
        for (const auto& variant : shape.variants) if (variant.named) unique(variant.slots);
    }
}

// Find a spelling available in the scope of the derivation. In particular,
// a prebuilt module may be imported under a different name in each file.
std::string Lowering::codec_source_name(SymbolId target, const Context& ctx, const Declaration& decl, CodecImports& imports) {
    std::map<SymbolId, std::string> suffixes;
    std::string suffix;
    for (auto owner = target; owner != no_symbol; owner = symbol(owner).owner) {
        suffixes.emplace(owner, suffix);
        suffix = "." + symbol(owner).name + suffix;
    }
    for (auto scope = ctx.owner; scope != no_symbol; scope = symbol(scope).owner) {
        const auto names = entries_[scope].names;
        for (const auto& [name, id] : names) {
            if (name.empty() || name.front() == '$') continue;
            auto visible = id;
            if (symbol(id).kind == SymbolKind::Import || symbol(id).kind == SymbolKind::Alias) {
                resolve(id);
                visible = symbol(id).type.symbol;
            }
            if (const auto found = suffixes.find(visible); found != suffixes.end()) return name + found->second;
        }
        for (const auto promoted : entries_[scope].promoted_modules) {
            if (const auto member = own_member(promoted, symbol(target).name); member == target)
                return symbol(target).name;
        }
    }
    // An exported record may mention a type from a dependency which only
    // its producer imported. Include that import in the displayed module.
    auto top = target;
    while (symbol(top).owner != no_symbol && symbol(symbol(top).owner).kind != SymbolKind::File)
        top = symbol(top).owner;
    const auto file = source_file(program_, target);
    if (file == no_symbol || symbol(top).kind != SymbolKind::Module)
        throw Diagnostic(decl.derive->arguments.front().span, "the JSON codec cannot name schema type '" +
                         symbol(target).name + "' from this scope").with_code("E0590");
    auto found = imports.find(top);
    if (found == imports.end()) {
        std::string path;
        if (const auto origin = artifact_origins_.find(top); origin != artifact_origins_.end()) {
            auto artifact = origin->second;
            for (const auto& parsed : *files_) if (parsed.file == decl.span.file)
                for (const auto& source : parsed.imports)
                    if (source.artifact && source.file == origin->second) artifact = source.artifact_path;
            path = record_syntax::string(decl.span, artifact).text + "." + symbol(top).name;
        } else {
            for (const auto& parsed : *files_)
                for (const auto& source : parsed.imports)
                    if (!source.artifact && source.file == symbol(file).name && !source.name.ends_with(".*"))
                        path = source.name;
            if (path.empty() && !symbol(file).library_path.empty()) {
                auto library = symbol(file).library_path;
                library.resize(library.size() - 4); // .nra
                bool upper = true;
                for (char c : library) {
                    if (c == '/' || c == '-') {
                        if (c == '/') path += ".";
                        upper = true;
                    } else {
                        path += upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
                        upper = false;
                    }
                }
            }
            if (path.empty()) path = symbol(top).name;
        }
        auto alias = "codec_" + decl.name + "_dependency" + std::to_string(imports.size());
        while (find(alias, ctx, decl.span) != no_symbol) alias += "_";
        found = imports.emplace(top, std::pair{std::move(alias), std::move(path)}).first;
    }
    return found->second.first + suffixes.at(top);
}

void Lowering::expand_derivation(SymbolId id) {
    const auto& decl = *entries_[id].declaration;
    const auto& syntax = *decl.derive;
    const auto ctx = context(symbol(id).owner);
    const auto at = syntax.generator.span;
    const auto invalid_generator = [&] {
        return Diagnostic(at, "'" + syntax.generator.name +
            "' is not a generator; this release derives 'Std.Encoding.Json.Codec<T>'").with_code("E0579");
    };
    SymbolId generator;
    try { generator = qualified(syntax.generator.name, at, ctx, true); }
    catch (const Diagnostic&) { throw invalid_generator(); }
    if (symbol(generator).kind == SymbolKind::Alias) generator = symbol(generator).type.symbol;
    const auto file = library_files_.find("std/encoding/json.nra");
    if (file == library_files_.end()) throw invalid_generator();
    const auto json = own_member(file->second, "Json");
    if (json == no_symbol || generator == no_symbol || own_member(json, "Codec") != generator) throw invalid_generator();
    if (syntax.arguments.size() != 1)
        throw Diagnostic(at, "Json.Codec expects one type argument").with_code("E0417");
    const auto root = type(syntax.arguments.front(), ctx);
    if ((root.kind != TypeKind::Object || !record_class(root.symbol)) && root.kind != TypeKind::Enum)
        throw Diagnostic(syntax.arguments.front().span, "a JSON codec root must be a record or an enum; got " + describe(root))
            .with_code("E0590");
    std::vector<IrType> active;
    std::vector<CodecShape> shapes;
    codec_shape(root, syntax.arguments.front().span, describe(root), active, shapes);
    codec_mappings(shapes, syntax, ctx);

    // Each alias is private to this generated module and binds by symbol,
    // including across a .nir import. Source cannot spell its '$' prefix.
    const auto library = [&](const char* path, const char* name) {
        return library_module(path, name, at, ctx, std::string("JSON derivation needs Std.") + name);
    };
    const auto result = library("std/core/result.nra", "Result");
    const auto maybe = library("std/core/optional.nra", "Optional");
    std::map<std::string, SymbolId> aliases{
        {"CodecJsonModule", json}, {"CodecJson", own_member(json, "JsonValue")},
        {"CodecResult", own_member(result, "Result")}, {"CodecOptional", own_member(maybe, "Optional")},
        {"CodecError", library("std/core/error.nra", "Error")},
        {"CodecSupport", library("std/encoding/json-codec-support.nra", "JsonCodecSupport")}
    };
    std::map<SymbolId, std::string> type_names;
    const auto spell = [&](const auto& self, const IrType& value) -> std::string {
        if (value.kind == TypeKind::Tuple) {
            std::string name = "[";
            for (const auto& arg : value.arguments) {
                if (name.size() > 1) name += ", ";
                name += self(self, arg);
            }
            return name + "]";
        }
        if (value.symbol == no_symbol) return value.name;
        const auto [it, added] = type_names.try_emplace(value.symbol, "CodecType" + std::to_string(type_names.size()));
        if (added) aliases.emplace(it->second, value.symbol);
        std::string name = it->second;
        if (!value.arguments.empty()) {
            name += "<";
            for (const auto& arg : value.arguments) {
                if (name.back() != '<') name += ", ";
                name += self(self, arg);
            }
            name += ">";
        }
        return name;
    };
    for (auto& shape : shapes) shape.spelling = spell(spell, shape.type);
    const auto expansion = json_codec_declaration(decl.name, shapes);
    auto tokens = lex(expansion, at.file);
    for (const auto& [name, target] : aliases) entries_[id].names.emplace("$" + name, target);
    const auto alias_token = [&](std::size_t i) {
        // A field, variant or named argument can itself be named CodecJson.
        // Only bare references emitted by the generator denote our aliases.
        return i > 1 && tokens[i].kind == TokenKind::Identifier && tokens[i - 1].text != "." &&
               tokens[i + 1].text != ":";
    };
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (alias_token(i) && aliases.contains(tokens[i].text)) tokens[i].text = "$" + tokens[i].text;
        tokens[i].span = at;
    }
    auto generated = parse(tokens);
    auto& children = derived_members_[id];
    const auto attribute = attribute_list({attribute_text("derive:json.codec"), attribute_span(at)});
    set_attribute(symbol(id), AttributeId::Generated, attribute);
    for (auto& member : generated.statements.front().declaration->members) {
        auto& owned = generated_declarations_.emplace_back(std::move(member));
        const auto child = declare(owned, id, SymbolKind::Global);
        set_attribute(symbol(child), AttributeId::Generated, attribute);
        children.push_back(child);
    }

    // The generator implements the trusted signatures, but it does not
    // confer the interface on any user class or on a primitive companion.
    if (symbol(generator).kind != SymbolKind::Interface || symbol(generator).generics.size() != 1)
        throw Diagnostic(at, "invalid Std.Encoding.Json.Codec contract").with_code("E0899");
    const Substitution replacements{{symbol(generator).generics.front(), root}};
    for (const auto* name : {"to_json", "encode", "from_json", "decode"}) {
        const auto required = own_member(generator, name);
        const auto implementation = own_member(id, name);
        if (required == no_symbol || !static_requirement(required))
            throw Diagnostic(at, "the JSON codec contract lacks static function '" + std::string(name) + "'")
                .with_code("E0899");
        resolve(required);
        resolve(implementation);
        const auto& contract = symbol(required);
        const auto& generated_function = symbol(implementation);
        if (contract.parameters.size() != 1 || generated_function.parameters.size() != 1 ||
            generated_function.type != substitute(contract.type, replacements) ||
            symbol(generated_function.parameters.front()).type != substitute(symbol(contract.parameters.front()).type, replacements))
            throw Diagnostic(at, "generated '" + std::string(name) + "' has a different type from Std.Encoding.Json.Codec")
                .with_code("E0899").label(name_span(required), "contract declared here");
        check_parameter_names(implementation, required);
    }

    CodecImports schema_imports;
    std::map<std::string, std::string> display;
    std::set<std::string> occupied;
    for (const auto& [target, placeholder] : type_names) {
        const auto name = codec_source_name(target, ctx, decl, schema_imports);
        display.emplace(placeholder, name);
        occupied.insert(name.substr(0, name.find('.')));
    }
    std::string imports;
    const auto printed_import = [&](std::string alias, std::string_view path) {
        while (!occupied.insert(alias).second) alias += "_";
        imports += "    let " + alias + " = import:" + std::string(path) + ";\n";
        return alias;
    };
    display["CodecJsonModule"] = printed_import("codec_json", "Std.Encoding.Json");
    display["CodecJson"] = display.at("CodecJsonModule") + ".JsonValue";
    display["CodecResult"] = printed_import("codec_result", "Std.Core.Result") + ".Result";
    display["CodecOptional"] = printed_import("codec_optional", "Std.Core.Optional") + ".Optional";
    display["CodecError"] = printed_import("codec_errors", "Std.Core.Error");
    display["CodecSupport"] = printed_import("codec_support", "Std.Encoding.JsonCodecSupport");
    auto printed = expansion;
    auto display_tokens = lex(expansion, at.file);
    for (std::size_t i = display_tokens.size(); i-- > 0;) {
        if (!alias_token(i)) continue;
        const auto& token = display_tokens[i];
        if (const auto alias = display.find(token.text); alias != display.end())
            printed.replace(token.span.offset, token.span.length, alias->second);
    }
    for (const auto& [module, binding] : schema_imports)
        imports += "    let " + binding.first + " = import:" + binding.second + ";\n";
    printed.insert(printed.find('\n') + 1, imports);
    if (decl.exported) printed.insert(0, "export ");
    DerivedCodec info{id, decl.span, syntax.close_brace, std::move(printed), {}};
    for (const auto& shape : shapes) {
        if (shape.type.symbol == no_symbol) continue;
        const auto prefix = codec_source_name(shape.type.symbol, ctx, decl, schema_imports) + ".";
        for (const auto& slot : shape.slots)
            info.members.push_back({slot.symbol, {}, prefix + slot.name, slot.wire, false, false});
        for (const auto& variant : shape.variants) {
            info.members.push_back({variant.symbol, {}, prefix + variant.name, variant.wire, true, false});
            if (variant.named) for (const auto& slot : variant.slots)
                info.members.push_back({variant.symbol, slot.name, prefix + variant.name + "." + slot.name,
                                        slot.wire, false, false});
        }
    }
    for (const auto& mapping : syntax.mappings) {
        std::string owner;
        for (std::size_t i = 0; i + 1 < mapping.parts.size(); ++i)
            owner += (owner.empty() ? "" : ".") + mapping.parts[i];
        auto target = qualified(owner, mapping.path, ctx);
        if (symbol(target).kind == SymbolKind::Alias) target = symbol(target).type.symbol;
        std::string payload;
        if (symbol(target).kind != SymbolKind::Variant) target = own_member(target, mapping.parts.back());
        else {
            payload = mapping.parts.back();
            program_.name_uses.emplace_back(mapping.part_spans[mapping.part_spans.size() - 2], target);
        }
        for (auto& member : info.members) if (member.symbol == target && member.payload_field == payload)
            member.pinned = true;
        if (payload.empty()) program_.name_uses.emplace_back(mapping.part_spans.back(), target);
    }
    for (const auto& [module, binding] : schema_imports) info.schema_imports.push_back(binding);
    program_.derived_codecs.push_back(std::move(info));
}
