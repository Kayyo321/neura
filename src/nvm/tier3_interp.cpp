#include "tier3.hpp"

#include "heap.hpp"
#include "lift/evaluate.hpp"

#include <algorithm>
#include <cstring>
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

// Words of the heap at an address, as native code reads and writes them
// (heap.hpp, "Published layout"; host byte order).
std::uint64_t load(std::uint64_t address, unsigned bytes) {
    std::uint64_t value = 0;
    switch (bytes) {
    case 1: { std::uint8_t v = 0; std::memcpy(&v, reinterpret_cast<const void*>(address), 1); value = v; break; }
    case 2: { std::uint16_t v = 0; std::memcpy(&v, reinterpret_cast<const void*>(address), 2); value = v; break; }
    case 4: { std::uint32_t v = 0; std::memcpy(&v, reinterpret_cast<const void*>(address), 4); value = v; break; }
    default: std::memcpy(&value, reinterpret_cast<const void*>(address), 8); break;
    }
    return value;
}

void store(std::uint64_t address, unsigned bytes, std::uint64_t value) {
    switch (bytes) {
    case 1: { const auto v = static_cast<std::uint8_t>(value); std::memcpy(reinterpret_cast<void*>(address), &v, 1); break; }
    case 2: { const auto v = static_cast<std::uint16_t>(value); std::memcpy(reinterpret_cast<void*>(address), &v, 2); break; }
    case 4: { const auto v = static_cast<std::uint32_t>(value); std::memcpy(reinterpret_cast<void*>(address), &v, 4); break; }
    default: std::memcpy(reinterpret_cast<void*>(address), &value, 8); break;
    }
}

// The entry of `handle` through the published handle table, or 0.
std::uint64_t lookup(const Context& context, std::uint64_t handle) {
    const auto* table = static_cast<const Heap::HandleTable*>(context.handle_table);
    const auto page = handle >> Heap::handle_page_bits();
    if (table == nullptr || page >= table->count) return 0;
    const auto address = reinterpret_cast<std::uintptr_t>(table->pages[page]);
    if (address == 0) return 0;
    return load(address + Heap::page_entries_offset() + 8 * (handle & Heap::handle_page_mask()), 8);
}

bool passes(const TypeTest& test, std::uint64_t entry) {
    if (load(entry + Heap::kind_offset(), 1) != test.kind) return false;
    if (test.any_type) return true;
    const auto type = static_cast<std::uint32_t>(load(entry + Heap::type_offset(), 4));
    return std::binary_search(test.types.begin(), test.types.end(), type);
}

// The address of field `field` of an object entry: after its initialized
// words, `words` of them when known, else as many as its count needs.
std::uint64_t field_address(std::uint64_t entry, std::uint64_t field, std::uint32_t words) {
    const auto count = words != none ? words : (load(entry + Heap::count_offset(), 8) + 63) / 64;
    return entry + Heap::initialized_offset() + 8 * (count + field);
}

// The width an element is stored at: the declared width while the storage
// is that, else eight bytes.
unsigned element_bytes(std::uint64_t storage, std::uint64_t declared) {
    return storage == declared ? static_cast<unsigned>(declared) : 8U;
}

} // namespace

bool allocate_inline(Context& context, std::uint32_t layout, std::uint64_t fields, std::uint64_t& handle) {
    if (context.fast_allocation == 0) return false;
    const auto bytes = Heap::object_block_bytes(static_cast<std::size_t>(fields));
    if (bytes > Heap::allocator_largest()) return false;
    auto& words = *static_cast<Heap::NativeAllocator*>(context.allocator);
    if (words.last >= context.max_handles) return false;
    const auto cells = 1 + fields;
    const auto live = *context.live_cells;
    const auto next = *context.next_collection;
    if (live > next || cells > next - live) return false;
    const auto h = words.last + 1;
    const auto* table = static_cast<const Heap::HandleTable*>(context.handle_table);
    const auto page_index = h >> Heap::handle_page_bits();
    if (page_index >= table->count) return false;
    const auto page = reinterpret_cast<std::uintptr_t>(table->pages[page_index]);
    if (page == 0) return false;
    const auto c = Heap::allocator_class_of(bytes);
    const auto size = Heap::allocator_class_size(c);
    auto* block = static_cast<unsigned char*>(words.free[c]);
    if (block == nullptr && static_cast<std::size_t>(words.end[c] - words.next[c]) < size) return false;
    // Every condition holds: what Blocks::allocate, the new object's
    // initialization, store_new() and the executor's count do.
    if (block != nullptr) {
        std::memcpy(&words.free[c], block, sizeof(void*));
    } else {
        block = words.next[c];
        words.next[c] += size;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(block);
    store(address, 8, std::uint64_t{0x08080000} | (std::uint64_t{layout} << 32));
    store(address + Heap::count_offset(), 8, fields);
    store(address + Heap::identity_offset(), 8, 0);
    const auto zeroed = (fields + 63) / 64 + fields;
    if (zeroed != 0) std::memset(block + Heap::initialized_offset(), 0, static_cast<std::size_t>(8 * zeroed));
    store(page + Heap::page_entries_offset() + 8 * (h & Heap::handle_page_mask()), 8, address);
    store(page + Heap::page_live_offset(), 8, load(page + Heap::page_live_offset(), 8) + 1);
    words.last = h;
    *context.live_cells = live + cells;
    if (*context.peak_cells < live + cells) *context.peak_cells = live + cells;
    handle = h;
    return true;
}

bool append_inline(Context& context, std::uint64_t entry, std::uint64_t value) {
    if (context.fast_allocation == 0) return false;
    const auto live = *context.live_cells;
    const auto next = *context.next_collection;
    if (live >= next) return false;
    const auto storage = static_cast<unsigned>(load(entry + Heap::storage_width_offset(), 1));
    if (storage < 8 && (value >> (8 * storage)) != 0) return false;
    const auto count = load(entry + Heap::count_offset(), 8);
    if (count >= load(entry + Heap::list_capacity_offset(), 8)) return false;
    // What append() does without widening or growing, and the count.
    store(load(entry + Heap::data_offset(), 8) + count * storage, storage, value);
    store(entry + Heap::count_offset(), 8, count + 1);
    store(entry + Heap::list_version_offset(), 8, load(entry + Heap::list_version_offset(), 8) + 1);
    *context.live_cells = live + 1;
    if (*context.peak_cells < live + 1) *context.peak_cells = live + 1;
    return true;
}

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

    const auto heap_reason = [](const Inst& in) {
        return (in.flags & ReferenceCheck) != 0 ? Reason::References : Reason::StateTraps;
    };
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
                if (helpers.record != nullptr && in.imm < fn.dispatch_origins.size()) {
                    const auto [program, index] = fn.dispatch_origins[in.imm];
                    helpers.record(&context, fn.programs[program], index, tag);
                }
                next = current.succs.at(exit);
                break;
            }
            case Op::Leave:
                context.C = operand(0);
                helpers.leave(&context, static_cast<std::uint32_t>(in.imm));
                return {Outcome::Leave, 0};
            case Op::Deopt: return deoptimize(in, Reason::Exits);
            case Op::CheckNotNil:
                if (operand(0) == 0) return deoptimize(in, heap_reason(in));
                break;
            case Op::Lookup: {
                const auto handle = operand(0);
                if (handle == 0 && (in.imm & 1U) != 0) {
                    values[id] = 0;
                    break;
                }
                const auto entry = lookup(context, handle);
                if (entry == 0) return deoptimize(in, heap_reason(in));
                values[id] = entry;
                break;
            }
            case Op::CheckKind: {
                const auto entry = operand(0);
                if (entry != 0 && !passes(fn.type_tests[in.imm], entry)) return deoptimize(in, heap_reason(in));
                break;
            }
            case Op::CheckInit: {
                const auto word = load(operand(0) + Heap::initialized_offset() + 8 * (in.imm / 64), 8);
                if (((word >> (in.imm % 64)) & 1U) == 0) return deoptimize(in, heap_reason(in));
                break;
            }
            case Op::CheckIndex:
                if (operand(0) >= operand(1)) return deoptimize(in, heap_reason(in));
                break;
            case Op::CheckGlobalInit:
                if (context.global_initialized[in.imm] == 0) return deoptimize(in, heap_reason(in));
                break;
            case Op::TestKind: {
                const auto entry = operand(0);
                values[id] = entry != 0 && passes(fn.type_tests[in.imm], entry) ? 1 : 0;
                break;
            }
            case Op::LoadTag: values[id] = load(operand(0) + Heap::type_offset(), 4); break;
            case Op::LoadField: values[id] = load(field_address(operand(0), in.imm, in.aux), 8); break;
            case Op::LoadLength: values[id] = load(operand(0) + Heap::length_offset(), 8); break;
            case Op::LoadData: values[id] = load(operand(0) + Heap::data_offset(), 8); break;
            case Op::LoadStorage: values[id] = load(operand(0) + Heap::storage_width_offset(), 1); break;
            case Op::LoadElement: {
                const auto bytes = element_bytes(operand(2), in.imm);
                values[id] = load(operand(0) + operand(1) * bytes, bytes);
                break;
            }
            case Op::LoadVersion: values[id] = load(operand(0) + Heap::list_version_offset(), 8); break;
            case Op::LoadGlobal: values[id] = context.globals[in.imm]; break;
            case Op::Select: {
                const auto index = operand(0);
                if (index >= in.count) throw std::logic_error("tier 3: a select past its values");
                values[id] = values[fn.extras[in.extra + index]];
                break;
            }
            case Op::StoreField: {
                const auto entry = operand(0);
                store(field_address(entry, in.imm, in.aux), 8, operand(1));
                const auto flags = entry + Heap::initialized_offset() + 8 * (in.imm / 64);
                store(flags, 8, load(flags, 8) | (std::uint64_t{1} << (in.imm % 64)));
                break;
            }
            case Op::StoreElement: {
                const auto bytes = element_bytes(operand(2), in.imm);
                store(operand(0) + operand(1) * bytes, bytes, operand(3));
                break;
            }
            case Op::StoreGlobal:
                context.globals[in.imm] = operand(0);
                context.global_initialized[in.imm] = 1;
                break;
            case Op::AllocObject:
            case Op::Append: {
                bool fast = false;
                std::uint64_t result = 0;
                if (in.op == Op::AllocObject) {
                    fast = allocate_inline(context, static_cast<std::uint32_t>(fn.exits[in.aux]->immediate), in.imm, result);
                    context.fast_allocations += fast ? 1 : 0;
                } else {
                    fast = append_inline(context, operand(1), operand(2));
                    context.fast_appends += fast ? 1 : 0;
                }
                if (fast) {
                    if (fn.check_heap) helpers.check_heap(&context);
                    values[id] = result;
                    break;
                }
                gather_arguments();
                context.C = operand(0);
                if (fn.states[in.state].caller != none) {
                    // Inside inlined code: every frame materialized, the
                    // helper, and the activation ends at its resume block.
                    const auto status = helpers.inlined_helper(&context, &fn, in.state, values.data(), fn.exits[in.aux],
                                                               arguments.data(), static_cast<std::uint32_t>(arguments.size()),
                                                               &result);
                    if (status == Status::Trapped) return {Outcome::Trapped, 0};
                    if (status == Status::Exception) return {Outcome::Exception, 0};
                    return {Outcome::Leave, 0};
                }
                // The helper (R3), after the frame is written back.
                helpers.write_back(&context, &fn, in.state, values.data());
                const auto status = helpers.helper(&context, fn.exits[in.aux], arguments.data(),
                                                   static_cast<std::uint32_t>(arguments.size()), &result);
                if (status == Status::Trapped) return {Outcome::Trapped, 0};
                if (status == Status::Exception) return {Outcome::Exception, 0};
                values[id] = result;
                break;
            }
            case Op::Widen:
                if (helpers.set_element(&context, operand(0), operand(1), operand(2)) != Status::Ok) {
                    return {Outcome::Exception, 0};
                }
                break;
            }
        }
        if (next == none) throw std::logic_error("tier 3: a block ended without a terminator");
        previous = block;
        block = next;
    }
}

} // namespace neura::nvm::tier3
