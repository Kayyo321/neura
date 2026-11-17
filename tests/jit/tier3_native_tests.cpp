// The code generator of tier 3 (src/jit/tier3_native.hpp) on IR built by
// hand, run natively against a context whose helpers are the test's: each
// lifted operation, for every width class (1, 2, 8, 16, 17, 31, 32, 33, 63,
// 64), signed and unsigned, with operands that fit the width, operands with
// bits above it, and a constant second operand, on boundary values, against
// lift::evaluate: the same result, or a deoptimization exactly where
// evaluate's check fails; the forms whose check the passes removed, where
// evaluate does not fail. Floats computed with the hardware's NaN read as the
// reference's bits wherever their bits are read, and float comparisons that
// branch. Also the Budget's comparison, and code that is the same for the
// same IR. Part of the `jit` suite (backend-jit); it runs where
// tier 3 has a code generator (x86-64 Linux).
#include "backend/fixtures.hpp"

#include "jit/jit.hpp"
#include "jit/tier3_lower.hpp"
#include "jit/tier3_native.hpp"
#include "lift/evaluate.hpp"
#include "nvm/tier3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace neura::testing {
namespace {

namespace t3 = nvm::tier3;
using lift::Opcode;

// --- A context whose helpers record what native code asks ----------------------

struct Fake {
    std::vector<std::uint64_t> slots;
    std::vector<std::uint8_t> flags;
    std::uint32_t block = 0;
    std::vector<std::uint64_t> values;
    std::vector<std::uint64_t> constants;
    t3::Context context;
    // What the endings recorded.
    std::uint32_t left_at = t3::none;
    std::uint64_t left_counter = 0;
    std::uint32_t deopt_state = t3::none;
    std::uint32_t deopt_reason = 0;
    std::uint64_t deopt_counter = 0;
    std::map<std::uint32_t, std::uint64_t> deopt_values;
    std::uint32_t evaluations = 0;
    std::vector<std::uint64_t> helper_slots;
    std::uint32_t helper_block = 0;
    std::uint64_t helper_counter = 0;
    // What the return helper was given (NVR 1.7): the value, the words after
    // it and their count, and C.
    std::uint64_t returned = 0;
    std::uint32_t returned_words = 0;
    std::vector<std::uint64_t> returned_extra;
    std::uint64_t returned_counter = 0;
};

Fake& fake_of(t3::Context* context) { return *static_cast<Fake*>(context->owner); }

std::uint64_t fake_leave(t3::Context* context, std::uint32_t block) {
    auto& fake = fake_of(context);
    fake.left_at = block;
    fake.left_counter = context->C;
    context->status = 0;
    return 0;
}

std::uint64_t fake_deoptimize(t3::Context* context, std::uint32_t state_id, std::uint32_t reason) {
    auto& fake = fake_of(context);
    const auto& state = context->program->ir.states[state_id];
    fake.deopt_state = state_id;
    fake.deopt_reason = reason;
    fake.deopt_counter = context->values[state.blocks];
    for (const auto& slot : state.slots) {
        if (slot.kind == t3::StateSlot::Kind::Value) {
            fake.deopt_values[static_cast<std::uint32_t>(slot.value)] = context->values[slot.value];
        }
    }
    context->status = 0;
    return 0;
}

std::uint64_t fake_evaluate(t3::Context* context, std::uint32_t inst, std::uint64_t a, std::uint64_t b) {
    auto& fake = fake_of(context);
    ++fake.evaluations;
    bool failed = false;
    const auto value = lift::evaluate(context->program->ir.insts[inst].lifted, a, b, failed);
    context->status = failed ? 1 : 0;
    return value;
}

[[noreturn]] void unexpected() {
    std::fprintf(stderr, "tier 3 native test: a helper the test does not provide was called\n");
    std::abort();
}
std::uint64_t fake_state(t3::Context*, std::uint32_t, const std::uint64_t*, std::uint32_t) { unexpected(); }
std::uint64_t fake_check_reference(t3::Context*, std::uint32_t, std::uint64_t) { unexpected(); }
std::uint64_t fake_call(t3::Context*, std::uint32_t, std::uint32_t, const std::uint64_t*, std::uint32_t) { unexpected(); }
std::uint64_t fake_return(t3::Context* context, std::uint64_t value, std::uint64_t blocks, std::uint32_t words) {
    auto& fake = fake_of(context);
    fake.returned = value;
    fake.returned_words = words;
    fake.returned_extra.assign(context->result_words, context->result_words + words);
    fake.returned_counter = blocks;
    context->C = blocks + 1;
    context->status = 0;
    return value;
}
// An R3 helper: it records the frame and the counter it is called with, and
// returns 1000 + its argument.
std::uint64_t fake_helper(t3::Context* context, std::uint32_t, const std::uint64_t* arguments, std::uint32_t count) {
    auto& fake = fake_of(context);
    fake.helper_slots.assign(context->slots, context->slots + fake.slots.size());
    fake.helper_block = *context->block;
    fake.helper_counter = context->C;
    context->status = 0;
    return 1000 + (count > 0 ? arguments[0] : 0);
}

// A native program from hand-built IR, or the reason there is none.
struct Compiled {
    std::unique_ptr<t3::Program> program;
    std::string failure;
    std::vector<std::uint8_t> bytes;
};

Compiled compile(t3::Function function) {
    Compiled compiled;
    compiled.program = std::make_unique<t3::Program>();
    compiled.program->ir = std::move(function);
    auto generated = jit::native::generate(compiled.program->ir, true);
    if (!generated.failure.empty()) {
        compiled.failure = generated.failure;
        compiled.program.reset();
        return compiled;
    }
    compiled.bytes = generated.code.bytes;
    compiled.program->machine = jit::Program::load(generated.code.bytes, compiled.failure);
    if (!compiled.program->machine) {
        compiled.program.reset();
        return compiled;
    }
    compiled.program->entry_code = generated.code.entries;
    compiled.program->trampoline = generated.code.trampoline;
    compiled.program->lazy_entries =
        std::make_unique<std::atomic<const std::uint8_t*>[]>(compiled.program->entry_code.size());
    return compiled;
}

// Runs entry 0 of `program` on a frame of `slots` (every flag clear).
Fake run(const t3::Program& program, const std::vector<std::uint64_t>& slots, std::uint64_t room = ~std::uint64_t{0}) {
    Fake fake;
    fake.slots = slots;
    fake.flags.assign(slots.size(), 0);
    fake.values.assign(program.ir.insts.size(), 0xDEADBEEFDEADBEEFULL);
    fake.constants = {0x1234, 0xFFFFFFFFFFFFFFFFULL};
    auto& c = fake.context;
    c.owner = &fake;
    c.C = 0;
    c.room = room;
    c.slots = fake.slots.data();
    c.flags = fake.flags.data();
    c.block = &fake.block;
    c.program = &program;
    c.values = fake.values.data();
    c.run_constants = fake.constants.data();
    c.native_depth = 1;
    c.functions = {&fake_state, &fake_check_reference, &fake_helper,     &fake_call,
                   &fake_return, &fake_leave,          &fake_deoptimize, &fake_evaluate,
                   nullptr};
    std::uint64_t bytes = 0;
    std::string failure;
    program.enter(c, program.entry(0, bytes, failure));
    return fake;
}

// --- Hand-built IR -------------------------------------------------------------------

struct Builder {
    t3::Function f;

    explicit Builder(std::uint32_t slots) {
        f.slot_count = slots;
        f.slot_types.assign(slots, {t3::Type::Int, 64});
        f.function = 0;
        f.blocks.resize(2);
        f.blocks[0].entry = 0;
        f.entries.push_back({0, 0});
        f.blocks[0].succs = {1};
        f.blocks[1].preds = {0};
        // Value 0 is a placeholder, as the builder makes it.
        add(0, {});
    }

    std::uint32_t add(std::uint32_t block, t3::Inst in) {
        const auto id = static_cast<std::uint32_t>(f.insts.size());
        f.insts.push_back(std::move(in));
        if (id != 0) f.blocks[block].insts.push_back(id);
        return id;
    }

    std::uint32_t value(std::uint32_t block, t3::Op op, unsigned width, std::uint64_t imm = 0,
                        std::uint32_t a = t3::none) {
        t3::Inst in;
        in.op = op;
        in.type = {t3::Type::Int, static_cast<std::uint8_t>(width)};
        in.imm = imm;
        in.operands[0] = a;
        return add(block, std::move(in));
    }

    // A state with every slot unchanged and `blocks` as C, at `block`.
    std::uint32_t state(std::uint32_t blocks, std::uint32_t block) {
        t3::FrameState s;
        s.function = 0;
        s.block = block;
        s.slots.resize(f.slot_count);
        s.flags.resize(f.slot_count);
        s.blocks = blocks;
        f.states.push_back(std::move(s));
        return static_cast<std::uint32_t>(f.states.size() - 1);
    }
};

unsigned bits_of(std::uint64_t value) {
    unsigned bits = 0;
    while (bits < 64 && (value >> bits) != 0) ++bits;
    return bits;
}

// How the operands of a test reach the operation.
enum class Operands { Fitting, Wide, ConstantB };

const char* name_of(Operands mode) {
    switch (mode) {
    case Operands::Fitting: return "fitting";
    case Operands::Wide: return "wide";
    default: return "constant";
    }
}

// entry: a = slot 0, b = slot 1 (or the constant), C; then block 1:
//   r = Lift(instruction) a, b (checked when it can fail, unless `unchecked`:
//   the form the passes make where they proved that it does not; state:
//   block 3's entry)
//   C + 1; WriteBack slot 2 = r (flag set, block 3); Leave at block 9.
t3::Function lift_function(const lift::Instruction& instruction, unsigned a_width, unsigned b_width, bool constant_b,
                           std::uint64_t b_value, bool unchecked = false) {
    Builder b(3);
    const auto arity = jit::native::lift_arity(instruction);
    const auto a = arity >= 1 ? b.value(0, t3::Op::EntryLoad, a_width, 0) : t3::none;
    std::uint32_t second = t3::none;
    if (arity >= 2 && !constant_b) second = b.value(0, t3::Op::EntryLoad, b_width, 1);
    const auto c = b.value(0, t3::Op::EntryCounter, 64, 0);
    b.f.insts[c].type = {t3::Type::Int, 64};
    t3::Inst jump;
    jump.op = t3::Op::Jump;
    b.add(0, jump);
    if (arity >= 2 && constant_b) second = b.value(1, t3::Op::Const, std::max(1U, bits_of(b_value)), b_value);
    const bool fails = instruction.op == Opcode::Add || instruction.op == Opcode::Subtract ||
                       instruction.op == Opcode::Negate || instruction.op == Opcode::Multiply ||
                       instruction.op == Opcode::Divide || instruction.op == Opcode::Remainder ||
                       instruction.op == Opcode::ShiftLeft || instruction.op == Opcode::ShiftRight ||
                       instruction.op == Opcode::Index || instruction.op == Opcode::ConvertInteger ||
                       instruction.op == Opcode::FloatToInteger;
    t3::Inst lift;
    lift.op = t3::Op::Lift;
    lift.lifted = instruction;
    lift.type = {t3::Type::Int, 64};
    lift.operands[0] = a;
    lift.operands[1] = second;
    if (fails && !unchecked) {
        lift.flags = t3::Checked;
        lift.state = b.state(c, 3);
    }
    const auto r = b.add(1, lift);
    const auto c1 = b.value(1, t3::Op::CounterAdd, 64, 1, c);
    const auto commit = b.state(c1, 3);
    b.f.states[commit].slots[2] = {t3::StateSlot::Kind::Value, r};
    b.f.states[commit].flags[2] = {t3::StateFlag::Kind::One, 0};
    t3::Inst back;
    back.op = t3::Op::WriteBack;
    back.flags = t3::Effect;
    back.state = commit;
    b.add(1, back);
    t3::Inst leave;
    leave.op = t3::Op::Leave;
    leave.operands[0] = c1;
    leave.imm = 9;
    b.add(1, leave);
    return std::move(b.f);
}

std::vector<std::uint64_t> boundary_values(unsigned w, bool wide) {
    const auto m = lift::mask_of(w);
    const auto s = std::uint64_t{1} << (w - 1);
    std::set<std::uint64_t> values{0, 1, 2, 3, 5, m, m - 1, m >> 1, s, s - 1, s + 1, m / 3, (m / 3) * 2 + 1};
    if (wide) {
        for (const std::uint64_t v : std::initializer_list<std::uint64_t>{
                 m + 1, m + 2, ~std::uint64_t{0}, std::uint64_t{1} << 63, (std::uint64_t{1} << 63) - 1, m << 1,
                 0x5555555555555555ULL, 0xAAAAAAAAAAAAAAAAULL, s | (m + 1)}) {
            values.insert(v);
        }
    }
    std::vector<std::uint64_t> out;
    for (const auto v : values) out.push_back(wide ? v : v & m);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

struct Tally {
    std::size_t cases = 0;
    std::size_t deopts = 0;
    std::size_t failures = 0;
    std::string first;
};

// An unchecked operation runs only on operands for which evaluate reports
// no failure (the passes' proof), so `unchecked` skips the others.
void compare_case(const t3::Program& program, const lift::Instruction& instruction, std::uint64_t a, std::uint64_t b,
                  bool constant_b, Tally& tally, const std::string& label, bool unchecked = false) {
    bool failed = false;
    const auto expected = lift::evaluate(instruction, a, b, failed);
    if (failed && unchecked) return;
    const auto fake = run(program, {a, constant_b ? 0 : b, 0x77});
    ++tally.cases;
    std::string problem;
    if (failed) {
        ++tally.deopts;
        if (fake.deopt_state == t3::none) problem = "no deoptimization where evaluate fails";
        else if (fake.deopt_reason != static_cast<std::uint32_t>(t3::Reason::Checks)) problem = "the wrong reason";
        else if (fake.deopt_counter != 0) problem = "the stub stored the wrong counter";
        else if (fake.slots[2] != 0x77 || fake.flags[2] != 0) problem = "the frame was written before the check";
    } else if (fake.left_at != 9) {
        problem = "no Leave where evaluate succeeds";
    } else if (fake.slots[2] != expected) {
        problem = "result " + std::to_string(fake.slots[2]) + ", evaluate gives " + std::to_string(expected);
    } else if (fake.flags[2] != 1 || fake.block != 3 || fake.left_counter != 1) {
        problem = "the write-back or the counter";
    } else if (fake.slots[0] != a) {
        problem = "an operand slot changed";
    }
    if (!problem.empty()) {
        if (tally.failures == 0) {
            tally.first = label + " a=" + std::to_string(a) + " b=" + std::to_string(b) + ": " + problem;
        }
        ++tally.failures;
    }
}

// Every pair of boundary values for `instruction`, in the three operand
// modes.
void sweep(const lift::Instruction& instruction, const std::vector<std::uint64_t>& b_values_extra, Tally& tally,
           const std::string& label, bool unchecked = false) {
    const unsigned w = instruction.width;
    for (const auto mode : {Operands::Fitting, Operands::Wide, Operands::ConstantB}) {
        if (mode == Operands::Wide && w == 64) continue;
        if (mode == Operands::ConstantB && jit::native::lift_arity(instruction) < 2) continue;
        const bool wide = mode == Operands::Wide;
        const auto a_values = boundary_values(w, wide);
        auto b_values = boundary_values(w, wide);
        for (const auto v : b_values_extra) b_values.push_back(wide ? v : v & lift::mask_of(w));
        const unsigned operand_width = wide ? 64 : w;
        const auto here = label + " (" + name_of(mode) + ")";
        if (mode == Operands::ConstantB) {
            for (const auto b : b_values) {
                const auto compiled =
                    compile(lift_function(instruction, operand_width, operand_width, true, b, unchecked));
                check(compiled.program != nullptr, here + " compiles: " + compiled.failure);
                for (const auto a : a_values) {
                    compare_case(*compiled.program, instruction, a, b, true, tally, here, unchecked);
                }
            }
            continue;
        }
        const auto compiled = compile(lift_function(instruction, operand_width, operand_width, false, 0, unchecked));
        check(compiled.program != nullptr, here + " compiles: " + compiled.failure);
        for (const auto a : a_values) {
            for (const auto b : b_values) {
                compare_case(*compiled.program, instruction, a, b, false, tally, here, unchecked);
            }
        }
    }
}

bool native_runs() { return jit::native::supported(); }

constexpr unsigned widths[] = {1, 2, 8, 16, 17, 31, 32, 33, 63, 64};

} // namespace

NEURA_TEST(jit, tier_3_checked_integer_operations_match_evaluate_at_every_width_class) {
    if (!native_runs()) return;
    Tally tally;
    for (const auto op : {Opcode::Add, Opcode::Subtract, Opcode::Multiply, Opcode::Negate, Opcode::Divide,
                          Opcode::Remainder, Opcode::ShiftLeft, Opcode::ShiftRight, Opcode::Index}) {
        for (const auto w : widths) {
            for (const bool is_signed : {false, true}) {
                lift::Instruction in;
                in.op = op;
                in.width = static_cast<std::uint8_t>(w);
                in.is_signed = is_signed;
                std::vector<std::uint64_t> extra;
                if (op == Opcode::ShiftLeft || op == Opcode::ShiftRight) {
                    extra = {w - 1U, w, w + 1U, 63, 64, 65, std::uint64_t{1} << 63};
                }
                if (op == Opcode::Divide || op == Opcode::Remainder) {
                    // Powers of two, and their neighbours, as constant
                    // divisors.
                    for (unsigned k = 2; k + 1 < w; k += (w > 16 ? 7 : 1)) {
                        extra.push_back(std::uint64_t{1} << k);
                        extra.push_back((std::uint64_t{1} << k) + 1);
                    }
                    extra.push_back(std::uint64_t{1} << (w - 2 < 64 ? w - 2 : 0));
                }
                sweep(in, extra, tally,
                      std::string(op == Opcode::Add ? "add" : op == Opcode::Subtract ? "subtract"
                                  : op == Opcode::Multiply                           ? "multiply"
                                  : op == Opcode::Negate                             ? "negate"
                                  : op == Opcode::Divide                             ? "divide"
                                  : op == Opcode::Remainder                          ? "remainder"
                                  : op == Opcode::ShiftLeft                          ? "shift left"
                                  : op == Opcode::ShiftRight                         ? "shift right"
                                                                                     : "index") +
                          (is_signed ? " signed " : " unsigned ") + std::to_string(w));
            }
        }
    }
    for (const auto w : widths) {
        for (const auto to : widths) {
            for (const bool is_signed : {false, true}) {
                for (const bool to_signed : {false, true}) {
                    lift::Instruction in;
                    in.op = Opcode::ConvertInteger;
                    in.width = static_cast<std::uint8_t>(w);
                    in.is_signed = is_signed;
                    in.to_width = static_cast<std::uint8_t>(to);
                    in.to_signed = to_signed;
                    sweep(in, {}, tally,
                          "convert " + std::to_string(w) + (is_signed ? "s" : "u") + " to " + std::to_string(to) +
                              (to_signed ? "s" : "u"));
                }
            }
        }
    }
    check(tally.failures == 0, std::to_string(tally.failures) + " of " + std::to_string(tally.cases) +
                                   " cases differ; the first: " + tally.first);
    check(tally.deopts > 1000 && tally.cases > 100000,
          "the sweep reaches failures and successes (" + std::to_string(tally.deopts) + " of " +
              std::to_string(tally.cases) + ")");
}

NEURA_TEST(jit, tier_3_unchecked_integer_operations_match_evaluate_at_every_width_class) {
    if (!native_runs()) return;
    Tally tally;
    for (const auto op : {Opcode::AddWrapping, Opcode::SubtractWrapping, Opcode::Equal, Opcode::NotEqual, Opcode::Less,
                          Opcode::LessEqual, Opcode::Greater, Opcode::GreaterEqual, Opcode::And, Opcode::Or,
                          Opcode::Not, Opcode::BitAnd, Opcode::BitOr, Opcode::BitXor, Opcode::BitNot,
                          Opcode::ValueHash}) {
        for (const auto w : widths) {
            for (const bool is_signed : {false, true}) {
                lift::Instruction in;
                in.op = op;
                in.width = static_cast<std::uint8_t>(w);
                in.is_signed = is_signed;
                sweep(in, {}, tally,
                      "op " + std::to_string(static_cast<int>(op)) + (is_signed ? " signed " : " unsigned ") +
                          std::to_string(w));
            }
        }
    }
    check(tally.failures == 0, std::to_string(tally.failures) + " of " + std::to_string(tally.cases) +
                                   " cases differ; the first: " + tally.first);
}

// The operations whose check the passes removed (a Multiply, Negate, Divide,
// Remainder, ShiftLeft, ShiftRight, Index or ConvertInteger with no Checked
// flag and no state) compute evaluate's result, operands with bits above
// the width included, wherever evaluate reports no failure.
NEURA_TEST(jit, tier_3_operations_whose_checks_the_passes_removed_match_evaluate) {
    if (!native_runs()) return;
    Tally tally;
    for (const auto op : {Opcode::Multiply, Opcode::Negate, Opcode::Divide, Opcode::Remainder, Opcode::ShiftLeft,
                          Opcode::ShiftRight, Opcode::Index}) {
        for (const auto w : widths) {
            for (const bool is_signed : {false, true}) {
                lift::Instruction in;
                in.op = op;
                in.width = static_cast<std::uint8_t>(w);
                in.is_signed = is_signed;
                std::vector<std::uint64_t> extra;
                if (op == Opcode::ShiftLeft || op == Opcode::ShiftRight) extra = {w - 1U, w / 2};
                if (op == Opcode::Divide || op == Opcode::Remainder) {
                    for (unsigned k = 2; k + 1 < w; k += (w > 16 ? 7 : 1)) {
                        extra.push_back(std::uint64_t{1} << k);
                        extra.push_back((std::uint64_t{1} << k) + 1);
                    }
                }
                sweep(in, extra, tally,
                      "unchecked op " + std::to_string(static_cast<int>(op)) + (is_signed ? " signed " : " unsigned ") +
                          std::to_string(w),
                      true);
            }
        }
    }
    for (const auto w : widths) {
        for (const auto to : widths) {
            for (const bool is_signed : {false, true}) {
                for (const bool to_signed : {false, true}) {
                    lift::Instruction in;
                    in.op = Opcode::ConvertInteger;
                    in.width = static_cast<std::uint8_t>(w);
                    in.is_signed = is_signed;
                    in.to_width = static_cast<std::uint8_t>(to);
                    in.to_signed = to_signed;
                    sweep(in, {}, tally,
                          "unchecked convert " + std::to_string(w) + (is_signed ? "s" : "u") + " to " +
                              std::to_string(to) + (to_signed ? "s" : "u"),
                          true);
                }
            }
        }
    }
    check(tally.failures == 0, std::to_string(tally.failures) + " of " + std::to_string(tally.cases) +
                                   " cases differ; the first: " + tally.first);
    check(tally.cases > 100000, "the sweep runs (" + std::to_string(tally.cases) + " cases)");
}

NEURA_TEST(jit, tier_3_float_operations_match_evaluate_bit_for_bit) {
    if (!native_runs()) return;
    Tally tally;
    const std::vector<std::uint64_t> doubles{0,
                                             0x8000000000000000ULL,
                                             0x3FF0000000000000ULL,
                                             0xBFF0000000000000ULL,
                                             0x7FF0000000000000ULL,
                                             0xFFF0000000000000ULL,
                                             0x7FF8000000000000ULL,
                                             0xFFF8000000000001ULL,
                                             0x7FF0000000000001ULL,
                                             0x0000000000000001ULL,
                                             0x000FFFFFFFFFFFFFULL,
                                             0x43E0000000000000ULL,
                                             0xC3E0000000000000ULL,
                                             0x43F0000000000000ULL,
                                             0x41DFFFFFFFC00000ULL,
                                             0xC1E0000000000000ULL,
                                             0x4415AF1D78B58C40ULL,
                                             0x3FE5555555555555ULL};
    const std::vector<std::uint64_t> singles{0,          0x80000000, 0x3F800000, 0xBF800000, 0x7F800000,
                                             0xFF800000, 0x7FC00000, 0xFFC00001, 0x7F800001, 0x00000001,
                                             0x007FFFFF, 0x5F000000, 0xDF000000, 0x4F000000, 0xCF000000,
                                             0x3EAAAAAB, 0x7F7FFFFF};
    for (const auto format : {lift::FloatType::Binary32, lift::FloatType::Binary64}) {
        const auto& values = format == lift::FloatType::Binary64 ? doubles : singles;
        const unsigned width = format == lift::FloatType::Binary64 ? 64 : 32;
        for (const auto op : {Opcode::FloatAdd, Opcode::FloatSubtract, Opcode::FloatMultiply, Opcode::FloatDivide,
                              Opcode::FloatEqual, Opcode::FloatNotEqual, Opcode::FloatLess, Opcode::FloatLessEqual,
                              Opcode::FloatGreater, Opcode::FloatGreaterEqual, Opcode::FloatNegate,
                              Opcode::FloatConvert, Opcode::FloatToInteger}) {
            std::vector<lift::Instruction> forms;
            lift::Instruction in;
            in.op = op;
            in.width = static_cast<std::uint8_t>(width);
            in.format = format;
            if (op == Opcode::FloatConvert) {
                in.to_format = format == lift::FloatType::Binary64 ? lift::FloatType::Binary32 : lift::FloatType::Binary64;
                forms.push_back(in);
                in.to_format = format;
                forms.push_back(in);
            } else if (op == Opcode::FloatToInteger) {
                for (const auto to : widths) {
                    for (const bool to_signed : {false, true}) {
                        in.to_width = static_cast<std::uint8_t>(to);
                        in.to_signed = to_signed;
                        forms.push_back(in);
                    }
                }
            } else {
                forms.push_back(in);
            }
            for (const auto& form : forms) {
                const auto compiled = compile(lift_function(form, width, width, false, 0));
                check(compiled.program != nullptr, "a float operation compiles: " + compiled.failure);
                for (const auto a : values) {
                    for (const auto b : values) {
                        compare_case(*compiled.program, form, a, b, false, tally,
                                     "float op " + std::to_string(static_cast<int>(op)) + " width " +
                                         std::to_string(width) + " to " + std::to_string(form.to_width));
                    }
                }
            }
        }
        // From integers of every width, signed and unsigned.
        for (const auto w : widths) {
            for (const bool is_signed : {false, true}) {
                lift::Instruction in;
                in.op = Opcode::FloatFromInteger;
                in.width = static_cast<std::uint8_t>(w);
                in.is_signed = is_signed;
                in.to_format = format;
                sweep(in, {}, tally, "float from integer " + std::to_string(w));
            }
        }
    }
    check(tally.failures == 0, std::to_string(tally.failures) + " of " + std::to_string(tally.cases) +
                                   " cases differ; the first: " + tally.first);
}

// A binary32 or binary64 sum of two slots computed with the hardware's NaN
// (Deferred, after tier3::place_canons), read by an integer operation on its
// bits (a shift: to_bits), by abs (a BitAnd with the magnitude mask), by a
// write of the frame, and through a negation; and a square root. Each result
// written back must be the reference's bits: lift::evaluate on the canonical
// sum, and the correctly rounded root with the canonical NaN.
NEURA_TEST(jit, tier_3_deferred_floats_are_canonical_wherever_their_bits_are_read) {
    if (!native_runs()) return;
    enum class Use { Bits, Abs, Frame, Negate, Root };
    Tally tally;
    const std::vector<std::uint64_t> doubles{0,
                                             0x8000000000000000ULL,
                                             0x3FF0000000000000ULL,
                                             0xC000000000000000ULL,
                                             0x7FF0000000000000ULL,
                                             0xFFF0000000000000ULL,
                                             0x7FF8000000000000ULL,
                                             0xFFF8000000000001ULL,
                                             0x7FF0000000000001ULL,
                                             0xFFFFFFFFFFFFFFFFULL,
                                             0x0000000000000001ULL,
                                             0x000FFFFFFFFFFFFFULL};
    const std::vector<std::uint64_t> singles{0,          0x80000000, 0x3F800000, 0xC0000000, 0x7F800000, 0xFF800000,
                                             0x7FC00000, 0xFFC00001, 0x7F800001, 0xFFFFFFFF, 0x00000001, 0x007FFFFF};
    for (const auto format : {lift::FloatType::Binary32, lift::FloatType::Binary64}) {
        const bool dbl = format == lift::FloatType::Binary64;
        const unsigned width = dbl ? 64 : 32;
        const t3::ValueType type{dbl ? t3::Type::F64 : t3::Type::F32, static_cast<std::uint8_t>(width)};
        const auto& values = dbl ? doubles : singles;
        for (const auto use : {Use::Bits, Use::Abs, Use::Frame, Use::Negate, Use::Root}) {
            if (use == Use::Root && !dbl) continue;
            Builder b(3);
            b.f.slot_types.assign(3, type);
            const auto x = b.value(0, t3::Op::EntryLoad, width, 0);
            const auto y = b.value(0, t3::Op::EntryLoad, width, 1);
            b.f.insts[x].type = b.f.insts[y].type = type;
            const auto c = b.value(0, t3::Op::EntryCounter, 64, 0);
            t3::Inst jump;
            jump.op = t3::Op::Jump;
            b.add(0, jump);
            lift::Instruction add;
            add.op = Opcode::FloatAdd;
            add.width = static_cast<std::uint8_t>(width);
            add.format = format;
            t3::Inst sum;
            sum.op = t3::Op::Lift;
            sum.lifted = add;
            sum.type = type;
            sum.operands[0] = x;
            sum.operands[1] = y;
            const auto s = b.add(1, sum);
            lift::Instruction read;
            read.width = static_cast<std::uint8_t>(width);
            t3::Inst result;
            result.op = t3::Op::Lift;
            result.operands[0] = s;
            switch (use) {
            case Use::Bits:
                read.op = Opcode::ShiftRight;
                result.operands[1] = b.value(1, t3::Op::Const, 1, 1);
                result.type = {t3::Type::Int, static_cast<std::uint8_t>(width)};
                break;
            case Use::Abs:
                read.op = Opcode::BitAnd;
                result.operands[1] = b.value(1, t3::Op::Const, width - 1, dbl ? 0x7FFFFFFFFFFFFFFFULL : 0x7FFFFFFFULL);
                result.type = {t3::Type::Int, static_cast<std::uint8_t>(width)};
                break;
            case Use::Negate:
                read.op = Opcode::FloatNegate;
                read.format = format;
                result.type = type;
                break;
            case Use::Root:
                result.op = t3::Op::Sqrt;
                result.operands[0] = x;
                result.type = type;
                break;
            case Use::Frame: break;
            }
            result.lifted = read;
            const auto r = use == Use::Frame ? s : b.add(1, result);
            const auto c1 = b.value(1, t3::Op::CounterAdd, 64, 1, c);
            const auto commit = b.state(c1, 3);
            b.f.states[commit].slots[2] = {t3::StateSlot::Kind::Value, r};
            b.f.states[commit].flags[2] = {t3::StateFlag::Kind::One, 0};
            t3::Inst back;
            back.op = t3::Op::WriteBack;
            back.flags = t3::Effect;
            back.state = commit;
            b.add(1, back);
            t3::Inst leave;
            leave.op = t3::Op::Leave;
            leave.operands[0] = c1;
            leave.imm = 9;
            b.add(1, leave);
            const nvr::Image image;
            t3::place_canons(image, b.f, false);
            const auto compiled = compile(std::move(b.f));
            check(compiled.program != nullptr, "the function compiles: " + compiled.failure);
            if (!compiled.program) continue;
            for (const auto a : values) {
                for (const auto other : values) {
                    bool failed = false;
                    const auto canonical = lift::evaluate(add, a, other, failed);
                    std::uint64_t expected = canonical;
                    switch (use) {
                    case Use::Bits: expected = canonical >> 1; break;
                    case Use::Abs: expected = canonical & (dbl ? 0x7FFFFFFFFFFFFFFFULL : 0x7FFFFFFFULL); break;
                    case Use::Negate: expected = lift::evaluate(read, canonical, 0, failed); break;
                    case Use::Root: {
                        double v = 0;
                        std::memcpy(&v, &a, 8);
                        const double root = std::sqrt(v);
                        std::memcpy(&expected, &root, 8);
                        expected = t3::canonical_nan(expected, 64);
                        break;
                    }
                    case Use::Frame: break;
                    }
                    const auto fake = run(*compiled.program, {a, other, 0x77});
                    ++tally.cases;
                    if (fake.left_at != 9 || fake.slots[2] != expected) {
                        if (tally.failures == 0) {
                            tally.first = "use " + std::to_string(static_cast<int>(use)) + " width " +
                                          std::to_string(width) + " a=" + std::to_string(a) +
                                          " b=" + std::to_string(other) + ": result " + std::to_string(fake.slots[2]) +
                                          ", expected " + std::to_string(expected);
                        }
                        ++tally.failures;
                    }
                }
            }
        }
    }
    check(tally.failures == 0, std::to_string(tally.failures) + " of " + std::to_string(tally.cases) +
                                   " cases differ; the first: " + tally.first);
}

// A function that returns several words (NVR 1.7): the value is in rsi, the
// words after the first are in the context's result_words, their count in the
// helper's last argument, and a deferred float word is the canonical NaN where
// the reference's sum is a NaN, as a frame write makes it.
NEURA_TEST(jit, tier_3_a_return_of_several_words_hands_every_word_to_the_helper) {
    if (!native_runs()) return;
    Tally tally;
    const std::vector<std::uint64_t> doubles{0, 0x8000000000000000ULL, 0x3FF0000000000000ULL, 0x7FF0000000000000ULL,
                                             0x7FF8000000000000ULL, 0xFFF8000000000001ULL, 0x7FF0000000000001ULL,
                                             0xFFFFFFFFFFFFFFFFULL};
    const t3::ValueType type{t3::Type::F64, 64};
    for (const std::uint32_t extra_words : {1U, 2U, 5U}) {
        Builder b(3);
        b.f.slot_types.assign(3, type);
        const auto x = b.value(0, t3::Op::EntryLoad, 64, 0);
        const auto y = b.value(0, t3::Op::EntryLoad, 64, 1);
        b.f.insts[x].type = b.f.insts[y].type = type;
        const auto c = b.value(0, t3::Op::EntryCounter, 64, 0);
        t3::Inst jump;
        jump.op = t3::Op::Jump;
        b.add(0, jump);
        lift::Instruction add;
        add.op = Opcode::FloatAdd;
        add.width = 64;
        add.format = lift::FloatType::Binary64;
        t3::Inst sum;
        sum.op = t3::Op::Lift;
        sum.lifted = add;
        sum.type = type;
        sum.operands[0] = x;
        sum.operands[1] = y;
        const auto s = b.add(1, sum);
        // The words after the first: the sum, then the inputs again.
        std::vector<std::uint32_t> rest{s};
        for (std::uint32_t i = 1; i < extra_words; ++i) rest.push_back(i % 2 ? y : x);
        t3::Inst ret;
        ret.op = t3::Op::Return;
        ret.operands[0] = c;
        ret.operands[1] = x;
        ret.flags = t3::Checked | t3::Effect;
        ret.state = b.state(c, 0);
        ret.extra = static_cast<std::uint32_t>(b.f.extras.size());
        ret.count = extra_words;
        b.f.extras.insert(b.f.extras.end(), rest.begin(), rest.end());
        b.add(1, ret);
        const nvr::Image image;
        t3::place_canons(image, b.f, false);
        const auto compiled = compile(std::move(b.f));
        check(compiled.program != nullptr, "the function compiles: " + compiled.failure);
        if (!compiled.program) continue;
        for (const auto a : doubles) {
            for (const auto other : doubles) {
                bool failed = false;
                const auto canonical = lift::evaluate(add, a, other, failed);
                const auto fake = run(*compiled.program, {a, other, 0x77});
                ++tally.cases;
                bool same = fake.returned == a && fake.returned_words == extra_words && fake.returned_counter == 0 &&
                            fake.returned_extra.size() == extra_words && fake.returned_extra[0] == canonical;
                for (std::uint32_t i = 1; same && i < extra_words; ++i) same = fake.returned_extra[i] == (i % 2 ? other : a);
                if (!same) {
                    if (tally.failures == 0) {
                        tally.first = std::to_string(extra_words) + " words, a=" + std::to_string(a) + " b=" +
                                      std::to_string(other) + ": sum " +
                                      std::to_string(fake.returned_extra.empty() ? 0 : fake.returned_extra[0]) +
                                      ", expected " + std::to_string(canonical);
                    }
                    ++tally.failures;
                }
            }
        }
    }
    check(tally.failures == 0, std::to_string(tally.failures) + " of " + std::to_string(tally.cases) +
                                   " cases differ; the first: " + tally.first);
}

// A deferred float sum that only a WriteBack reads (no helper or call, before
// or after) is still written by a spill between the write-back and the end of
// the code, so a move into its frame home stores the canonical NaN: the
// lowering marks its virtual register (Lowered::canon_vreg) for every value
// live at a write-back, not only for those live at a helper or call.
NEURA_TEST(jit, tier_3_a_deferred_float_live_at_a_write_back_has_a_canonicalizing_home) {
    if (!native_runs()) return;
    for (const auto format : {lift::FloatType::Binary32, lift::FloatType::Binary64}) {
        const bool dbl = format == lift::FloatType::Binary64;
        const unsigned width = dbl ? 64 : 32;
        const t3::ValueType type{dbl ? t3::Type::F64 : t3::Type::F32, static_cast<std::uint8_t>(width)};
        Builder b(3);
        b.f.slot_types.assign(3, type);
        const auto x = b.value(0, t3::Op::EntryLoad, width, 0);
        const auto y = b.value(0, t3::Op::EntryLoad, width, 1);
        b.f.insts[x].type = b.f.insts[y].type = type;
        const auto c = b.value(0, t3::Op::EntryCounter, 64, 0);
        t3::Inst jump;
        jump.op = t3::Op::Jump;
        b.add(0, jump);
        lift::Instruction add;
        add.op = Opcode::FloatAdd;
        add.width = static_cast<std::uint8_t>(width);
        add.format = format;
        t3::Inst sum;
        sum.op = t3::Op::Lift;
        sum.lifted = add;
        sum.type = type;
        sum.operands[0] = x;
        sum.operands[1] = y;
        const auto s = b.add(1, sum);
        const auto c1 = b.value(1, t3::Op::CounterAdd, 64, 1, c);
        const auto commit = b.state(c1, 3);
        b.f.states[commit].slots[2] = {t3::StateSlot::Kind::Value, s};
        b.f.states[commit].flags[2] = {t3::StateFlag::Kind::One, 0};
        t3::Inst back;
        back.op = t3::Op::WriteBack;
        back.flags = t3::Effect;
        back.state = commit;
        b.add(1, back);
        t3::Inst leave;
        leave.op = t3::Op::Leave;
        leave.operands[0] = c1;
        leave.imm = 9;
        b.add(1, leave);
        const nvr::Image image;
        t3::place_canons(image, b.f, false);
        const auto lowered = jit::native::lower(b.f, {0});
        bool marked = false;
        for (const auto format_bits : lowered.canon_vreg) marked = marked || format_bits == width;
        check(marked, "a float live at a write-back has a canonicalizing home (width " + std::to_string(width) + ")");
    }
}

// A float comparison of two slots that a branch reads: fused into ucomis and
// a conditional jump, where an unordered pair (a NaN) is false for every
// comparison but NotEqual. Each branch writes its own value to slot 2.
NEURA_TEST(jit, tier_3_float_comparisons_that_branch_take_the_reference_edge) {
    if (!native_runs()) return;
    Tally tally;
    const std::vector<std::uint64_t> doubles{0, 0x8000000000000000ULL, 0x3FF0000000000000ULL, 0xBFF0000000000000ULL,
                                             0x7FF0000000000000ULL, 0x7FF8000000000000ULL, 0xFFF8000000000001ULL,
                                             0x7FF0000000000001ULL};
    const std::vector<std::uint64_t> singles{0, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0x7FC00000, 0xFFC00001,
                                             0x7F800001};
    for (const auto format : {lift::FloatType::Binary32, lift::FloatType::Binary64}) {
        const bool dbl = format == lift::FloatType::Binary64;
        const unsigned width = dbl ? 64 : 32;
        const t3::ValueType type{dbl ? t3::Type::F64 : t3::Type::F32, static_cast<std::uint8_t>(width)};
        for (const auto op : {Opcode::FloatEqual, Opcode::FloatNotEqual, Opcode::FloatLess, Opcode::FloatLessEqual,
                              Opcode::FloatGreater, Opcode::FloatGreaterEqual}) {
            Builder b(3);
            b.f.slot_types.assign(3, type);
            const auto x = b.value(0, t3::Op::EntryLoad, width, 0);
            const auto y = b.value(0, t3::Op::EntryLoad, width, 1);
            b.f.insts[x].type = b.f.insts[y].type = type;
            const auto c = b.value(0, t3::Op::EntryCounter, 64, 0);
            t3::Inst jump;
            jump.op = t3::Op::Jump;
            b.add(0, jump);
            lift::Instruction compare;
            compare.op = op;
            compare.width = 1;
            compare.format = format;
            t3::Inst condition;
            condition.op = t3::Op::Lift;
            condition.lifted = compare;
            condition.type = {t3::Type::Int, 1};
            condition.operands[0] = x;
            condition.operands[1] = y;
            const auto holds = b.add(1, condition);
            t3::Inst branch;
            branch.op = t3::Op::Branch;
            branch.operands[0] = holds;
            b.add(1, branch);
            b.f.blocks.resize(4);
            b.f.blocks[1].succs = {2, 3};
            for (std::uint32_t k = 2; k < 4; ++k) {
                b.f.blocks[k].preds = {1};
                const auto value = b.value(k, t3::Op::Const, 2, k);
                const auto c1 = b.value(k, t3::Op::CounterAdd, 64, 1, c);
                const auto commit = b.state(c1, 3);
                b.f.states[commit].slots[2] = {t3::StateSlot::Kind::Value, value};
                b.f.states[commit].flags[2] = {t3::StateFlag::Kind::One, 0};
                t3::Inst back;
                back.op = t3::Op::WriteBack;
                back.flags = t3::Effect;
                back.state = commit;
                b.add(k, back);
                t3::Inst leave;
                leave.op = t3::Op::Leave;
                leave.operands[0] = c1;
                leave.imm = 9;
                b.add(k, leave);
            }
            const auto compiled = compile(std::move(b.f));
            check(compiled.program != nullptr, "the function compiles: " + compiled.failure);
            if (!compiled.program) continue;
            const auto& values = dbl ? doubles : singles;
            for (const auto a : values) {
                for (const auto other : values) {
                    bool failed = false;
                    const auto expected = lift::evaluate(compare, a, other, failed) != 0 ? 2U : 3U;
                    const auto fake = run(*compiled.program, {a, other, 0x77});
                    ++tally.cases;
                    if (fake.left_at != 9 || fake.slots[2] != expected) {
                        if (tally.failures == 0) {
                            tally.first = "compare " + std::to_string(static_cast<int>(op)) + " width " +
                                          std::to_string(width) + " a=" + std::to_string(a) + " b=" +
                                          std::to_string(other) + ": branch " + std::to_string(fake.slots[2]);
                        }
                        ++tally.failures;
                    }
                }
            }
        }
    }
    check(tally.failures == 0, std::to_string(tally.failures) + " of " + std::to_string(tally.cases) +
                                   " cases differ; the first: " + tally.first);
}

NEURA_TEST(jit, tier_3_budget_deoptimizes_only_when_the_counter_passes_the_room) {
    if (!native_runs()) return;
    // entry: C; block 1: Budget(P = 5) at block 3; C + 1; WriteBack; Leave.
    Builder b(1);
    const auto c = b.value(0, t3::Op::EntryCounter, 64, 0);
    t3::Inst jump;
    jump.op = t3::Op::Jump;
    b.add(0, jump);
    t3::Inst budget;
    budget.op = t3::Op::Budget;
    budget.operands[0] = c;
    budget.imm = 5;
    budget.flags = t3::Checked;
    budget.state = b.state(c, 3);
    b.add(1, budget);
    const auto c1 = b.value(1, t3::Op::CounterAdd, 64, 1, c);
    t3::Inst back;
    back.op = t3::Op::WriteBack;
    back.state = b.state(c1, 3);
    b.add(1, back);
    t3::Inst leave;
    leave.op = t3::Op::Leave;
    leave.operands[0] = c1;
    leave.imm = 4;
    b.add(1, leave);
    const auto compiled = compile(std::move(b.f));
    check(compiled.program != nullptr, "the budget function compiles: " + compiled.failure);
    for (std::uint64_t room = 0; room <= 7; ++room) {
        const auto fake = run(*compiled.program, {0}, room);
        if (room >= 5) {
            check(fake.left_at == 4 && fake.deopt_state == t3::none,
                  "C + P = 5 within a room of " + std::to_string(room) + " runs on");
        } else {
            check(fake.deopt_state == 0 && fake.deopt_reason == static_cast<std::uint32_t>(t3::Reason::Budget) &&
                      fake.left_at == t3::none,
                  "C + P = 5 beyond a room of " + std::to_string(room) + " deoptimizes for the budget");
        }
    }
}

NEURA_TEST(jit, tier_3_spills_no_value_into_a_slot_that_holds_another) {
    if (!native_runs()) return;
    // entry: v = slot 0, slot 1 (both read), C; block 1:
    //   w = v + 1 (wrapping); slot 0 = w, slot 1 = v (`old = x; x = x + 1`)
    //   C + 1; WriteBack {0: w, 1: v}; h = Helper(v) (clobbers every
    //   caller-saved register: v lives across it in a callee-saved register
    //   or its stack location); r = v + h; C + 1; WriteBack {0: w, 1: v,
    //   2: r}; Leave.
    // v is the entry value of slot 0, so that slot is a candidate home; but
    // slot 0 holds w at the helper, where v is live: v's home can only be
    // slot 1. Six more values live across the helper keep the callee-saved
    // registers busy, so v is spilled.
    static const nvr::Exit exit{};
    Builder b(10);
    b.f.exits.push_back(&exit);
    std::vector<std::uint32_t> loads;
    for (std::uint32_t r = 0; r < 9; ++r) loads.push_back(b.value(0, t3::Op::EntryLoad, 64, r == 1 ? 0 : r));
    const auto v = loads[0];
    const auto c = b.value(0, t3::Op::EntryCounter, 64, 0);
    t3::Inst jump;
    jump.op = t3::Op::Jump;
    b.add(0, jump);
    lift::Instruction add;
    add.op = Opcode::AddWrapping;
    add.width = 64;
    t3::Inst plus;
    plus.op = t3::Op::Lift;
    plus.lifted = add;
    plus.type = {t3::Type::Int, 64};
    plus.operands[0] = v;
    const auto one = b.value(1, t3::Op::Const, 1, 1);
    plus.operands[1] = one;
    const auto w = b.add(1, plus);
    const auto c1 = b.value(1, t3::Op::CounterAdd, 64, 1, c);
    const auto commit = b.state(c1, 2);
    b.f.states[commit].slots[0] = {t3::StateSlot::Kind::Value, w};
    b.f.states[commit].slots[1] = {t3::StateSlot::Kind::Value, v};
    t3::Inst back;
    back.op = t3::Op::WriteBack;
    back.state = commit;
    b.add(1, back);
    t3::Inst helper;
    helper.op = t3::Op::Helper;
    helper.type = {t3::Type::Int, 64};
    helper.operands[0] = c1;
    helper.aux = 0;
    helper.state = commit;
    helper.extra = static_cast<std::uint32_t>(b.f.extras.size());
    helper.count = 1;
    b.f.extras.push_back(v);
    const auto h = b.add(1, helper);
    // Values live across the helper: the other loads, summed after it.
    plus.operands[0] = v;
    plus.operands[1] = h;
    auto sum = b.add(1, plus);
    for (std::uint32_t k = 2; k < loads.size(); ++k) {
        plus.operands[0] = sum;
        plus.operands[1] = loads[k];
        sum = b.add(1, plus);
    }
    const auto c2 = b.value(1, t3::Op::CounterAdd, 64, 1, c1);
    const auto done = b.state(c2, 3);
    b.f.states[done].slots[0] = {t3::StateSlot::Kind::Value, w};
    b.f.states[done].slots[1] = {t3::StateSlot::Kind::Value, v};
    b.f.states[done].slots[9] = {t3::StateSlot::Kind::Value, sum};
    back.state = done;
    b.add(1, back);
    t3::Inst leave;
    leave.op = t3::Op::Leave;
    leave.operands[0] = c2;
    leave.imm = 4;
    b.add(1, leave);
    const auto compiled = compile(std::move(b.f));
    check(compiled.program != nullptr, "the function compiles: " + compiled.failure);
    const std::vector<std::uint64_t> frame{40, 0, 2, 3, 4, 5, 6, 7, 8, 0};
    const auto fake = run(*compiled.program, frame);
    check(fake.helper_slots.size() == frame.size() && fake.helper_slots[0] == 41 && fake.helper_slots[1] == 40,
          "the helper sees slot 0 = x + 1 and slot 1 = the old x");
    check_equal(fake.helper_block, 2U, "the helper sees the block of its state");
    check_equal(fake.helper_counter, std::uint64_t{1}, "the helper sees the block's step");
    check_equal(fake.left_at, 4U, "the code leaves at block 4");
    check_equal(fake.slots[0], std::uint64_t{41}, "slot 0 after the helper");
    check_equal(fake.slots[1], std::uint64_t{40}, "slot 1 after the helper");
    check_equal(fake.slots[9], std::uint64_t{40 + 1040 + 2 + 3 + 4 + 5 + 6 + 7 + 8}, "the sum read v after the helper");
}

NEURA_TEST(jit, tier_3_leaves_no_spilled_value_in_a_slot_a_later_state_calls_unchanged) {
    if (!native_runs()) return;
    // Slot 0 (`a`) holds e at entry. Block 1: v = e + 3 (`a := v`; e is
    // dead from here on, so slot 0 is free for v). Block 2: a check whose
    // state says slot 0 = v, so slot 0 is a home v may have; then 20 values
    // made and summed, while v (used last) is live: v is spilled; then
    // w = v + the sum, and `a := e` again (the program's copy of it, which no
    // state names). Block 3: a check that fails, whose state says slot 0 is
    // unchanged (the frame holds e) and slot 1 = w. v is dead at block 3, so
    // only the slot's later state forbids the home: a spill of v there would
    // leave v's bits in slot 0 at the deoptimization.
    constexpr std::uint32_t fillers = 20;
    Builder b(2 + fillers);
    b.f.blocks.resize(4);
    b.f.blocks[1].succs = {2};
    b.f.blocks[2].preds = {1};
    b.f.blocks[2].succs = {3};
    b.f.blocks[3].preds = {2};
    const auto e = b.value(0, t3::Op::EntryLoad, 64, 0);
    std::vector<std::uint32_t> loads;
    for (std::uint32_t r = 0; r < fillers; ++r) loads.push_back(b.value(0, t3::Op::EntryLoad, 64, 2 + r));
    const auto c = b.value(0, t3::Op::EntryCounter, 64, 0);
    t3::Inst jump;
    jump.op = t3::Op::Jump;
    b.add(0, jump);
    lift::Instruction add;
    add.op = Opcode::AddWrapping;
    add.width = 64;
    t3::Inst plus;
    plus.op = t3::Op::Lift;
    plus.lifted = add;
    plus.type = {t3::Type::Int, 64};
    plus.operands[0] = e;
    plus.operands[1] = b.value(1, t3::Op::Const, 2, 3);
    const auto v = b.add(1, plus);
    const auto c1 = b.value(1, t3::Op::CounterAdd, 64, 1, c);
    b.add(1, jump);
    // Block 2.
    const auto entry2 = b.state(c1, 2);
    b.f.states[entry2].slots[0] = {t3::StateSlot::Kind::Value, v};
    t3::Inst guard;
    guard.op = t3::Op::Guard;
    guard.flags = t3::Checked;
    guard.operands[0] = b.value(2, t3::Op::Const, 1, 1);
    guard.state = entry2;
    b.add(2, guard);
    // Twenty values live at once, each needing a register, none with a
    // frame slot of its own.
    const auto seven = b.value(2, t3::Op::Const, 3, 7);
    std::vector<std::uint32_t> values;
    for (const auto load : loads) {
        plus.operands[0] = load;
        plus.operands[1] = seven;
        values.push_back(b.add(2, plus));
    }
    auto sum = values[0];
    for (std::uint32_t k = 1; k < values.size(); ++k) {
        plus.operands[0] = sum;
        plus.operands[1] = values[k];
        sum = b.add(2, plus);
    }
    plus.operands[0] = v;
    plus.operands[1] = sum;
    const auto w = b.add(2, plus);
    const auto c2 = b.value(2, t3::Op::CounterAdd, 64, 1, c1);
    b.add(2, jump);
    // Block 3.
    const auto entry3 = b.state(c2, 3);
    b.f.states[entry3].slots[1] = {t3::StateSlot::Kind::Value, w};
    guard.operands[0] = b.value(3, t3::Op::Const, 1, 0);
    guard.state = entry3;
    b.add(3, guard);
    t3::Inst leave;
    leave.op = t3::Op::Leave;
    leave.operands[0] = c2;
    leave.imm = 4;
    b.add(3, leave);
    const auto compiled = compile(std::move(b.f));
    check(compiled.program != nullptr, "the function compiles: " + compiled.failure);
    std::vector<std::uint64_t> frame(2 + fillers, 0);
    frame[0] = 100;
    std::uint64_t total = 0;
    for (std::uint32_t r = 0; r < fillers; ++r) {
        frame[2 + r] = 1000 + r;
        total += 1000 + r + 7;
    }
    const auto fake = run(*compiled.program, frame);
    check_equal(fake.deopt_state, entry3, "the run deoptimizes at block 3");
    check_equal(fake.deopt_values.at(w), std::uint64_t{103} + total, "the state's w");
    check_equal(fake.slots[0], std::uint64_t{100}, "slot 0 still holds its entry value");
}

NEURA_TEST(jit, tier_3_code_is_the_same_for_the_same_ir) {
    if (!native_runs()) return;
    lift::Instruction in;
    in.op = Opcode::Multiply;
    in.width = 33;
    in.is_signed = true;
    const auto first = compile(lift_function(in, 64, 64, false, 0));
    const auto second = compile(lift_function(in, 64, 64, false, 0));
    check(first.program != nullptr && second.program != nullptr, "the function compiles");
    check(first.bytes == second.bytes && !first.bytes.empty(), "the same bytes");
}

} // namespace neura::testing
