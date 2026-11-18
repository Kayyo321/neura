// Frozen source-level names for HTML binding checks (Html030.md).
namespace html_bindings {

constexpr std::string_view html_elements =
    "main section article aside header footer nav address search hgroup h1 h2 h3 h4 h5 h6 "
    "div p hr pre blockquote ol ul menu li dl dt dd figure figcaption "
    "a em strong small s cite q dfn abbr data time code var samp kbd sub sup i b u mark bdi bdo "
    "span br wbr ruby rt rp ins del img picture source audio video track "
    "table caption colgroup col thead tbody tfoot tr th td "
    "form label input button select optgroup option textarea output progress meter fieldset legend datalist "
    "details summary dialog";
constexpr std::string_view svg_elements =
    "svg g path circle ellipse line polyline polygon rect text tspan title desc defs use symbol "
    "linearGradient radialGradient stop clipPath mask";
constexpr std::string_view global_attributes =
    "id class title lang dir hidden tabindex role inert autofocus enterkeyhint inputmode spellcheck translate";
constexpr std::string_view svg_presentation =
    "fill fill-opacity fill-rule stroke stroke-width stroke-opacity stroke-linecap stroke-linejoin "
    "stroke-miterlimit stroke-dasharray stroke-dashoffset opacity transform clip-path mask color "
    "font-size font-family text-anchor dominant-baseline visibility display";
constexpr std::string_view boolean_attributes =
    "disabled checked selected readonly required multiple hidden open autofocus inert novalidate formnovalidate "
    "reversed controls loop muted default playsinline";
constexpr std::string_view numeric_attributes =
    "colspan rowspan min max step width height tabindex maxlength minlength size rows cols start span";
constexpr std::string_view url_attributes = "href src poster cite action formaction";

bool contains(std::string_view words, std::string_view word) {
    for (auto pos = words.find(word); pos != std::string_view::npos; pos = words.find(word, pos + 1)) {
        if ((pos == 0 || words[pos - 1] == ' ') &&
            (pos + word.size() == words.size() || words[pos + word.size()] == ' ')) return true;
    }
    return false;
}

void append_words(std::vector<std::string>& names, std::string_view words) {
    while (!words.empty()) {
        const auto end = words.find(' ');
        names.emplace_back(words.substr(0, end));
        if (end == std::string_view::npos) break;
        words.remove_prefix(end + 1);
    }
}

std::string_view local_attributes(std::string_view name, bool svg) {
    static constexpr std::pair<std::string_view, std::string_view> html[] = {
        {"a", "href target rel download hreflang type referrerpolicy"},
        {"audio", "src controls loop muted preload crossorigin"},
        {"blockquote", "cite"},
        {"button", "type name value disabled form formaction formenctype formmethod formnovalidate formtarget"},
        {"col", "span"}, {"colgroup", "span"}, {"data", "value"}, {"del", "cite datetime"}, {"details", "open name"},
        {"dialog", "open"}, {"fieldset", "disabled form name"},
        {"form", "accept-charset action autocomplete enctype method name novalidate rel target"},
        {"img", "src srcset sizes alt width height loading decoding crossorigin referrerpolicy fetchpriority"},
        {"input", "type name value placeholder disabled checked readonly required multiple accept alt autocomplete "
                  "capture dirname form formaction formenctype formmethod formnovalidate formtarget height width "
                  "list max maxlength min minlength pattern size src step"},
        {"ins", "cite datetime"}, {"label", "for form"}, {"li", "value"}, {"meter", "value min max low high optimum"},
        {"ol", "reversed start type"}, {"optgroup", "label disabled"}, {"option", "value label disabled selected"},
        {"output", "for form name"}, {"progress", "max value"}, {"q", "cite"},
        {"select", "name autocomplete disabled form multiple required size"},
        {"source", "src srcset sizes media type width height"},
        {"td", "colspan rowspan headers"},
        {"textarea", "name value placeholder disabled readonly required maxlength minlength rows cols wrap "
                     "autocomplete dirname form"},
        {"th", "colspan rowspan headers scope abbr"}, {"time", "datetime"},
        {"track", "src kind srclang label default"},
        {"video", "src poster controls loop muted playsinline preload width height crossorigin"},
    };
    static constexpr std::pair<std::string_view, std::string_view> svg_attributes[] = {
        {"svg", "viewBox width height x y"},
        {"path", "d pathLength"}, {"rect", "x y width height rx ry pathLength"},
        {"circle", "cx cy r pathLength"}, {"ellipse", "cx cy rx ry pathLength"},
        {"line", "x1 y1 x2 y2 pathLength"}, {"polygon", "points pathLength"}, {"polyline", "points pathLength"},
        {"text", "x y dx dy rotate"}, {"tspan", "x y dx dy rotate"}, {"use", "href x y width height"},
        {"symbol", "viewBox x y width height"},
        {"linearGradient", "x1 y1 x2 y2 href"}, {"radialGradient", "cx cy r fx fy fr href"},
        {"stop", "offset stop-color stop-opacity"}, {"mask", "x y width height"},
    };
    const std::span choices = svg ? std::span<const std::pair<std::string_view, std::string_view>>(svg_attributes)
                                  : std::span<const std::pair<std::string_view, std::string_view>>(html);
    const auto choice = std::ranges::find(choices, name, &std::pair<std::string_view, std::string_view>::first);
    return choice == choices.end() ? std::string_view{} : choice->second;
}

struct Event {
    std::string_view name;
    std::string_view type;
    bool prevent_default;
    bool stop_propagation;
    std::string_view elements{};
};

constexpr Event events[] = {
    {"click", "ClickEvent", true, true}, {"dblclick", "ClickEvent", true, true},
    {"input", "InputEvent", false, true, "input select textarea"},
    {"change", "ChangeEvent", false, true, "input select textarea"},
    {"submit", "SubmitEvent", true, true, "form"},
    {"keydown", "KeyEvent", true, true}, {"keyup", "KeyEvent", true, true},
    {"focus", "FocusEvent", false, false}, {"blur", "FocusEvent", false, false},
    {"pointerdown", "PointerEvent", true, true}, {"pointerup", "PointerEvent", false, true},
    {"pointermove", "PointerEvent", false, true}, {"scroll", "ScrollEvent", false, false},
    {"toggle", "ToggleEvent", false, false, "details dialog"}, {"close", "CloseEvent", false, false, "dialog"},
};

struct Property {
    std::string_view name;
    std::string_view elements;
    bool flag;
};

constexpr Property properties[] = {
    {"value", "input textarea select", false}, {"checked", "input", true}, {"indeterminate", "input", true},
    {"selected", "option", true}, {"open", "details dialog", true},
};

bool allowed_url(std::string_view value) {
    if (std::ranges::any_of(value, [](unsigned char c) { return c <= 32 || c == 127 || c == '\\'; })) return false;
    const auto delimiter = value.find_first_of(":/?#");
    if (delimiter == std::string_view::npos || value[delimiter] != ':') return true;
    std::string scheme(value.substr(0, delimiter));
    for (auto& c : scheme) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return contains("http https mailto tel", scheme);
}

} // namespace html_bindings
