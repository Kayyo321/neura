#include "jit/regalloc.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace neura::jit::mir {
namespace {

// Positions. Block b owns [start(b), start(b + 1)): its first eight positions
// are its entry, where its phis are defined and its live-in values begin, and
// each instruction j owns the eight positions from p = start(b) + 8 + 8j:
//
//   p      the gap before the instruction: moves, and every split
//   p + 2  the uses are read
//   p + 3  the clobbered registers are destroyed and the temps used
//   p + 4  the defs are written
//   p + 6  the gap after the instruction: moves out of fixed def registers
//   p + 7  where the moves of the next gap read their sources
//
// Every split position is a multiple of 8: a block's entry or a gap before an
// instruction. Ranges are half-open, so a value whose range ends at p + 3 is
// read at p + 2 and leaves its register free for a def at p + 4.
using Pos = std::uint32_t;
constexpr Pos kNever = std::numeric_limits<Pos>::max();
constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
constexpr Pos kStride = 8;
constexpr Pos kRead = 2, kClobber = 3, kWrite = 4, kAfter = 6;

constexpr Pos gap_at_or_before(Pos pos) { return pos & ~(kStride - 1); }

std::uint64_t allocatable(const Function& f, RegClass cls) {
    return cls == RegClass::Gpr ? f.allocatable_gpr : f.allocatable_xmm;
}

std::uint8_t scratch(RegClass cls) { return cls == RegClass::Gpr ? scratch_gpr : scratch_xmm; }

bool is_memory(const Location& location) { return location.kind != LocKind::Reg; }

// Whether two (location, class) pairs name the same place: registers of
// different classes are different places, slots are not.
bool same_place(const Location& a, RegClass a_cls, const Location& b, RegClass b_cls) {
    if (a.kind != b.kind || a.index != b.index) return false;
    return a.kind != LocKind::Reg || a_cls == b_cls;
}

struct Range {
    Pos from;
    Pos to;
};

// The first range of `ranges` that ends after `pos`.
std::size_t first_after(const std::vector<Range>& ranges, Pos pos) {
    return static_cast<std::size_t>(std::partition_point(ranges.begin(), ranges.end(), [pos](const Range& r) {
                                        return r.to <= pos;
                                    }) -
                                    ranges.begin());
}

bool covers(const std::vector<Range>& ranges, Pos pos) {
    const auto i = first_after(ranges, pos);
    return i < ranges.size() && ranges[i].from <= pos;
}

// The first position at or after `from` covered by both, looking at a from
// range i and at b from range j on.
Pos intersection(const std::vector<Range>& a, std::size_t i, const std::vector<Range>& b, std::size_t j, Pos from) {
    while (i < a.size() && j < b.size()) {
        const Pos lo = std::max({a[i].from, b[j].from, from});
        const Pos hi = std::min(a[i].to, b[j].to);
        if (lo < hi) return lo;
        if (a[i].to <= b[j].to) {
            ++i;
        } else {
            ++j;
        }
    }
    return kNever;
}

Pos intersection(const std::vector<Range>& a, const std::vector<Range>& b, Pos from) {
    return intersection(a, first_after(a, from), b, first_after(b, from), from);
}

struct Interval {
    std::uint32_t vreg = kNone;  // kNone: a temp
    RegClass cls = RegClass::Gpr;
    std::vector<Range> ranges;   // sorted, disjoint, never adjacent
    std::vector<Pos> uses;       // every use position, sorted
    std::vector<Pos> reg_uses;   // the use positions that need a register, sorted
    int reg = -1;
    bool spilled = false;        // in the vreg's stack location
    std::size_t cursor = 0;      // the scan's first range that may cover its position

    Pos start() const { return ranges.front().from; }
    Pos end() const { return ranges.back().to; }
};

// A register the allocator would like an interval to get. It applies to the
// part of the vreg's interval that covers `at`: either the register `reg` or
// the register `partner` has at `partner_at`.
struct Hint {
    Pos at = 0;
    std::uint8_t reg = 0;
    std::uint32_t partner = kNone;
    Pos partner_at = 0;
    bool only_if_ends = false;  // only when that part ends at at + 1
};

struct UseCopy {
    std::uint32_t vreg;
    std::uint8_t reg;
};

struct InstInfo {
    Pos base = 0;
    std::vector<UseCopy> copies;       // fixed uses: the value copied to its register
    std::vector<int> same_as_def;      // by use: the SameAsUse def that names it, or -1
    std::vector<std::uint32_t> temps_gpr, temps_xmm;  // temp intervals
};

class Allocator {
public:
    explicit Allocator(const Function& f) : f_(f) {}

    Allocation run() {
        validate();
        number();
        analyze_cfg();
        check_ssa();
        liveness();
        build_intervals();
        scan(RegClass::Gpr);
        scan(RegClass::Xmm);
        assign_stack();
        return resolve();
    }

private:
    using Set = std::vector<std::uint32_t>;  // sorted vreg ids

    const Function& f_;
    std::uint32_t n_ = 0;
    std::uint32_t entry_count_ = 0;
    std::vector<Pos> start_;  // by block, and the end of the last block
    std::vector<std::vector<std::uint32_t>> pred_index_;  // [b][k]: b's index in succs[k]'s preds
    std::vector<std::uint32_t> def_block_;
    std::vector<std::int64_t> def_inst_;  // -1: a phi

    // Control flow.
    std::vector<std::uint32_t> postorder_;
    std::vector<std::vector<bool>> back_;  // [b][k]: b -> succs[k] is a back edge
    std::vector<std::uint32_t> idom_;
    std::vector<std::uint32_t> rpo_number_;
    std::vector<std::uint32_t> dom_pre_, dom_post_;
    struct Loop {
        std::uint32_t header;
        std::vector<std::uint32_t> body;
    };
    std::vector<Loop> loops_;  // headers in reverse postorder: outer loops first
    std::vector<std::uint32_t> depth_;
    std::vector<std::vector<std::uint32_t>> min_depth_;  // sparse table over blocks

    // Liveness.
    std::vector<Set> live_in_, live_out_;

    // Intervals.
    std::vector<Interval> intervals_;
    std::vector<std::vector<std::uint32_t>> parts_;  // by vreg: its intervals
    std::vector<std::vector<Hint>> hints_;
    std::vector<std::vector<InstInfo>> info_;
    std::array<std::array<std::vector<Range>, 16>, 2> fixed_;
    std::vector<std::vector<Range>> life_;  // by vreg: its ranges before any split

    // The scan.
    std::vector<std::uint8_t> regs_;
    RegClass cls_ = RegClass::Gpr;
    using Item = std::pair<Pos, std::uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> unhandled_;
    std::vector<std::uint32_t> active_;  // at most one per register
    // By register: the intervals with that register and a hole at the scan's
    // position, by the start of their next range.
    std::array<std::set<std::pair<Pos, std::uint32_t>>, 16> inactive_;
    std::array<std::size_t, 16> fixed_cursor_{};  // the first fixed range of each register that may matter

    // Stack locations.
    std::vector<Location> stack_loc_;
    std::vector<bool> has_stack_;
    std::uint32_t stack_slots_ = 0;

    // ---- errors -----------------------------------------------------------

    [[noreturn]] static void reject(const std::string& why) { throw AllocationError(why); }

    static std::string at(std::uint32_t block, std::size_t inst) {
        return "block " + std::to_string(block) + ", instruction " + std::to_string(inst) + ": ";
    }

    std::uint32_t block_containing(Pos pos) const {
        const auto it = std::upper_bound(start_.begin(), start_.begin() + n_, pos);
        return static_cast<std::uint32_t>(it - start_.begin()) - 1;
    }

    std::string where(Pos pos) const {
        const auto b = block_containing(pos);
        const Pos offset = pos - start_[b];
        if (offset < kStride) return "block " + std::to_string(b) + " entry: ";
        return at(b, offset / kStride - 1);
    }

    // ---- validation -------------------------------------------------------

    void check_vreg(const VReg& v, const std::string& context) const {
        if (v.id >= f_.vreg_count) reject(context + "vreg " + std::to_string(v.id) + " out of range");
        if (v.cls != f_.classes[v.id]) reject(context + "vreg " + std::to_string(v.id) + " with the wrong class");
    }

    void check_fixed(const Operand& op, const std::string& context) const {
        if (op.fixed >= 16 || ((allocatable(f_, op.vreg.cls) >> op.fixed) & 1U) == 0) {
            reject(context + "a fixed register that is not allocatable");
        }
    }

    void define(std::uint32_t v, std::uint32_t block, std::int64_t inst, const std::string& context) {
        if (def_block_[v] != kNone) reject(context + "vreg " + std::to_string(v) + " defined twice");
        def_block_[v] = block;
        def_inst_[v] = inst;
    }

    void validate() {
        const auto& blocks = f_.blocks;
        if (blocks.empty()) reject("a function without blocks");
        if (blocks.size() >= kNone) reject("too many blocks");
        n_ = static_cast<std::uint32_t>(blocks.size());
        if (f_.classes.size() != f_.vreg_count || f_.spill_home.size() != f_.vreg_count) {
            reject("classes and spill homes must have one entry per vreg");
        }
        if ((f_.allocatable_gpr >> 16) != 0 || (f_.allocatable_xmm >> 16) != 0) {
            reject("an allocatable register above 15");
        }
        if ((f_.allocatable_gpr & ((1U << stack_pointer) | (1U << scratch_gpr))) != 0) {
            reject("rsp or r11 is allocatable");
        }
        if ((f_.allocatable_xmm & (1U << scratch_xmm)) != 0) reject("xmm15 is allocatable");
        for (const auto home : f_.spill_home) {
            if (home < -1) reject("a spill home below -1");
        }

        while (entry_count_ < n_ && blocks[entry_count_].preds.empty()) ++entry_count_;
        if (entry_count_ == 0) reject("block 0 has predecessors: no entry block");
        for (std::uint32_t b = entry_count_; b < n_; ++b) {
            if (blocks[b].preds.empty()) reject("block " + std::to_string(b) + " is an entry after a non-entry block");
        }

        pred_index_.assign(n_, {});
        for (std::uint32_t b = 0; b < n_; ++b) {
            const auto& block = blocks[b];
            const std::string context = "block " + std::to_string(b) + ": ";
            for (std::size_t k = 0; k < block.succs.size(); ++k) {
                const auto s = block.succs[k];
                if (s >= n_) reject(context + "a successor out of range");
                if (std::count(block.succs.begin(), block.succs.end(), s) != 1) {
                    reject(context + "a repeated successor");
                }
                const auto& preds = blocks[s].preds;
                const auto it = std::find(preds.begin(), preds.end(), b);
                if (it == preds.end()) reject(context + "a successor that does not list it as a predecessor");
                pred_index_[b].push_back(static_cast<std::uint32_t>(it - preds.begin()));
                if (block.succs.size() > 1 && preds.size() > 1) {
                    reject(context + "a critical edge to block " + std::to_string(s));
                }
            }
            for (const auto p : block.preds) {
                if (p >= n_) reject(context + "a predecessor out of range");
                if (std::count(block.preds.begin(), block.preds.end(), p) != 1) {
                    reject(context + "a repeated predecessor");
                }
                const auto& succs = blocks[p].succs;
                if (std::find(succs.begin(), succs.end(), b) == succs.end()) {
                    reject(context + "a predecessor that does not list it as a successor");
                }
            }
        }

        def_block_.assign(f_.vreg_count, kNone);
        def_inst_.assign(f_.vreg_count, -1);
        for (std::uint32_t b = 0; b < n_; ++b) {
            const auto& block = blocks[b];
            const std::string context = "block " + std::to_string(b) + ": ";
            for (const auto& phi : block.phis) {
                check_vreg(phi.def, context);
                if (phi.inputs.size() != block.preds.size()) {
                    reject(context + "a phi without one input per predecessor");
                }
                for (const auto& input : phi.inputs) {
                    check_vreg(input, context);
                    if (input.cls != phi.def.cls) reject(context + "a phi input of another class");
                }
                define(phi.def.id, b, -1, context);
            }
            for (std::size_t j = 0; j < block.insts.size(); ++j) {
                const auto& inst = block.insts[j];
                const std::string where_inst = at(b, j);
                if ((inst.clobbers_gpr >> 16) != 0 || (inst.clobbers_xmm >> 16) != 0) {
                    reject(where_inst + "a clobbered register above 15");
                }
                for (std::size_t k = 0; k < inst.uses.size(); ++k) {
                    const auto& use = inst.uses[k];
                    check_vreg(use.vreg, where_inst);
                    if (use.c == Constraint::SameAsUse) reject(where_inst + "a use with SameAsUse");
                    if (use.c != Constraint::Fixed) continue;
                    check_fixed(use, where_inst);
                    for (std::size_t m = 0; m < k; ++m) {
                        const auto& other = inst.uses[m];
                        if (other.c == Constraint::Fixed && other.fixed == use.fixed &&
                            other.vreg.cls == use.vreg.cls && other.vreg.id != use.vreg.id) {
                            reject(where_inst + "two values fixed to one register");
                        }
                    }
                }
                for (const auto& use : inst.state_uses) {
                    check_vreg(use.vreg, where_inst);
                    if (use.c != Constraint::Any) reject(where_inst + "a state use that is not Any");
                }
                std::vector<bool> named(inst.uses.size(), false);
                for (std::size_t k = 0; k < inst.defs.size(); ++k) {
                    const auto& def = inst.defs[k];
                    check_vreg(def.vreg, where_inst);
                    if (def.c == Constraint::Fixed) {
                        check_fixed(def, where_inst);
                        for (std::size_t m = 0; m < k; ++m) {
                            const auto& other = inst.defs[m];
                            if (other.c == Constraint::Fixed && other.fixed == def.fixed &&
                                other.vreg.cls == def.vreg.cls) {
                                reject(where_inst + "two defs fixed to one register");
                            }
                        }
                    }
                    if (def.c == Constraint::SameAsUse) {
                        if (def.same_as >= inst.uses.size()) reject(where_inst + "SameAsUse names no use");
                        const auto& use = inst.uses[def.same_as];
                        if (use.vreg.cls != def.vreg.cls) reject(where_inst + "SameAsUse names a use of another class");
                        if (use.c != Constraint::Register && use.c != Constraint::Any) {
                            reject(where_inst + "SameAsUse names a use that is not Register or Any");
                        }
                        if (named[def.same_as]) reject(where_inst + "two defs name the same use");
                        named[def.same_as] = true;
                    }
                    define(def.vreg.id, b, static_cast<std::int64_t>(j), where_inst);
                }
            }
        }
    }

    void number() {
        start_.assign(n_ + 1, 0);
        std::uint64_t pos = 0;
        for (std::uint32_t b = 0; b < n_; ++b) {
            start_[b] = static_cast<Pos>(pos);
            pos += kStride * (1 + static_cast<std::uint64_t>(f_.blocks[b].insts.size()));
            if (pos >= (std::uint64_t{1} << 31)) reject("a function too large to number");
        }
        start_[n_] = static_cast<Pos>(pos);
    }

    Pos base(std::uint32_t block, std::size_t inst) const {
        return start_[block] + kStride + kStride * static_cast<Pos>(inst);
    }

    // ---- control flow -----------------------------------------------------

    bool dominates(std::uint32_t a, std::uint32_t b) const {
        return dom_pre_[a] <= dom_pre_[b] && dom_post_[b] <= dom_post_[a];
    }

    void analyze_cfg() {
        const auto& blocks = f_.blocks;
        std::vector<std::uint8_t> state(n_, 0);  // 0 new, 1 on the stack, 2 done
        back_.assign(n_, {});
        for (std::uint32_t b = 0; b < n_; ++b) back_[b].assign(blocks[b].succs.size(), false);
        std::vector<std::pair<std::uint32_t, std::size_t>> stack;
        for (std::uint32_t e = 0; e < entry_count_; ++e) {
            state[e] = 1;
            stack.push_back({e, 0});
            while (!stack.empty()) {
                auto& [b, k] = stack.back();
                if (k < blocks[b].succs.size()) {
                    const auto index = k++;
                    const auto s = blocks[b].succs[index];
                    if (state[s] == 0) {
                        state[s] = 1;
                        stack.push_back({s, 0});
                    } else if (state[s] == 1) {
                        back_[b][index] = true;
                    }
                } else {
                    state[b] = 2;
                    postorder_.push_back(b);
                    stack.pop_back();
                }
            }
        }
        for (std::uint32_t b = 0; b < n_; ++b) {
            if (state[b] != 2) reject("block " + std::to_string(b) + " is not reachable from an entry");
        }

        // Dominators (Cooper, Harvey and Kennedy) under a virtual root n_
        // whose children are the entries; numbers in reverse postorder, the
        // root first.
        rpo_number_.assign(n_ + 1, 0);
        for (std::size_t i = 0; i < postorder_.size(); ++i) {
            rpo_number_[postorder_[i]] = static_cast<std::uint32_t>(postorder_.size() - i);
        }
        rpo_number_[n_] = 0;
        idom_.assign(n_ + 1, kNone);
        idom_[n_] = n_;
        for (std::uint32_t e = 0; e < entry_count_; ++e) idom_[e] = n_;
        const auto intersect = [&](std::uint32_t a, std::uint32_t b) {
            while (a != b) {
                while (rpo_number_[a] > rpo_number_[b]) a = idom_[a];
                while (rpo_number_[b] > rpo_number_[a]) b = idom_[b];
            }
            return a;
        };
        for (bool changed = true; changed;) {
            changed = false;
            for (auto it = postorder_.rbegin(); it != postorder_.rend(); ++it) {
                const auto b = *it;
                if (b < entry_count_) continue;
                std::uint32_t next = kNone;
                for (const auto p : blocks[b].preds) {
                    if (idom_[p] == kNone) continue;
                    next = next == kNone ? p : intersect(p, next);
                }
                if (next != idom_[b]) {
                    idom_[b] = next;
                    changed = true;
                }
            }
        }
        std::vector<std::vector<std::uint32_t>> children(n_ + 1);
        for (std::uint32_t b = 0; b < n_; ++b) children[idom_[b]].push_back(b);
        dom_pre_.assign(n_ + 1, 0);
        dom_post_.assign(n_ + 1, 0);
        std::uint32_t counter = 0;
        std::vector<std::pair<std::uint32_t, std::size_t>> walk{{n_, 0}};
        dom_pre_[n_] = counter++;
        while (!walk.empty()) {
            auto& [b, k] = walk.back();
            if (k < children[b].size()) {
                const auto c = children[b][k++];
                dom_pre_[c] = counter++;
                walk.push_back({c, 0});
            } else {
                dom_post_[b] = counter++;
                walk.pop_back();
            }
        }

        // Every retreating edge must go to a block that dominates its source.
        for (std::uint32_t b = 0; b < n_; ++b) {
            for (std::size_t k = 0; k < blocks[b].succs.size(); ++k) {
                if (back_[b][k] && !dominates(blocks[b].succs[k], b)) {
                    reject("block " + std::to_string(b) + ": an irreducible loop through block " +
                           std::to_string(blocks[b].succs[k]));
                }
            }
        }

        // Natural loops, headers in reverse postorder (outer loops first).
        std::vector<std::vector<std::uint32_t>> latches(n_);
        for (std::uint32_t b = 0; b < n_; ++b) {
            for (std::size_t k = 0; k < blocks[b].succs.size(); ++k) {
                if (back_[b][k]) latches[blocks[b].succs[k]].push_back(b);
            }
        }
        depth_.assign(n_, 0);
        std::vector<std::uint32_t> mark(n_, kNone);
        for (auto it = postorder_.rbegin(); it != postorder_.rend(); ++it) {
            const auto h = *it;
            if (latches[h].empty()) continue;
            Loop loop{h, {h}};
            mark[h] = h;
            std::vector<std::uint32_t> work = latches[h];
            while (!work.empty()) {
                const auto x = work.back();
                work.pop_back();
                if (mark[x] == h) continue;
                mark[x] = h;
                loop.body.push_back(x);
                for (const auto p : blocks[x].preds) {
                    if (mark[p] != h) work.push_back(p);
                }
            }
            std::sort(loop.body.begin(), loop.body.end());
            for (const auto x : loop.body) ++depth_[x];
            loops_.push_back(std::move(loop));
        }

        // A sparse table over the layout for the shallowest block of a range
        // (the later one on a tie): where spill code goes.
        min_depth_.assign(1, std::vector<std::uint32_t>(n_));
        for (std::uint32_t b = 0; b < n_; ++b) min_depth_[0][b] = b;
        for (std::size_t level = 1; (std::size_t{1} << level) <= n_; ++level) {
            const auto& prev = min_depth_[level - 1];
            std::vector<std::uint32_t> row(n_ - (std::size_t{1} << level) + 1);
            for (std::size_t i = 0; i < row.size(); ++i) {
                row[i] = shallower(prev[i], prev[i + (std::size_t{1} << (level - 1))]);
            }
            min_depth_.push_back(std::move(row));
        }
    }

    std::uint32_t shallower(std::uint32_t a, std::uint32_t b) const {
        if (depth_[a] != depth_[b]) return depth_[a] < depth_[b] ? a : b;
        return std::max(a, b);
    }

    std::uint32_t shallowest(std::uint32_t lo, std::uint32_t hi) const {
        std::size_t level = 0;
        while ((std::size_t{2} << level) <= hi - lo + 1) ++level;
        return shallower(min_depth_[level][lo], min_depth_[level][hi + 1 - (std::size_t{1} << level)]);
    }

    void check_ssa() const {
        const auto& blocks = f_.blocks;
        const auto strictly_dominates = [&](std::uint32_t a, std::uint32_t b) { return a != b && dominates(a, b); };
        for (std::uint32_t b = 0; b < n_; ++b) {
            const auto& block = blocks[b];
            for (const auto& phi : block.phis) {
                for (std::size_t k = 0; k < phi.inputs.size(); ++k) {
                    const auto v = phi.inputs[k].id;
                    const auto p = block.preds[k];
                    if (def_block_[v] == kNone) reject("block " + std::to_string(b) + ": a phi input never defined");
                    if (def_block_[v] != p && !dominates(def_block_[v], p)) {
                        reject("block " + std::to_string(b) +
                               ": a phi input whose definition does not dominate its edge");
                    }
                }
            }
            for (std::size_t j = 0; j < block.insts.size(); ++j) {
                const auto& inst = block.insts[j];
                const auto check = [&](const Operand& op) {
                    const auto v = op.vreg.id;
                    if (def_block_[v] == kNone) {
                        reject(at(b, j) + "vreg " + std::to_string(v) + " used but never defined");
                    }
                    const bool ok = def_block_[v] == b ? def_inst_[v] < static_cast<std::int64_t>(j)
                                                       : strictly_dominates(def_block_[v], b);
                    if (!ok) {
                        reject(at(b, j) + "vreg " + std::to_string(v) + " used where its definition does not dominate");
                    }
                };
                for (const auto& use : inst.uses) check(use);
                for (const auto& use : inst.state_uses) check(use);
            }
        }
    }

    // ---- liveness ---------------------------------------------------------

    // Liveness on the loop nesting forest (Boissinot et al., "Computing
    // Liveness Sets for SSA-Form Programs", 2011), exact for reducible
    // control flow: a backward pass in postorder over the edges that are not
    // back edges, then every value live into a loop header is live into and
    // out of every block of the loop. Live-in sets leave out the block's own
    // phis; a phi input is live out of its predecessor. Each block's sets are
    // sorted vectors, built in one dense membership array.
    void liveness() {
        const auto& blocks = f_.blocks;
        live_in_.assign(n_, {});
        live_out_.assign(n_, {});
        std::vector<std::uint8_t> member(f_.vreg_count, 0);
        Set touched;
        const auto add = [&](std::uint32_t v) {
            if (member[v] != 0) return;
            member[v] = 1;
            touched.push_back(v);
        };
        const auto members = [&] {
            Set out;
            for (const auto v : touched) {
                if (member[v] != 0) out.push_back(v);
            }
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            return out;
        };
        for (const auto b : postorder_) {
            const auto& block = blocks[b];
            for (std::size_t k = 0; k < block.succs.size(); ++k) {
                const auto s = block.succs[k];
                if (!back_[b][k]) {
                    for (const auto v : live_in_[s]) add(v);
                }
                for (const auto& phi : blocks[s].phis) add(phi.inputs[pred_index_[b][k]].id);
            }
            live_out_[b] = members();
            for (auto j = block.insts.size(); j-- > 0;) {
                const auto& inst = block.insts[j];
                for (const auto& def : inst.defs) member[def.vreg.id] = 0;
                for (const auto& use : inst.uses) add(use.vreg.id);
                for (const auto& use : inst.state_uses) add(use.vreg.id);
            }
            for (const auto& phi : block.phis) member[phi.def.id] = 0;
            live_in_[b] = members();
            for (const auto v : touched) member[v] = 0;
            touched.clear();
        }
        const auto unite = [](Set& into, const Set& from) {
            Set out;
            out.reserve(into.size() + from.size());
            std::set_union(into.begin(), into.end(), from.begin(), from.end(), std::back_inserter(out));
            into = std::move(out);
        };
        for (const auto& loop : loops_) {
            const Set through = live_in_[loop.header];
            for (const auto x : loop.body) {
                unite(live_in_[x], through);
                unite(live_out_[x], through);
            }
        }
        for (std::uint32_t e = 0; e < entry_count_; ++e) {
            if (!live_in_[e].empty()) reject("block " + std::to_string(e) + ": a value live into an entry block");
        }
    }

    // ---- intervals --------------------------------------------------------

    std::uint32_t new_interval(std::uint32_t vreg, RegClass cls) {
        Interval it;
        it.vreg = vreg;
        it.cls = cls;
        intervals_.push_back(std::move(it));
        return static_cast<std::uint32_t>(intervals_.size() - 1);
    }

    void add_fixed(RegClass cls, std::uint8_t reg, Pos from, Pos to) {
        fixed_[cls == RegClass::Gpr ? 0 : 1][reg].push_back({from, to});
    }

    void build_intervals() {
        const auto& blocks = f_.blocks;
        intervals_.clear();
        for (std::uint32_t v = 0; v < f_.vreg_count; ++v) new_interval(v, f_.classes[v]);
        hints_.assign(f_.vreg_count, {});
        info_.assign(n_, {});
        std::vector<Pos> open(f_.vreg_count, 0);  // 0: not open
        std::vector<std::uint32_t> opened;
        const auto add_range = [&](std::uint32_t id, Pos from, Pos to) { intervals_[id].ranges.push_back({from, to}); };
        const auto use_at = [&](std::uint32_t v, Pos end, Pos where, bool needs_register) {
            if (open[v] == 0) opened.push_back(v);
            open[v] = std::max(open[v], end);
            intervals_[v].uses.push_back(where);
            if (needs_register) intervals_[v].reg_uses.push_back(where);
        };
        const auto add_temp = [&](RegClass cls, Pos p) {
            const auto id = new_interval(kNone, cls);
            intervals_[id].ranges.push_back({p + kRead, p + kWrite + 1});
            intervals_[id].uses.push_back(p + kRead);
            intervals_[id].reg_uses.push_back(p + kRead);
            return id;
        };

        for (std::uint32_t b = 0; b < n_; ++b) {
            const auto& block = blocks[b];
            const Pos block_start = start_[b];
            for (const auto v : live_out_[b]) {
                open[v] = start_[b + 1];
                opened.push_back(v);
            }
            info_[b].resize(block.insts.size());
            for (auto j = block.insts.size(); j-- > 0;) {
                const auto& inst = block.insts[j];
                const Pos p = base(b, j);
                auto& ii = info_[b][j];
                ii.base = p;
                ii.same_as_def.assign(inst.uses.size(), -1);
                for (std::size_t k = 0; k < inst.defs.size(); ++k) {
                    if (inst.defs[k].c == Constraint::SameAsUse) {
                        ii.same_as_def[inst.defs[k].same_as] = static_cast<int>(k);
                    }
                }

                for (const auto& def : inst.defs) {
                    const auto v = def.vreg.id;
                    Pos from = p + kWrite;
                    Pos dead_to = p + kWrite + 1;
                    auto& it = intervals_[v];
                    switch (def.c) {
                    case Constraint::Register:
                        it.uses.push_back(p + kWrite);
                        it.reg_uses.push_back(p + kWrite);
                        break;
                    case Constraint::Any:
                        it.uses.push_back(p + kWrite);
                        break;
                    case Constraint::SameAsUse:
                        from = p;
                        for (const Pos at_pos : {p, p + kRead, p + kWrite}) {
                            it.uses.push_back(at_pos);
                            it.reg_uses.push_back(at_pos);
                        }
                        hints_[v].push_back({p, 0, inst.uses[def.same_as].vreg.id, p - 1, false});
                        break;
                    case Constraint::Fixed:
                        from = p + kAfter;
                        add_fixed(def.vreg.cls, def.fixed, p + kWrite, p + kAfter);
                        hints_[v].push_back({p + kAfter, def.fixed, kNone, 0, false});
                        break;
                    }
                    if (open[v] != 0) {
                        add_range(v, from, open[v]);
                        open[v] = 0;
                    } else if (def.c != Constraint::Fixed) {
                        add_range(v, from, dead_to);
                    }
                }

                for (std::uint8_t t = 0; t < inst.temps_gpr; ++t) ii.temps_gpr.push_back(add_temp(RegClass::Gpr, p));
                for (std::uint8_t t = 0; t < inst.temps_xmm; ++t) ii.temps_xmm.push_back(add_temp(RegClass::Xmm, p));

                for (std::size_t k = 0; k < inst.uses.size(); ++k) {
                    const auto& use = inst.uses[k];
                    const auto v = use.vreg.id;
                    if (use.c == Constraint::Fixed) {
                        const bool seen = std::any_of(ii.copies.begin(), ii.copies.end(), [&](const UseCopy& c) {
                            return c.vreg == v && c.reg == use.fixed && f_.classes[c.vreg] == use.vreg.cls;
                        });
                        if (!seen) {
                            ii.copies.push_back({v, use.fixed});
                            add_fixed(use.vreg.cls, use.fixed, p, p + kRead + 1);
                            hints_[v].push_back({p - 1, use.fixed, kNone, 0, true});
                        }
                        use_at(v, p, p - 1, false);
                    } else if (ii.same_as_def[k] >= 0) {
                        use_at(v, p, p - 1, false);
                    } else {
                        use_at(v, p + kRead + 1, p + kRead, use.c == Constraint::Register);
                    }
                }
                for (const auto& use : inst.state_uses) use_at(use.vreg.id, p + kWrite + 1, p + kRead, false);

                for (std::uint8_t r = 0; r < 16; ++r) {
                    if (((inst.clobbers_gpr & f_.allocatable_gpr) >> r) & 1U) {
                        add_fixed(RegClass::Gpr, r, p + kClobber, p + kWrite);
                    }
                    if (((inst.clobbers_xmm & f_.allocatable_xmm) >> r) & 1U) {
                        add_fixed(RegClass::Xmm, r, p + kClobber, p + kWrite);
                    }
                }
            }
            for (const auto& phi : block.phis) {
                const auto v = phi.def.id;
                if (open[v] != 0) {
                    add_range(v, block_start, open[v]);
                    open[v] = 0;
                }
                for (std::size_t k = 0; k < phi.inputs.size(); ++k) {
                    const Pos edge = start_[block.preds[k] + 1] - 1;
                    hints_[v].push_back({block_start, 0, phi.inputs[k].id, edge, false});
                    hints_[phi.inputs[k].id].push_back({edge, 0, v, block_start, false});
                }
            }
            for (const auto v : opened) {
                if (open[v] != 0) add_range(v, block_start, open[v]);
                open[v] = 0;
            }
            opened.clear();
        }

        for (auto& it : intervals_) {
            auto& ranges = it.ranges;
            std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) { return a.from < b.from; });
            std::size_t out = 0;
            for (const auto& r : ranges) {
                if (out > 0 && r.from <= ranges[out - 1].to) {
                    ranges[out - 1].to = std::max(ranges[out - 1].to, r.to);
                } else {
                    ranges[out++] = r;
                }
            }
            ranges.resize(out);
            std::sort(it.uses.begin(), it.uses.end());
            it.uses.erase(std::unique(it.uses.begin(), it.uses.end()), it.uses.end());
            std::sort(it.reg_uses.begin(), it.reg_uses.end());
            it.reg_uses.erase(std::unique(it.reg_uses.begin(), it.reg_uses.end()), it.reg_uses.end());
        }
        for (auto& by_reg : fixed_) {
            for (auto& ranges : by_reg) {
                std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) { return a.from < b.from; });
                for (std::size_t i = 1; i < ranges.size(); ++i) {
                    if (ranges[i].from < ranges[i - 1].to) {
                        reject(where(ranges[i].from) + "two fixed uses of one register");
                    }
                }
            }
        }
        for (auto& hints : hints_) {
            std::stable_sort(hints.begin(), hints.end(), [](const Hint& a, const Hint& b) { return a.at < b.at; });
        }
        parts_.assign(f_.vreg_count, {});
        life_.assign(f_.vreg_count, {});
        for (std::uint32_t v = 0; v < f_.vreg_count; ++v) {
            if (intervals_[v].ranges.empty()) continue;
            parts_[v].push_back(v);
            life_[v] = intervals_[v].ranges;
        }
    }

    // ---- the scan ---------------------------------------------------------

    static Pos next_reg_use(const Interval& it, Pos from) {
        const auto i = std::lower_bound(it.reg_uses.begin(), it.reg_uses.end(), from);
        return i == it.reg_uses.end() ? kNever : *i;
    }

    // The last use before `pos`, or kNever.
    static Pos last_use_before(const Interval& it, Pos pos) {
        const auto i = std::lower_bound(it.uses.begin(), it.uses.end(), pos);
        return i == it.uses.begin() ? kNever : *std::prev(i);
    }

    bool covers_now(Interval& it, Pos pos) {
        while (it.cursor < it.ranges.size() && it.ranges[it.cursor].to <= pos) ++it.cursor;
        return it.cursor < it.ranges.size() && it.ranges[it.cursor].from <= pos;
    }

    // Where to split between `lo` (exclusive) and `hi` (inclusive): the
    // latest split position, unless a block entry in between lies in a
    // shallower loop, so that spill and reload code leaves loops. kNever when
    // there is no split position in between.
    Pos split_position(Pos lo, Pos hi) const {
        const Pos latest = gap_at_or_before(hi);
        if (latest <= lo) return kNever;
        const auto last_block = block_containing(latest);
        Pos best = latest;
        const auto first_block = static_cast<std::uint32_t>(
            std::upper_bound(start_.begin(), start_.begin() + n_, lo) - start_.begin());
        if (first_block <= last_block) {
            const auto c = shallowest(first_block, last_block);
            if (depth_[c] < depth_[last_block]) best = start_[c];
        }
        return best;
    }

    // Splits interval `id` at `pos`, which lies strictly inside it, and
    // returns the part from `pos` on.
    std::uint32_t split(std::uint32_t id, Pos pos) {
        if (pos <= intervals_[id].start() || pos >= intervals_[id].end()) {
            reject(where(pos) + "internal error: a split outside its interval");
        }
        Interval child;
        {
            auto& it = intervals_[id];
            child.vreg = it.vreg;
            child.cls = it.cls;
            const auto k = first_after(it.ranges, pos);
            child.ranges.assign(it.ranges.begin() + static_cast<std::ptrdiff_t>(k), it.ranges.end());
            if (it.ranges[k].from < pos) {
                // `pos` cuts range k: the parent keeps its part before `pos`.
                child.ranges.front().from = pos;
                it.ranges[k].to = pos;
                it.ranges.resize(k + 1);
            } else {
                it.ranges.resize(k);
            }
            const auto u = std::lower_bound(it.uses.begin(), it.uses.end(), pos);
            child.uses.assign(u, it.uses.end());
            it.uses.erase(u, it.uses.end());
            const auto r = std::lower_bound(it.reg_uses.begin(), it.reg_uses.end(), pos);
            child.reg_uses.assign(r, it.reg_uses.end());
            it.reg_uses.erase(r, it.reg_uses.end());
            it.cursor = 0;
        }
        intervals_.push_back(std::move(child));
        const auto child_id = static_cast<std::uint32_t>(intervals_.size() - 1);
        if (intervals_[child_id].vreg != kNone) parts_[intervals_[child_id].vreg].push_back(child_id);
        return child_id;
    }

    void push(std::uint32_t id, Pos now) {
        if (intervals_[id].start() <= now) reject(where(now) + "internal error: a split behind the scan");
        unhandled_.push({intervals_[id].start(), id});
    }

    [[noreturn]] void no_register(std::uint32_t id, Pos pos) const {
        const auto& it = intervals_[id];
        const std::string what = it.vreg == kNone ? std::string("a temp") : "vreg " + std::to_string(it.vreg);
        reject(where(pos) + "no register for " + what + ": the instruction needs more registers than the class has");
    }

    // The register a part of a vreg would like, or -1: from the first of at
    // most eight hints inside the part that applies.
    int hint_for(std::uint32_t id) const {
        const auto& it = intervals_[id];
        if (it.vreg == kNone) return -1;
        const auto mask = allocatable(f_, cls_);
        const auto& hints = hints_[it.vreg];
        auto h_it = std::lower_bound(hints.begin(), hints.end(), it.start(),
                                     [](const Hint& h, Pos p) { return h.at < p; });
        for (int seen = 0; h_it != hints.end() && h_it->at < it.end() && seen < 8; ++h_it, ++seen) {
            const auto& h = *h_it;
            if (!covers(it.ranges, h.at)) continue;
            if (h.only_if_ends && it.end() != h.at + 1) continue;
            if (h.partner == kNone) {
                if ((mask >> h.reg) & 1U) return h.reg;
                continue;
            }
            for (const auto part : parts_[h.partner]) {
                const auto& other = intervals_[part];
                if (other.reg >= 0 && other.cls == cls_ && covers(other.ranges, h.partner_at)) return other.reg;
            }
        }
        return -1;
    }

    void scan(RegClass cls) {
        cls_ = cls;
        regs_.clear();
        const auto mask = allocatable(f_, cls);
        for (std::uint8_t r = 0; r < 16; ++r) {
            if ((mask >> r) & 1U) regs_.push_back(r);
        }
        unhandled_ = {};
        active_.clear();
        for (auto& set : inactive_) set.clear();
        fixed_cursor_.fill(0);
        for (std::uint32_t id = 0; id < intervals_.size(); ++id) {
            if (intervals_[id].cls == cls && !intervals_[id].ranges.empty()) {
                unhandled_.push({intervals_[id].start(), id});
            }
        }
        while (!unhandled_.empty()) {
            const auto [pos, id] = unhandled_.top();
            unhandled_.pop();
            advance(pos);
            if (!try_free(id)) allocate_blocked(id);
            if (intervals_[id].reg >= 0) active_.push_back(id);
        }
    }

    // Moves the scan to `pos`: intervals that end drop out, intervals that
    // enter a hole become inactive, inactive intervals whose next range has
    // begun become active.
    void advance(Pos pos) {
        std::vector<std::uint32_t> still_active;
        const auto place = [&](std::uint32_t a) {
            auto& it = intervals_[a];
            if (it.end() <= pos) return;
            if (covers_now(it, pos)) {
                still_active.push_back(a);
            } else {
                inactive_[static_cast<std::size_t>(it.reg)].insert({next_start(it), a});
            }
        };
        for (const auto a : active_) place(a);
        for (const auto r : regs_) {
            auto& set = inactive_[r];
            while (!set.empty() && set.begin()->first <= pos) {
                const auto a = set.begin()->second;
                set.erase(set.begin());
                place(a);
            }
        }
        active_ = std::move(still_active);
    }

    const std::vector<Range>& fixed_ranges(std::uint8_t reg) const {
        return fixed_[cls_ == RegClass::Gpr ? 0 : 1][reg];
    }

    // The first position from the interval's start where register `reg` is
    // fixed and the interval is live, or kNever. The scan's positions only
    // grow, so each register's cursor only moves forward.
    Pos fixed_intersection(std::uint8_t reg, const Interval& it) {
        const auto& ranges = fixed_ranges(reg);
        auto& cursor = fixed_cursor_[reg];
        const Pos start = it.start();
        while (cursor < ranges.size() && ranges[cursor].to <= start) ++cursor;
        if (cursor == ranges.size() || ranges[cursor].from >= it.end()) return kNever;
        return intersection(ranges, cursor, it.ranges, 0, start);
    }

    // The start of an inactive interval's next range: the scan left its
    // cursor on the first range that ends after the scan's position, and the
    // interval does not cover that position.
    static Pos next_start(const Interval& it) { return it.ranges[it.cursor].from; }

    bool try_free(std::uint32_t id) {
        const Pos start = intervals_[id].start();
        const Pos end = intervals_[id].end();
        std::array<Pos, 16> free{};
        for (const auto r : regs_) free[r] = kNever;
        for (const auto a : active_) free[static_cast<std::size_t>(intervals_[a].reg)] = 0;
        for (const auto r : regs_) {
            for (const auto& [next, a] : inactive_[r]) {
                if (next >= free[r] || next >= end) break;
                const auto& it = intervals_[a];
                free[r] = std::min(free[r], intersection(it.ranges, it.cursor, intervals_[id].ranges, 0, start));
            }
        }
        for (const auto r : regs_) {
            if (free[r] > start) free[r] = std::min(free[r], fixed_intersection(r, intervals_[id]));
        }

        const int hint = hint_for(id);
        int reg = -1;
        if (hint >= 0 && free[static_cast<std::size_t>(hint)] >= end) {
            reg = hint;
        } else {
            for (const auto r : regs_) {
                if (free[r] >= end && (reg < 0 || free[r] < free[static_cast<std::size_t>(reg)])) reg = r;
            }
        }
        if (reg >= 0) {
            intervals_[id].reg = reg;
            return true;
        }
        for (const auto r : regs_) {
            if (reg < 0 || free[r] > free[static_cast<std::size_t>(reg)] ||
                (free[r] == free[static_cast<std::size_t>(reg)] && r == hint)) {
                reg = r;
            }
        }
        if (reg < 0) return false;
        const Pos until = free[static_cast<std::size_t>(reg)];
        if (gap_at_or_before(until) <= start) return false;
        const auto child = split(id, split_position(start, until));
        intervals_[id].reg = reg;
        push(child, start);
        return true;
    }

    void allocate_blocked(std::uint32_t id) {
        const Pos start = intervals_[id].start();
        const Pos end = intervals_[id].end();
        const Pos gap = gap_at_or_before(start);
        const Pos first = next_reg_use(intervals_[id], start);
        std::array<Pos, 16> use{}, block{};
        for (const auto r : regs_) use[r] = block[r] = kNever;
        // An active interval gives up its register at the latest split
        // position at or before `start`, so it needs the register again at
        // its first register use from there.
        for (const auto a : active_) {
            const auto r = static_cast<std::size_t>(intervals_[a].reg);
            use[r] = std::min(use[r], next_reg_use(intervals_[a], gap));
        }
        for (const auto r : regs_) {
            for (const auto& [next, a] : inactive_[r]) {
                if (next >= use[r] || next >= end) break;
                const auto& it = intervals_[a];
                if (intersection(it.ranges, it.cursor, intervals_[id].ranges, 0, start) == kNever) continue;
                use[r] = std::min(use[r], next_reg_use(it, start));
            }
        }
        // A register fixed from x on serves only up to the split position at
        // or before x.
        for (const auto r : regs_) {
            const Pos x = fixed_intersection(r, intervals_[id]);
            block[r] = x;
            if (x < end) use[r] = std::min(use[r], gap_at_or_before(x));
        }
        const int hint = hint_for(id);
        int reg = -1;
        for (const auto r : regs_) {
            if (reg < 0 || use[r] > use[static_cast<std::size_t>(reg)] ||
                (use[r] == use[static_cast<std::size_t>(reg)] && r == hint)) {
                reg = r;
            }
        }

        if (reg < 0 || use[static_cast<std::size_t>(reg)] <= first) {
            // Every register is needed no later than this interval needs one:
            // it goes to memory until just before its first register use.
            if (first == kNever) {
                intervals_[id].spilled = true;
                return;
            }
            if (gap_at_or_before(first) <= start) no_register(id, first);
            const auto child = split(id, split_position(start, first));
            intervals_[id].spilled = true;
            push(child, start);
            return;
        }

        const auto r = static_cast<std::size_t>(reg);
        intervals_[id].reg = reg;
        if (block[r] < end) push(split(id, split_position(start, block[r])), start);
        std::vector<std::uint32_t> keep;
        for (const auto a : active_) {
            if (intervals_[a].reg == reg) {
                evict_active(a, start, gap);
            } else {
                keep.push_back(a);
            }
        }
        active_ = std::move(keep);
        std::vector<std::pair<Pos, std::uint32_t>> evicted;
        const Pos now_end = intervals_[id].end();
        for (const auto& entry : inactive_[r]) {
            if (entry.first >= now_end) break;
            if (intersection(intervals_[entry.second].ranges, intervals_[id].ranges, start) != kNever) {
                evicted.push_back(entry);
            }
        }
        for (const auto& entry : evicted) {
            inactive_[r].erase(entry);
            evict_inactive(entry.second, start);
        }
    }

    // An active interval loses its register to the interval starting at
    // `start`: it goes to memory from a split position at or before `gap`
    // (after its last use before there) and back to a register just before
    // its next register use.
    void evict_active(std::uint32_t a, Pos start, Pos gap) {
        const Pos u = next_reg_use(intervals_[a], gap);
        if (u != kNever) {
            if (gap_at_or_before(u) <= start) {
                reject(where(start) + "internal error: an eviction without a reload position");
            }
            push(split(a, split_position(start, u)), start);
        }
        if (intervals_[a].start() >= gap) {
            intervals_[a].reg = -1;
            intervals_[a].spilled = true;
            return;
        }
        const Pos last = last_use_before(intervals_[a], gap);
        const Pos lo = last == kNever ? intervals_[a].start() : std::max(intervals_[a].start(), last);
        const auto spilled = split(a, split_position(lo, gap));
        intervals_[spilled].spilled = true;
    }

    // An inactive interval loses its register from its next range on: that
    // part gets a new chance when the scan reaches it.
    void evict_inactive(std::uint32_t a, Pos start) {
        const auto& ranges = intervals_[a].ranges;
        const auto k = first_after(ranges, start);
        const Pos next = ranges[k].from;
        push(split(a, split_position(start, next)), start);
    }

    // ---- stack locations --------------------------------------------------

    void assign_stack() {
        stack_loc_.assign(f_.vreg_count, Location{});
        has_stack_.assign(f_.vreg_count, false);
        std::vector<std::uint32_t> order;
        for (std::uint32_t v = 0; v < f_.vreg_count; ++v) {
            const bool spilled = std::any_of(parts_[v].begin(), parts_[v].end(), [&](std::uint32_t p) {
                return intervals_[p].spilled;
            });
            if (spilled) order.push_back(v);
        }
        std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
            const Pos sa = life_[a].front().from, sb = life_[b].front().from;
            return sa != sb ? sa < sb : a < b;
        });

        // A vreg spills to its frame slot when no vreg already there is
        // live at the same time.
        std::map<std::int32_t, std::map<Pos, Pos>> occupied;
        std::vector<std::uint32_t> native;
        for (const auto v : order) {
            const auto home = f_.spill_home[v];
            if (home < 0) {
                native.push_back(v);
                continue;
            }
            auto& slot = occupied[home];
            bool clash = false;
            for (const auto& r : life_[v]) {
                const auto next = slot.lower_bound(r.from);
                if (next != slot.end() && next->first < r.to) clash = true;
                if (next != slot.begin() && std::prev(next)->second > r.from) clash = true;
                if (clash) break;
            }
            if (clash) {
                native.push_back(v);
                continue;
            }
            for (const auto& r : life_[v]) slot.emplace(r.from, r.to);
            stack_loc_[v] = {LocKind::FrameSlot, static_cast<std::uint32_t>(home)};
            has_stack_[v] = true;
        }

        // Native slots by lifetime hull: a slot is free again once the hull of
        // its last vreg has ended.
        std::sort(native.begin(), native.end(), [&](std::uint32_t a, std::uint32_t b) {
            const Pos sa = life_[a].front().from, sb = life_[b].front().from;
            return sa != sb ? sa < sb : a < b;
        });
        using Busy = std::pair<Pos, std::uint32_t>;
        std::priority_queue<Busy, std::vector<Busy>, std::greater<Busy>> busy;
        std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, std::greater<std::uint32_t>> free;
        for (const auto v : native) {
            const Pos from = life_[v].front().from;
            while (!busy.empty() && busy.top().first <= from) {
                free.push(busy.top().second);
                busy.pop();
            }
            std::uint32_t slot;
            if (free.empty()) {
                slot = stack_slots_++;
            } else {
                slot = free.top();
                free.pop();
            }
            busy.push({life_[v].back().to, slot});
            stack_loc_[v] = {LocKind::Stack, slot};
            has_stack_[v] = true;
        }
    }

    // ---- resolution -------------------------------------------------------

    Location location_of(std::uint32_t id) const {
        const auto& it = intervals_[id];
        if (it.reg >= 0) return {LocKind::Reg, static_cast<std::uint32_t>(it.reg)};
        if (!it.spilled || it.vreg == kNone || !has_stack_[it.vreg]) {
            reject("internal error: an interval without a location");
        }
        return stack_loc_[it.vreg];
    }

    // The part of vreg v that covers `pos`.
    std::uint32_t part_at(std::uint32_t v, Pos pos) const {
        const auto& parts = parts_[v];
        if (parts.size() == 1 && intervals_[parts[0]].start() <= pos && pos < intervals_[parts[0]].end()) {
            return parts[0];
        }
        const auto it = std::upper_bound(parts.begin(), parts.end(), pos, [&](Pos p, std::uint32_t id) {
            return p < intervals_[id].start();
        });
        if (it == parts.begin() || !covers(intervals_[*std::prev(it)].ranges, pos)) {
            reject(where(pos) + "internal error: vreg " + std::to_string(v) + " has no location here");
        }
        return *std::prev(it);
    }

    Location location_at(std::uint32_t v, Pos pos) const { return location_of(part_at(v, pos)); }

    std::vector<Move> ordered(const std::vector<Move>& parallel, std::uint32_t spare, bool& spare_used) const {
        if (parallel.empty()) return {};
        bool used = false;
        auto moves = sequentialize(parallel, spare, used);
        spare_used = spare_used || used;
        return moves;
    }

    Allocation resolve() {
        const auto& blocks = f_.blocks;
        for (auto& parts : parts_) {
            std::sort(parts.begin(), parts.end(), [&](std::uint32_t a, std::uint32_t b) {
                return intervals_[a].start() < intervals_[b].start();
            });
        }
        std::vector<std::vector<std::vector<Move>>> before(n_), after(n_);
        for (std::uint32_t b = 0; b < n_; ++b) {
            before[b].resize(blocks[b].insts.size());
            after[b].resize(blocks[b].insts.size());
        }

        // A part that begins inside a block continues the part before it at
        // that gap; a part that begins at a block's entry is joined to its
        // predecessors on the edges.
        for (std::uint32_t v = 0; v < f_.vreg_count; ++v) {
            const auto& parts = parts_[v];
            for (std::size_t i = 1; i < parts.size(); ++i) {
                const Pos pos = intervals_[parts[i]].start();
                const auto b = block_containing(pos);
                if (pos == start_[b]) continue;
                if (intervals_[parts[i - 1]].end() != pos || (pos - start_[b]) % kStride != 0) {
                    reject(where(pos) + "internal error: a split that is not at a gap");
                }
                const auto from = location_of(parts[i - 1]);
                const auto to = location_of(parts[i]);
                if (!(from == to)) before[b][(pos - start_[b]) / kStride - 1].push_back({from, to, f_.classes[v]});
            }
        }

        Allocation result;
        result.blocks.resize(n_);
        const std::uint32_t spare = stack_slots_;
        bool spare_used = false;
        for (std::uint32_t b = 0; b < n_; ++b) {
            const auto& block = blocks[b];
            auto& out = result.blocks[b];
            out.insts.resize(block.insts.size());
            for (std::size_t j = 0; j < block.insts.size(); ++j) {
                const auto& inst = block.insts[j];
                const auto& ii = info_[b][j];
                const Pos p = ii.base;
                auto& ia = out.insts[j];
                auto& pre = before[b][j];
                for (const auto& copy : ii.copies) {
                    const auto from = location_at(copy.vreg, p - 1);
                    const Location to{LocKind::Reg, copy.reg};
                    if (!(from == to)) pre.push_back({from, to, f_.classes[copy.vreg]});
                }
                for (std::size_t k = 0; k < inst.uses.size(); ++k) {
                    const auto& use = inst.uses[k];
                    if (use.c == Constraint::Fixed) {
                        ia.uses.push_back({LocKind::Reg, use.fixed});
                    } else if (ii.same_as_def[k] >= 0) {
                        const auto& def = inst.defs[static_cast<std::size_t>(ii.same_as_def[k])];
                        ia.uses.push_back(location_at(def.vreg.id, p));
                    } else {
                        ia.uses.push_back(location_at(use.vreg.id, p + kRead));
                    }
                }
                for (const auto& def : inst.defs) {
                    const auto d = def.vreg.id;
                    switch (def.c) {
                    case Constraint::Fixed:
                        ia.defs.push_back({LocKind::Reg, def.fixed});
                        if (!parts_[d].empty()) {
                            const auto to = location_at(d, p + kAfter);
                            if (!(to == Location{LocKind::Reg, def.fixed})) {
                                after[b][j].push_back({{LocKind::Reg, def.fixed}, to, def.vreg.cls});
                            }
                        }
                        break;
                    case Constraint::SameAsUse: {
                        const auto to = location_at(d, p);
                        ia.defs.push_back(to);
                        const auto source = inst.uses[def.same_as].vreg.id;
                        const auto from = location_at(source, p - 1);
                        if (!(from == to)) pre.push_back({from, to, def.vreg.cls});
                        break;
                    }
                    default:
                        ia.defs.push_back(location_at(d, p + kWrite));
                        break;
                    }
                }
                for (const auto& use : inst.state_uses) ia.state_uses.push_back(location_at(use.vreg.id, p + kRead));
                for (const auto t : ii.temps_gpr) ia.temps_gpr.push_back(static_cast<std::uint8_t>(intervals_[t].reg));
                for (const auto t : ii.temps_xmm) ia.temps_xmm.push_back(static_cast<std::uint8_t>(intervals_[t].reg));
                ia.before = ordered(pre, spare, spare_used);
                ia.after = ordered(after[b][j], spare, spare_used);
            }

            const Pos edge = start_[b + 1] - 1;
            for (std::size_t k = 0; k < block.succs.size(); ++k) {
                const auto s = block.succs[k];
                const Pos entry = start_[s];
                std::vector<Move> moves;
                for (const auto v : live_in_[s]) {
                    if (parts_[v].size() == 1) continue;  // one location throughout
                    const auto from = location_at(v, edge);
                    const auto to = location_at(v, entry);
                    if (!(from == to)) moves.push_back({from, to, f_.classes[v]});
                }
                for (const auto& phi : blocks[s].phis) {
                    const auto d = phi.def.id;
                    if (parts_[d].empty()) continue;
                    const auto from = location_at(phi.inputs[pred_index_[b][k]].id, edge);
                    const auto to = location_at(d, entry);
                    if (!(from == to)) moves.push_back({from, to, phi.def.cls});
                }
                out.edges.push_back(ordered(moves, spare, spare_used));
            }

            const Pos entry = start_[b];
            for (const auto v : live_in_[b]) out.entry.push_back({v, location_at(v, entry)});
            for (const auto& phi : block.phis) {
                if (!parts_[phi.def.id].empty()) out.entry.push_back({phi.def.id, location_at(phi.def.id, entry)});
            }
            std::sort(out.entry.begin(), out.entry.end(),
                      [](const auto& x, const auto& y) { return x.first < y.first; });
        }
        result.stack_slots = stack_slots_ + (spare_used ? 1 : 0);
        return result;
    }
};

}  // namespace

std::vector<Move> sequentialize(const std::vector<Move>& moves, std::uint32_t spare_slot, bool& spare_used) {
    std::vector<Move> pending;
    for (const auto& m : moves) {
        if (!same_place(m.from, m.cls, m.to, m.cls)) pending.push_back(m);
    }
    std::vector<Move> out;
    const auto emit = [&](const Move& m) {
        if (is_memory(m.from) && is_memory(m.to)) {
            const Location s{LocKind::Reg, scratch(m.cls)};
            out.push_back({m.from, s, m.cls});
            out.push_back({s, m.to, m.cls});
        } else {
            out.push_back(m);
        }
    };
    // Whether a pending move other than `except` reads `where`.
    const auto read_by_other = [&](const Location& where, RegClass cls, std::size_t except) {
        for (std::size_t i = 0; i < pending.size(); ++i) {
            if (i != except && same_place(pending[i].from, pending[i].cls, where, cls)) return true;
        }
        return false;
    };
    while (!pending.empty()) {
        bool progress = true;
        while (progress) {
            progress = false;
            for (std::size_t i = 0; i < pending.size();) {
                if (read_by_other(pending[i].to, pending[i].cls, i)) {
                    ++i;
                    continue;
                }
                emit(pending[i]);
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
                progress = true;
            }
        }
        if (pending.empty()) break;

        // Only cycles are left: every location in one is read by exactly one
        // move. Collect the cycle through the first pending move.
        std::vector<std::size_t> cycle{0};
        for (;;) {
            const auto& last = pending[cycle.back()];
            std::size_t next = pending.size();
            for (std::size_t i = 0; i < pending.size(); ++i) {
                if (same_place(pending[i].from, pending[i].cls, last.to, last.cls)) {
                    next = i;
                    break;
                }
            }
            if (next == 0 || next == pending.size()) break;
            cycle.push_back(next);
        }
        // Save the source of a memory-to-memory move if there is one (the
        // move then reads the save), else the source of the first move.
        std::size_t breaking = cycle.front();
        for (const auto i : cycle) {
            if (is_memory(pending[i].from) && is_memory(pending[i].to)) {
                breaking = i;
                break;
            }
        }
        const RegClass cls = pending[breaking].cls;
        bool needs_slot = false;
        for (const auto i : cycle) {
            if (i != breaking && pending[i].cls == cls && is_memory(pending[i].from) && is_memory(pending[i].to)) {
                needs_slot = true;
            }
        }
        const Location source = pending[breaking].from;
        const Location reg{LocKind::Reg, scratch(cls)};
        Location save = reg;
        if (needs_slot) {
            spare_used = true;
            save = {LocKind::Stack, spare_slot};
            if (is_memory(source)) {
                out.push_back({source, reg, cls});
                out.push_back({reg, save, cls});
            } else {
                out.push_back({source, save, cls});
            }
        } else {
            out.push_back({source, save, cls});
        }
        pending[breaking].from = save;
    }
    return out;
}

Allocation allocate(const Function& function) { return Allocator(function).run(); }

}  // namespace neura::jit::mir
