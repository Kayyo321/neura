bool svg_cased_name(std::string_view name) {
    return one_of(name, {"linearGradient", "radialGradient", "clipPath", "viewBox"});
}

bool closes_paragraph(std::string_view name) {
    return one_of(name, {"address", "article", "aside", "blockquote", "details", "dialog", "div",
                         "dl", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2",
                         "h3", "h4", "h5", "h6", "header", "hgroup", "hr", "main", "menu", "nav",
                         "ol", "p", "pre", "search", "section", "table", "ul"});
}

std::string_view required_parent(std::string_view name, std::string_view parent) {
    if (name == "li" && !one_of(parent, {"ol", "ul", "menu"})) return "'ol', 'ul', or 'menu'";
    if (name == "tr" && !one_of(parent, {"thead", "tbody", "tfoot"})) return "'tbody', 'thead', or 'tfoot'";
    if (one_of(name, {"td", "th"}) && parent != "tr") return "'tr'";
    if (one_of(name, {"caption", "colgroup", "thead", "tbody", "tfoot"}) && parent != "table") return "'table'";
    if (name == "col" && parent != "colgroup") return "'colgroup'";
    if (name == "optgroup" && parent != "select") return "'select'";
    if (name == "option" && !one_of(parent, {"select", "optgroup", "datalist"})) return "'select', 'optgroup', or 'datalist'";
    return {};
}

bool parent_accepts(std::string_view parent, std::string_view child) {
    if (parent == "table") return one_of(child, {"caption", "colgroup", "thead", "tbody", "tfoot"});
    if (one_of(parent, {"thead", "tbody", "tfoot"})) return child == "tr";
    if (parent == "tr") return one_of(child, {"td", "th"});
    if (parent == "colgroup") return child == "col";
    if (parent == "select") return one_of(child, {"optgroup", "option"});
    if (parent == "optgroup") return child == "option";
    return !one_of(parent, {"option", "textarea"});
}

bool table_only_elements(std::string_view parent) {
    return one_of(parent, {"table", "thead", "tbody", "tfoot", "tr", "colgroup"});
}

bool interactive(const TemplateElement& element) {
    if (one_of(element.name, {"a", "button", "details", "label", "select", "textarea"})) return true;
    if (element.name == "input") {
        return std::ranges::none_of(element.attributes, [](const TemplateAttribute& attr) {
            const auto* value = std::get_if<std::string>(&attr.value);
            return attr.name == "type" && value && *value == "hidden";
        });
    }
    if (one_of(element.name, {"audio", "video"})) {
        return std::ranges::any_of(element.attributes, [](const TemplateAttribute& attr) {
            return attr.name == "controls";
        });
    }
    return false;
}
