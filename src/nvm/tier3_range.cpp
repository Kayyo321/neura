#include "tier3_passes.hpp"

#include "lift/evaluate.hpp"
#include "util/int128.hpp"

#include <algorithm>
#include <optional>

// Range analysis and check elimination (docs/runtime/NVM.md, "Execution
// tiers"). Every Int value gets an interval, as an unsigned register value
// and as a signed value of its declared width, computed in 128-bit integers
// so that no bound can overflow. A check is removed only when the intervals
// of its operands, at the width and signedness of the instruction, make
// lift::evaluate report no failure for every value in them: the proof is of
// `failed` as evaluate defines it, not of the language rule it implements.
namespace neura::nvm::tier3::detail {
namespace {

using i128 = neura::Int128;
using u128 = neura::UInt128;

struct Iv {
    i128 lo = 0;
    i128 hi = -1;
    bool empty() const { return lo > hi; }
    bool operator==(const Iv&) const = default;
};

// A set of values of a `width`-bit integer; `bottom` is the empty set (a
// value that never exists).
struct Range {
    bool bottom = true;
    Iv u;
    Iv s;
    bool operator==(const Range&) const = default;
};

i128 pow2(unsigned w) { return i128{1} << w; }
i128 umax(unsigned w) { return pow2(w) - 1; }
i128 smin(unsigned w) { return -pow2(w - 1); }
i128 smax(unsigned w) { return pow2(w - 1) - 1; }

Range top(unsigned w) {
    Range r;
    r.bottom = false;
    r.u = {0, umax(w)};
    r.s = {smin(w), smax(w)};
    return r;
}

Range bottom_range() { return {}; }

// Narrows each view to the type and to what the other says.
void reduce(Range& r, unsigned w) {
    if (r.bottom) return;
    r.u.lo = std::max<i128>(r.u.lo, 0);
    r.u.hi = std::min(r.u.hi, umax(w));
    r.s.lo = std::max(r.s.lo, smin(w));
    r.s.hi = std::min(r.s.hi, smax(w));
    for (int round = 0; round < 2; ++round) {
        if (r.s.lo >= 0) {
            r.u.lo = std::max(r.u.lo, r.s.lo);
            r.u.hi = std::min(r.u.hi, r.s.hi);
        } else if (r.s.hi < 0) {
            r.u.lo = std::max(r.u.lo, r.s.lo + pow2(w));
            r.u.hi = std::min(r.u.hi, r.s.hi + pow2(w));
        }
        if (r.u.hi <= smax(w)) {
            r.s.lo = std::max(r.s.lo, r.u.lo);
            r.s.hi = std::min(r.s.hi, r.u.hi);
        } else if (r.u.lo > smax(w)) {
            r.s.lo = std::max(r.s.lo, r.u.lo - pow2(w));
            r.s.hi = std::min(r.s.hi, r.u.hi - pow2(w));
        }
    }
    if (r.u.empty() || r.s.empty()) r = bottom_range();
}

Range from_u(unsigned w, i128 lo, i128 hi) {
    Range r = top(w);
    r.u = {lo, hi};
    reduce(r, w);
    return r;
}

Range from_s(unsigned w, i128 lo, i128 hi) {
    Range r = top(w);
    r.s = {lo, hi};
    reduce(r, w);
    return r;
}

Range exact(unsigned w, std::uint64_t bits) {
    if (static_cast<i128>(bits) > umax(w)) return top(w);
    return from_u(w, bits, bits);
}

Range meet(const Range& a, const Range& b, unsigned w) {
    if (a.bottom || b.bottom) return bottom_range();
    Range r;
    r.bottom = false;
    r.u = {std::max(a.u.lo, b.u.lo), std::min(a.u.hi, b.u.hi)};
    r.s = {std::max(a.s.lo, b.s.lo), std::min(a.s.hi, b.s.hi)};
    reduce(r, w);
    return r;
}

Range join(const Range& a, const Range& b) {
    if (a.bottom) return b;
    if (b.bottom) return a;
    Range r;
    r.bottom = false;
    r.u = {std::min(a.u.lo, b.u.lo), std::max(a.u.hi, b.u.hi)};
    r.s = {std::min(a.s.lo, b.s.lo), std::max(a.s.hi, b.s.hi)};
    return r;
}

// A bound that moved is taken to the type's end.
Range widen(const Range& old, const Range& next, unsigned w) {
    if (old.bottom) return next;
    Range r = next;
    if (next.u.lo < old.u.lo) r.u.lo = 0;
    if (next.u.hi > old.u.hi) r.u.hi = umax(w);
    if (next.s.lo < old.s.lo) r.s.lo = smin(w);
    if (next.s.hi > old.s.hi) r.s.hi = smax(w);
    return r;
}

// The register values of `r` as an operand of an instruction of width `w`,
// when every one fits in `w` bits.
std::optional<Iv> unsigned_view(const Range& r, unsigned w) {
    if (r.bottom || r.u.hi > umax(w)) return std::nullopt;
    return r.u;
}

// The signed values (sign-extended from `w` bits) of an operand of declared
// width `declared`, when every register value fits in `w` bits.
std::optional<Iv> signed_view(const Range& r, unsigned declared, unsigned w) {
    if (r.bottom || r.u.hi > umax(w)) return std::nullopt;
    if (declared == w) return r.s;
    if (r.u.hi <= smax(w)) return r.u;
    if (r.u.lo > smax(w)) return Iv{r.u.lo - pow2(w), r.u.hi - pow2(w)};
    return Iv{smin(w), smax(w)};
}

bool singleton(const Iv& i) { return i.lo == i.hi; }

// The range of a value declared `from` bits wide as a value declared `to` bits
// wide (a phi joining values of two widths, a mask that drops nothing): the
// register values stay, and the signed view is the signed view of those
// registers at the new width.
Range rewidth(const Range& r, unsigned from, unsigned to) {
    if (r.bottom || from == to) return r;
    if (r.u.hi > umax(to)) return top(to);
    return from_u(to, r.u.lo, r.u.hi);
}

// Which comparison a fact states between its operands.
enum class Rel { Lt, Le, Gt, Ge, Eq, Ne };

Rel negate(Rel r) {
    switch (r) {
    case Rel::Lt: return Rel::Ge;
    case Rel::Le: return Rel::Gt;
    case Rel::Gt: return Rel::Le;
    case Rel::Ge: return Rel::Lt;
    case Rel::Eq: return Rel::Ne;
    case Rel::Ne: return Rel::Eq;
    }
    return r;
}

Rel flip(Rel r) {
    switch (r) {
    case Rel::Lt: return Rel::Gt;
    case Rel::Le: return Rel::Ge;
    case Rel::Gt: return Rel::Lt;
    case Rel::Ge: return Rel::Le;
    default: return r;
    }
}

void constrain(Iv& i, Rel rel, const Iv& o) {
    switch (rel) {
    case Rel::Lt: i.hi = std::min(i.hi, o.hi - 1); break;
    case Rel::Le: i.hi = std::min(i.hi, o.hi); break;
    case Rel::Gt: i.lo = std::max(i.lo, o.lo + 1); break;
    case Rel::Ge: i.lo = std::max(i.lo, o.lo); break;
    case Rel::Eq:
        i.lo = std::max(i.lo, o.lo);
        i.hi = std::min(i.hi, o.hi);
        break;
    case Rel::Ne:
        if (singleton(o)) {
            if (i.lo == o.lo) ++i.lo;
            if (i.hi == o.lo) --i.hi;
        }
        break;
    }
}

bool is_compare(const Function& f, std::uint32_t v) {
    if (v == none || f.insts[v].op != Op::Lift) return false;
    switch (f.insts[v].lifted.op) {
    case lift::Opcode::Equal:
    case lift::Opcode::NotEqual:
    case lift::Opcode::Less:
    case lift::Opcode::LessEqual:
    case lift::Opcode::Greater:
    case lift::Opcode::GreaterEqual: return true;
    default: return false;
    }
}

// The relation the comparison `cmp` states on its operands when its value is
// `sense`, and the order and width it compares in. A NotEqual that exits an
// induction loop and names its guard states a strict order on its true edge
// (see Inst::guard).
bool relation_of(const Function& f, std::uint32_t cmp, bool sense, Rel& rel, bool& is_signed, unsigned& width) {
    if (!is_compare(f, cmp)) return false;
    const auto& c = f.insts[cmp];
    switch (c.lifted.op) {
    case lift::Opcode::Equal: rel = Rel::Eq; break;
    case lift::Opcode::NotEqual: rel = Rel::Ne; break;
    case lift::Opcode::Less: rel = Rel::Lt; break;
    case lift::Opcode::LessEqual: rel = Rel::Le; break;
    case lift::Opcode::Greater: rel = Rel::Gt; break;
    default: rel = Rel::Ge; break;
    }
    is_signed = c.lifted.is_signed;
    width = c.lifted.width;
    if (!sense) rel = negate(rel);
    if (c.lifted.op == lift::Opcode::NotEqual && sense && c.guard != none) {
        const bool second = (c.imm & 1U) != 0;
        const bool down = (c.imm & 2U) != 0;
        is_signed = (c.imm & 4U) != 0;
        rel = down ? (second ? Rel::Lt : Rel::Gt) : (second ? Rel::Gt : Rel::Lt);
    }
    return true;
}

unsigned declared_width(const Function& f, std::uint32_t v) {
    const auto& t = f.insts[v].type;
    return t.type == Type::Int ? t.width : 64;
}

bool tracked(const Inst& in) { return in.type.type == Type::Int; }

// The result of the lifted instruction `l`, whose result is `rw` bits wide,
// on operands in the ranges x and y (declared dx and dy bits wide; y absent
// when `has_y` is false), over the executions where its check passes.
Range transfer_lift(const lift::Instruction& l, unsigned rw, const Range& x, const Range& y, unsigned dx, unsigned dy,
                    bool has_y) {
    using lift::Opcode;
    const unsigned w = l.width;
    const bool sgn = l.is_signed;
    const auto declared = [&](unsigned i) { return i == 0 ? dx : dy; };
    const auto sv = [&](const Range& r, unsigned i) { return signed_view(r, declared(i), w); };
    const auto uv = [&](const Range& r) { return unsigned_view(r, w); };
    const auto signed_result = [&](i128 lo, i128 hi) { return from_s(rw, std::max(lo, smin(w)), std::min(hi, smax(w))); };
    const auto unsigned_result = [&](i128 lo, i128 hi) { return from_u(rw, std::max<i128>(lo, 0), std::min(hi, umax(w))); };
        switch (l.op) {
        case Opcode::Add:
            if (sgn) {
                const auto a = sv(x, 0);
                const auto b = sv(y, 1);
                if (a && b) return signed_result(a->lo + b->lo, a->hi + b->hi);
            } else {
                // Below 64 bits the failure is a 64-bit sum above 2^w: a sum that
                // wraps in 64 bits (registers wider than the width) can pass.
                if (w < 64 && x.u.hi + y.u.hi >= pow2(64)) return top(rw);
                return unsigned_result(x.u.lo + y.u.lo, x.u.hi + y.u.hi);
            }
            return top(rw);
        case Opcode::AddWrapping: {
            const auto a = uv(x);
            const auto b = uv(y);
            if (!a || !b) return top(rw);
            const Iv sum{a->lo + b->lo, a->hi + b->hi};
            if (sum.hi <= umax(w)) return from_u(rw, sum.lo, sum.hi);
            if (sum.lo > umax(w)) return from_u(rw, sum.lo - pow2(w), sum.hi - pow2(w));
            return top(rw);
        }
        case Opcode::Subtract:
            if (sgn) {
                const auto a = sv(x, 0);
                const auto b = sv(y, 1);
                if (a && b) return signed_result(a->lo - b->hi, a->hi - b->lo);
            } else {
                // (a - b) & m: the difference itself only when it is below 2^w.
                const Iv d{std::max<i128>(x.u.lo - y.u.hi, 0), x.u.hi - y.u.lo};
                if (d.empty()) return bottom_range();
                return d.hi <= umax(w) ? from_u(rw, d.lo, d.hi) : top(rw);
            }
            return top(rw);
        case Opcode::SubtractWrapping: {
            const auto a = uv(x);
            const auto b = uv(y);
            if (!a || !b) return top(rw);
            const Iv d{a->lo - b->hi, a->hi - b->lo};
            if (d.lo >= 0) return from_u(rw, d.lo, d.hi);
            if (d.hi < 0) return from_u(rw, d.lo + pow2(w), d.hi + pow2(w));
            return top(rw);
        }
        case Opcode::Negate:
            if (sgn) {
                const auto a = sv(x, 0);
                if (a) return signed_result(-a->hi, -a->lo);
                return top(rw);
            }
            return x.u.lo == 0 ? exact(rw, 0) : top(rw);
        case Opcode::Multiply:
            if (sgn) {
                const auto a = sv(x, 0);
                const auto b = sv(y, 1);
                if (!a || !b) return top(rw);
                const i128 corners[4] = {a->lo * b->lo, a->lo * b->hi, a->hi * b->lo, a->hi * b->hi};
                return signed_result(*std::min_element(corners, corners + 4), *std::max_element(corners, corners + 4));
            } else {
                const u128 lo = static_cast<u128>(x.u.lo) * static_cast<u128>(y.u.lo);
                const u128 hi = static_cast<u128>(x.u.hi) * static_cast<u128>(y.u.hi);
                const u128 limit = static_cast<u128>(umax(w));
                if (hi <= limit) return unsigned_result(static_cast<i128>(lo), static_cast<i128>(hi));
                // The failure is a product above 2^w in 64-bit arithmetic: no
                // wrap for 32 bits and fewer with operands in the width, and
                // none for 64; between, a wrapped product can pass.
                const bool exact_failure = w == 64 || (w <= 32 && x.u.hi <= umax(w) && y.u.hi <= umax(w));
                if (!exact_failure) return top(rw);
                return unsigned_result(lo > limit ? umax(w) + 1 : static_cast<i128>(lo), umax(w));
            }
        case Opcode::Divide:
            if (sgn) {
                const auto a = sv(x, 0);
                const auto b = sv(y, 1);
                if (!a || !b) return top(rw);
                if (a->lo >= 0 && b->lo > 0) return signed_result(a->lo / b->hi, a->hi / b->lo);
                i128 magnitude = std::max(a->hi < 0 ? -a->hi : a->hi, a->lo < 0 ? -a->lo : a->lo);
                return signed_result(-magnitude, magnitude);
            } else {
                if (y.u.hi < 1) return top(rw);
                const auto low = std::max<i128>(y.u.lo, 1);
                return from_u(rw, x.u.lo / y.u.hi, x.u.hi / low);
            }
        case Opcode::Remainder:
            if (sgn) {
                const auto a = sv(x, 0);
                const auto b = sv(y, 1);
                if (!a || !b) return top(rw);
                const i128 bound = std::max(b->hi < 0 ? -b->hi : b->hi, b->lo < 0 ? -b->lo : b->lo);
                if (bound == 0) return top(rw);
                const i128 below = bound - 1;
                const i128 up = std::min(a->hi, below);
                const i128 down = std::min(a->lo < 0 ? -a->lo : a->lo, below);
                if (a->lo >= 0) return signed_result(0, up);
                if (a->hi <= 0) return signed_result(-down, 0);
                return signed_result(-down, up);
            } else {
                if (y.u.hi < 1) return top(rw);
                return from_u(rw, 0, std::min(x.u.hi, y.u.hi - 1));
            }
        case Opcode::Equal:
        case Opcode::NotEqual:
        case Opcode::Less:
        case Opcode::LessEqual:
        case Opcode::Greater:
        case Opcode::GreaterEqual: {
            Iv a = x.u;
            Iv b = y.u;
            const bool ordered = l.op != Opcode::Equal && l.op != Opcode::NotEqual;
            if (ordered && sgn) {
                const auto sa = sv(x, 0);
                const auto sb = sv(y, 1);
                if (!sa || !sb) return top(1);
                a = *sa;
                b = *sb;
            }
            bool always = false;
            bool never = false;
            switch (l.op) {
            case Opcode::Less: always = a.hi < b.lo; never = a.lo >= b.hi; break;
            case Opcode::LessEqual: always = a.hi <= b.lo; never = a.lo > b.hi; break;
            case Opcode::Greater: always = a.lo > b.hi; never = a.hi <= b.lo; break;
            case Opcode::GreaterEqual: always = a.lo >= b.hi; never = a.hi < b.lo; break;
            case Opcode::Equal:
                always = singleton(a) && singleton(b) && a.lo == b.lo;
                never = a.hi < b.lo || a.lo > b.hi;
                break;
            default:
                never = singleton(a) && singleton(b) && a.lo == b.lo;
                always = a.hi < b.lo || a.lo > b.hi;
                break;
            }
            if (always) return exact(1, 1);
            if (never) return exact(1, 0);
            return top(1);
        }
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Not:
            if (singleton(x.u) && (!has_y || singleton(y.u))) {
                const auto a = static_cast<std::uint64_t>(x.u.lo);
                const auto b = static_cast<std::uint64_t>(y.u.lo);
                const auto r = l.op == Opcode::And ? (a & b & 1U) : l.op == Opcode::Or ? ((a | b) & 1U) : ((a ^ 1U) & 1U);
                return exact(1, r);
            }
            return top(1);
        case Opcode::BitAnd:
            return from_u(rw, 0, std::min(x.u.hi, y.u.hi));
        case Opcode::BitOr:
        case Opcode::BitXor: {
            const auto high = std::max(x.u.hi, y.u.hi);
            unsigned bits = 0;
            while (bits < 64 && (high >> bits) != 0) ++bits;
            return from_u(rw, l.op == Opcode::BitOr ? std::max(x.u.lo, y.u.lo) : 0, bits == 0 ? 0 : umax(bits));
        }
        case Opcode::BitNot: {
            const auto a = uv(x);
            if (!a) return from_u(rw, 0, umax(w));
            return from_u(rw, umax(w) - a->hi, umax(w) - a->lo);
        }
        case Opcode::ShiftLeft:
        case Opcode::ShiftRight: {
            // On the executions where the count is below the width.
            const Iv count{y.u.lo, std::min<i128>(y.u.hi, w - 1)};
            if (count.empty()) return top(rw);
            if (l.op == Opcode::ShiftLeft) {
                const auto a = uv(x);
                if (!a) return top(rw);
                const Iv shifted{a->lo << static_cast<int>(count.lo), a->hi << static_cast<int>(count.hi)};
                return shifted.hi <= umax(w) ? from_u(rw, shifted.lo, shifted.hi) : top(rw);
            }
            if (!sgn) {
                const auto a = uv(x);
                if (!a) return top(rw);
                return from_u(rw, a->lo >> static_cast<int>(count.hi), a->hi >> static_cast<int>(count.lo));
            }
            const auto a = sv(x, 0);
            if (!a) return top(rw);
            const i128 corners[4] = {a->lo >> static_cast<int>(count.lo), a->lo >> static_cast<int>(count.hi),
                                     a->hi >> static_cast<int>(count.lo), a->hi >> static_cast<int>(count.hi)};
            return from_s(rw, *std::min_element(corners, corners + 4), *std::max_element(corners, corners + 4));
        }
        case Opcode::ValueHash: return from_u(rw, 0, 0xffffffffLL);
        case Opcode::Index:
            if (!sgn) return rewidth(x, declared(0), rw);
            if (x.u.hi > umax(w)) return top(rw);
            return from_u(rw, x.u.lo, std::min(x.u.hi, smax(w)));
        case Opcode::ConvertInteger: {
            const unsigned to = l.to_width;
            Iv source;
            if (sgn) {
                const auto a = sv(x, 0);
                if (!a) return top(rw);
                source = *a;
            } else {
                source = x.u;
            }
            const Iv target = l.to_signed ? Iv{smin(to), smax(to)} : Iv{0, umax(to)};
            source.lo = std::max(source.lo, target.lo);
            source.hi = std::min(source.hi, target.hi);
            if (source.empty()) return top(rw);
            return l.to_signed ? from_s(rw, source.lo, source.hi) : from_u(rw, source.lo, source.hi);
        }
        default: return top(rw);
        }
}

// Narrows `r` (a value declared `vw` bits wide) by the fact `value rel other`,
// where the comparison orders `w`-bit values as signed or unsigned numbers.
void refine_by(Rel rel, bool is_signed, unsigned w, Range& r, const Range& o, unsigned vw, unsigned ow) {
    if (!is_signed || rel == Rel::Eq || rel == Rel::Ne) {
        constrain(r.u, rel, o.u);
    } else if (vw == w) {
        const auto view = signed_view(o, ow, w);
        if (!view) return;
        constrain(r.s, rel, *view);
    } else if (vw < w) {
        const auto view = signed_view(o, ow, w);
        if (!view) return;
        constrain(r.u, rel, *view);
    } else {
        return;
    }
    reduce(r, vw);
}

// --- The analysis ---------------------------------------------------------------------------------

// What a branch edge or a guard states, as a chain from the block that holds
// it up through its dominators: a fact about the operands of a comparison.
struct Node {
    std::uint32_t parent = none;
    std::uint32_t cmp = none;
    bool sense = true;
};

struct UpperBound {
    std::uint32_t value;
    bool inclusive;
};

// Index passes through the full input word on success. A signed Index
// also rejects negative values before the resulting word can be used.
std::uint32_t index_input(const Function& f, std::uint32_t index) {
    const auto& in = f.insts[index];
    const auto input = in.operands[0];
    if (in.op != Op::Lift || in.lifted.op != lift::Opcode::Index || input == none ||
        in.lifted.width != declared_width(f, input) || in.type.width < in.lifted.width) return index;
    return input;
}

class Analysis {
public:
    Analysis(const Function& f, const Cfg& cfg) : f_(f), cfg_(cfg) {}

    // False when the intervals did not settle: none of them may be used.
    bool run() {
        if (f_.insts.size() > 60000) return false;
        build_facts();
        base_.assign(f_.insts.size(), bottom_range());
        grown_.assign(f_.insts.size(), 0);
        for (unsigned round = 0; round < 80; ++round) {
            if (!sweep()) return true;
        }
        return false;
    }

    Range base(std::uint32_t v) const {
        const auto& in = f_.insts[v];
        if (!tracked(in)) return top(64);
        return base_[v].bottom ? top(in.type.width) : base_[v];
    }

    // The range of `v` where the facts of `node` hold.
    Range refined(std::uint32_t v, std::uint32_t node) const {
        const auto& in = f_.insts[v];
        if (!tracked(in)) return top(64);
        Range r = base_[v];
        if (r.bottom) return top(in.type.width);
        for (auto n = node; n != none && !r.bottom; n = nodes_[n].parent) apply(nodes_[n], v, r);
        // A fact that empties the range says the point is never reached; the
        // caller then learns nothing.
        return r.bottom ? base(v) : r;
    }

    std::uint32_t start_node(std::uint32_t block) const { return fin_[block]; }
    std::uint32_t after(std::uint32_t node, std::uint32_t inst) const {
        const auto found = guard_node_.find(inst);
        return found == guard_node_.end() ? node : found->second;
    }

    // Whether the facts of `node` say index < length, for a bounds check of
    // `index` against `length`, a count word: a comparison that holds there
    // of the index (or the value an Index operation checked and passed on)
    // with that count (the same value, or a conversion of it, which passed
    // only if it kept the count), in an
    // order that means the unsigned order of the whole values. Unsigned:
    // both operands fit the compared width. Signed: besides, the index is
    // not negative, so the bound is above it and not negative either.
    bool index_below(std::uint32_t index, std::uint32_t length, std::uint32_t node) const {
        const auto count_of = [&](std::uint32_t v) {
            while (f_.insts[v].op == Op::Lift && f_.insts[v].lifted.op == lift::Opcode::ConvertInteger &&
                   f_.insts[v].operands[0] != none) {
                v = f_.insts[v].operands[0];
            }
            return v;
        };
        // The same count word read once: two reads of it are one value only
        // where value numbering made them one (an append between them, inline
        // or not, keeps them apart).
        const auto same_length = [&](std::uint32_t v) { return count_of(v) == count_of(length); };
        const auto& checked = f_.insts[index];
        const bool via_index = checked.op == Op::Lift && checked.lifted.op == lift::Opcode::Index;
        const auto same_index = [&](std::uint32_t v) { return v == index || (via_index && v == checked.operands[0]); };
        for (auto n = node; n != none; n = nodes_[n].parent) {
            Rel rel{};
            bool is_signed = false;
            unsigned w = 0;
            if (!relation_of(f_, nodes_[n].cmp, nodes_[n].sense, rel, is_signed, w)) continue;
            const auto& c = f_.insts[nodes_[n].cmp];
            std::uint32_t i = none;
            std::uint32_t bound = none;
            if (rel == Rel::Lt && c.operands[0] != none && same_index(c.operands[0])) {
                i = c.operands[0];
                bound = c.operands[1];
            } else if (rel == Rel::Gt && c.operands[1] != none && same_index(c.operands[1])) {
                i = c.operands[1];
                bound = c.operands[0];
            }
            if (i == none || bound == none || !same_length(bound)) continue;
            if (!tracked(f_.insts[i]) || !tracked(f_.insts[bound])) continue;
            if (declared_width(f_, i) > w || declared_width(f_, bound) > w) continue;
            if (is_signed && declared_width(f_, i) == w) {
                const auto r = refined(i, node);
                if (r.bottom || r.s.lo < 0) continue;
            }
            return true;
        }
        return false;
    }

    // Upper bounds on the entire unsigned index word at this use. A signed
    // relation has the same order when the index is nonnegative. On a path
    // taking that relation, its bound then has to be nonnegative too.
    std::vector<UpperBound> upper_bounds(std::uint32_t index, std::uint32_t node) const {
        const auto input = index_input(f_, index);
        const auto& checked = f_.insts[index];
        std::vector<UpperBound> bounds;
        for (auto n = node; n != none; n = nodes_[n].parent) {
            Rel rel{};
            bool is_signed = false;
            unsigned width = 0;
            if (!relation_of(f_, nodes_[n].cmp, nodes_[n].sense, rel, is_signed, width)) continue;
            const auto& compare = f_.insts[nodes_[n].cmp];
            auto value = compare.operands[0];
            auto bound = compare.operands[1];
            if (rel == Rel::Gt || rel == Rel::Ge) {
                std::swap(value, bound);
                rel = flip(rel);
            }
            if ((rel != Rel::Lt && rel != Rel::Le) || value == none || bound == none ||
                (value != index && value != input) || !tracked(f_.insts[value]) || !tracked(f_.insts[bound])) continue;
            const auto value_width = declared_width(f_, value);
            if (value_width > width || declared_width(f_, bound) > width) continue;
            const bool checked_nonnegative = input != index && value == input && checked.lifted.is_signed &&
                                             (checked.flags & Checked) != 0;
            if (is_signed && value_width == width && !checked_nonnegative && refined(value, node).s.lo < 0) continue;
            bounds.push_back({bound, rel == Rel::Le});
        }
        return bounds;
    }

private:
    std::uint32_t push(std::uint32_t parent, std::uint32_t cmp, bool sense) {
        nodes_.push_back({parent, cmp, sense});
        return static_cast<std::uint32_t>(nodes_.size() - 1);
    }

    void build_facts() {
        const auto n = f_.blocks.size();
        fin_.assign(n, none);
        fout_.assign(n, none);
        for (const auto b : cfg_.rpo) {
            const auto& block = f_.blocks[b];
            const auto parent = cfg_.idom[b];
            std::uint32_t node = parent == cfg_.root ? none : fout_[parent];
            if (block.preds.size() == 1) {
                const auto x = block.preds[0];
                const auto& xb = f_.blocks[x];
                if (cfg_.reachable(x) && xb.succs.size() == 2 && xb.succs[0] != xb.succs[1] && !xb.insts.empty()) {
                    const auto& last = f_.insts[xb.insts.back()];
                    if (last.op == Op::Branch && is_compare(f_, last.operands[0])) {
                        node = push(fout_[x], last.operands[0], xb.succs[0] == b);
                    }
                }
            }
            fin_[b] = node;
            for (const auto id : block.insts) {
                const auto& in = f_.insts[id];
                if (in.op == Op::Guard && is_compare(f_, in.operands[0])) {
                    node = push(node, in.operands[0], true);
                    guard_node_[id] = node;
                }
            }
            fout_[b] = node;
        }
    }

    void apply(const Node& fact, std::uint32_t v, Range& r) const {
        Rel rel{};
        bool is_signed = false;
        unsigned w = 0;
        if (!relation_of(f_, fact.cmp, fact.sense, rel, is_signed, w)) return;
        const auto& c = f_.insts[fact.cmp];
        const auto x = c.operands[0];
        const auto y = c.operands[1];
        if (x == y || (v != x && v != y)) return;
        const auto other = v == x ? y : x;
        if (!tracked(f_.insts[other]) || !tracked(f_.insts[v])) return;
        if (v == y) rel = flip(rel);
        const auto& o = base_[other];
        if (o.bottom) return;
        refine_by(rel, is_signed, w, r, o, declared_width(f_, v), declared_width(f_, other));
    }

    // One pass over every value; whether any interval changed.
    bool sweep() {
        bool changed = false;
        for (const auto b : cfg_.rpo) {
            auto node = fin_[b];
            for (const auto id : f_.blocks[b].insts) {
                const auto& in = f_.insts[id];
                if (in.op == Op::Guard) {
                    node = after(node, id);
                    continue;
                }
                if (!tracked(in)) continue;
                Range next = transfer(id, in, node, b);
                const auto w = in.type.width;
                if (next.bottom) next = base_[id];
                Range joined = join(base_[id], next);
                if (in.op == Op::Phi && !base_[id].bottom && joined != base_[id]) {
                    if (grown_[id] >= 2) joined = widen(base_[id], joined, w);
                    ++grown_[id];
                }
                if (joined != base_[id]) {
                    base_[id] = joined;
                    changed = true;
                }
            }
        }
        return changed;
    }

    Range operand(std::uint32_t v, std::uint32_t node) const {
        const auto& in = f_.insts[v];
        if (!tracked(in)) return top(64);
        Range r = base_[v];
        if (r.bottom) return r;
        for (auto n = node; n != none && !r.bottom; n = nodes_[n].parent) apply(nodes_[n], v, r);
        return r;
    }

    Range transfer(std::uint32_t id, const Inst& in, std::uint32_t node, std::uint32_t block) const {
        const auto w = in.type.width;
        switch (in.op) {
        case Op::Const: return exact(w, in.imm);
        case Op::Phi: {
            Range r = bottom_range();
            const auto& preds = f_.blocks[block].preds;
            for (std::uint32_t i = 0; i < in.count; ++i) {
                const auto v = f_.extras[in.extra + i];
                if (!tracked(f_.insts[v])) return top(w);
                auto operand_range = rewidth(operand(v, i < preds.size() ? fout_[preds[i]] : none), declared_width(f_, v), w);
                if (operand_range.bottom) continue;
                r = join(r, operand_range);
            }
            if (r.bottom) return r;
            return meet(r, top(w), w);
        }
        case Op::Mask: {
            const auto x = operand(in.operands[0], node);
            if (x.bottom) return x;
            const auto m = static_cast<unsigned>(in.imm);
            if (x.u.hi <= umax(m)) return rewidth(x, declared_width(f_, in.operands[0]), w);
            if ((x.u.lo >> m) == (x.u.hi >> m)) return from_u(w, x.u.lo & umax(m), x.u.hi & umax(m));
            return from_u(w, 0, umax(m));
        }
        case Op::Lift: return lifted(in, node);
        default: return top(w);
        }
        (void)id;
    }

    // The result of a lifted instruction on operands in these ranges, on
    // the executions where its check passes.
    Range lifted(const Inst& in, std::uint32_t node) const {
        const auto& l = in.lifted;
        const unsigned rw = in.type.width;
        if (l.op == lift::Opcode::Constant) return exact(rw, l.immediate);
        const bool integer_operands = (in.operands[0] == none || tracked(f_.insts[in.operands[0]])) &&
                                      (in.operands[1] == none || tracked(f_.insts[in.operands[1]]));
        if (!integer_operands || lift::float_opcode(l.op)) return top(rw);
        const auto get = [&](unsigned i) { return in.operands[i] == none ? Range{} : operand(in.operands[i], node); };
        const auto declared = [&](unsigned i) { return in.operands[i] == none ? 64U : declared_width(f_, in.operands[i]); };
        const Range x = get(0);
        const Range y = get(1);
        if (x.bottom && in.operands[0] != none) return bottom_range();
        if (y.bottom && in.operands[1] != none) return bottom_range();
        return transfer_lift(l, rw, x, y, declared(0), declared(1), in.operands[1] != none);
    }

    const Function& f_;
    const Cfg& cfg_;
    std::vector<Range> base_;
    std::vector<std::uint8_t> grown_;
    std::vector<Node> nodes_;
    std::vector<std::uint32_t> fin_;
    std::vector<std::uint32_t> fout_;
    std::map<std::uint32_t, std::uint32_t> guard_node_;
};

// --- Which checks cannot fail -------------------------------------------------------------------------------

// The checks of the heap operations: a bounds check (CheckIndex) goes in
// `eliminate` when the index's range is below the length's or a comparison
// on every path to it says index < length (Analysis::index_below); the nil,
// lookup, kind and initialized checks go in value numbering when one with
// the same operands dominates them (tier3_passes.cpp, Memory), and leave
// loops in loop-invariant code motion (tier3_licm.cpp).
//
// The kind of the check of `l` (operands in the ranges x and y, declared dx
// and dy bits wide) when lift::evaluate reports no failure for any pair of
// values in them; null when one can fail.
const char* removable_kind(const lift::Instruction& l, unsigned dx, unsigned dy, bool has_y, const Range& x, const Range& y) {
    using lift::Opcode;
    const unsigned w = l.width;
    const bool sgn = l.is_signed;
    const auto declared = [&](unsigned i) { return i == 0 ? dx : dy; };
    const auto sv = [&](const Range& r, unsigned i) { return signed_view(r, declared(i), w); };
    if (x.bottom || (has_y && y.bottom)) return nullptr;
    switch (l.op) {
    case Opcode::Add:
        if (sgn) {
            const auto a = sv(x, 0);
            const auto b = sv(y, 1);
            return a && b && a->lo + b->lo >= smin(w) && a->hi + b->hi <= smax(w) ? "elim.add" : nullptr;
        }
        return x.u.hi + y.u.hi <= umax(w) ? "elim.add" : nullptr;
    case Opcode::Subtract:
        if (sgn) {
            const auto a = sv(x, 0);
            const auto b = sv(y, 1);
            return a && b && a->lo - b->hi >= smin(w) && a->hi - b->lo <= smax(w) ? "elim.subtract" : nullptr;
        }
        return x.u.lo >= y.u.hi ? "elim.subtract" : nullptr;
    case Opcode::Multiply:
        if (sgn) {
            const auto a = sv(x, 0);
            const auto b = sv(y, 1);
            if (!a || !b) return nullptr;
            const i128 corners[4] = {a->lo * b->lo, a->lo * b->hi, a->hi * b->lo, a->hi * b->hi};
            return *std::min_element(corners, corners + 4) >= smin(w) && *std::max_element(corners, corners + 4) <= smax(w)
                       ? "elim.multiply"
                       : nullptr;
        }
        return static_cast<u128>(x.u.hi) * static_cast<u128>(y.u.hi) <= static_cast<u128>(umax(w)) ? "elim.multiply"
                                                                                                   : nullptr;
    case Opcode::Negate:
        if (sgn) return x.u.hi < pow2(w - 1) || x.u.lo > pow2(w - 1) ? "elim.negate" : nullptr;
        return x.u.lo == 0 && x.u.hi == 0 ? "elim.negate" : nullptr;
    case Opcode::Divide:
        if (sgn) {
            const auto a = sv(x, 0);
            const auto b = sv(y, 1);
            if (!a || !b) return nullptr;
            const bool nonzero = b->lo > 0 || b->hi < 0;
            const bool no_overflow = b->lo > -1 || b->hi < -1 || a->lo > smin(w);
            return nonzero && no_overflow ? "elim.divide" : nullptr;
        }
        return y.u.lo >= 1 ? "elim.divide" : nullptr;
    case Opcode::Remainder:
        if (sgn) {
            const auto b = sv(y, 1);
            return b && (b->lo > 0 || b->hi < 0) ? "elim.remainder" : nullptr;
        }
        return y.u.lo >= 1 ? "elim.remainder" : nullptr;
    case Opcode::ShiftLeft:
    case Opcode::ShiftRight: return y.u.hi < w ? "elim.shift" : nullptr;
    case Opcode::Index:
        if (!sgn) return "elim.index";
        return x.u.hi < pow2(w - 1) ? "elim.index" : nullptr;
    case Opcode::ConvertInteger: {
        const unsigned to = l.to_width;
        const Iv target = l.to_signed ? Iv{smin(to), smax(to)} : Iv{0, umax(to)};
        Iv source;
        if (sgn) {
            const auto a = sv(x, 0);
            if (!a) return nullptr;
            source = *a;
        } else {
            source = x.u;
        }
        return source.lo >= target.lo && source.hi <= target.hi ? "elim.convert" : nullptr;
    }
    default: return nullptr;
    }
}

// The wrapping form of a checked Add or Subtract; the others lose their
// Checked flag (their result is the one lift::evaluate computes).
void drop_check(Inst& in) {
    if (in.lifted.op == lift::Opcode::Add) in.lifted.op = lift::Opcode::AddWrapping;
    else if (in.lifted.op == lift::Opcode::Subtract) in.lifted.op = lift::Opcode::SubtractWrapping;
    in.lifted.reason = 0;
    in.lifted.trap_exit = lift::no_exit;
    in.flags = static_cast<std::uint16_t>(in.flags & ~Checked);
    in.state = none;
}

// What an elimination round did to an instruction: the ids it touched.
struct Round {
    bool changed = false;
    std::vector<std::uint32_t> removed;
    std::vector<std::uint32_t> constants;
};

// Applies what `analysis` proves: removes the checks that cannot fail, the
// masks that do nothing, and folds what has one value. `guards` are the
// guards of this round (instructions that rely on one name it).
Round eliminate(Function& f, const Cfg& cfg, const Analysis& analysis, Counts& counts,
                const std::vector<std::pair<std::uint32_t, std::uint32_t>>& guards) {
    Round round;
    Replacements repl(f.insts.size());
    std::vector<std::uint8_t> gone(f.insts.size(), 0);
    const auto guard_for = [&](std::uint32_t block) {
        std::uint32_t best = none;
        std::uint32_t best_number = 0;
        for (const auto& [guard, guard_block] : guards) {
            if (!cfg.dominates(guard_block, block)) continue;
            if (best == none || cfg.number[guard_block] >= best_number) {
                best = guard;
                best_number = cfg.number[guard_block];
            }
        }
        return best;
    };
    for (const auto b : cfg.rpo) {
        auto node = analysis.start_node(b);
        for (const auto id : f.blocks[b].insts) {
            auto& in = f.insts[id];
            if (in.op == Op::Guard) {
                node = analysis.after(node, id);
                continue;
            }
            if (in.op == Op::Lift && (in.flags & Checked) != 0) {
                const auto x = in.operands[0] == none ? Range{} : analysis.refined(in.operands[0], node);
                const auto y = in.operands[1] == none ? Range{} : analysis.refined(in.operands[1], node);
                const auto dx = in.operands[0] == none ? 64U : declared_width(f, in.operands[0]);
                const auto dy = in.operands[1] == none ? 64U : declared_width(f, in.operands[1]);
                if (const auto* kind = removable_kind(in.lifted, dx, dy, in.operands[1] != none, x, y)) {
                    drop_check(in);
                    in.guard = guard_for(b);
                    ++counts[kind];
                    round.changed = true;
                    round.removed.push_back(id);
                }
                continue;
            }
            if (in.op == Op::CheckIndex) {
                const auto x = analysis.refined(in.operands[0], node);
                const auto len = analysis.refined(in.operands[1], node);
                if ((!x.bottom && !len.bottom && x.u.hi < len.u.lo) || analysis.index_below(in.operands[0], in.operands[1], node)) {
                    gone[id] = 1;
                    ++counts["elim.bounds"];
                    round.changed = true;
                    round.removed.push_back(id);
                }
                continue;
            }
            if (in.op == Op::SwitchCheck) {
                const auto x = analysis.refined(in.operands[0], node);
                if (!x.bottom && x.u.hi < static_cast<i128>(in.imm)) {
                    gone[id] = 1;
                    ++counts["elim.switch"];
                    round.changed = true;
                    round.removed.push_back(id);
                }
                continue;
            }
            if (in.op == Op::Mask) {
                const auto x = analysis.refined(in.operands[0], node);
                const auto m = static_cast<unsigned>(in.imm);
                if (!x.bottom && x.u.hi <= umax(m) && f.insts[in.operands[0]].type.type == Type::Int) {
                    repl.set(id, in.operands[0]);
                    gone[id] = 1;
                    ++counts["copy.mask"];
                    round.changed = true;
                    continue;
                }
            }
            // A pure value with one possible value is that constant.
            if (tracked(in) && (in.op == Op::Mask || in.op == Op::Gather ||
                                (in.op == Op::Lift && (in.flags & Checked) == 0 && !lift::float_opcode(in.lifted.op) &&
                                 in.lifted.op != lift::Opcode::Constant))) {
                const auto r = analysis.base(id);
                if (!r.bottom && singleton(r.u)) {
                    in.op = Op::Const;
                    in.operands[0] = in.operands[1] = in.operands[2] = none;
                    in.count = 0;
                    in.imm = static_cast<std::uint64_t>(r.u.lo);
                    in.lifted = {};
                    in.state = none;
                    in.guard = none;
                    in.flags = 0;
                    ++counts["fold.range"];
                    round.changed = true;
                    round.constants.push_back(id);
                }
            }
        }
    }
    if (repl.any() || std::any_of(gone.begin(), gone.end(), [](std::uint8_t g) { return g != 0; })) {
        repl.apply(f);
        for (auto& block : f.blocks) {
            std::vector<std::uint32_t> kept;
            for (const auto id : block.insts) {
                if (!gone[id]) kept.push_back(id);
            }
            block.insts = std::move(kept);
        }
    }
    return round;
}

// --- Induction variables and their guards --------------------------------------------------------------------

struct Candidate {
    std::uint32_t phi = none;
    // The value the preheader gives the phi, `init`, must be at least 0
    // (kind 0), or at most / at least the bound `limit` (kind 1 and 2).
    int kind = 0;
    std::uint32_t limit = none;
    std::uint32_t compare = none;
    bool second = false;
    bool down = false;
    bool is_signed = true;
    unsigned width = 0;
};

struct LoopCandidates {
    Loop loop;
    std::vector<Candidate> list;
};

// A value that does not change while the loop runs: defined outside it, a
// constant, or a phi of the head that only ever carries itself (it holds what
// the preheader gives it).
bool invariant_in(const Function& f, const Places& places, const Loop& loop, std::uint32_t v) {
    if (f.insts[v].op == Op::Const) return true;
    const auto b = places.block[v];
    if (b == none) return false;
    if (!(b < loop.in_body.size() && loop.in_body[b])) return true;
    if (f.insts[v].op != Op::Phi || b != loop.header) return false;
    const auto& header = f.blocks[loop.header];
    for (std::uint32_t k = 0; k < header.preds.size(); ++k) {
        if (loop.in_body[header.preds[k]] && f.extras[f.insts[v].extra + k] != v) return false;
    }
    return true;
}

// Whether the loop's preheader already holds the guard that `init` is not
// negative.
bool has_nonnegative_guard(const Function& f, const Loop& loop, std::uint32_t init) {
    const auto& header = f.blocks[loop.header];
    for (const auto p : header.preds) {
        if (!f.blocks[p].preheader || (p < loop.in_body.size() && loop.in_body[p])) continue;
        for (const auto id : f.blocks[p].insts) {
            const auto& g = f.insts[id];
            if (g.op != Op::Guard || !is_compare(f, g.operands[0])) continue;
            const auto& c = f.insts[g.operands[0]];
            if (c.lifted.op == lift::Opcode::GreaterEqual && c.operands[0] == init && c.operands[1] != none &&
                f.insts[c.operands[1]].op == Op::Const && f.insts[c.operands[1]].imm == 0) {
                return true;
            }
        }
    }
    return false;
}

std::vector<Candidate> candidates_of(const Function& f, const Cfg& cfg, const Places& places, const Analysis& analysis,
                                     const Loop& loop) {
    std::vector<Candidate> result;
    const auto& header = f.blocks[loop.header];
    std::vector<std::uint32_t> back;
    std::vector<std::uint32_t> entering;
    for (std::uint32_t k = 0; k < header.preds.size(); ++k) {
        (loop.in_body[header.preds[k]] ? back : entering).push_back(k);
    }
    if (back.empty() || entering.empty()) return result;
    for (const auto id : header.insts) {
        const auto& phi = f.insts[id];
        if (phi.op != Op::Phi) break;
        if (phi.type.type != Type::Int) continue;
        const auto next = f.extras[phi.extra + back[0]];
        bool same = true;
        for (const auto k : back) same = same && f.extras[phi.extra + k] == next;
        if (!same) continue;
        const auto& step = f.insts[next];
        if (step.op != Op::Lift || next >= places.block.size() || places.block[next] == none) continue;
        const auto op = step.lifted.op;
        if (op != lift::Opcode::Add && op != lift::Opcode::AddWrapping && op != lift::Opcode::Subtract &&
            op != lift::Opcode::SubtractWrapping) {
            continue;
        }
        if (step.lifted.width != phi.type.width) continue;
        const bool adds = op == lift::Opcode::Add || op == lift::Opcode::AddWrapping;
        std::uint32_t other = none;
        if (step.operands[0] == id) other = step.operands[1];
        else if (adds && step.operands[1] == id) other = step.operands[0];
        if (other == none || !invariant_in(f, places, loop, other)) continue;
        const auto step_range = analysis.base(other);
        const bool non_negative = !step_range.bottom && step_range.u.hi <= smax(phi.type.width);
        const bool signed_iv = step.lifted.is_signed;
        // The values the preheader gives the phi, from their own ranges.
        Range entry = bottom_range();
        for (const auto k : entering) {
            const auto v = f.extras[phi.extra + k];
            entry = join(entry, rewidth(analysis.base(v), declared_width(f, v), phi.type.width));
        }
        bool counts_up = adds && non_negative;
        bool guarded = false;
        for (const auto k : entering) {
            guarded = guarded || (f.blocks[header.preds[k]].preheader && has_nonnegative_guard(f, loop, f.extras[phi.extra + k]));
        }
        if (counts_up && signed_iv && !guarded && !entry.bottom && entry.s.lo < 0) {
            Candidate c;
            c.phi = id;
            c.kind = 0;
            c.width = phi.type.width;
            result.push_back(c);
        }
        // The exit test i != y of a loop that steps by one.
        const bool unit = step.operands[0] == id ? (f.insts[step.operands[1]].op == Op::Const && f.insts[step.operands[1]].imm == 1)
                                                  : (f.insts[step.operands[0]].op == Op::Const && f.insts[step.operands[0]].imm == 1);
        if ((step.flags & Checked) == 0 || !unit) continue;
        for (const auto b : loop.body) {
            const auto& block = f.blocks[b];
            if (block.insts.empty() || block.succs.size() != 2) continue;
            const auto& last = f.insts[block.insts.back()];
            if (last.op != Op::Branch || !is_compare(f, last.operands[0])) continue;
            const auto& cmp = f.insts[last.operands[0]];
            if (cmp.lifted.op != lift::Opcode::NotEqual || cmp.guard != none) continue;
            const bool first = cmp.operands[0] == id;
            if (!first && cmp.operands[1] != id) continue;
            const auto y = first ? cmp.operands[1] : cmp.operands[0];
            if (!invariant_in(f, places, loop, y)) continue;
            const auto taken = block.succs[0];
            if (!loop.in_body[taken] || f.blocks[taken].preds.size() != 1 || loop.in_body[block.succs[1]]) continue;
            // The update comes after the test on every path: the true edge
            // leads to every latch and to the update.
            bool dominates_all = cfg.dominates(taken, places.block[next]);
            for (const auto latch : loop.latches) dominates_all = dominates_all && cfg.dominates(taken, latch);
            if (!dominates_all) continue;
            Candidate c;
            c.phi = id;
            c.kind = adds ? 1 : 2;
            c.limit = y;
            c.compare = last.operands[0];
            c.second = !first;
            c.down = !adds;
            c.is_signed = signed_iv;
            c.width = phi.type.width;
            result.push_back(c);
            break;
        }
    }
    return result;
}

bool has_checked_op(const Function& f, const Loop& loop) {
    for (const auto b : loop.body) {
        for (const auto id : f.blocks[b].insts) {
            const auto& in = f.insts[id];
            if (in.op == Op::Lift && (in.flags & Checked) != 0) return true;
        }
    }
    return false;
}

std::uint32_t add_inst(Function& f, std::uint32_t block, Inst in) {
    in.origin_block = f.blocks[block].origin_block;
    in.origin_function = f.blocks[block].origin_function;
    const auto id = static_cast<std::uint32_t>(f.insts.size());
    f.insts.push_back(std::move(in));
    auto& insts = f.blocks[block].insts;
    insts.insert(insts.end() - 1, id);
    return id;
}

struct HeapBound {
    std::uint32_t bound;
    std::uint32_t length;
    // An affine index is bounded by bound squared. This limit on the
    // factor certifies that the square and the source index fit.
    std::uint64_t square_limit = 0;
    bool inclusive = false;
    auto operator<=>(const HeapBound&) const = default;
};

bool full_width_arithmetic(const Function& f, const Inst& in) {
    return in.op == Op::Lift && tracked(in) && in.lifted.width != 0 && in.lifted.width <= in.type.width &&
           std::all_of(in.operands, in.operands + 2, [&](std::uint32_t value) {
               return value != none && tracked(f.insts[value]) && declared_width(f, value) <= in.lifted.width;
           });
}

std::uint64_t floor_sqrt(std::uint64_t value) {
    std::uint64_t low = 0;
    std::uint64_t high = std::uint64_t{1} << 32;
    while (high - low > 1) {
        const auto middle = (low + high) / 2;
        if (middle * middle <= value) low = middle;
        else high = middle;
    }
    return low;
}

// For row*side+column, a shared strict bound on both subscripts gives
// row*side+column < side*side. The guard also proves the intermediate
// product and sum fit their original widths and the square fits uint64.
std::optional<HeapBound> affine_bound(const Function& f, const Analysis& analysis, std::uint32_t index,
                                      std::uint32_t length, std::uint32_t node) {
    const auto& add = f.insts[index_input(f, index)];
    if (!full_width_arithmetic(f, add) ||
        (add.lifted.op != lift::Opcode::Add && add.lifted.op != lift::Opcode::AddWrapping)) return std::nullopt;
    const auto maximum = [](const Inst& in) {
        return in.lifted.is_signed ? smax(in.lifted.width) : umax(in.lifted.width);
    };
    for (unsigned side = 0; side != 2; ++side) {
        const auto& mul = f.insts[add.operands[side]];
        if (!full_width_arithmetic(f, mul) || mul.lifted.op != lift::Opcode::Multiply) continue;
        const auto column = analysis.upper_bounds(add.operands[1 - side], node);
        for (unsigned factor = 0; factor != 2; ++factor) {
            const auto bound = mul.operands[factor];
            const auto row = analysis.upper_bounds(mul.operands[1 - factor], node);
            const auto below = [bound](const UpperBound& upper) { return !upper.inclusive && upper.value == bound; };
            if (!std::any_of(column.begin(), column.end(), below) || !std::any_of(row.begin(), row.end(), below)) continue;
            const auto largest = std::min(umax(64), std::min(maximum(add), maximum(mul)) + 1);
            return HeapBound{bound, length, floor_sqrt(static_cast<std::uint64_t>(largest))};
        }
    }
    return std::nullopt;
}

bool stable_length(const Function& f, const Loop& loop) {
    for (const auto block : loop.body) {
        for (const auto id : f.blocks[block].insts) {
            const auto& in = f.insts[id];
            // A fixed-width element replacement preserves the heap entry
            // and its length even if the write uses an alias of this list.
            if (in.op != Op::StoreElement && is_effect(in)) return false;
        }
    }
    return true;
}

bool available_at_header(const Function& f, const Cfg& cfg, const Places& places, const Loop& loop,
                         std::uint32_t value) {
    const auto block = places.block[value];
    return f.insts[value].op == Op::Const ||
           (block != none && block != loop.header && !loop.in_body[block] && cfg.dominates(block, loop.header));
}

std::uint32_t insert_heap_bound(Function& f, const HeapBound& proof, std::uint32_t preheader, std::uint32_t state) {
    const auto guard = [&](lift::Opcode op, std::uint32_t lhs, std::uint32_t rhs) {
        Inst compare;
        compare.op = Op::Lift;
        compare.type = {Type::Int, 1};
        compare.lifted.op = op;
        compare.lifted.width = 64;
        compare.operands[0] = lhs;
        compare.operands[1] = rhs;
        const auto condition = add_inst(f, preheader, compare);
        Inst check;
        check.op = Op::Guard;
        check.operands[0] = condition;
        check.state = state;
        check.flags = Checked;
        return add_inst(f, preheader, check);
    };
    auto bound = proof.bound;
    if (f.insts[bound].op == Op::Const) bound = add_inst(f, preheader, f.insts[bound]);
    if (proof.square_limit != 0) {
        Inst limit;
        limit.op = Op::Const;
        limit.type = {Type::Int, 64};
        limit.imm = proof.square_limit;
        const auto max_factor = add_inst(f, preheader, limit);
        const auto fits = guard(lift::Opcode::LessEqual, bound, max_factor);
        Inst square;
        square.op = Op::Lift;
        square.type = {Type::Int, 64};
        square.lifted.op = lift::Opcode::Multiply;
        square.lifted.width = 64;
        square.operands[0] = bound;
        square.operands[1] = bound;
        square.guard = fits;
        bound = add_inst(f, preheader, square);
    }
    return guard(proof.inclusive ? lift::Opcode::Less : lift::Opcode::LessEqual, bound, proof.length);
}

// Guards in the preheader for the candidates of `loop_header`'s loop; the
// ids of the guards made, and the preheader.
using Made = std::vector<std::pair<std::uint32_t, const char*>>;

Made make_guards(Function& f, const Loop& loop, const std::vector<Candidate>& list, std::uint32_t& preheader_out,
                 Counts& counts) {
    const auto preheader = make_preheader(f, loop);
    preheader_out = preheader;
    const auto state = preheader_state(f, loop, preheader);
    Made made;
    for (const auto& c : list) {
        const auto init = entry_operand(f, loop, c.phi);
        Inst compare;
        compare.op = Op::Lift;
        compare.type = {Type::Int, 1};
        compare.operands[0] = init;
        if (c.kind == 0) {
            Inst zero;
            zero.op = Op::Const;
            zero.type = {Type::Int, static_cast<std::uint8_t>(c.width)};
            zero.imm = 0;
            const auto zero_id = add_inst(f, preheader, zero);
            compare.operands[1] = zero_id;
            compare.lifted.op = lift::Opcode::GreaterEqual;
            compare.lifted.is_signed = true;
        } else {
            // A bound the head's phi carries unchanged is what the preheader
            // gives it; a constant of the loop body is made again in the
            // preheader, where the one in the body is not defined.
            const bool head_phi = f.insts[c.limit].op == Op::Phi &&
                                  std::find(f.blocks[loop.header].insts.begin(), f.blocks[loop.header].insts.end(), c.limit) !=
                                      f.blocks[loop.header].insts.end();
            if (head_phi) {
                compare.operands[1] = entry_operand(f, loop, c.limit);
            } else if (f.insts[c.limit].op == Op::Const) {
                Inst bound = f.insts[c.limit];
                bound.guard = none;
                compare.operands[1] = add_inst(f, preheader, bound);
            } else {
                compare.operands[1] = c.limit;
            }
            compare.lifted.op = c.kind == 1 ? lift::Opcode::LessEqual : lift::Opcode::GreaterEqual;
            compare.lifted.is_signed = c.is_signed;
        }
        compare.lifted.width = static_cast<std::uint8_t>(c.width);
        const auto compare_id = add_inst(f, preheader, compare);
        Inst guard;
        guard.op = Op::Guard;
        guard.operands[0] = compare_id;
        guard.state = state;
        guard.flags = Checked;
        const auto guard_id = add_inst(f, preheader, guard);
        const char* kind = c.kind == 0 ? "guard.init_nonnegative" : "guard.init_bounded";
        made.emplace_back(guard_id, kind);
        if (c.kind != 0) {
            auto& ne = f.insts[c.compare];
            ne.guard = guard_id;
            ne.imm = (c.second ? 1U : 0U) | (c.down ? 2U : 0U) | (c.is_signed ? 4U : 0U);
        }
        ++counts[kind];
    }
    return made;
}

// Takes out the guards nothing relies on, with the comparison and the
// constant they read.
void prune_guards(Function& f, const Made& made, Counts& counts) {
    std::vector<std::uint8_t> named(f.insts.size(), 0);
    for (const auto& in : f.insts) {
        if (in.guard != none && in.guard < named.size()) named[in.guard] = 1;
    }
    for (const auto& [g, kind] : made) {
        if (named[g]) continue;
        const auto compare = f.insts[g].operands[0];
        for (auto& block : f.blocks) {
            std::vector<std::uint32_t> kept;
            for (const auto id : block.insts) {
                if (id != g && id != compare) kept.push_back(id);
            }
            block.insts = std::move(kept);
        }
        --counts[kind];
        ++counts["guard.pruned"];
    }
}

} // namespace

std::string audit_lift(const lift::Instruction& l, unsigned result_width, unsigned dx, unsigned dy, bool has_y, std::uint64_t x_lo,
                       std::uint64_t x_hi, std::uint64_t y_lo, std::uint64_t y_hi, std::uint64_t samples) {
    const auto x = from_u(dx, x_lo, x_hi);
    const auto y = has_y ? from_u(dy, y_lo, y_hi) : Range{};
    const auto result = transfer_lift(l, result_width, x, y, dx, dy, has_y);
    const auto* removable = removable_kind(l, dx, dy, has_y, x, y);
    const auto describe_pair = [&](std::uint64_t a, std::uint64_t b) {
        return "op " + std::to_string(static_cast<int>(l.op)) + " w" + std::to_string(l.width) + (l.is_signed ? "s" : "u") +
               " operands " + std::to_string(a) + "," + std::to_string(b) + " in [" + std::to_string(x_lo) + "," +
               std::to_string(x_hi) + "]x[" + std::to_string(y_lo) + "," + std::to_string(y_hi) + "]";
    };
    // Every pair when the intervals are small, else the ends and `samples`
    // others of each.
    const auto points = [&](std::uint64_t lo, std::uint64_t hi) {
        std::vector<std::uint64_t> list;
        if (hi - lo < 64) {
            for (std::uint64_t i = 0; i <= hi - lo; ++i) list.push_back(lo + i);
            return list;
        }
        list = {lo, lo + 1, hi - 1, hi};
        std::uint64_t seed = lo * 0x9e3779b97f4a7c15ULL + hi;
        for (std::uint64_t i = 0; i < samples; ++i) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            list.push_back(hi - lo == ~std::uint64_t{0} ? seed : lo + (seed >> 11) % (hi - lo + 1));
        }
        return list;
    };
    for (const auto a : points(x_lo, x_hi)) {
        for (const auto b : has_y ? points(y_lo, y_hi) : std::vector<std::uint64_t>{0}) {
            bool failed = false;
            const auto r = lift::evaluate(l, a, b, failed);
            if (failed) {
                if (removable) return "a check that can fail is removable: " + describe_pair(a, b);
                continue;
            }
            if (x.bottom || (has_y && y.bottom)) continue;
            if (result.bottom) return "an execution that passes has a result the range excludes (bottom): " + describe_pair(a, b);
            if (static_cast<i128>(r) < result.u.lo || static_cast<i128>(r) > result.u.hi) {
                return "the result " + std::to_string(r) + " is outside the unsigned range: " + describe_pair(a, b);
            }
            const auto sr = static_cast<i128>(lift::sign_extend(r, result_width));
            if (sr < result.s.lo || sr > result.s.hi) {
                return "the result " + std::to_string(r) + " is outside the signed range: " + describe_pair(a, b);
            }
        }
    }
    return "";
}

std::string audit_refine(lift::Opcode op, bool sense, bool is_signed, unsigned w, unsigned dx, unsigned dy, std::uint64_t x_lo,
                         std::uint64_t x_hi, std::uint64_t y_lo, std::uint64_t y_hi, std::uint64_t samples) {
    Rel rel{};
    switch (op) {
    case lift::Opcode::Equal: rel = Rel::Eq; break;
    case lift::Opcode::NotEqual: rel = Rel::Ne; break;
    case lift::Opcode::Less: rel = Rel::Lt; break;
    case lift::Opcode::LessEqual: rel = Rel::Le; break;
    case lift::Opcode::Greater: rel = Rel::Gt; break;
    default: rel = Rel::Ge; break;
    }
    if (!sense) rel = negate(rel);
    const auto x = from_u(dx, x_lo, x_hi);
    const auto y = from_u(dy, y_lo, y_hi);
    auto xr = x;
    auto yr = y;
    refine_by(rel, is_signed, w, xr, y, dx, dy);
    refine_by(flip(rel), is_signed, w, yr, x, dy, dx);
    lift::Instruction l;
    l.op = op;
    l.width = static_cast<std::uint8_t>(w);
    l.is_signed = is_signed;
    const auto points = [&](std::uint64_t lo, std::uint64_t hi) {
        std::vector<std::uint64_t> list;
        if (hi - lo < 64) {
            for (std::uint64_t i = 0; i <= hi - lo; ++i) list.push_back(lo + i);
            return list;
        }
        list = {lo, lo + 1, hi - 1, hi};
        std::uint64_t seed = lo * 0x9e3779b97f4a7c15ULL + hi;
        for (std::uint64_t i = 0; i < samples; ++i) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            list.push_back(hi - lo == ~std::uint64_t{0} ? seed : lo + (seed >> 11) % (hi - lo + 1));
        }
        return list;
    };
    const auto inside = [](const Range& r, std::uint64_t v, unsigned declared) {
        return !r.bottom && static_cast<i128>(v) >= r.u.lo && static_cast<i128>(v) <= r.u.hi &&
               static_cast<i128>(lift::sign_extend(v, declared)) >= r.s.lo &&
               static_cast<i128>(lift::sign_extend(v, declared)) <= r.s.hi;
    };
    for (const auto a : points(x_lo, x_hi)) {
        for (const auto b : points(y_lo, y_hi)) {
            bool failed = false;
            const bool holds = (lift::evaluate(l, a, b, failed) == 1) == sense;
            if (!holds) continue;
            if (!inside(xr, a, dx) || !inside(yr, b, dy)) {
                return "a pair that satisfies " + std::string(sense ? "" : "not ") + "op " + std::to_string(static_cast<int>(op)) + " w" +
                       std::to_string(w) + (is_signed ? "s" : "u") + " is outside the refined ranges: " + std::to_string(a) + "," +
                       std::to_string(b) + " in [" + std::to_string(x_lo) + "," + std::to_string(x_hi) + "]x[" +
                       std::to_string(y_lo) + "," + std::to_string(y_hi) + "]";
            }
        }
    }
    return "";
}

bool eliminate_checks(const nvr::Image&, Function& f, Counts& counts) {
    bool any = false;
    {
        Cfg cfg(f);
        Analysis analysis(f, cfg);
        if (!analysis.run()) return false;
        any = eliminate(f, cfg, analysis, counts, {}).changed;
    }
    // Guards of induction variables: facts about the value a loop starts
    // with, which an entry from the interpreter gives from the frame.
    for (unsigned round = 0; round < 4; ++round) {
        Cfg cfg(f);
        Places places(f);
        Analysis analysis(f, cfg);
        if (!analysis.run()) return any;
        const auto loops = find_loops(f, cfg);
        std::vector<std::pair<std::uint32_t, std::vector<Candidate>>> chosen;
        for (const auto& loop : loops) {
            if (head_state(f, loop) == none || !has_checked_op(f, loop)) continue;
            auto list = candidates_of(f, cfg, places, analysis, loop);
            if (!list.empty()) chosen.emplace_back(loop.header, std::move(list));
        }
        if (chosen.empty()) return any;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> guards;
        Made made_all;
        for (const auto& [header, list] : chosen) {
            Cfg now(f);
            const auto current = find_loops(f, now);
            const auto found = std::find_if(current.begin(), current.end(), [&](const Loop& l) { return l.header == header; });
            if (found == current.end()) continue;
            std::uint32_t preheader = none;
            const auto made = make_guards(f, *found, list, preheader, counts);
            for (const auto& m : made) {
                guards.emplace_back(m.first, preheader);
                made_all.push_back(m);
            }
        }
        Cfg cfg2(f);
        Analysis with_guards(f, cfg2);
        if (!with_guards.run()) {
            // The guards stay (they are sound), the analysis just does not
            // use them.
            return any;
        }
        const auto result = eliminate(f, cfg2, with_guards, counts, guards);
        prune_guards(f, made_all, counts);
        if (!result.changed) return any;
        any = true;
    }
    return any;
}

bool guard_heap_bounds(Function& f, Counts& counts) {
    if (std::none_of(f.insts.begin(), f.insts.end(), [](const Inst& in) { return in.op == Op::CheckIndex; })) return false;
    const Cfg cfg(f);
    auto loops = find_loops(f, cfg);
    std::erase_if(loops, [&](const Loop& loop) { return head_state(f, loop) == none || !stable_length(f, loop); });
    if (loops.empty()) return false;
    // Prefer the furthest enclosing stable loop so the bounds need not be
    // checked again at every entry into a nested loop.
    std::reverse(loops.begin(), loops.end());
    Analysis analysis(f, cfg);
    if (!analysis.run()) return false;
    const Places places(f);

    struct Use {
        std::uint32_t block;
        std::uint32_t check;
    };
    std::map<std::uint32_t, std::map<HeapBound, std::vector<Use>>> plans;
    for (const auto block : cfg.rpo) {
        auto node = analysis.start_node(block);
        for (const auto id : f.blocks[block].insts) {
            const auto& in = f.insts[id];
            node = analysis.after(node, id);
            if (in.op != Op::CheckIndex || in.guard != none) continue;
            const auto index = in.operands[0];
            const auto length = in.operands[1];
            if (f.insts[length].op != Op::LoadLength) continue;
            const auto bounds = analysis.upper_bounds(index, node);
            const auto affine = affine_bound(f, analysis, index, length, node);
            for (const auto& loop : loops) {
                if (!loop.in_body[block] || !available_at_header(f, cfg, places, loop, length)) continue;
                const auto direct = std::find_if(bounds.begin(), bounds.end(), [&](const UpperBound& upper) {
                    return available_at_header(f, cfg, places, loop, upper.value);
                });
                if (direct != bounds.end()) {
                    plans[loop.header][{direct->value, length, 0, direct->inclusive}].push_back({block, id});
                    break;
                }
                if (affine && available_at_header(f, cfg, places, loop, affine->bound)) {
                    plans[loop.header][*affine].push_back({block, id});
                    break;
                }
            }
        }
    }

    std::vector<std::uint8_t> removed(f.insts.size(), 0);
    for (const auto& [header, group] : plans) {
        // Other preheaders can have inserted blocks inside this loop.
        const auto current = find_loops(f, Cfg(f));
        const auto loop = std::find_if(current.begin(), current.end(), [&](const Loop& item) { return item.header == header; });
        if (loop == current.end()) continue;
        const auto preheader = make_preheader(f, *loop);
        const auto state = preheader_state(f, *loop, preheader);
        for (const auto& [proof, uses] : group) {
            const auto guard = insert_heap_bound(f, proof, preheader, state);
            const auto entry = f.insts[proof.length].operands[0];
            ++counts[proof.square_limit == 0 ? "guard.heap_bounds" : "guard.heap_affine"];
            for (const auto& [block, check] : uses) {
                const auto index = f.insts[check].operands[0];
                for (const auto id : f.blocks[block].insts) {
                    auto& inst = f.insts[id];
                    if ((inst.op != Op::LoadElement && inst.op != Op::StoreElement) || inst.operands[1] != index ||
                        inst.guard != none || places.position[id] <= places.position[check]) continue;
                    const auto& data = f.insts[inst.operands[0]];
                    if (data.op == Op::LoadData && data.operands[0] == entry) inst.guard = guard;
                }
                removed[check] = 1;
                ++counts["elim.bounds"];
            }
        }
    }
    if (plans.empty()) return false;
    for (auto& block : f.blocks) {
        std::erase_if(block.insts, [&](std::uint32_t id) { return id < removed.size() && removed[id]; });
    }
    return true;
}

} // namespace neura::nvm::tier3::detail
