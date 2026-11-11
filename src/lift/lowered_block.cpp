#include "lowered_block.hpp"

#include "checked.hpp"
#include "evaluate.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#define NEURA_LIFT_FLOAT_X86_64 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define NEURA_LIFT_FLOAT_AARCH64 1
#endif

namespace neura::lift {
namespace {

FloatType float_type(const nvr::Type& type) {
    if (type.kind == nvr::TypeKind::BFloat) return FloatType::BFloat16;
    if (type.kind != nvr::TypeKind::Float) return FloatType::None;
    if (type.width == 16) return FloatType::Binary16;
    if (type.width == 32) return FloatType::Binary32;
    return FloatType::Binary64;
}

bool hardware_format(FloatType type) { return type == FloatType::Binary32 || type == FloatType::Binary64; }

unsigned float_width(FloatType type) {
    switch (type) {
    case FloatType::Binary16:
    case FloatType::BFloat16: return 16;
    case FloatType::Binary32: return 32;
    case FloatType::Binary64: return 64;
    case FloatType::None: break;
    }
    return 0;
}

constexpr std::uint64_t canonical_nan32 = 0x7fc00000U;
constexpr std::uint64_t canonical_nan64 = 0x7ff8000000000000ULL;

// The value of an encoding of any supported format, exactly: every value of
// binary16, bfloat16, and binary32 is a normal binary64 value or zero, and
// the decoding uses integer arithmetic and exact scaling only, so it does
// not depend on the floating-point environment.
double decode(std::uint64_t bits, FloatType type) {
    if (type == FloatType::Binary64) {
        double value = 0;
        std::memcpy(&value, &bits, 8);
        return value;
    }
    unsigned exponent_bits = 8;
    unsigned fraction_bits = 23;
    if (type == FloatType::Binary16) {
        exponent_bits = 5;
        fraction_bits = 10;
    } else if (type == FloatType::BFloat16) {
        fraction_bits = 7;
    }
    const unsigned width = 1 + exponent_bits + fraction_bits;
    const bool negative = ((bits >> (width - 1)) & 1U) != 0;
    const auto exponent = static_cast<int>((bits >> fraction_bits) & ((1U << exponent_bits) - 1));
    const auto fraction = bits & ((std::uint64_t{1} << fraction_bits) - 1);
    const int bias = (1 << (exponent_bits - 1)) - 1;
    double magnitude = 0;
    if (exponent == (1 << exponent_bits) - 1) {
        magnitude = fraction == 0 ? std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
    } else if (exponent == 0) {
        magnitude = std::ldexp(static_cast<double>(fraction), 1 - bias - static_cast<int>(fraction_bits));
    } else {
        magnitude = std::ldexp(static_cast<double>(fraction | (std::uint64_t{1} << fraction_bits)),
                               exponent - bias - static_cast<int>(fraction_bits));
    }
    return negative ? -magnitude : magnitude;
}

float to_single(std::uint64_t bits) {
    const auto word = static_cast<std::uint32_t>(bits);
    float value = 0;
    std::memcpy(&value, &word, 4);
    return value;
}

double to_double(std::uint64_t bits) {
    double value = 0;
    std::memcpy(&value, &bits, 8);
    return value;
}

std::uint64_t single_bits(float value) {
    if (std::isnan(value)) return canonical_nan32;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

std::uint64_t double_bits(double value) {
    if (std::isnan(value)) return canonical_nan64;
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, 8);
    return bits;
}

// An integer to binary64, rounded to nearest even by the hardware. An
// unsigned value of 2^63 or more is halved with its lowest bit kept as a
// sticky bit, which is exact for the rounding, and doubled after.
double integer_to_double(std::uint64_t value, bool is_signed, unsigned width) {
    if (is_signed) {
        return static_cast<double>(sign_extend(value, width));
    }
    if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return static_cast<double>(static_cast<std::int64_t>(value));
    }
    const auto half = static_cast<double>(static_cast<std::int64_t>((value >> 1) | (value & 1U)));
    return half + half;
}

float integer_to_single(std::uint64_t value, bool is_signed, unsigned width) {
    if (is_signed) {
        return static_cast<float>(sign_extend(value, width));
    }
    if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return static_cast<float>(static_cast<std::int64_t>(value));
    }
    const auto half = static_cast<float>(static_cast<std::int64_t>((value >> 1) | (value & 1U)));
    return half + half;
}

class Compiler {
public:
    Compiler(const nvr::Image& image, const nvr::Block& block, LoweredBlock& out)
        : image_(image), block_(block), out_(out) {
        for (const auto& input : block.inputs) {
            out_.input_widths.push_back(nvr::lane_count(image.types.at(input.type)));
        }
    }

    std::string failure;

    void trap_exit(std::uint32_t reason, std::uint32_t exit) { traps_.emplace(reason, exit); }

    // The register holding the value of `e`, or nothing when it cannot be
    // lowered.
    std::optional<std::uint32_t> emit(const Expression& e) {
        if (const auto it = registers_.find(&e); it != registers_.end()) {
            return it->second;
        }
        auto result = compile(e);
        if (result) {
            registers_.emplace(&e, *result);
        }
        return result;
    }

private:
    std::optional<std::uint32_t> refuse(std::string why) {
        if (failure.empty()) failure = std::move(why);
        return std::nullopt;
    }

    std::uint32_t push(Instruction instruction) {
        if (instruction.reason != 0) {
            const auto it = traps_.find(instruction.reason);
            instruction.trap_exit = it == traps_.end() ? no_exit : it->second;
        }
        out_.code.push_back(instruction);
        return static_cast<std::uint32_t>(out_.input_widths.size() + out_.code.size() - 1);
    }

    std::optional<std::uint32_t> compile(const Expression& e) {
        const auto& type = image_.types.at(e.type);
        switch (e.kind) {
        case Expression::Kind::Input:
            return static_cast<std::uint32_t>(e.input);
        case Expression::Kind::Immediate: {
            Instruction instruction;
            instruction.op = Opcode::Constant;
            instruction.immediate = e.bits & width_mask(nvr::lane_count(type));
            return push(instruction);
        }
        case Expression::Kind::Unary: {
            const auto a = emit(*e.operands[0]);
            if (!a) return std::nullopt;
            const auto& from = image_.types.at(e.operands[0]->type);
            Instruction instruction;
            instruction.a = *a;
            instruction.width = static_cast<std::uint8_t>(nvr::lane_count(from));
            instruction.is_signed = from.kind == nvr::TypeKind::Signed;
            if (e.op == "!") {
                instruction.op = Opcode::Not;
            } else if (e.op == "~") {
                instruction.op = Opcode::BitNot;
            } else if (e.op == "index") {
                instruction.op = Opcode::Index;
                instruction.reason = instruction.is_signed ? 2 : 0;
            } else if (is_floating(type)) {
                instruction.op = Opcode::FloatNegate;
                instruction.format = float_type(type);
            } else {
                instruction.op = Opcode::Negate;
                instruction.reason = 1;
            }
            return push(instruction);
        }
        case Expression::Kind::Convert: {
            const auto a = emit(*e.operands[0]);
            if (!a) return std::nullopt;
            const auto& from = image_.types.at(e.operands[0]->type);
            Instruction instruction;
            instruction.a = *a;
            instruction.width = static_cast<std::uint8_t>(nvr::lane_count(from));
            instruction.is_signed = from.kind == nvr::TypeKind::Signed;
            instruction.to_width = static_cast<std::uint8_t>(nvr::lane_count(type));
            instruction.to_signed = type.kind == nvr::TypeKind::Signed;
            instruction.format = float_type(from);
            instruction.to_format = float_type(type);
            if (is_floating(from) && is_floating(type)) {
                // The circuit passes a value to its own format unchanged,
                // NaN payloads included.
                if (instruction.to_format == instruction.format) {
                    return *a;
                }
                // Widening is exact; narrowing rounds, which only the
                // hardware does for binary64 to binary32.
                const bool widening = float_width(instruction.to_format) > float_width(instruction.format);
                if (!hardware_format(instruction.to_format) ||
                    !(widening || instruction.format == FloatType::Binary64)) {
                    return refuse("float conversion to a 16-bit format");
                }
                instruction.op = Opcode::FloatConvert;
                out_.uses_float = true;
            } else if (is_floating(type)) {
                if (!hardware_format(instruction.to_format)) {
                    return refuse("integer conversion to a 16-bit float format");
                }
                instruction.op = Opcode::FloatFromInteger;
                out_.uses_float = true;
            } else if (is_floating(from)) {
                instruction.op = Opcode::FloatToInteger;
                instruction.reason = 1;
                out_.uses_float = true;
            } else {
                instruction.op = Opcode::ConvertInteger;
                instruction.reason = 1;
            }
            return push(instruction);
        }
        case Expression::Kind::Binary: {
            const auto a = emit(*e.operands[0]);
            if (!a) return std::nullopt;
            const auto b = emit(*e.operands[1]);
            if (!b) return std::nullopt;
            const auto& t = image_.types.at(e.operands[0]->type);
            Instruction instruction;
            instruction.a = *a;
            instruction.b = *b;
            instruction.width = static_cast<std::uint8_t>(nvr::lane_count(t));
            instruction.is_signed = t.kind == nvr::TypeKind::Signed;
            const auto& op = e.op;
            if (t.kind == nvr::TypeKind::Bool) {
                instruction.op = op == "&&" ? Opcode::And
                               : op == "||" ? Opcode::Or
                               : op == "==" ? Opcode::Equal
                                            : Opcode::NotEqual;
                return push(instruction);
            }
            if (is_floating(t)) {
                instruction.format = float_type(t);
                out_.uses_float = true;
                if (is_comparison(op)) {
                    instruction.op = op == "==" ? Opcode::FloatEqual
                                   : op == "!=" ? Opcode::FloatNotEqual
                                   : op == "<"  ? Opcode::FloatLess
                                   : op == "<=" ? Opcode::FloatLessEqual
                                   : op == ">"  ? Opcode::FloatGreater
                                                : Opcode::FloatGreaterEqual;
                    return push(instruction);
                }
                if (!hardware_format(instruction.format)) {
                    return refuse("16-bit float arithmetic");
                }
                instruction.op = op == "+"   ? Opcode::FloatAdd
                               : op == "-"   ? Opcode::FloatSubtract
                               : op == "*"   ? Opcode::FloatMultiply
                                             : Opcode::FloatDivide;
                return push(instruction);
            }
            if (is_comparison(op)) {
                instruction.op = op == "==" ? Opcode::Equal
                               : op == "!=" ? Opcode::NotEqual
                               : op == "<"  ? Opcode::Less
                               : op == "<=" ? Opcode::LessEqual
                               : op == ">"  ? Opcode::Greater
                                            : Opcode::GreaterEqual;
                return push(instruction);
            }
            if (op == "+~" || op == "&" || op == "|" || op == "^") {
                instruction.op = op == "+~" ? Opcode::AddWrapping
                               : op == "&"  ? Opcode::BitAnd
                               : op == "|"  ? Opcode::BitOr
                                            : Opcode::BitXor;
                return push(instruction);
            }
            if (op == "<<" || op == ">>") {
                instruction.op = op == "<<" ? Opcode::ShiftLeft : Opcode::ShiftRight;
                instruction.reason = 1;
                return push(instruction);
            }
            instruction.op = op == "+"   ? Opcode::Add
                           : op == "-"   ? Opcode::Subtract
                           : op == "*"   ? Opcode::Multiply
                           : op == "/"   ? Opcode::Divide
                                         : Opcode::Remainder;
            instruction.reason = 1;
            return push(instruction);
        }
        }
        return refuse("unknown expression kind");
    }

    const nvr::Image& image_;
    const nvr::Block& block_;
    LoweredBlock& out_;
    std::map<const Expression*, std::uint32_t> registers_;
    std::map<std::uint32_t, std::uint32_t> traps_;
};

LoweringResult refused(std::string why) {
    LoweringResult result;
    result.failure = std::move(why);
    return result;
}

} // namespace

bool float_lowering_supported() {
#if (defined(NEURA_LIFT_FLOAT_X86_64) || defined(NEURA_LIFT_FLOAT_AARCH64)) && defined(FLT_EVAL_METHOD)
    return FLT_EVAL_METHOD == 0 && std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559;
#else
    return false;
#endif
}

bool float_environment_is_default() {
#if defined(NEURA_LIFT_FLOAT_X86_64)
    // MXCSR: rounding control (bits 13-14) to nearest, flush to zero (bit
    // 15) and denormals are zero (bit 6) off.
    return (_mm_getcsr() & 0xe040U) == 0;
#elif defined(NEURA_LIFT_FLOAT_AARCH64)
    // FPCR: RMode (bits 22-23) to nearest, FZ (bit 24), and the alternate
    // handling bits FIZ and AH (bits 0-1) off.
    std::uint64_t fpcr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    return (fpcr & ((std::uint64_t{3} << 22) | (std::uint64_t{1} << 24) | std::uint64_t{3})) == 0;
#else
    return false;
#endif
}

LoweringResult lower(const nvr::Image& image, const nvr::Block& block, const LiftedForm& form) {
    if (const auto* error = form_type_error(image, block, form)) {
        return refused(std::string("ill-typed form: ") + error);
    }
    const auto& graph = image.graphs.at(block.graph);
    LoweredBlock out;
    out.exit_count = static_cast<std::uint32_t>(block.exits.size());
    out.selector_start = block.selector_start;
    out.output_lanes = static_cast<std::uint32_t>(graph.outputs.size());
    if (block.exits.empty()) {
        return refused("a block without exits");
    }
    const auto selector = block.exits.size() > 1 ? nvr::selector_lanes(block.exits.size()) : 0U;
    // Lanes each exit reads, by first lane.
    std::map<std::uint32_t, std::uint32_t> targets;
    for (const auto& exit : block.exits) {
        for (const auto& value : exit.arguments) {
            if (value.kind == nvr::ValueKind::Lanes) targets.emplace(value.index, value.type);
        }
        for (const auto& assignment : exit.assignments) {
            if (assignment.value.kind == nvr::ValueKind::Lanes) targets.emplace(assignment.value.index, assignment.value.type);
        }
    }
    Compiler compiler(image, block, out);

    if (!form.dispatch.empty()) {
        if (block.inputs.size() != 2 || out.input_widths[0] != 32 || out.input_widths[1] != 64) {
            return refused("a dispatch block without a tag and a receiver");
        }
        std::map<std::uint64_t, std::uint32_t> table;
        for (const auto& [exit, layouts] : form.dispatch) {
            if (exit + 1 >= block.exits.size() || block.exits[exit].kind != nvr::ExitKind::Call) {
                return refused("a dispatch to an exit that is not a call");
            }
            for (const auto layout : layouts) {
                if (!table.emplace(layout, static_cast<std::uint32_t>(exit)).second) {
                    return refused("a layout dispatched twice");
                }
            }
        }
        out.dispatch.assign(table.begin(), table.end());
        out.dispatch_default = static_cast<std::uint32_t>(block.exits.size() - 1);
        out.selection = block.exits.size() > 1 ? LoweredBlock::Selection::Dispatch : LoweredBlock::Selection::Fixed;
        for (const auto& [lane, type] : targets) {
            if (lane != selector || nvr::lane_count(image.types.at(type)) != 64) {
                return refused("a dispatch exit that reads lanes other than the receiver");
            }
            out.outputs.push_back({lane, 64, 1});
        }
    } else {
        // The exits as the block synthesizer orders them: the given exits
        // first, then one trap per check reason.
        const bool any_normal = std::any_of(block.exits.begin(), block.exits.end(),
                                            [](const nvr::Exit& e) { return e.kind != nvr::ExitKind::Trap; });
        std::vector<std::uint32_t> specs;
        for (std::uint32_t i = 0; i < block.exits.size(); ++i) {
            const auto& exit = block.exits[i];
            if (exit.kind == nvr::ExitKind::Trap && !specs.empty() && exit.reason != 0 && any_normal) {
                compiler.trap_exit(exit.reason, i);
                continue;
            }
            if (specs.size() != i) {
                return refused("an exit after the check traps");
            }
            specs.push_back(i);
        }
        if (form.exit_when_true) {
            if (specs.size() != 2 || *form.exit_when_true > 1) {
                return refused("a condition that does not choose between two exits");
            }
        } else if (specs.size() != 1) {
            return refused("several exits without a condition");
        }
        // A shared form computes its scheduled operations first, in order,
        // as its replay builds them, so its checks fail in the same order.
        for (const auto& expression : form.schedule) {
            if (!compiler.emit(*expression)) return refused(compiler.failure);
        }
        for (const auto& [lane, expression] : form.computation_order()) {
            const auto reg = compiler.emit(*expression);
            if (!reg) return refused(compiler.failure);
            out.outputs.push_back({lane, nvr::lane_count(image.types.at(expression->type)), *reg});
        }
        if (form.condition) {
            const auto reg = compiler.emit(*form.condition);
            if (!reg) return refused(compiler.failure);
            out.selection = LoweredBlock::Selection::Condition;
            out.condition = *reg;
            out.exit_if_true = specs[*form.exit_when_true];
            out.exit_if_false = specs[1 - *form.exit_when_true];
        } else {
            out.selection = LoweredBlock::Selection::Fixed;
            out.fixed_exit = specs.front();
        }
        std::sort(out.outputs.begin(), out.outputs.end(),
                  [](const LoweredOutput& a, const LoweredOutput& b) { return a.first_lane < b.first_lane; });
    }

    // Every graph output lane is the selector or part of a value an exit
    // reads, so the lowered block yields all of them.
    std::vector<std::uint8_t> covered(graph.outputs.size(), 0);
    const auto cover = [&](std::uint32_t first, std::uint32_t count) {
        if (first > covered.size() || count > covered.size() - first) return false;
        std::fill_n(covered.begin() + first, count, 1);
        return true;
    };
    if (!cover(block.selector_start, selector)) {
        return refused("a selector outside the graph's outputs");
    }
    for (const auto& output : out.outputs) {
        if (!cover(output.first_lane, output.lanes)) {
            return refused("a value outside the graph's outputs");
        }
    }
    if (std::find(covered.begin(), covered.end(), 0) != covered.end()) {
        return refused("a graph output lane no exit reads");
    }
    if (out.uses_float && !float_lowering_supported()) {
        return refused("float operations on a platform without bit-exact hardware arithmetic");
    }
    LoweringResult result;
    result.block = std::move(out);
    return result;
}

std::uint64_t evaluate_float(const Instruction& in, std::uint64_t a, std::uint64_t b, bool& failed) {
    const unsigned w = in.width;
    std::uint64_t r = 0;
    switch (in.op) {
    case Opcode::FloatAdd:
    case Opcode::FloatSubtract:
    case Opcode::FloatMultiply:
    case Opcode::FloatDivide:
        if (in.format == FloatType::Binary32) {
            const float x = to_single(a);
            const float y = to_single(b);
            const float z = in.op == Opcode::FloatAdd        ? x + y
                            : in.op == Opcode::FloatSubtract ? x - y
                            : in.op == Opcode::FloatMultiply ? x * y
                                                             : x / y;
            r = single_bits(z);
        } else {
            const double x = to_double(a);
            const double y = to_double(b);
            const double z = in.op == Opcode::FloatAdd        ? x + y
                             : in.op == Opcode::FloatSubtract ? x - y
                             : in.op == Opcode::FloatMultiply ? x * y
                                                              : x / y;
            r = double_bits(z);
        }
        break;
    case Opcode::FloatNegate:
        r = a ^ (std::uint64_t{1} << (float_width(in.format) - 1));
        break;
    case Opcode::FloatEqual:
    case Opcode::FloatNotEqual:
    case Opcode::FloatLess:
    case Opcode::FloatLessEqual:
    case Opcode::FloatGreater:
    case Opcode::FloatGreaterEqual: {
        const double x = decode(a, in.format);
        const double y = decode(b, in.format);
        bool holds = false;
        switch (in.op) {
        case Opcode::FloatEqual: holds = x == y; break;
        case Opcode::FloatNotEqual: holds = x != y; break;
        case Opcode::FloatLess: holds = x < y; break;
        case Opcode::FloatLessEqual: holds = x <= y; break;
        case Opcode::FloatGreater: holds = x > y; break;
        default: holds = x >= y; break;
        }
        r = holds ? 1 : 0;
        break;
    }
    case Opcode::FloatFromInteger:
        r = in.to_format == FloatType::Binary32 ? single_bits(integer_to_single(a, in.is_signed, w))
                                                : double_bits(integer_to_double(a, in.is_signed, w));
        break;
    case Opcode::FloatToInteger: {
        const double t = std::trunc(decode(a, in.format));
        const unsigned to = in.to_width;
        // Powers of two are exact doubles, so the bounds compare exactly;
        // NaN fails every comparison.
        const double low = in.to_signed ? std::ldexp(-1.0, static_cast<int>(to) - 1) : 0.0;
        const double high = std::ldexp(1.0, in.to_signed ? static_cast<int>(to) - 1 : static_cast<int>(to));
        if (!(t >= low && t < high)) {
            failed = true;
        } else if (in.to_signed) {
            r = static_cast<std::uint64_t>(static_cast<std::int64_t>(t)) & width_mask(to);
        } else if (t < 9223372036854775808.0) {
            r = static_cast<std::uint64_t>(static_cast<std::int64_t>(t));
        } else {
            r = static_cast<std::uint64_t>(static_cast<std::int64_t>(t - 9223372036854775808.0)) +
                (std::uint64_t{1} << 63);
        }
        break;
    }
    case Opcode::FloatConvert: {
        const double x = decode(a, in.format);
        if (std::isnan(x)) {
            r = in.to_format == FloatType::Binary32 ? canonical_nan32 : canonical_nan64;
        } else if (in.to_format == FloatType::Binary64) {
            r = double_bits(x);
        } else {
            // Exact for a binary32 or 16-bit source; rounded to nearest
            // even from binary64.
            r = single_bits(static_cast<float>(x));
        }
        break;
    }
    default:
        break;
    }
    return r;
}

std::optional<std::uint32_t> execute(const LoweredBlock& block, std::span<const std::uint64_t> inputs,
                                     std::span<std::uint64_t> outputs) {
    thread_local std::vector<std::uint64_t> registers;
    registers.resize(block.input_widths.size() + block.code.size());
    return execute(block, inputs, outputs, registers);
}

std::optional<std::uint32_t> execute(const LoweredBlock& block, std::span<const std::uint64_t> inputs,
                                     std::span<std::uint64_t> outputs, std::span<std::uint64_t> registers) {
    if (block.uses_float && !float_environment_is_default()) {
        return std::nullopt;
    }
    const auto count = block.input_widths.size();
    for (std::size_t i = 0; i < count; ++i) {
        registers[i] = inputs[i] & width_mask(block.input_widths[i]);
    }
    for (std::size_t k = 0; k < block.code.size(); ++k) {
        const auto& in = block.code[k];
        bool failed = false;
        const auto r = evaluate(in, registers[in.a], registers[in.b], failed);
        if (failed) {
            return in.trap_exit == no_exit ? std::nullopt : std::optional<std::uint32_t>(in.trap_exit);
        }
        registers[count + k] = r;
    }
    for (std::size_t i = 0; i < block.outputs.size(); ++i) {
        outputs[i] = registers[block.outputs[i].reg];
    }
    switch (block.selection) {
    case LoweredBlock::Selection::Fixed:
        return block.fixed_exit;
    case LoweredBlock::Selection::Condition:
        return (registers[block.condition] & 1U) != 0 ? block.exit_if_true : block.exit_if_false;
    case LoweredBlock::Selection::Dispatch: {
        const auto tag = registers[0];
        const auto it = std::lower_bound(block.dispatch.begin(), block.dispatch.end(), tag,
                                         [](const auto& entry, std::uint64_t key) { return entry.first < key; });
        return it != block.dispatch.end() && it->first == tag ? it->second : block.dispatch_default;
    }
    }
    return std::nullopt;
}

void write_lanes(const LoweredBlock& block, std::uint32_t exit, std::span<const std::uint64_t> outputs,
                 std::span<std::int64_t> lanes) {
    if (block.exit_count > 1) {
        const auto count = nvr::selector_lanes(block.exit_count);
        for (unsigned j = 0; j < count; ++j) {
            lanes[block.selector_start + j] = static_cast<std::int64_t>((exit >> j) & 1U);
        }
    }
    for (std::size_t i = 0; i < block.outputs.size(); ++i) {
        const auto& output = block.outputs[i];
        for (std::uint32_t b = 0; b < output.lanes; ++b) {
            lanes[output.first_lane + b] = static_cast<std::int64_t>((outputs[i] >> b) & 1U);
        }
    }
}

} // namespace neura::lift
