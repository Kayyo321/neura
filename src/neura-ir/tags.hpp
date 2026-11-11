#pragma once

// Source tags (docs/lang/Tags.md): the registry in tags.def, the checks that
// lowering runs when it declares a tagged symbol and when it resolves a use
// of one, and the warnings they report.
//
// Every accepted tag is persisted on its symbol as a NeuraIR 1.10 `tag`
// attribute (attributes.hpp), so a program that imports a prebuilt .nir
// module sees the tags of its symbols and the use-site checks still run.

#include "neura-ir/ir.hpp"
#include "parser/syntax.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neura {

enum class TagStatus : std::uint8_t { Implemented, Informational, Reserved };

enum class TagGroup : std::uint8_t {
    Lifecycle, Correctness, Optimization, Concurrency, Testing, Documentation, Interop, Diagnostics, Security,
    Serialization, Resources, Numerics,
};

// The kinds of declaration a tag may precede.
enum class TagTarget : std::uint8_t {
    Module, Class, Interface, Enum, Variant, Function, Method, Field, Global, Local, Parameter,
};

// Masks of TagTarget bits, as tags.def writes them.
namespace tag_targets {
inline constexpr std::uint32_t Module = 1u << 0;
inline constexpr std::uint32_t Class = 1u << 1;
inline constexpr std::uint32_t Interface = 1u << 2;
inline constexpr std::uint32_t Enum = 1u << 3;
inline constexpr std::uint32_t Variant = 1u << 4;
inline constexpr std::uint32_t Function = 1u << 5;
inline constexpr std::uint32_t Method = 1u << 6;
inline constexpr std::uint32_t Field = 1u << 7;
inline constexpr std::uint32_t Global = 1u << 8;
inline constexpr std::uint32_t Local = 1u << 9;
inline constexpr std::uint32_t Parameter = 1u << 10;
inline constexpr std::uint32_t Types = Class | Interface | Enum;
inline constexpr std::uint32_t Callables = Function | Method;
inline constexpr std::uint32_t Values = Field | Global;
// Everything a file, module, class, interface, or enum declares.
inline constexpr std::uint32_t Declarations = Module | Types | Variant | Callables | Values;
} // namespace tag_targets

enum class TagId : std::uint16_t {
#define NEURA_TAG(name, id, ...) name = id,
#include "tags.def"
};

// One parameter of a tag, parsed from the registry's argument schema.
struct TagParameter {
    std::string name;
    // t string, i integer, b bool, x number, n name, w warning, a any literal.
    char type = 't';
    bool required = true;
    // The last parameter, taking every remaining positional argument.
    bool rest = false;
    // `+`: the rest needs at least one argument.
    bool at_least_one = false;
    std::optional<std::string> default_value;
};

struct TagInfo {
    TagId id;
    std::string_view name;
    TagGroup group;
    std::uint32_t targets;
    TagStatus status;
    bool repeats;
    std::string_view arguments;
    std::string_view since;
    std::string_view meaning;
    std::string_view alternative;
};

// Every registered tag in id order.
std::span<const TagInfo> tag_registry();
const TagInfo* find_tag(std::string_view name);
const TagInfo* find_tag(std::uint16_t id);
std::vector<TagParameter> tag_parameters(const TagInfo& tag);
// `Deprecated(as_of: string = "", ...)`, as `neura tags` prints it.
std::string tag_signature(const TagInfo& tag);
std::string_view tag_group_name(TagGroup group);
std::string_view tag_status_name(TagStatus status);
std::string_view tag_target_name(TagTarget target);
// "classes and methods": the plural names of the targets in a mask.
std::string describe_tag_targets(std::uint32_t mask);
// Prints the catalogue (`neura tags`), or one tag's entry.
void print_tag_catalogue(std::ostream& out);
void print_tag(std::ostream& out, const TagInfo& tag);

// The tags persisted on a symbol.
bool has_tag(const IrSymbol& symbol, TagId tag);
// The value of a named text argument of the symbol's first `tag`, or the
// tag's default when the argument was not written.
std::optional<std::string> tag_text(const IrSymbol& symbol, TagId tag, std::string_view argument);
// The source span of the symbol's first `tag`.
std::optional<Span> tag_span(const IrSymbol& symbol, TagId tag);

// The target kind of a declared symbol: a function owned by a class or
// interface is a method, a module or file binding is a global, and so on.
TagTarget tag_target(const IrProgram& program, SymbolId symbol);

// Validates `tags` for a declaration of `target` (E0900-E0908, E0915) and
// appends each as a `tag` attribute of `symbol` and of every symbol in
// `also` (the branches of an overload group). Applies the attributes that
// implemented tags imply, such as inline.directive. `declaration` is the span
// that a wrong-target error labels.
void attach_tags(IrProgram& program, SymbolId symbol, TagTarget target, const std::vector<TagSyntax>& tags,
                 const Span& declaration, std::span<const SymbolId> also = {});
// Validates tags that have no symbol to persist on (the parameters of a
// function value).
void check_tags(const std::vector<TagSyntax>& tags, TagTarget target, const Span& declaration);

// While alive, the declaration `symbol` is being resolved or lowered: a use
// found meanwhile is judged from inside it (for #Allow, #Deny, and uses
// inside deprecated or experimental code). Nests.
class TagScope {
public:
    explicit TagScope(SymbolId symbol);
    ~TagScope();
    TagScope(const TagScope&) = delete;
    TagScope& operator=(const TagScope&) = delete;
};

// Resolves a simple name at the use site, or no_symbol.
using NameLookup = std::function<SymbolId(std::string_view)>;

// A use of `target` at `use`, from inside `scope` (the lexical owner of the
// use, or no_symbol when unknown). Reports W0001 and W0002 and throws E0911.
// `lookup`, when given, decides whether a deprecated declaration's
// replacement resolves at a bare-name use.
void check_tagged_use(const IrProgram& program, SymbolId target, const Span& use, SymbolId scope,
                      const NameLookup& lookup = {});
// An expression statement that discards `value` (W0003).
void check_discarded_result(const IrProgram& program, const IrNode& value, const Span& statement, SymbolId scope);

// What the whole-program checks need from lowering about a declaration it
// lowered: the spans of its base and implemented types and of its name.
struct TagDeclarationSpans {
    std::optional<Span> base;
    std::vector<Span> interfaces;
    Span name;
};

// The checks that need every symbol resolved: #Final and #Sealed against
// bases, implementations, and overrides (E0912-E0914), deprecated overrides
// (W0001), and #NoDiscard on functions without a result (E0916). `first`
// is the first symbol this compilation declared; symbols before it come from
// imported artifacts. Then turns the warnings that #Deny made errors, which
// lowering recorded since sink entry `mark`, into the thrown error, and
// drops duplicate warnings.
void finish_tags(const IrProgram& program, SymbolId first, std::size_t mark,
                 const std::function<std::optional<TagDeclarationSpans>(SymbolId)>& spans);

// Reports `warning` (which has a W code and a name) as seen from inside
// `scope`: nothing under #Allow, an error under #Deny, and otherwise the
// warning itself.
void report_scoped_warning(const IrProgram& program, SymbolId scope, Diagnostic warning);
// The same judgement without reporting: nothing under #Allow, the warning
// made an error under #Deny, or the warning itself.
std::optional<Diagnostic> scoped_tag_warning(const IrProgram& program, SymbolId scope, Diagnostic warning);

} // namespace neura
