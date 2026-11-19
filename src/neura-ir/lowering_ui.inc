#include "html_bindings.inc"

class Lowering::UiLowering {
public:
    UiLowering(Lowering& compiler, Context& context, const Span& span)
        : compiler_(compiler),
          ui_(compiler_.library_module("std/ui.nra", "Ui", span, context, "an HTML factory requires Std.Ui")),
          build_(compiler_.library_module("std/ui/internal/build.nra", "Build", span, context,
                                          "an HTML factory requires Std.Ui.Internal.Build")) {}

    IrType view_type(const Span& span) { return type_member(ui_, "View", span); }

    std::vector<IrNode> body(SymbolId function, Context& context, const TemplateSyntax& syntax) {
        syntax_ = &syntax;
        const auto file = source_file(compiler_.program_, lexical_owner(context));
        const auto constant = compiler_.add(SymbolKind::Global,
            "$ui_descriptor" + std::to_string(compiler_.program_.symbols.size()), syntax.span, file);
        compiler_.symbol(constant).type = string_type;
        compiler_.symbol(constant).constant = true;
        auto descriptor = node(IrOp::Load, syntax.span, string_type);
        descriptor.symbol = constant;
        descriptor_ = std::move(descriptor);

        std::vector<IrNode> code;
        const auto root = new_fragment();
        auto value = fragment(root, syntax.nodes, syntax.span, code, context, 0);
        for (const auto& [id, span] : labels_) {
            if (!ids_.contains(id))
                throw Diagnostic(span, "invalid static document structure").with_code("E1014")
                    .label("no element in this template has the static id '" + id + "'");
        }

        auto literal = node(IrOp::String, syntax.span, string_type);
        Json::Array fragments;
        for (auto& fragment : fragments_) {
            fragments.emplace_back(Json::Object{
                {"nodes", std::move(fragment.nodes)}, {"slots", std::move(fragment.slots)}
            });
        }
        literal.text = Json(Json::Object{{"encoding", 1}, {"fragments", std::move(fragments)}}).dump();
        auto init = node(IrOp::Initialize, syntax.span);
        init.symbol = constant;
        init.operands.push_back(std::move(literal));
        compiler_.template_constants_[file].push_back(std::move(init));
        add_attribute(compiler_.symbol(function), AttributeId::UiFactory, attribute_symbol(constant));

        auto result = node(IrOp::Return, syntax.span);
        result.operands.push_back(std::move(value));
        code.push_back(std::move(result));
        return code;
    }

private:
    using Json = tooling::Json;
    using AttributeKind = TemplateAttribute::Kind;

    struct Fragment {
        Json::Array nodes;
        Json::Array slots;
    };

    Lowering& compiler_;
    SymbolId ui_;
    SymbolId build_;
    const TemplateSyntax* syntax_ = nullptr;
    IrNode descriptor_{};
    std::vector<Fragment> fragments_;
    std::map<std::string, Span, std::less<>> ids_;
    std::vector<std::pair<std::string, Span>> labels_;
    std::size_t node_count_ = 0;
    std::size_t slot_count_ = 0;

    SymbolId required_member(SymbolId owner, std::string_view name, const Span& span) {
        const auto id = compiler_.own_member(owner, name);
        if (id == no_symbol) {
            throw Diagnostic(span, "missing UI support member '" + std::string(name) + "'").with_code("E0899");
        }
        compiler_.resolve(id);
        return id;
    }

    IrType type_member(SymbolId module, std::string_view name, const Span& span) {
        return compiler_.symbol(required_member(module, name, span)).type;
    }

    IrNode invoke(std::string_view name, const Span& span, std::vector<IrNode> arguments = {},
                  std::optional<IrType> argument_type = std::nullopt) {
        const auto member = required_member(build_, name, span);
        auto call = node(IrOp::Call, span, compiler_.symbol(member).type);
        call.symbol = member;
        call.operands = std::move(arguments);
        if (argument_type) call.type_arguments.push_back(std::move(*argument_type));
        return call;
    }

    static void discard(IrNode value, std::vector<IrNode>& code) {
        auto statement = node(IrOp::Discard, value.span);
        statement.operands.push_back(std::move(value));
        code.push_back(std::move(statement));
    }

    void put(const IrNode& builder, std::uint32_t slot, const Span& span, IrNode value,
             std::vector<IrNode>& code) {
        const auto value_type = value.type;
        discard(invoke("put", span, {builder, integer(slot, uint32_type, span), std::move(value)}, value_type), code);
    }

    static IrNode integer(std::uint32_t value, const IrType& type, const Span& span) {
        auto literal = node(IrOp::Integer, span, type);
        literal.text = std::to_string(value);
        return literal;
    }

    static IrNode stringify(IrNode value) {
        if (value.type.kind == TypeKind::String) return value;
        auto text = node(IrOp::ValueText, value.span, string_type);
        text.operands.push_back(std::move(value));
        return text;
    }

    [[noreturn]] void limit(const char* id, std::size_t used, std::size_t maximum) const {
        throw Diagnostic(syntax_->span, "template or stylesheet exceeds a limit").with_code("E1015")
            .label(std::string(id) + ": " + std::to_string(used) + " exceeds " + std::to_string(maximum));
    }

    std::uint32_t new_fragment() {
        fragments_.emplace_back();
        return static_cast<std::uint32_t>(fragments_.size() - 1);
    }

    std::uint32_t new_slot(std::uint32_t fragment, Json::Object slot) {
        if (++slot_count_ > 1024) limit("U-3 dynamic slots", slot_count_, 1024);
        auto& slots = fragments_[fragment].slots;
        const auto index = static_cast<std::uint32_t>(slots.size());
        slots.emplace_back(std::move(slot));
        return index;
    }

    static Json slot_node(std::uint32_t slot) {
        return Json::Object{{"kind", "slot"}, {"slot", slot}};
    }

    IrNode fragment(std::uint32_t index, const std::vector<TemplateNode>& nodes, const Span& span,
                    std::vector<IrNode>& code, Context& context, std::size_t depth) {
        auto builder = compiler_.save_temporary(invoke("begin", span), code, context, "$ui_slots");
        auto children = render(nodes, index, builder, code, context, depth);
        fragments_[index].nodes = std::move(children);
        return invoke("finish", span, {builder, descriptor_, integer(index, uint32_type, span)});
    }

    static std::string line_endings(std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] != '\r') {
                result += value[i];
                continue;
            }
            result += '\n';
            if (i + 1 < value.size() && value[i + 1] == '\n') ++i;
        }
        return result;
    }

    Json::Array render(const std::vector<TemplateNode>& nodes, std::uint32_t index, const IrNode& builder,
                       std::vector<IrNode>& code, Context& context, std::size_t depth, bool svg_contents = false) {
        Json::Array children;
        for (const auto& child : nodes) {
            if (++node_count_ > 4096) limit("U-1 nodes", node_count_, 4096);
            if (const auto* text = std::get_if<TemplateText>(&child.value)) {
                children.emplace_back(Json::Object{{"kind", "text"}, {"value", line_endings(text->value)}});
            } else if (const auto* element = std::get_if<std::shared_ptr<TemplateElement>>(&child.value)) {
                if (depth >= 64) limit("U-2 nesting depth", depth + 1, 64);
                children.push_back(render_element(**element, index, builder, code, context, depth, svg_contents));
            } else {
                if (svg_contents)
                    throw Diagnostic(child.span, "unknown attribute, property or event").with_code("E1002")
                        .label("SVG content is static");
                if (const auto* hole = std::get_if<TemplateHole>(&child.value)) {
                    children.push_back(render_hole(*hole, child.span, index, builder, code, context));
                } else {
                    if (depth >= 64) limit("U-2 nesting depth", depth + 1, 64);
                    if (const auto* choice = std::get_if<std::shared_ptr<TemplateIf>>(&child.value))
                        children.push_back(render_if(**choice, child.span, index, builder, code, context, depth));
                    else
                        children.push_back(render_for(*std::get<std::shared_ptr<TemplateFor>>(child.value), child.span,
                                                       index, builder, code, context, depth));
                }
            }
        }
        return children;
    }

    Json render_hole(const TemplateHole& hole, const Span& span, std::uint32_t index, const IrNode& builder,
                     std::vector<IrNode>& code, Context& context) {
        auto value = compiler_.value(hole.expression, context);
        const bool child = value.type == view_type(span);
        const bool text = value.type.kind == TypeKind::String || value.type.kind == TypeKind::Integer ||
                          value.type.name == "float32" || value.type.name == "float64";
        if (!child && !text)
            throw Diagnostic(span, "value cannot be rendered").with_code("E1004")
                .label("got " + describe(value.type) + "; write text, a number or a ui.View");
        const auto slot = new_slot(index, Json::Object{{"kind", child ? "child" : "text"}});
        if (text) value = stringify(std::move(value));
        put(builder, slot, span, std::move(value), code);
        return slot_node(slot);
    }

    static const Span& attribute_span(const TemplateAttribute& attribute) {
        return attribute.value_span ? *attribute.value_span : attribute.name_span;
    }

    static bool starts_with_name(std::string_view name, std::string_view prefix) {
        return name.starts_with(prefix) && name.size() > prefix.size();
    }

    bool optional_string(const IrType& type) const {
        return type.kind == TypeKind::Enum && type.arguments.size() == 1 && type.arguments.front() == string_type &&
               nominal(type, "std/core/optional.nra", "Optional");
    }

    bool nominal(const IrType& type, std::string_view file, std::string_view name) const {
        if (type.symbol == no_symbol || compiler_.symbol(type.symbol).name != name) return false;
        const auto source = source_file(compiler_.program_, type.symbol);
        return source != no_symbol && compiler_.symbol(source).library_path == file;
    }

    [[noreturn]] void wrong_attribute(const TemplateAttribute& attribute, const IrType& observed,
                                      std::string_view expected) const {
        throw Diagnostic(attribute_span(attribute), "attribute or property value of the wrong type").with_code("E1005")
            .label("expected " + std::string(expected) + ", got " + describe(observed));
    }

    [[noreturn]] void wrong_name(const TemplateAttribute& attribute, std::vector<std::string> names) const {
        auto error = Diagnostic(attribute.name_span, "unknown attribute, property or event").with_code("E1002")
            .label("'" + attribute.name + "' cannot bind this element");
        if (const auto nearest = closest_name(attribute.name, names))
            error.help("did you mean '" + *nearest + "'?");
        throw error;
    }

    const html_bindings::Event& find_event(const TemplateElement& element, const TemplateAttribute& attribute) const {
        std::vector<std::string> names;
        for (const auto& event : html_bindings::events) {
            if (!event.elements.empty() && !html_bindings::contains(event.elements, element.name)) continue;
            if (event.name == attribute.name) return event;
            names.emplace_back(event.name);
        }
        wrong_name(attribute, std::move(names));
    }

    bool property_flag(const TemplateElement& element, const TemplateAttribute& attribute) const {
        std::vector<std::string> names;
        for (const auto& property : html_bindings::properties) {
            if (!html_bindings::contains(property.elements, element.name)) continue;
            if (property.name == attribute.name) return property.flag;
            names.emplace_back(property.name);
        }
        wrong_name(attribute, std::move(names));
    }

    void require_attribute(const TemplateElement& element, const TemplateAttribute& attribute) const {
        const auto specific = html_bindings::local_attributes(element.name, element.svg);
        const auto& name = attribute.name;
        if (html_bindings::contains(html_bindings::global_attributes, name) ||
            html_bindings::contains(specific, name) || starts_with_name(name, "aria-") ||
            starts_with_name(name, "data-") ||
            (element.svg && html_bindings::contains(html_bindings::svg_presentation, name))) return;
        std::vector<std::string> names;
        html_bindings::append_words(names, html_bindings::global_attributes);
        html_bindings::append_words(names, specific);
        if (element.svg) html_bindings::append_words(names, html_bindings::svg_presentation);
        wrong_name(attribute, std::move(names));
    }

    void static_attribute(const TemplateElement& element, const TemplateAttribute& attribute,
                          std::string_view value) {
        const auto& span = attribute_span(attribute);
        if (html_bindings::contains(html_bindings::url_attributes, attribute.name) && !html_bindings::allowed_url(value))
            throw Diagnostic(span, "URL not allowed").with_code("E1007")
                .label("use a relative URL or the https, http, mailto or tel scheme");
        if (attribute.name == "srcset") {
            for (const auto& [begin, end] : srcset_urls(value)) {
                const auto candidate = value.substr(begin, end - begin);
                const auto colon = candidate.find(':');
                std::string scheme(colon < candidate.find_first_of("/?#") ? candidate.substr(0, colon) : "");
                for (auto& c : scheme) {
                    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                }
                if (!html_bindings::allowed_url(candidate) || scheme == "mailto" || scheme == "tel")
                    throw Diagnostic(span, "URL not allowed").with_code("E1007")
                        .label("each srcset candidate needs a relative URL or the https or http scheme");
            }
        }
        if (attribute.name == "id") {
            const auto [first, inserted] = ids_.emplace(value, span);
            if (!inserted)
                throw Diagnostic(span, "invalid static document structure").with_code("E1014")
                    .label("this static id is written more than once").label(first->second, "first written here");
        } else if (element.name == "label" && attribute.name == "for") {
            labels_.emplace_back(value, span);
        }
    }

    Json bind_attribute(const TemplateElement& element, const TemplateAttribute& attribute, std::uint32_t index,
                        const IrNode& builder, std::vector<IrNode>& code, Context& context, bool svg_contents) {
        const auto& name = attribute.name;
        const auto* expression = std::get_if<Expr>(&attribute.value);
        if (element.svg && (expression || attribute.kind != AttributeKind::Attribute) &&
            (svg_contents || !(name == "class" || starts_with_name(name, "aria-") ||
                               attribute.kind == AttributeKind::Variable)))
            throw Diagnostic(attribute.name_span, "unknown attribute, property or event").with_code("E1002")
                .label("SVG content is static; class, aria-* and var: can bind the root SVG element");

        const auto& span = attribute_span(attribute);
        if (attribute.kind == AttributeKind::Attribute) require_attribute(element, attribute);
        const bool boolean = attribute.kind == AttributeKind::Attribute &&
                             html_bindings::contains(html_bindings::boolean_attributes, name);
        if (!expression) {
            const auto* value = std::get_if<std::string>(&attribute.value);
            if (boolean && value) wrong_attribute(attribute, string_type, "a bare flag or a bool hole");
            auto text = value ? line_endings(*value) : std::string{};
            static_attribute(element, attribute, text);
            if (attribute.asset_error) throw *attribute.asset_error;
            if (!attribute.assets.empty()) {
                // Local media files: the bundle holds each under its digest.
                auto& files = compiler_.program_.bundle_files;
                for (const auto& asset : attribute.assets) {
                    if (std::ranges::none_of(files, [&](const IrBundleFile& file) { return file.path == asset->path; }))
                        files.push_back({asset->path, asset->sha256, asset->contents});
                }
                text = attribute.asset_target;
            }
            return Json::Object{{"name", name}, {"value", text}};
        }

        Json::Object slot{{"name", name}};
        std::optional<IrType> expected;
        const html_bindings::Event* event = nullptr;
        switch (attribute.kind) {
        case AttributeKind::Attribute:
            slot.emplace("kind", boolean ? "flag" : "attribute");
            break;
        case AttributeKind::Property:
            slot.emplace("kind", "property");
            expected = property_flag(element, attribute) ? bool_type : string_type;
            break;
        case AttributeKind::Variable:
            slot.emplace("kind", "custom-property");
            expected = type_member(ui_, "CssValue", span);
            break;
        case AttributeKind::Event:
            slot.emplace("kind", "handler");
            event = &find_event(element, attribute);
            expected = compiler_.callable_type({type_member(ui_, event->type, span), void_type}, span);
            break;
        }
        auto value = compiler_.value(*expression, context);
        if (event) {
            Json::Array modifiers;
            std::set<std::string, std::less<>> seen;
            for (const auto& modifier : attribute.modifiers) {
                const bool valid = modifier.text == "prevent_default" ? event->prevent_default :
                                   modifier.text == "stop_propagation" && event->stop_propagation;
                if (!valid || !seen.insert(modifier.text).second)
                    throw Diagnostic(modifier.span, "modifier not valid for this event").with_code("E1003")
                        .label("use each supported modifier once on '" + std::string(event->name) + "'");
                modifiers.emplace_back(modifier.text);
            }
            if (value.type != *expected)
                throw Diagnostic(span, "handler does not match the event").with_code("E1006")
                    .label("expected " + describe(*expected) + ", got " + describe(value.type));
            slot.emplace("modifiers", std::move(modifiers));
        } else if (expected) {
            if (value.type != *expected) wrong_attribute(attribute, value.type, describe(*expected));
        } else if (boolean) {
            if (value.type != bool_type) wrong_attribute(attribute, value.type, "bool");
        } else if (html_bindings::contains(html_bindings::url_attributes, name)) {
            if (value.type != type_member(ui_, "Url", span))
                throw Diagnostic(span, "URL not allowed").with_code("E1007")
                    .label("a dynamic URL must have type ui.Url").help("use ui.url(text) to validate a URL");
        } else if (name == "srcset") {
            throw Diagnostic(span, "URL not allowed").with_code("E1007")
                .label("srcset must use a quoted value");
        } else if (value.type != string_type && !optional_string(value.type)) {
            const bool numeric = html_bindings::contains(html_bindings::numeric_attributes, name) ||
                (name == "value" && html_bindings::contains("progress meter li data", element.name));
            const bool aria = starts_with_name(name, "aria-");
            const bool data = starts_with_name(name, "data-");
            if ((numeric || data) && value.type.kind == TypeKind::Integer) value = stringify(std::move(value));
            else if (aria && value.type == bool_type) value = stringify(std::move(value));
            else wrong_attribute(attribute, value.type, numeric || data ? "string, Optional<string> or integer" :
                                 aria ? "string, Optional<string> or bool" : "string or Optional<string>");
        }
        const auto id = new_slot(index, std::move(slot));
        put(builder, id, span, std::move(value), code);
        return Json::Object{{"slot", id}};
    }

    Json render_element(const TemplateElement& element, std::uint32_t index, const IrNode& builder,
                        std::vector<IrNode>& code, Context& context, std::size_t depth, bool svg_contents) {
        const auto tags = element.svg ? html_bindings::svg_elements : html_bindings::html_elements;
        if (!html_bindings::contains(tags, element.name)) {
            std::vector<std::string> names;
            html_bindings::append_words(names, tags);
            auto error = Diagnostic(element.name_span, "unknown element").with_code("E1001")
                .label("this element is unavailable in templates");
            if (const auto nearest = closest_name(element.name, names))
                error.help("did you mean '<" + *nearest + ">'?");
            throw error;
        }
        if (element.name == "img" && std::ranges::none_of(element.attributes, [](const TemplateAttribute& attr) {
                return attr.kind == AttributeKind::Attribute && attr.name == "alt";
            }))
            throw Diagnostic(element.opening, "invalid static document structure").with_code("E1014")
                .label("an image needs an 'alt' attribute");
        Json::Array attributes;
        for (const auto& attribute : element.attributes)
            attributes.push_back(bind_attribute(element, attribute, index, builder, code, context, svg_contents));
        auto children = render(element.children, index, builder, code, context, depth + 1, element.svg);
        return Json::Object{{"attributes", std::move(attributes)}, {"children", std::move(children)},
                            {"kind", "element"}, {"name", element.name}, {"svg", element.svg}};
    }

    Json render_if(const TemplateIf& choice, const Span& span, std::uint32_t index, const IrNode& builder,
                   std::vector<IrNode>& code, Context& context, std::size_t depth) {
        const auto slot = new_slot(index, Json::Object{});
        Json::Array branch_fragments;
        auto* destination = &code;
        for (const auto& branch : choice.branches) {
            std::optional<IrNode> condition;
            if (branch.condition) condition = compiler_.value(*branch.condition, context, &bool_type);
            const auto child = new_fragment();
            branch_fragments.emplace_back(child);
            std::vector<IrNode> statements;
            context.scopes.emplace_back();
            auto view = fragment(child, branch.children, branch.span, statements, context, depth + 1);
            context.scopes.pop_back();
            put(builder, slot, branch.span, std::move(view), statements);
            if (!condition) {
                *destination = std::move(statements);
                break;
            }
            auto test = node(IrOp::Branch, span);
            test.operands.push_back(std::move(*condition));
            test.body = std::move(statements);
            destination->push_back(std::move(test));
            destination = &destination->back().alternative;
        }
        fragments_[index].slots[slot] = Json::Object{{"fragments", std::move(branch_fragments)}, {"kind", "block"}};
        return slot_node(slot);
    }

    IrNode frozen_member(const IrNode& input, std::string_view name, const Span& span, std::vector<IrNode> args,
                         const IrType& output) {
        const auto member = compiler_.value_member(input.type, name, true);
        if (member == no_symbol)
            throw Diagnostic(span, "missing FrozenList render member").with_code("E0899");
        compiler_.resolve(member);
        const auto type = compiler_.symbol(member).kind == SymbolKind::Field ? IrOp::Field : IrOp::VirtualCall;
        auto result = node(type, span, output);
        result.symbol = member;
        result.operands.push_back(input);
        for (auto& argument : args) result.operands.push_back(std::move(argument));
        return result;
    }

    Json render_for(const TemplateFor& loop, const Span& span, std::uint32_t index, const IrNode& builder,
                    std::vector<IrNode>& code, Context& context, std::size_t depth) {
        auto source = compiler_.value(loop.head.input, context);
        const bool array = source.type.kind == TypeKind::Array;
        if (!array && !(source.type.kind == TypeKind::Object && source.type.arguments.size() == 1 &&
                        nominal(source.type, "std/collections/frozen.nra", "FrozenList")))
            throw Diagnostic(loop.head.input.span, "value cannot be iterated by a template").with_code("E1008")
                .label("use an array or FrozenList, got " + describe(source.type));
        const auto element = source.type.arguments.front();
        const auto input = compiler_.save_temporary(std::move(source), code, context, "$ui_source");
        const auto items = compiler_.save_temporary(invoke("begin_items", span), code, context, "$ui_items");
        const auto position = compiler_.save_temporary(integer(0, int32_type, span), code, context, "$ui_index", false);
        const auto slot = new_slot(index, Json::Object{});
        const auto child = new_fragment();

        auto collection_length = node(IrOp::Length, span, int32_type);
        auto access = node(IrOp::Index, loop.head.binding.span, element);
        if (array) {
            collection_length.operands.push_back(input);
            access.operands = {input, position};
        } else {
            collection_length = frozen_member(input, "length", span, {}, int32_type);
            access = frozen_member(input, "get", loop.head.binding.span, {position}, element);
        }
        auto condition = node(IrOp::Binary, span, bool_type);
        condition.text = "<";
        condition.operands = {position, std::move(collection_length)};
        auto iteration = node(IrOp::Loop, span);
        iteration.operands.push_back(std::move(condition));

        context.scopes.emplace_back();
        auto declared = loop.head.type ? compiler_.type(*loop.head.type, context) : element;
        compiler_.require_type(element, declared, loop.head.binding.span);
        const auto binding = compiler_.add(SymbolKind::Local, loop.head.binding.text, loop.head.binding.span, context.owner);
        compiler_.symbol(binding).type = std::move(declared);
        compiler_.symbol(binding).constant = loop.head.constant;
        context.scopes.back().emplace(loop.head.binding.text, binding);
        auto initialize = node(IrOp::Initialize, loop.head.binding.span);
        initialize.symbol = binding;
        initialize.operands.push_back(std::move(access));
        iteration.body.push_back(std::move(initialize));
        auto key = compiler_.value(loop.head.key, context);
        if (key.type.kind != TypeKind::Integer && key.type.kind != TypeKind::String)
            throw Diagnostic(loop.head.key.span, "key of the wrong type").with_code("E1009")
                .label("a key must be a string or fixed-width integer, got " + describe(key.type));
        key = compiler_.save_temporary(stringify(std::move(key)), iteration.body, context, "$ui_key");
        const auto view = fragment(child, loop.children, span, iteration.body, context, depth + 1);
        discard(invoke("add_item", span, {items, key, view}), iteration.body);
        context.scopes.pop_back();

        auto increment = node(IrOp::Binary, span, int32_type);
        increment.text = "+";
        increment.operands = {position, integer(1, int32_type, span)};
        auto advance = node(IrOp::Store, span);
        advance.operands = {position, std::move(increment)};
        iteration.body.push_back(std::move(advance));
        code.push_back(std::move(iteration));
        put(builder, slot, span, items, code);
        fragments_[index].slots[slot] = Json::Object{{"fragments", Json::Array{child}}, {"kind", "block"}, {"list", true}};
        return slot_node(slot);
    }
};

const TemplateSyntax& template_source(const Expr& expression) {
    const auto& factory = *expression.html_factory;
    if (const auto* inline_body = std::get_if<TemplateSyntax>(&factory.source)) return *inline_body;
    if (factory.external) return factory.external->body;
    throw Diagnostic(std::get<Token>(factory.source).span, "template or stylesheet file cannot be used")
        .with_code("E1011").label("the template must be loaded with its declaring source file");
}

IrType Lowering::html_view_type(const Span& span, const Context& context) {
    const auto module = library_module("std/ui.nra", "Ui", span, context, "an HTML factory requires Std.Ui");
    const auto view = own_member(module, "View");
    if (view == no_symbol || symbol(view).kind != SymbolKind::Class)
        throw Diagnostic(span, "Std.Ui must declare the class View").with_code("E0899");
    resolve(view);
    return symbol(view).type;
}

std::vector<IrNode> Lowering::html_body(SymbolId id, const Expr& expression, Context& context) {
    UiLowering render(*this, context, expression.span);
    return render.body(id, context, template_source(expression));
}

IrNode Lowering::html_factory(const Expr& expression, Context& context) {
    const auto& syntax = *expression.html_factory;
    const auto& span = expression.span;
    UiLowering render(*this, context, span);
    if (!syntax.overloads.empty())
        throw Diagnostic(span, "an overloaded factory requires a named function").with_code("E0433");

    std::vector<IrType> parameters;
    std::vector<std::string> names;
    for (const auto& parameter : syntax.parameters) {
        if (parameter.variadic)
            throw Diagnostic(parameter.span, "a variadic parameter requires a named function").with_code("E0207");
        parameters.push_back(parameter_type(parameter, context));
        names.push_back(parameter.name);
        check_tags(parameter.tags, TagTarget::Parameter, parameter.span);
    }
    parameters.push_back(render.view_type(span));
    const auto callable = callable_type(std::move(parameters), span);
    ClosureEnvironment environment{closure_layout(span, context), context, {}, {}};
    const auto method = closure_method(environment.layout, callable, span, names);
    Context inner{method, method, context.receiver, 0, {entries_[method].names}, &environment};
    auto body = render.body(method, inner, template_source(expression));
    program_.bodies.push_back({method, std::move(body)});
    auto result = closure_value(callable, environment, span);
    if (syntax.retained) result.text = owned_lambda;
    return result;
}

namespace {

// The constant's name qualified by the modules that declare it, within its file.
std::string qualified_constant(const IrProgram& program, SymbolId id) {
    std::string result = program.symbols[id].name;
    for (auto owner = program.symbols[id].owner;
         owner != no_symbol && program.symbols[owner].kind != SymbolKind::File; owner = program.symbols[owner].owner)
        result = program.symbols[owner].name + "." + result;
    return result;
}

} // namespace

IrNode Lowering::stylesheet(const Expr& expr, Context& context) {
    const auto& syntax = *expr.stylesheet;
    if (!syntax.resolved)
        throw Diagnostic(syntax.source.span, "template or stylesheet file cannot be used").with_code("E1011")
            .label("the stylesheet must be loaded with its declaring source file");
    const auto& sheet = *syntax.resolved;
    (void)context;
    const auto found = stylesheet_constants_.find(&expr);
    const auto constant = found == stylesheet_constants_.end() ? no_symbol : found->second;
    if (constant == no_symbol || symbol(constant).kind != SymbolKind::Global || !symbol(constant).constant)
        throw Diagnostic(expr.span, "html or css form not allowed here").with_code("E0255")
            .label("a stylesheet must be the whole initializer of a module-level 'const'");
    // Without a project the package name is empty. The key of a file is its
    // path below the source root; an inline sheet adds the qualified name of
    // its constant to the path of the declaring file.
    const auto key = sheet.external ? sheet.path : sheet.path + ":" + qualified_constant(program_, constant);
    auto rewritten = rewrite_stylesheet(sheet.text, sheet.body, "", key, sheet.targets);
    auto digest = util::sha256_hex(rewritten.text);
    const auto path = "styles/" + digest + ".css";
    const auto add_file = [&](IrBundleFile file) {
        if (std::ranges::none_of(program_.bundle_files, [&](const IrBundleFile& item) { return item.path == file.path; }))
            program_.bundle_files.push_back(std::move(file));
    };
    for (const auto& file : sheet.files) add_file({file.path, file.sha256, file.contents});
    add_file({path, digest, std::move(rewritten.text)});
    stylesheet_links_.push_back({constant, expr.span.offset, path});
    if (syntax.module)
        style_modules_[constant] = {std::move(rewritten.classes), std::move(rewritten.keyframes)};

    const auto build = library_module("std/ui/internal/build.nra", "Build", expr.span, context,
                                      "a stylesheet requires Std.Ui.Internal.Build");
    const auto member = own_member(build, syntax.module ? "style_module" : "stylesheet");
    if (member == no_symbol) throw Diagnostic(expr.span, "missing UI support member").with_code("E0899");
    resolve(member);
    auto call = node(IrOp::Call, expr.span, symbol(member).type);
    call.symbol = member;
    auto text = node(IrOp::String, expr.span, string_type);
    text.text = std::move(digest);
    call.operands.push_back(std::move(text));
    return call;
}

std::optional<IrNode> Lowering::style_module_name(const Expr& expr, Context& context) {
    const auto& callee = expr.operands.front();
    if (callee.kind != ExprKind::Member || (callee.text != "class_name" && callee.text != "keyframes_name"))
        return std::nullopt;
    const auto& receiver = callee.operands.front();
    if (receiver.kind != ExprKind::Name && receiver.kind != ExprKind::Member) return std::nullopt;
    const auto target = expression(receiver, context);
    const auto is_module = [&](const IrType& type) {
        if (type.kind != TypeKind::Object || type.symbol == no_symbol || symbol(type.symbol).name != "StyleModule")
            return false;
        const auto file = source_file(program_, type.symbol);
        return file != no_symbol && symbol(file).library_path == "std/ui.nra";
    };
    if (!is_module(target.type)) return std::nullopt;
    const bool classes = callee.text == "class_name";
    const auto constant = target.op == IrOp::Load ? target.symbol : no_symbol;
    if (constant != no_symbol && !style_modules_.contains(constant)) resolve(constant);
    const auto found = constant == no_symbol ? style_modules_.end() : style_modules_.find(constant);
    if (found == style_modules_.end())
        throw Diagnostic(receiver.span, "not a class of the CSS module").with_code("E1012")
            .label("'" + callee.text + "' needs a css.module constant declared in source here");
    if (expr.operands.size() != 2 || expr.operands[1].kind != ExprKind::String)
        throw Diagnostic(expr.operands.size() == 2 ? expr.operands[1].span : expr.span,
                         "not a class of the CSS module").with_code("E1012")
            .label("the argument must be one string literal naming a " +
                   std::string(classes ? "class" : "keyframes rule") + " of the module");
    const auto& names = classes ? found->second.classes : found->second.keyframes;
    const auto name = decode_string_literal(expr.operands[1].text);
    const auto scoped = names.find(name);
    if (scoped == names.end()) {
        std::vector<std::string> candidates;
        for (const auto& [local, ignored] : names) candidates.push_back(local);
        auto error = Diagnostic(expr.operands[1].span, "not a class of the CSS module").with_code("E1012")
            .label("the module declares no " + std::string(classes ? "class" : "keyframes rule") + " '" + name + "'");
        if (const auto nearest = closest_name(name, candidates)) error.help("did you mean '" + *nearest + "'?");
        throw error;
    }
    auto result = node(IrOp::String, expr.span, string_type);
    result.text = scoped->second;
    return result;
}

void Lowering::order_stylesheets() {
    if (stylesheet_links_.empty()) return;
    // Module initialization order, then source order (Html030.md, Stylesheets).
    std::map<SymbolId, std::size_t> rank;
    for (const auto file : file_initialization_order(program_)) rank.emplace(file, rank.size());
    std::ranges::stable_sort(stylesheet_links_, [&](const StylesheetLink& left, const StylesheetLink& right) {
        const auto left_file = source_file(program_, left.constant);
        const auto right_file = source_file(program_, right.constant);
        return std::pair{rank[left_file], left.offset} < std::pair{rank[right_file], right.offset};
    });
    for (const auto& link : stylesheet_links_) {
        if (std::ranges::find(program_.stylesheet_links, link.path) == program_.stylesheet_links.end())
            program_.stylesheet_links.push_back(link.path);
    }
    constexpr std::size_t maximum = 256; // U-8
    if (program_.stylesheet_links.size() > maximum)
        throw Diagnostic(symbol(stylesheet_links_.back().constant).span, "template or stylesheet exceeds a limit")
            .with_code("E1015").label("U-8 stylesheets registered: " + std::to_string(program_.stylesheet_links.size()) +
                                      " exceeds " + std::to_string(maximum));
}
