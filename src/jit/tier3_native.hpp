#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace neura::nvm::tier3 {
struct Function;
}

namespace neura::jit::native {

// The code generator of tier 3 (docs/runtime/NVM.md, "Execution tiers"):
// the IR of a native program (src/nvm/tier3.hpp) lowered to machine IR on
// virtual registers (tier3_lower.cpp), allocated by the linear-scan
// allocator (regalloc.hpp), and emitted with the x86-64 encoder (x64.hpp,
// tier3_emit.cpp). The code does, op for op, what the IR interpreter
// (src/nvm/tier3_interp.cpp) does, through the helpers of
// nvm::tier3::NativeHelpers where that runs reference code.
//
// - Registers: r13 holds the context (nvm::tier3::Context), r12 the block
//   counter C, r15 the slots of the innermost frame (reloaded after every
//   helper and native call), r11 and xmm15 are scratch; rax, rcx, rdx, rbx,
//   rbp, rsi, rdi, r8-r10 and r14 are allocated.
// - Each IR entry's code is a native function: it takes the context in r13,
//   C in r12 and the frame in r15, and returns an nvm::tier3::Outcome in ecx,
//   with a returned value in rax and C in r12. A native call enters the
//   callee's entry 0 this way; any outcome but Return ends the caller too.
// - The trampoline is what the executor calls, as void(Context*, const void*
//   entry): it saves the callee-saved registers, enters the entry, and stores
//   the outcome in the context.
// - The code of an entry into the middle of a loop or at a loop head is a
//   unit of its own (the allocator takes only loops entered at their head),
//   made the first time a run enters there and loaded on its own: most such
//   entries are never entered, as calls between native programs continue in
//   the caller's code.
// - Every check jumps to a stub that stores the values its frame state names
//   into the context's value array and calls the deoptimization helper.
//
// The bytes depend only on the IR: the same function gives the same code.

// The entry offset of an entry whose code another generate_entry makes.
inline constexpr std::uint32_t lazy = 0xFFFFFFFFU;

struct Code {
    std::vector<std::uint8_t> bytes;
    // The offset of each IR entry's code (Function::entries order), or
    // `lazy`, and of the trampoline.
    std::vector<std::uint32_t> entries;
    std::uint32_t trampoline = 0;
    // The stack frame of one activation, in bytes.
    std::uint32_t frame_bytes = 0;
};

struct Generated {
    Code code;
    // Why there is no code: the lowering refused the IR, the allocator
    // rejected it, the code is over 1 MiB, or the frame over 2 KiB.
    std::string failure;
};

inline constexpr std::size_t max_code_bytes = std::size_t{1} << 20;
inline constexpr std::uint32_t max_frame_bytes = 2048;

// Whether this build generates tier-3 code: x86-64 Linux.
bool supported();

// The code of `function`'s entry 0 and of every entry that shares its code
// (tier3_lower.hpp, entry_units), and the trampoline; every other entry is
// `lazy`: its code, which is the code its entry reaches, is made by
// generate_entry when a run first enters there. With `verify`, the register
// allocation is checked (mir::check), and a failure aborts the process with
// the checker's report.
Generated generate(const nvm::tier3::Function& function, bool verify);

// The code of a `lazy` entry of `function`, at offset `entries[entry]`; no
// trampoline (the one generate made enters it).
Generated generate_entry(const nvm::tier3::Function& function, std::uint32_t entry, bool verify);

} // namespace neura::jit::native
