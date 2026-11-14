#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace neura::jit::mir {

// The machine IR of tier 3: x86-64 instructions on virtual registers, as the
// lowering produces them and the register allocator (regalloc.hpp) and its
// checker (mir_check.hpp) read them. An instruction's opcode is opaque here;
// what the allocator needs is in its operands, clobbers and temps.
//
// Execution model of one instruction, which the allocator and the checker
// both assume:
//
// 1. the moves before it run;
// 2. it reads its uses;
// 3. it destroys its clobbered registers and uses its temps;
// 4. it writes its defs;
// 5. its deoptimization stub, if it takes it, reads the state uses: they are
//    read after steps 3 and 4, so a state use never shares a location with
//    a def, a temp or a clobbered register of its instruction;
// 6. the moves after it run.
//
// A def may therefore share a register with a use that is not live after the
// instruction, and the instruction must read every use before it writes a
// def. Moves run only between instructions, so a value an instruction reads
// from a register and that is live after it keeps that register through the
// instruction: in each class, those values, the Register and SameAsUse defs,
// the fixed registers and the temps must fit in the allocatable registers at
// once, or the allocator rejects the function.

enum class RegClass : std::uint8_t { Gpr, Xmm };

// A virtual register. Each is defined exactly once (SSA): by an instruction's
// def or by a phi.
struct VReg {
    std::uint32_t id = 0;
    RegClass cls = RegClass::Gpr;
};

// Physical registers are numbered as in x64.hpp: GPR 0-15 (rax, rcx, rdx,
// rbx, rsp, rbp, rsi, rdi, r8-r15) and XMM 0-15. r11 and xmm15 are the
// scratch registers of move sequences and are never allocatable, nor is
// rsp.
inline constexpr std::uint8_t scratch_gpr = 11;
inline constexpr std::uint8_t scratch_xmm = 15;
inline constexpr std::uint8_t stack_pointer = 4;

enum class Constraint : std::uint8_t {
    // In a register of the value's class (uses and defs).
    Register,
    // In the physical register `fixed` (uses and defs). The register must be
    // allocatable.
    Fixed,
    // In a register or in the value's stack location, so the lowering can use
    // a memory operand (uses and defs). State uses are always Any.
    Any,
    // Defs only: in the register of use number `same_as`, which must be a
    // Register or Any use of the same class (the x86 two-address forms). The
    // instruction reads that use from the def's register, where the allocator
    // copies the use's value before the instruction; when the use's value is
    // not live after the instruction the copy is usually a no-op. Two defs
    // cannot name the same use.
    SameAsUse,
};

struct Operand {
    VReg vreg;
    Constraint c = Constraint::Register;
    std::uint8_t fixed = 0;
    std::uint8_t same_as = 0;
};

struct Inst {
    std::uint32_t opcode = 0;
    std::vector<Operand> uses, defs;
    // The values the frame state at this instruction names, read by its
    // deoptimization stub from wherever they are (all Any). They keep their
    // values alive through the instruction.
    std::vector<Operand> state_uses;
    // Physical registers the instruction destroys (bit n is register n). They
    // are authoritative: a helper call clobbers the caller-saved registers, a
    // native call every register.
    std::uint64_t clobbers_gpr = 0, clobbers_xmm = 0;
    // Scratch registers the instruction needs: distinct from its uses, defs
    // and clobbered registers, and holding nothing live, so the instruction
    // may use them freely from its first to its last machine instruction.
    std::uint8_t temps_gpr = 0, temps_xmm = 0;
    // Not read by the allocator: the clobbers say what a call destroys.
    bool is_call = false;
};

// One input per predecessor, in the order of Block::preds.
struct Phi {
    VReg def;
    std::vector<VReg> inputs;
};

// Blocks are given in their final layout order (reverse postorder, loops
// contiguous, cold blocks last). The entry blocks are the blocks without
// predecessors, and they come first. The allocator rejects a function with a
// critical edge (from a block with several successors to a block with several
// predecessors), an irreducible loop or a block no entry reaches. It finds the
// loops itself, so it reads neither `loop_depth` nor `cold`.
struct Block {
    std::vector<Phi> phis;
    std::vector<Inst> insts;
    std::vector<std::uint32_t> preds, succs;
    std::uint32_t loop_depth = 0;
    bool cold = false;
};

struct Function {
    std::vector<Block> blocks;
    std::uint32_t vreg_count = 0;
    std::vector<RegClass> classes;  // by vreg id
    // By vreg id: the frame slot whose current value the vreg is (its stack
    // location is then that frame slot, design 8.3), or -1 (a native stack
    // slot). The allocator writes a vreg to its frame slot anywhere the vreg
    // is live, so the lowering gives a vreg a home only where such a write is
    // always valid: in particular no frame state at a point where the vreg is
    // live may say that slot is unchanged since entry. Two vregs with the same
    // home whose lifetimes overlap never share the frame slot: the later one
    // gets a native slot.
    std::vector<std::int32_t> spill_home;
    // The allocatable registers (bit n is register n). Never rsp, r11 or
    // xmm15.
    std::uint64_t allocatable_gpr = 0, allocatable_xmm = 0;
};

// Where a value is. A register's file is the class of the value it holds (the
// operand's vreg, or the move's class). Stack slots and frame slots hold 8
// bytes of either class.
enum class LocKind : std::uint8_t {
    Reg,        // physical register `index`
    Stack,      // native stack slot `index` (8 bytes each)
    FrameSlot,  // frame slot `index`: [r15 + 8 * index]
};

struct Location {
    LocKind kind = LocKind::Reg;
    std::uint32_t index = 0;
    friend bool operator==(const Location&, const Location&) = default;
};

// One machine move of a value of class `cls`. At most one end is a stack or
// frame slot; r11 and xmm15 appear only in moves.
struct Move {
    Location from, to;
    RegClass cls = RegClass::Gpr;
    friend bool operator==(const Move&, const Move&) = default;
};

// The allocation of one instruction. A move list is a parallel move already
// put in order: the moves run one after another, as listed.
//
// A Fixed use is in its fixed register, where a move before the instruction
// copies the value (which keeps its own location too). The use a SameAsUse def
// names is in the def's register. A Fixed def is in its fixed register; when
// its value is used later, a move after the instruction takes it to the
// vreg's location. `before` also holds the moves between the parts of a value
// whose location changes at this instruction.
struct InstAllocation {
    std::vector<Location> uses, defs, state_uses;  // as Inst's operands
    std::vector<std::uint8_t> temps_gpr, temps_xmm;  // the temps' registers
    std::vector<Move> before;  // run before the instruction
    std::vector<Move> after;   // run after it
    friend bool operator==(const InstAllocation&, const InstAllocation&) = default;
};

struct BlockAllocation {
    // Every vreg live into the block, and every phi of the block whose value
    // is used, with its location at the block's entry; sorted by vreg id. A
    // phi without a location is never read.
    std::vector<std::pair<std::uint32_t, Location>> entry;
    std::vector<InstAllocation> insts;
    // By successor, as Block::succs: the moves on the edge, phi inputs
    // included. They run after the block's last instruction and its moves
    // (at the end of the block when it has one successor, else at the start of
    // the successor, which then has one predecessor).
    std::vector<std::vector<Move>> edges;
    friend bool operator==(const BlockAllocation&, const BlockAllocation&) = default;
};

struct Allocation {
    std::vector<BlockAllocation> blocks;
    std::uint32_t stack_slots = 0;  // native 8-byte slots, indices 0..stack_slots-1
    friend bool operator==(const Allocation&, const Allocation&) = default;
};

}  // namespace neura::jit::mir
