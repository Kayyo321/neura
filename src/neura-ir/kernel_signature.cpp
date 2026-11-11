#include "kernel_signature.hpp"

#include "kernel_binding.hpp"

namespace neura {
namespace {

using kernels::Kind;

bool scalar_matches(const IrType& type, Kind kind) {
    if (kind == Kind::Bool) return type.kind == TypeKind::Bool;
    const auto name = kernels::neura_scalar_name(kind);
    if (kernels::float_kind(kind)) return type.kind == TypeKind::Float && type.name == name;
    return type.kind == TypeKind::Integer && type.name == name;
}

bool sequence_of(const IrType& type, Kind element, bool list_only) {
    if (type.kind != TypeKind::List && (list_only || type.kind != TypeKind::Array)) return false;
    return type.arguments.size() == 1 && scalar_matches(type.arguments.front(), element);
}

// Whether a Neura parameter type passes a C parameter of this kind.
bool argument_matches(const IrType& type, Kind kind) {
    if (kernels::scalar_kind(kind)) return scalar_matches(type, kind);
    if (kind == Kind::Text) return type.kind == TypeKind::String;
    return sequence_of(type, kernels::element_kind(kind), false);
}

std::string argument_spelling(Kind kind) {
    if (kernels::scalar_kind(kind)) return std::string(kernels::neura_scalar_name(kind));
    if (kind == Kind::Text) return "string";
    return std::string(kernels::neura_scalar_name(kernels::element_kind(kind))) + "[list]";
}

bool value_matches(const IrType& type, Kind kind) {
    if (kind == Kind::OutText) return type.kind == TypeKind::String;
    if (kind == Kind::OutBytes) return sequence_of(type, Kind::U8, true);
    return scalar_matches(type, kind);
}

std::string value_spelling(Kind kind) {
    if (kind == Kind::OutText) return "string";
    if (kind == Kind::OutBytes) return "uint8[list]";
    return std::string(kernels::neura_scalar_name(kind));
}

bool result_matches(const IrType& type, const kernels::Binding& binding) {
    switch (binding.result) {
    case kernels::ResultShape::Void:
        return type.kind == TypeKind::Void;
    case kernels::ResultShape::Scalar:
    case kernels::ResultShape::Text:
    case kernels::ResultShape::Bytes:
        return value_matches(type, binding.value_kind);
    case kernels::ResultShape::Outcome: {
        const std::size_t count = binding.value_kind == Kind::Status ? 2 : 3;
        return type.kind == TypeKind::Tuple && type.arguments.size() == count &&
               scalar_matches(type.arguments[0], Kind::U32) && scalar_matches(type.arguments[1], Kind::I64) &&
               (count == 2 || value_matches(type.arguments[2], binding.value_kind));
    }
    }
    return false;
}

std::string result_spelling(const kernels::Binding& binding) {
    switch (binding.result) {
    case kernels::ResultShape::Void:
        return "void";
    case kernels::ResultShape::Outcome:
        return binding.value_kind == Kind::Status ? "[uint32, int64]"
                                                  : "[uint32, int64, " + value_spelling(binding.value_kind) + "]";
    default:
        return value_spelling(binding.value_kind);
    }
}

} // namespace

std::optional<std::pair<std::string, std::uint32_t>> kernel_binding(const IrSymbol& symbol) {
    const auto* found = find_attribute(symbol, AttributeId::KernelBinding);
    if (!found || found->value.kind != AttributeValueKind::List || found->value.items.size() != 2) return std::nullopt;
    return std::pair{found->value.items[0].text, static_cast<std::uint32_t>(found->value.items[1].integer)};
}

bool kernel_known(const std::string& name, std::uint32_t version) {
    return kernels::find_kernel(name, version) != nullptr;
}

std::optional<std::string> kernel_signature_problem(const IrProgram& program, const IrSymbol& function,
                                                    const std::string& name, std::uint32_t version) {
    const auto* info = kernels::find_kernel(name, version);
    if (!info) {
        return kernels::known_kernel_name(name)
                   ? "this runtime has no version " + std::to_string(version) + " of kernel '" + name + "'"
                   : "no kernel is named '" + name + "'";
    }
    const auto binding = kernels::plan_binding(*info);
    if (binding.problem) return "kernel '" + name + "' cannot be bound: " + binding.problem;
    std::string expected = "func(";
    for (std::size_t i = 0; i < binding.arguments.size(); ++i) {
        const auto param = binding.arguments[i];
        expected += (i == 0 ? "" : ", ") + std::string(info->param_names[param]) + ": " +
                    argument_spelling(info->params[param]);
    }
    expected += ") -> " + result_spelling(binding);
    bool matches = function.parameters.size() == binding.arguments.size() && result_matches(function.type, binding);
    for (std::size_t i = 0; matches && i < binding.arguments.size(); ++i) {
        const auto parameter = function.parameters[i];
        matches = parameter < program.symbols.size() &&
                  argument_matches(program.symbols[parameter].type, info->params[binding.arguments[i]]);
    }
    if (matches) return std::nullopt;
    auto problem = "kernel '" + name + "' version " + std::to_string(version) + " needs the signature " + expected;
    if (expected.find("[list]") < expected.find(") ->")) problem += " (an array may stand for a list parameter)";
    return problem;
}

} // namespace neura
