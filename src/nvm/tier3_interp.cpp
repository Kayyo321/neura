#include "tier3.hpp"

#include "lift/evaluate.hpp"

#include <algorithm>
#include <stdexcept>

namespace neura::nvm::tier3 {
namespace {

// One native activation: the depth it holds in the context, and the program
// the context names while it runs.
struct Activation {
    Context& context;
    const tier2::Region* region;
    Activation(Context& c, const tier2::Region* running) : context(c), region(c.region) {
        ++context.native_depth;
        context.region = running;
    }
    ~Activation() {
        --context.native_depth;
        context.region = region;
    }
    Activation(const Activation&) = delete;
    Activation& operator=(const Activation&) = delete;
};

} // namespace

// The IR interpreter (docs/runtime/NVM.md, "Execution tiers"). It does for
// each op what native code does, through the same helpers: values are in an
// array, the frame is written only by WriteBack, Return and the endings.
// `values` is one array per native depth, so a nested activation never moves
// its caller's.
Result interpret(Context& context, const Program& program, std::uint32_t entry_index, bool from_native) {
    const auto& fn = program.ir;
    const auto& helpers = *context.helpers;
    if (context.pool.empty()) context.pool.resize(max_native_depth + 2);
    const Activation activation(context, fn.region);
    auto& values = context.pool[context.native_depth - 1];
    values.assign(fn.insts.size(), 0);
    std::uint64_t* slots = nullptr;
    std::uint8_t* initialized = nullptr;
    helpers.frame(&context, &slots, &initialized);

    const auto deoptimize = [&](const Inst& in, Reason reason) {
        helpers.deoptimize(&context, &fn, in.state, reason, values.data());
        return Result{Outcome::Deopt, 0};
    };
    std::vector<std::uint64_t> arguments;
    std::vector<std::uint64_t> carried;
    std::uint32_t block = fn.entries.at(entry_index).ir_block;
    std::uint32_t previous = none;
    for (;;) {
        const auto& current = fn.blocks[block];
        // Phis read the values of the edge taken, all before any is written.
        std::size_t first = 0;
        if (previous != none) {
            const auto edge = static_cast<std::size_t>(
                std::find(current.preds.begin(), current.preds.end(), previous) - current.preds.begin());
            carried.clear();
            while (first < current.insts.size() && fn.insts[current.insts[first]].op == Op::Phi) {
                const auto& phi = fn.insts[current.insts[first]];
                carried.push_back(values[fn.extras[phi.extra + edge]]);
                ++first;
            }
            for (std::size_t i = 0; i < carried.size(); ++i) values[current.insts[i]] = carried[i];
        }
        std::uint32_t next = none;
        for (std::size_t k = first; k < current.insts.size(); ++k) {
            const auto id = current.insts[k];
            const auto& in = fn.insts[id];
            const auto operand = [&](unsigned i) { return in.operands[i] == none ? std::uint64_t{0} : values[in.operands[i]]; };
            const auto gather_arguments = [&] {
                arguments.clear();
                for (std::uint32_t i = 0; i < in.count; ++i) arguments.push_back(values[fn.extras[in.extra + i]]);
            };
            switch (in.op) {
            case Op::Const: values[id] = in.imm; break;
            case Op::RunConst:
                values[id] = helpers.run_constant(&context, static_cast<std::uint32_t>(in.imm)) &
                             lift::mask_of(in.type.width);
                break;
            case Op::EntryLoad: values[id] = slots[in.imm]; break;
            case Op::EntryFlag: values[id] = initialized[in.imm]; break;
            case Op::EntryCounter: values[id] = in.imm == 0 ? context.C : context.K; break;
            case Op::Phi: break;
            case Op::Lift: {
                bool failed = false;
                const auto result = lift::evaluate(in.lifted, operand(0), operand(1), failed);
                if (failed && (in.flags & Checked) != 0) return deoptimize(in, Reason::Checks);
                values[id] = result;
                break;
            }
            case Op::Mask: values[id] = operand(0) & lift::mask_of(static_cast<unsigned>(in.imm)); break;
            case Op::Gather: {
                std::uint64_t result = 0;
                for (std::uint32_t i = 0; i < in.imm; ++i) {
                    const auto source = values[fn.extras[in.extra + 2 * i]];
                    result |= ((source >> fn.extras[in.extra + 2 * i + 1]) & 1U) << i;
                }
                values[id] = result;
                break;
            }
            case Op::CounterAdd: values[id] = operand(0) + in.imm; break;
            case Op::Budget:
                if (operand(0) + in.imm > context.room) return deoptimize(in, Reason::Budget);
                break;
            case Op::SwitchCheck:
                if (operand(0) >= in.imm) return deoptimize(in, Reason::Selectors);
                break;
            case Op::Guard:
                if ((operand(0) & 1U) == 0) return deoptimize(in, Reason::Guards);
                break;
            case Op::StateCall: {
                if ((in.flags & CheckReference) != 0) {
                    const auto status =
                        helpers.check_reference(&context, static_cast<std::uint32_t>(in.imm), operand(0));
                    if (status == Status::Fault) return deoptimize(in, Reason::References);
                    if (status == Status::Exception) return {Outcome::Exception, 0};
                    break;
                }
                gather_arguments();
                std::uint64_t result = 0;
                const auto status = helpers.state(&context, fn.exits[in.aux], arguments.data(),
                                                  static_cast<std::uint32_t>(arguments.size()), &result);
                if (status == Status::Fault) return deoptimize(in, Reason::StateTraps);
                if (status == Status::Exception) return {Outcome::Exception, 0};
                values[id] = result;
                break;
            }
            case Op::WriteBack: helpers.write_back(&context, &fn, in.state, values.data()); break;
            case Op::Helper: {
                gather_arguments();
                context.C = operand(0);
                std::uint64_t result = 0;
                const auto status = helpers.helper(&context, fn.exits[in.aux], arguments.data(),
                                                   static_cast<std::uint32_t>(arguments.size()), &result);
                if (status == Status::Trapped) return {Outcome::Trapped, 0};
                if (status == Status::Exception) return {Outcome::Exception, 0};
                values[id] = result;
                break;
            }
            case Op::Call: {
                gather_arguments();
                context.C = operand(0);
                const auto callee = static_cast<std::uint32_t>(in.imm);
                const auto status = helpers.call(&context, fn.exits[in.aux], callee, arguments.data(),
                                                 static_cast<std::uint32_t>(arguments.size()), fn.programs[0]);
                if (status == Status::Trapped) return {Outcome::Trapped, 0};
                const auto* code = helpers.callee_code(&context, callee);
                if (!code || context.native_depth >= max_native_depth) {
                    // The callee's frame is pushed and the call has completed:
                    // the dispatcher runs the callee from its first block.
                    helpers.leave(&context, 0);
                    return {Outcome::Leave, 0};
                }
                const auto result = interpret(context, *code, 0, true);
                // Any ending but a return ends this activation too, touching
                // nothing: every frame is canonical.
                if (result.outcome != Outcome::Return) return result;
                values[id] = result.value;
                break;
            }
            case Op::Return: {
                const bool has_value = in.operands[1] != none;
                const auto value = has_value ? values[in.operands[1]] : 0;
                if (helpers.ret(&context, value, has_value, operand(0)) == Status::Fault) {
                    return deoptimize(in, Reason::Exits);
                }
                if (from_native) return {Outcome::Return, value};
                return {Outcome::Leave, 0};
            }
            case Op::Jump: next = current.succs[0]; break;
            case Op::Branch: next = (operand(0) & 1U) != 0 ? current.succs[0] : current.succs[1]; break;
            case Op::Switch: {
                const auto index = operand(0);
                if (index >= current.succs.size()) throw std::logic_error("tier 3: a switch selects past its exits");
                next = current.succs[index];
                break;
            }
            case Op::Dispatch: {
                const auto& table = fn.dispatch_tables[in.imm];
                const auto tag = operand(0);
                const auto found = std::lower_bound(table.exits.begin(), table.exits.end(), tag,
                                                    [](const auto& entry, std::uint64_t key) { return entry.first < key; });
                const auto exit = found != table.exits.end() && found->first == tag ? found->second : table.fallback;
                next = current.succs.at(exit);
                break;
            }
            case Op::Leave:
                context.C = operand(0);
                helpers.leave(&context, static_cast<std::uint32_t>(in.imm));
                return {Outcome::Leave, 0};
            case Op::Deopt: return deoptimize(in, Reason::Exits);
            }
        }
        if (next == none) throw std::logic_error("tier 3: a block ended without a terminator");
        previous = block;
        block = next;
    }
}

} // namespace neura::nvm::tier3
