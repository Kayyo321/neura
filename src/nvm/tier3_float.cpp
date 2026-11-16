// Floats in tier 3 (design 7.2): canonical NaNs, deferred.
//
// The reference gives the canonical quiet NaN of its format for every NaN
// result of FloatAdd, FloatSubtract, FloatMultiply and FloatDivide (and the
// sqrt kernel). place_canons marks those operations (and every Sqrt)
// Deferred: native code computes them with the hardware's NaN, which is a NaN
// exactly where the reference's is and equal to it elsewhere, and a Canon
// replaces it by the canonical NaN only where its bits can be observed:
//
// - an insensitive use (float arithmetic, a comparison, FloatToInteger,
//   FloatConvert, a float ValueHash, Sqrt, Canon) reads any NaN of the
//   operation's format as the reference reads its NaN, so it takes a
//   deferred value of that format as it is;
// - a propagating op (FloatNegate, abs, a phi) gives a value that is a NaN
//   exactly where the reference's is; whether Canon of it is the reference's
//   bits is its class (NanClass): a negation of a deferred value, or a phi of
//   one and of a value that may be a NaN other than the canonical one, is
//   not, and Canons go on its deferred operands where its bits are needed;
// - every other use is sensitive (a store, an argument, a returned value, an
//   integer operation on the bits) and reads only the reference's bits: a
//   deferred operand gets a Canon just before it.
//
// Frame writes of deoptimization and write-backs canonicalize a deferred
// value themselves (Function::frame_canon gives the format), in cold code; a
// value of the other classes that a frame state names gets Canons on its
// operands like a sensitive use.
#include "tier3.hpp"

#include "tier3_passes.hpp"

#include "lift/evaluate.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace neura::nvm::tier3 {
namespace detail {
namespace {

constexpr std::uint64_t canonical64 = 0x7FF8000000000000ULL;
constexpr std::uint64_t canonical32 = 0x7FC00000ULL;

bool nan64(std::uint64_t bits) {
    return (bits & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL && (bits & 0x000FFFFFFFFFFFFFULL) != 0;
}

bool nan32(std::uint64_t bits) {
    return (bits & 0x7F800000ULL) == 0x7F800000ULL && (bits & 0x007FFFFFULL) != 0;
}

unsigned format_bits(lift::FloatType type) {
    switch (type) {
    case lift::FloatType::Binary32: return 32;
    case lift::FloatType::Binary64: return 64;
    case lift::FloatType::Binary16:
    case lift::FloatType::BFloat16: return 16;
    case lift::FloatType::None: break;
    }
    return 0;
}

// FloatAdd, FloatSubtract, FloatMultiply and FloatDivide of a format the
// hardware computes.
bool arithmetic(const lift::Instruction& l) {
    switch (l.op) {
    case lift::Opcode::FloatAdd:
    case lift::Opcode::FloatSubtract:
    case lift::Opcode::FloatMultiply:
    case lift::Opcode::FloatDivide:
        return l.format == lift::FloatType::Binary32 || l.format == lift::FloatType::Binary64;
    default: return false;
    }
}

NanClass deferred_class(unsigned format) { return format == 32 ? NanClass::Deferred32 : NanClass::Deferred64; }

NanClass mixed_class(unsigned format) {
    return format == 32 ? NanClass::Mixed32 : format == 64 ? NanClass::Mixed64 : NanClass::Mixed;
}

NanClass clean_class(unsigned format) {
    return format == 32 ? NanClass::Clean32 : format == 64 ? NanClass::Clean64 : NanClass::Exact;
}

bool deferred(NanClass c) { return c == NanClass::Deferred32 || c == NanClass::Deferred64; }

bool mixed(NanClass c) { return c == NanClass::Mixed32 || c == NanClass::Mixed64 || c == NanClass::Mixed; }

bool clean(NanClass c) { return c == NanClass::Clean || c == NanClass::Clean32 || c == NanClass::Clean64; }

// Whether a clean class has no NaN but the canonical NaN of `format`.
bool clean_in(NanClass c, unsigned format) {
    return c == NanClass::Clean || (format == 32 && c == NanClass::Clean32) || (format == 64 && c == NanClass::Clean64);
}

// The class of bits that hold no NaN but a canonical one, by format.
NanClass clean_bits(std::uint64_t bits) {
    const bool c64 = !nan64(bits) || bits == canonical64;
    const bool c32 = (bits >> 32) == 0 && (!nan32(bits) || bits == canonical32);
    return c32 && c64 ? NanClass::Clean : c64 ? NanClass::Clean64 : c32 ? NanClass::Clean32 : NanClass::Exact;
}

// The classes with an extra "not known yet" for the fixed point.
constexpr std::uint8_t top = 0xFF;

// The class of a value that is one of two values, path by path.
NanClass meet(NanClass x, NanClass y) {
    if (x == y) return x;
    if (clean(y) && !clean(x)) std::swap(x, y);
    if (clean(x)) {
        if (clean(y)) {
            if (x == NanClass::Clean) return y;
            if (y == NanClass::Clean) return x;
            return NanClass::Exact;
        }
        if (y == NanClass::Exact) return NanClass::Exact;
        // A clean value of the deferred value's format is that value's
        // Canon; another one is not.
        if (deferred(y)) return clean_in(x, nan_format(y)) ? y : mixed_class(nan_format(y));
        return y;
    }
    if (x == NanClass::Exact) return mixed_class(nan_format(y));
    if (y == NanClass::Exact) return mixed_class(nan_format(x));
    const auto fx = nan_format(x);
    return fx != 0 && fx == nan_format(y) ? mixed_class(fx) : NanClass::Mixed;
}

std::uint8_t meet(std::uint8_t a, std::uint8_t b) {
    if (a == top) return b;
    if (b == top) return a;
    return static_cast<std::uint8_t>(meet(static_cast<NanClass>(a), static_cast<NanClass>(b)));
}

// The class of a value whose sign bit (at `format`) is cleared, or flipped
// (`negate`), from a value of class `c`.
NanClass sign_changed(NanClass c, unsigned format, bool negate) {
    if (clean(c)) return !negate && clean_in(c, format) ? clean_class(format) : NanClass::Exact;
    if (c == NanClass::Exact) return NanClass::Exact;
    const auto f = nan_format(c);
    if (f != format) return NanClass::Mixed;
    // abs(Canon(x)) is Canon(abs(x)); -Canon(x) is no Canon of -x.
    return deferred(c) && !negate ? c : mixed_class(f);
}

std::uint8_t transfer(const Function& f, const Inst& in, const std::vector<std::uint8_t>& cls) {
    const auto of = [&](std::uint32_t v) { return v == none || v >= cls.size() ? std::uint8_t{top} : cls[v]; };
    const auto as = [](NanClass c) { return static_cast<std::uint8_t>(c); };
    switch (in.op) {
    case Op::Phi: {
        auto c = top;
        for (std::uint32_t i = 0; i < in.count; ++i) c = meet(c, of(f.extras[in.extra + i]));
        return c;
    }
    case Op::Lift: {
        if (const auto x = abs_operand(f, in); x != none) {
            const auto c = of(x);
            return c == top ? top : as(sign_changed(static_cast<NanClass>(c), in.type.width, false));
        }
        const auto& l = in.lifted;
        if (arithmetic(l)) {
            const auto format = format_bits(l.format);
            return as((in.flags & Deferred) != 0 ? deferred_class(format) : clean_class(format));
        }
        switch (l.op) {
        case lift::Opcode::FloatNegate: {
            const auto c = of(in.operands[0]);
            return c == top ? top : as(sign_changed(static_cast<NanClass>(c), format_bits(l.format), true));
        }
        case lift::Opcode::FloatConvert:
        case lift::Opcode::FloatFromInteger: return as(clean_class(format_bits(l.to_format)));
        default: return as(NanClass::Exact);
        }
    }
    case Op::Sqrt: return as((in.flags & Deferred) != 0 ? NanClass::Deferred64 : NanClass::Clean64);
    case Op::Canon: return as(clean_class(in.type.width));
    case Op::Const: return as(clean_bits(in.imm));
    case Op::EntryLoad:
        // Assumed: an entry whose value joins a deferred one checks it
        // (Function::clean_slots).
        if (in.type.type == Type::F32 || in.type.type == Type::F64) return as(clean_class(in.type.width));
        return as(NanClass::Exact);
    default: return as(NanClass::Exact);
    }
}

// Whether a value of class `c` may be read by a use of kind `use`.
bool readable(NanClass c, BitsUse use) {
    if (exact(c) || use == BitsUse::Propagating) return true;
    if (use == BitsUse::Insensitive32) return c == NanClass::Deferred32 || c == NanClass::Mixed32;
    if (use == BitsUse::Insensitive64) return c == NanClass::Deferred64 || c == NanClass::Mixed64;
    return false;
}

std::uint32_t& operand_at(Function& f, Inst& in, unsigned position) {
    return position < operand_slots ? in.operands[position] : f.extras[in.extra + position - operand_slots];
}

class Placement {
public:
    explicit Placement(Function& f) : f_(f) {}

    void run() {
        retype_abs();
        mark_deferred();
        // Each round makes the uses it finds read the reference's bits; a
        // Canon makes a value clean, so the classes only ever improve, and
        // the rounds end.
        for (unsigned round = 0;; ++round) {
            if (round > 64) throw std::logic_error("tier 3: the Canons of a function do not converge");
            cls_ = nan_classes(f_);
            const Cfg cfg(f_);
            places_ = Places(f_);
            made_.clear();
            fixed_.clear();
            insertions_.clear();
            bool any = false;
            for (const auto b : cfg.rpo) {
                const auto insts = f_.blocks[b].insts;
                for (const auto id : insts) {
                    if (f_.insts[id].op == Op::Phi) continue;
                    for (const auto position : value_positions(f_.insts[id])) {
                        const auto v = operand_at(f_, f_.insts[id], position);
                        const auto c = cls_[v];
                        if (readable(c, bits_use(f_, f_.insts[id], position))) continue;
                        any = true;
                        if (deferred(c)) {
                            const auto canon = canon_before(v, nan_format(c), b, id);
                            operand_at(f_, f_.insts[id], position) = canon;
                        } else {
                            fix(v);
                        }
                    }
                    for_each_state_value(f_, f_.insts[id].state, [&](std::uint32_t v) {
                        if (mixed(cls_[v])) {
                            any = true;
                            fix(v);
                        }
                    });
                }
            }
            apply();
            if (!any) break;
        }
        const auto final_classes = nan_classes(f_);
        f_.frame_canon.assign(f_.insts.size(), 0);
        for (std::uint32_t v = 0; v < f_.insts.size(); ++v) {
            if (deferred(final_classes[v])) f_.frame_canon[v] = static_cast<std::uint8_t>(nan_format(final_classes[v]));
        }
        f_.clean_slots = clean_slots_needed(f_, final_classes);
    }

private:
    // A Lift BitAnd of a float-typed value and the magnitude mask of its
    // format is abs (Float64.abs, Float32.abs): typed as the float.
    void retype_abs() {
        for (auto& in : f_.insts) {
            if (in.op != Op::Lift || in.lifted.op != lift::Opcode::BitAnd) continue;
            for (int k = 0; k < 2; ++k) {
                const auto x = in.operands[k];
                const auto m = in.operands[1 - k];
                if (x == none || m == none || f_.insts[m].op != Op::Const) continue;
                const auto type = f_.insts[x].type;
                if ((type.type == Type::F64 && f_.insts[m].imm == 0x7FFFFFFFFFFFFFFFULL) ||
                    (type.type == Type::F32 && f_.insts[m].imm == 0x7FFFFFFFULL)) {
                    in.type = type;
                    break;
                }
            }
        }
    }

    void mark_deferred() {
        for (auto& in : f_.insts) {
            if ((in.op == Op::Lift && arithmetic(in.lifted)) || in.op == Op::Sqrt) in.flags |= Deferred;
        }
    }

    // Makes the value `v` (of a mixed class) the reference's bits: Canons on
    // the deferred operands of the propagating ops it is made of.
    void fix(std::uint32_t v) {
        if (!fixed_.insert(v).second) return;
        // Canons are appended to the instructions: no reference into them is
        // held across one.
        const auto op = f_.insts[v].op;
        if (op == Op::Phi) {
            const auto block = places_.block[v];
            const auto count = f_.insts[v].count;
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto u = f_.extras[f_.insts[v].extra + i];
                const auto c = cls_[u];
                if (deferred(c)) {
                    const auto canon = canon_at_end(u, nan_format(c), f_.blocks[block].preds[i]);
                    f_.extras[f_.insts[v].extra + i] = canon;
                } else if (mixed(c)) {
                    fix(u);
                }
            }
            return;
        }
        if (op != Op::Lift) return;
        unsigned position = 0;
        if (const auto x = abs_operand(f_, f_.insts[v]); x != none) {
            position = f_.insts[v].operands[0] == x ? 0 : 1;
        } else if (f_.insts[v].lifted.op != lift::Opcode::FloatNegate) {
            return;
        }
        const auto u = f_.insts[v].operands[position];
        const auto c = cls_[u];
        if (deferred(c)) {
            const auto canon = canon_before(u, nan_format(c), places_.block[v], v);
            f_.insts[v].operands[position] = canon;
        } else if (mixed(c)) {
            fix(u);
        }
    }

    // A Canon of `v` just before the instruction `before` of `block` (or
    // before its terminator when `before` is none), shared with an earlier
    // one of the same block.
    std::uint32_t canon_before(std::uint32_t v, unsigned format, std::uint32_t block, std::uint32_t before) {
        const auto& insts = f_.blocks[block].insts;
        const auto position = before == none ? insts.size() - 1 : places_.position[before];
        const auto key = std::make_pair(v, block);
        if (const auto found = made_.find(key); found != made_.end() && found->second.second <= position) {
            return found->second.first;
        }
        const auto& at = f_.insts[insts[position]];
        Inst canon;
        canon.op = Op::Canon;
        canon.type = format == 32 ? ValueType{Type::F32, 32} : ValueType{Type::F64, 64};
        canon.operands[0] = v;
        canon.origin_block = at.origin_block;
        canon.origin_function = at.origin_function;
        canon.origin_seq = at.origin_seq;
        const auto id = static_cast<std::uint32_t>(f_.insts.size());
        f_.insts.push_back(canon);
        cls_.push_back(clean_class(format));
        insertions_.push_back({block, insts[position], id});
        if (const auto found = made_.find(key); found == made_.end() || found->second.second > position) {
            made_[key] = {id, position};
        }
        return id;
    }

    std::uint32_t canon_at_end(std::uint32_t v, unsigned format, std::uint32_t block) {
        return canon_before(v, format, block, none);
    }

    // Puts the Canons made this round into their blocks.
    void apply() {
        if (insertions_.empty()) return;
        std::map<std::uint32_t, std::vector<std::pair<std::uint32_t, std::uint32_t>>> by_block;
        for (const auto& insertion : insertions_) by_block[insertion.block].emplace_back(insertion.before, insertion.canon);
        for (auto& [block, list] : by_block) {
            auto& insts = f_.blocks[block].insts;
            std::vector<std::uint32_t> result;
            for (const auto id : insts) {
                for (const auto& [before, canon] : list) {
                    if (before == id) result.push_back(canon);
                }
                result.push_back(id);
            }
            insts = std::move(result);
        }
    }

    struct Insertion {
        std::uint32_t block;
        std::uint32_t before;
        std::uint32_t canon;
    };

    Function& f_;
    std::vector<NanClass> cls_;
    Places places_{Function{}};
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<std::uint32_t, std::size_t>> made_;
    std::set<std::uint32_t> fixed_;
    std::vector<Insertion> insertions_;
};

} // namespace

bool exact(NanClass c) { return clean(c) || c == NanClass::Exact; }

unsigned nan_format(NanClass c) {
    switch (c) {
    case NanClass::Deferred32:
    case NanClass::Mixed32: return 32;
    case NanClass::Deferred64:
    case NanClass::Mixed64: return 64;
    default: return 0;
    }
}

std::uint32_t abs_operand(const Function& f, const Inst& in) {
    if (in.op != Op::Lift || in.lifted.op != lift::Opcode::BitAnd) return none;
    std::uint64_t magnitude = 0;
    if (in.type.type == Type::F64) magnitude = 0x7FFFFFFFFFFFFFFFULL;
    else if (in.type.type == Type::F32) magnitude = 0x7FFFFFFFULL;
    else return none;
    for (int k = 0; k < 2; ++k) {
        const auto x = in.operands[k];
        const auto m = in.operands[1 - k];
        if (x == none || m == none || x >= f.insts.size() || m >= f.insts.size()) continue;
        if (f.insts[m].op == Op::Const && f.insts[m].imm == magnitude && f.insts[x].type == in.type) return x;
    }
    return none;
}

std::vector<unsigned> value_positions(const Inst& in) {
    std::vector<unsigned> positions;
    for_each_operand_position(in, [&](unsigned position) { positions.push_back(position); });
    return positions;
}

BitsUse bits_use(const Function& f, const Inst& in, unsigned position) {
    const auto insensitive = [](unsigned format) {
        return format == 32 ? BitsUse::Insensitive32 : format == 64 ? BitsUse::Insensitive64 : BitsUse::Sensitive;
    };
    switch (in.op) {
    case Op::Phi: return BitsUse::Propagating;
    // A heap write's value (StoreField operands[1], StoreElement
    // operands[3], StoreGlobal operands[0], Widen and Append operands[2]),
    // and the helper arguments of AllocObject and Append (which hold the
    // appended value), leave the float domain; so do the choices of a
    // Select, whose result is taken as the reference's bits.
    case Op::StoreField:
    case Op::StoreElement:
    case Op::StoreGlobal:
    case Op::Widen:
    case Op::AllocObject:
    case Op::Append:
    case Op::Select: return BitsUse::Sensitive;
    case Op::Canon: return insensitive(in.type.width);
    case Op::Sqrt: return BitsUse::Insensitive64;
    case Op::Lift: {
        if (position < operand_slots) {
            if (const auto x = abs_operand(f, in); x != none && in.operands[position] == x) return BitsUse::Propagating;
        }
        const auto& l = in.lifted;
        switch (l.op) {
        case lift::Opcode::FloatAdd:
        case lift::Opcode::FloatSubtract:
        case lift::Opcode::FloatMultiply:
        case lift::Opcode::FloatDivide:
        case lift::Opcode::FloatEqual:
        case lift::Opcode::FloatNotEqual:
        case lift::Opcode::FloatLess:
        case lift::Opcode::FloatLessEqual:
        case lift::Opcode::FloatGreater:
        case lift::Opcode::FloatGreaterEqual:
        case lift::Opcode::FloatToInteger:
        case lift::Opcode::FloatConvert: return insensitive(format_bits(l.format));
        case lift::Opcode::FloatNegate: return BitsUse::Propagating;
        case lift::Opcode::ValueHash:
            return l.format == lift::FloatType::None ? BitsUse::Sensitive : insensitive(format_bits(l.format));
        default: return BitsUse::Sensitive;
        }
    }
    default: return BitsUse::Sensitive;
    }
}

std::vector<std::vector<std::uint32_t>> clean_slots_needed(const Function& f, const std::vector<NanClass>& classes) {
    // The phis and abs ops that read each value as what they propagate.
    std::vector<std::vector<std::uint32_t>> users(f.insts.size());
    const Cfg cfg(f);
    for (const auto b : cfg.rpo) {
        for (const auto id : f.blocks[b].insts) {
            const auto& in = f.insts[id];
            if (in.op == Op::Phi) {
                for (std::uint32_t i = 0; i < in.count; ++i) users[f.extras[in.extra + i]].push_back(id);
            } else if (const auto x = abs_operand(f, in); x != none) {
                users[x].push_back(id);
            }
        }
    }
    std::vector<std::vector<std::uint32_t>> needed(f.entries.size());
    for (std::uint32_t e = 0; e < f.entries.size(); ++e) {
        const auto block = f.entries[e].ir_block;
        if (block >= f.blocks.size()) continue;
        for (const auto id : f.blocks[block].insts) {
            const auto& in = f.insts[id];
            if (in.op != Op::EntryLoad || (in.type.type != Type::F32 && in.type.type != Type::F64)) continue;
            std::vector<std::uint8_t> seen(f.insts.size(), 0);
            std::vector<std::uint32_t> work{id};
            seen[id] = 1;
            bool reaches = false;
            while (!work.empty() && !reaches) {
                const auto v = work.back();
                work.pop_back();
                for (const auto user : users[v]) {
                    if (seen[user]) continue;
                    seen[user] = 1;
                    reaches = reaches || deferred(classes[user]);
                    work.push_back(user);
                }
            }
            const auto slot = static_cast<std::uint32_t>(in.imm);
            if (reaches && std::find(needed[e].begin(), needed[e].end(), slot) == needed[e].end()) {
                needed[e].push_back(slot);
            }
        }
        std::sort(needed[e].begin(), needed[e].end());
    }
    return needed;
}

std::vector<NanClass> nan_classes(const Function& f) {
    std::vector<std::uint8_t> cls(f.insts.size(), top);
    const Cfg cfg(f);
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto b : cfg.rpo) {
            for (const auto id : f.blocks[b].insts) {
                const auto c = transfer(f, f.insts[id], cls);
                if (c != cls[id]) {
                    cls[id] = c;
                    changed = true;
                }
            }
        }
    }
    std::vector<NanClass> result(f.insts.size(), NanClass::Exact);
    for (std::size_t v = 0; v < cls.size(); ++v) {
        if (cls[v] != top) result[v] = static_cast<NanClass>(cls[v]);
    }
    return result;
}

} // namespace detail

void place_canons(const nvr::Image& image, Function& function, bool verify_after) {
    detail::Placement(function).run();
    if (!verify_after) return;
    const auto violations = verify(image, function);
    if (violations.empty()) return;
    std::fprintf(stderr, "neura: NEURA_TIER3_VERIFY: the IR of function %u is not well formed after placing Canons:\n",
                 function.function);
    for (const auto& violation : violations) std::fprintf(stderr, "  %s\n", violation.c_str());
    std::fprintf(stderr, "%s", describe(function).c_str());
    std::abort();
}

} // namespace neura::nvm::tier3
