// Lifted blocks (src/lift, docs/runtime/NVM.md "Execution tiers").
//
// `proof`: the structural proof accepts a regenerated block and rejects an
// edited graph and forged or ill-typed hints.
// `overflow`: the interval proof and the 0/1 lane arguments on constructed
// graphs, including graphs that overflow.
// `execute`: lowered operations against the reference evaluator, op by op
// on constructed blocks (exhaustive for 8-bit operands), then every block of
// a corpus of compiled programs, with coverage reported, then the benchmark
// programs run with a device that checks each executed lifted block against
// the reference and weights coverage by executed steps.
// `floats`: binary32 and binary64 addition, subtraction, multiplication,
// and division are proven, and their hardware lowering matches the
// reference lane by lane on a dense boundary set (all pairs) and on a
// million seeded random pairs per operation and width (a tenth for
// division).

#include "backend/converter.hpp"
#include "circuit/float_circuit.hpp"
#include "ir-optimization/support.hpp"
#include "lift/checked.hpp"
#include "lift/hints.hpp"
#include "lift/lift.hpp"
#include "lift/overflow.hpp"
#include "lift/proof.hpp"
#include "lift/recognize.hpp"
#include "nvm/graph_device.hpp"
#include "nvm/host.hpp"
#include "nvm/machine.hpp"
#include "nvm/providers.hpp"
#include "nvr/format.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace neura::testing {
namespace {

using lift::Expression;
using lift::ExpressionPtr;
using lift::LiftedForm;

std::uint64_t mask(unsigned width) { return lift::width_mask(width); }

// ---------------------------------------------------------------------------
// Inputs and the reference.

struct Reference {
    bool overflow = false;
    std::vector<std::int64_t> lanes;
};

Reference reference(const nvr::Graph& graph, const std::vector<unsigned>& widths,
                    const std::vector<std::uint64_t>& values) {
    std::vector<std::int64_t> inputs;
    for (std::size_t i = 0; i < widths.size(); ++i) {
        for (unsigned b = 0; b < widths[i]; ++b) {
            inputs.push_back(static_cast<std::int64_t>((values[i] >> b) & 1U));
        }
    }
    Reference result;
    std::vector<std::int64_t> scratch;
    result.lanes.resize(graph.outputs.size());
    result.overflow = nvm::evaluate_outputs(graph, inputs, scratch, result.lanes).has_value();
    return result;
}

// Boundary values of an operand, by its type.
std::vector<std::uint64_t> boundaries(const nvr::Type& type) {
    const unsigned w = nvr::lane_count(type);
    std::set<std::uint64_t> values;
    if (type.kind == nvr::TypeKind::Bool) return {0, 1};
    if (type.kind == nvr::TypeKind::Float || type.kind == nvr::TypeKind::BFloat) {
        const unsigned fraction = type.kind == nvr::TypeKind::BFloat ? 7 : w == 16 ? 10 : w == 32 ? 23 : 52;
        const unsigned exponent = w - 1 - fraction;
        const auto sign = std::uint64_t{1} << (w - 1);
        const auto infinity = ((std::uint64_t{1} << exponent) - 1) << fraction;
        const auto one = ((std::uint64_t{1} << (exponent - 1)) - 1) << fraction;
        for (const std::uint64_t magnitude :
             {std::uint64_t{0}, std::uint64_t{1}, (std::uint64_t{1} << fraction) - 1, std::uint64_t{1} << fraction,
              one, one | 1, one | (std::uint64_t{1} << (fraction - 1)), one + (std::uint64_t{1} << fraction),
              infinity - 1, infinity, infinity | 1, infinity | (std::uint64_t{1} << (fraction - 1)),
              infinity | (std::uint64_t{1} << (fraction - 1)) | 5, (one >> 1) + (std::uint64_t{3} << (fraction - 2)),
              one + (std::uint64_t{30} << fraction), one + (std::uint64_t{62} << fraction) + 12345 % (std::uint64_t{1} << fraction)}) {
            values.insert(magnitude & (sign - 1));
            values.insert((magnitude & (sign - 1)) | sign);
        }
        return {values.begin(), values.end()};
    }
    const bool is_signed = type.kind == nvr::TypeKind::Signed;
    const auto m = mask(w);
    const auto min = is_signed ? std::uint64_t{1} << (w - 1) : 0;
    const auto max = is_signed ? (std::uint64_t{1} << (w - 1)) - 1 : m;
    for (const auto v : {std::uint64_t{0}, std::uint64_t{1}, m, min, max, min + 1, max - 1, std::uint64_t{2}, m - 1}) {
        values.insert(v & m);
    }
    for (unsigned k = 2; k < w; k += k < 8 ? 1 : 7) {
        const auto p = std::uint64_t{1} << k;
        values.insert(p & m);
        values.insert((p - 1) & m);
        values.insert((p + 1) & m);
        values.insert((0 - p) & m);
    }
    return {values.begin(), values.end()};
}

std::uint64_t random_value(std::mt19937_64& random, const nvr::Type& type) {
    const unsigned w = nvr::lane_count(type);
    auto v = random();
    // Small magnitudes hit carries and comparisons near zero.
    if ((v & 3U) == 0) v = (random() & 0xff) * ((v & 4U) ? ~std::uint64_t{0} : 1);
    return v & mask(w);
}

// Input vectors for a block: every input when there are at most 16 input
// lanes; otherwise the boundary values of each operand crossed while the
// product stays small, each boundary value against random others, and
// seeded random vectors.
std::vector<std::vector<std::uint64_t>> input_vectors(const nvr::Image& image, const std::vector<std::uint32_t>& types,
                                                      std::size_t random_count, std::uint64_t seed) {
    std::vector<unsigned> widths;
    unsigned total = 0;
    for (const auto t : types) {
        widths.push_back(nvr::lane_count(image.types.at(t)));
        total += widths.back();
    }
    std::vector<std::vector<std::uint64_t>> result;
    if (total <= 16) {
        for (std::uint64_t bits = 0; bits < (std::uint64_t{1} << total); ++bits) {
            std::vector<std::uint64_t> values;
            auto rest = bits;
            for (const auto w : widths) {
                values.push_back(rest & mask(w));
                rest >>= w;
            }
            result.push_back(std::move(values));
        }
        return result;
    }
    std::vector<std::vector<std::uint64_t>> sets;
    std::size_t product = 1;
    for (const auto t : types) {
        sets.push_back(boundaries(image.types.at(t)));
        product = std::min<std::size_t>(product * sets.back().size(), 1'000'000);
    }
    std::mt19937_64 random(seed);
    if (product <= 4096) {
        std::vector<std::size_t> index(types.size(), 0);
        for (std::size_t n = 0; n < product; ++n) {
            std::vector<std::uint64_t> values;
            for (std::size_t i = 0; i < types.size(); ++i) values.push_back(sets[i][index[i]]);
            result.push_back(std::move(values));
            for (std::size_t i = 0; i < index.size() && ++index[i] == sets[i].size(); ++i) index[i] = 0;
        }
    } else {
        for (std::size_t i = 0; i < types.size(); ++i) {
            for (const auto v : sets[i]) {
                std::vector<std::uint64_t> values;
                for (std::size_t j = 0; j < types.size(); ++j) {
                    values.push_back(j == i ? v
                                            : (random() & 1U) ? sets[j][random() % sets[j].size()]
                                                              : random_value(random, image.types.at(types[j])));
                }
                result.push_back(std::move(values));
            }
        }
    }
    for (std::size_t n = 0; n < random_count; ++n) {
        std::vector<std::uint64_t> values;
        for (const auto t : types) values.push_back(random_value(random, image.types.at(t)));
        result.push_back(std::move(values));
    }
    return result;
}

// Whether `exit` of `block` is a check's trap: a trap exit after the first
// non-trap exit, which reads no lane.
bool check_trap(const nvr::Block& block, std::uint32_t exit) {
    if (block.exits[exit].kind != nvr::ExitKind::Trap) return false;
    for (std::uint32_t e = 0; e < exit; ++e) {
        if (block.exits[e].kind != nvr::ExitKind::Trap) return true;
    }
    return false;
}

struct Comparison {
    std::size_t vectors = 0;
    std::size_t declined = 0;
    std::string mismatch;
};

// Runs the lowered block and the reference on each vector and requires the
// same exit, 0/1 lanes, and, unless a check traps, the same output lanes.
Comparison compare(const nvr::Image& image, const nvr::Block& block, const lift::LoweredBlock& lowered,
                   const std::vector<std::vector<std::uint64_t>>& vectors) {
    const auto& graph = image.graphs.at(block.graph);
    Comparison result;
    std::vector<std::uint64_t> outputs(lowered.outputs.size());
    std::vector<std::int64_t> lanes(graph.outputs.size());
    for (const auto& values : vectors) {
        ++result.vectors;
        const auto expected = reference(graph, lowered.input_widths, values);
        std::ostringstream where;
        where << "inputs";
        for (const auto v : values) where << " 0x" << std::hex << v;
        if (expected.overflow) {
            result.mismatch = "the reference overflows on " + where.str();
            return result;
        }
        for (const auto lane : expected.lanes) {
            if (lane != 0 && lane != 1) {
                result.mismatch = "a reference lane is not 0 or 1 on " + where.str();
                return result;
            }
        }
        std::uint32_t reference_exit = 0;
        if (block.exits.size() > 1) {
            for (unsigned j = 0; j < nvr::selector_lanes(block.exits.size()); ++j) {
                reference_exit |= static_cast<std::uint32_t>(expected.lanes[block.selector_start + j]) << j;
            }
        }
        const auto exit = lift::execute(lowered, values, outputs);
        if (!exit) {
            ++result.declined;
            result.mismatch = "the lowered block declined " + where.str();
            return result;
        }
        if (*exit != reference_exit) {
            result.mismatch = "exit " + std::to_string(*exit) + ", reference " + std::to_string(reference_exit) +
                              ", on " + where.str();
            return result;
        }
        if (check_trap(block, *exit)) continue;
        std::fill(lanes.begin(), lanes.end(), -1);
        lift::write_lanes(lowered, *exit, outputs, lanes);
        if (lanes != expected.lanes) {
            result.mismatch = "output lanes differ on " + where.str();
            return result;
        }
    }
    return result;
}

std::vector<std::uint32_t> input_types(const nvr::Block& block) {
    std::vector<std::uint32_t> types;
    for (const auto& input : block.inputs) types.push_back(input.type);
    return types;
}

// ---------------------------------------------------------------------------
// Constructed blocks: one operation over slot inputs, regenerated by the
// circuit library, as the converter would emit it.

enum T : std::uint32_t { Bool, I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, F16, BF16, TypeCount };

nvr::Image op_image() {
    nvr::Image image;
    image.types = {
        {nvr::TypeKind::Bool, 0, 0},     {nvr::TypeKind::Signed, 8, 0},   {nvr::TypeKind::Unsigned, 8, 0},
        {nvr::TypeKind::Signed, 16, 0},  {nvr::TypeKind::Unsigned, 16, 0}, {nvr::TypeKind::Signed, 32, 0},
        {nvr::TypeKind::Unsigned, 32, 0}, {nvr::TypeKind::Signed, 64, 0},  {nvr::TypeKind::Unsigned, 64, 0},
        {nvr::TypeKind::Float, 32, 0},   {nvr::TypeKind::Float, 64, 0},   {nvr::TypeKind::Float, 16, 0},
        {nvr::TypeKind::BFloat, 16, 0},
    };
    return image;
}

// An image whose function 0 block 0 returns `value` and, with a
// `condition`, branches on it between two returns. Returns false when the
// circuit library cannot assemble it.
bool construct(nvr::Image& image, const std::vector<std::uint32_t>& inputs, const ExpressionPtr& value,
               const ExpressionPtr& condition, LiftedForm& form) {
    nvr::Block original;
    for (std::uint32_t i = 0; i < inputs.size(); ++i) original.inputs.push_back({nvr::ValueKind::Slot, inputs[i], i});
    nvr::Exit exit;
    exit.kind = nvr::ExitKind::Return;
    exit.arguments.push_back({nvr::ValueKind::Lanes, value->type, 0});
    original.exits.push_back(exit);
    form = {};
    form.values[0] = value;
    if (condition) {
        // The other exit returns input 0, which has the value's type. (Two
        // exits reading one computed value would materialize it twice.)
        auto other = exit;
        other.arguments = {{nvr::ValueKind::Slot, inputs.front(), 0}};
        original.exits.push_back(other);
        form.condition = condition;
        form.exit_when_true = 0;
    }
    nvr::Graph graph;
    const auto first = lift::replay(image, original, {{0, value}}, condition, form.exit_when_true, graph, nullptr);
    if (!first) return false;
    // The selector comes before the value lanes, so the value moves; build
    // the block again with its final lane numbering.
    const auto lane = first->exits.front().arguments.front().index;
    form.values.clear();
    form.values[lane] = value;
    const auto block = lift::replay(image, *first, {{lane, value}}, condition, form.exit_when_true, graph, nullptr);
    if (!block || !(*block == *first)) return false;
    image.graphs = {graph};
    nvr::Function function;
    function.parameter_count = static_cast<std::uint32_t>(inputs.size());
    function.result_type = value->type;
    function.frame_types = inputs;
    function.blocks = {*block};
    function.blocks[0].graph = 0;
    image.functions = {function};
    return true;
}

struct OpCase {
    std::string label;
    std::vector<std::uint32_t> inputs;
    ExpressionPtr value;
    ExpressionPtr condition;
    bool expect_lowered = true;
};

std::string type_name(std::uint32_t t) {
    static const char* names[] = {"bool", "int8", "uint8", "int16", "uint16", "int32", "uint32",
                                  "int64", "uint64", "float32", "float64", "float16", "bfloat16"};
    return names[t];
}

std::vector<OpCase> op_cases() {
    std::vector<OpCase> cases;
    const auto in = [](std::uint32_t t, std::size_t i) { return lift::input(t, i); };
    const std::vector<std::uint32_t> integers{I8, U8, I16, U16, I32, U32, I64, U64};
    for (const auto t : integers) {
        for (const auto* op : {"+", "-", "*", "/", "%", "+~", "-~", "&", "|", "^", "<<", ">>"}) {
            cases.push_back({type_name(t) + " " + op, {t, t}, lift::binary(op, t, in(t, 0), in(t, 1)), nullptr});
        }
        for (const auto* op : {"==", "!=", "<", "<=", ">", ">="}) {
            cases.push_back({type_name(t) + " " + op, {t, t}, lift::binary(op, Bool, in(t, 0), in(t, 1)), nullptr});
        }
        cases.push_back({type_name(t) + " neg", {t}, lift::unary("neg", t, in(t, 0)), nullptr});
        cases.push_back({type_name(t) + " ~", {t}, lift::unary("~", t, in(t, 0)), nullptr});
        cases.push_back({type_name(t) + " index", {t}, lift::unary("index", U64, in(t, 0)), nullptr});
        for (const auto to : integers) {
            if (to != t) {
                cases.push_back({type_name(t) + " as " + type_name(to), {t}, lift::convert(to, in(t, 0)), nullptr});
            }
        }
        for (const auto to : {F32, F64}) {
            cases.push_back({type_name(t) + " as " + type_name(to), {t}, lift::convert(to, in(t, 0)), nullptr});
        }
        cases.push_back({type_name(t) + " as float16", {t}, lift::convert(F16, in(t, 0)), nullptr, false});
    }
    // A value and a condition in one block, with checks of two reasons.
    cases.push_back({"int32 (a / b) if (index(a) < 10)", {I32, I32}, lift::binary("/", I32, in(I32, 0), in(I32, 1)),
                     lift::binary("<", Bool, lift::unary("index", U64, in(I32, 0)), lift::immediate(U64, 10))});
    for (const auto* op : {"&&", "||", "==", "!="}) {
        cases.push_back({std::string("bool ") + op, {Bool, Bool}, lift::binary(op, Bool, in(Bool, 0), in(Bool, 1)), nullptr});
    }
    cases.push_back({"bool !", {Bool}, lift::unary("!", Bool, in(Bool, 0)), nullptr});
    for (const auto t : {F32, F64, F16, BF16}) {
        const bool hardware = t == F32 || t == F64;
        for (const auto* op : {"+", "-", "*", "/"}) {
            cases.push_back({type_name(t) + " " + op, {t, t}, lift::binary(op, t, in(t, 0), in(t, 1)), nullptr, hardware});
        }
        for (const auto* op : {"==", "!=", "<", "<=", ">", ">="}) {
            cases.push_back({type_name(t) + " " + op, {t, t}, lift::binary(op, Bool, in(t, 0), in(t, 1)), nullptr});
        }
        cases.push_back({type_name(t) + " neg", {t}, lift::unary("neg", t, in(t, 0)), nullptr});
        for (const auto to : integers) {
            cases.push_back({type_name(t) + " as " + type_name(to), {t}, lift::convert(to, in(t, 0)), nullptr});
        }
        for (const auto to : {F32, F64, F16, BF16}) {
            // Widening and binary64 to binary32 are exact or hardware
            // rounded, and a format to itself is unchanged; nothing else
            // reaches a 16-bit format.
            const bool lowerable = to == F32 || to == F64 || to == t;
            cases.push_back({type_name(t) + " as " + type_name(to), {t}, lift::convert(to, in(t, 0)), nullptr,
                             lowerable});
        }
    }
    // A comparison of a sum with a constant, as the recognizer lifts it.
    cases.push_back({"float64 (a + 1.5) <= b", {F64, F64},
                     lift::binary("<=", Bool, lift::binary("+", F64, in(F64, 0), lift::immediate(F64, 0x3ff8000000000000ULL)),
                                  in(F64, 1)),
                     nullptr});
    return cases;
}

// ---------------------------------------------------------------------------
// The corpus.

struct Program {
    std::string label;
    std::vector<fs::path> files;
    std::vector<fs::path> roots;
    // Benchmarks: the size to run at, or empty.
    std::string size;
};

std::vector<Program> corpus_programs(const Environment& environment) {
    std::vector<Program> programs;
    const auto root = environment.source_dir;
    for (const auto& entry : fs::directory_iterator(root / "tests" / "backend" / "programs")) {
        if (entry.path().extension() == ".nra") {
            programs.push_back({"programs/" + entry.path().filename().string(), {entry.path()}, {}, ""});
        }
    }
    const auto examples = environment.examples();
    const std::vector<fs::path> example_roots{examples / "guide", examples / "types", examples / "testing"};
    for (const auto* driver : {"core", "overlay", "classes", "collections", "polymorphism", "errors", "resources"}) {
        programs.push_back({std::string("examples/") + driver,
                            {root / "tests" / "backend" / "examples" / (std::string(driver) + "-driver.nra")},
                            example_roots, ""});
    }
    programs.push_back({"examples/hello-world", {examples / "hello-world" / "hello-world.nra"}, {}, ""});
    programs.push_back({"examples/multi-file", {examples / "modules" / "multi-file" / "main.nra"},
                        {examples / "modules" / "multi-file"}, ""});
    for (const auto* program : {"linked-list/programs/reclamation", "regex/programs/key-values",
                                "sha256/programs/million-a", "big-integer/programs/factorial-rsa",
                                "math/programs/elementary", "strings/programs/word-report",
                                "hash-map/programs/retention-bound", "tree-set/programs/reclamation",
                                "bit-set/programs/reclamation", "array-deque/programs/reclamation",
                                "ordinal-map/programs/reclamation", "hash-core/programs/set-stack-reclamation"}) {
        programs.push_back({std::string("stdlib/") + program,
                            {root / "tests" / "stdlib" / (std::string(program) + ".nra")}, {}, ""});
    }
    for (const auto& entry : fs::directory_iterator(root / "tests" / "benchmarks")) {
        if (entry.path().extension() != ".nra") continue;
        std::ifstream file(entry.path());
        std::string line;
        std::string size;
        while (std::getline(file, line)) {
            if (line.rfind("// sizes: ", 0) == 0) {
                size = line.substr(10, line.find(' ', 10) - 10);
                break;
            }
        }
        programs.push_back({"benchmarks/" + entry.path().stem().string(), {entry.path()}, {}, size});
    }
    std::sort(programs.begin(), programs.end(), [](const Program& a, const Program& b) { return a.label < b.label; });
    return programs;
}

nvr::Image build(const Program& program) {
    backend::BuildOptions options;
    options.source_map = false;
    auto roots = program.roots;
    for (const auto& file : program.files) roots.push_back(file.parent_path());
    return nvr::load(nvr::encode(backend::build(lower_files(program.files, roots), options).image));
}

// A device that evaluates every graph by reference, counts the steps of
// each graph, and checks each step whose graph has one lowered block
// against the reference lanes.
class CheckingDevice final : public nvm::GraphDevice {
public:
    CheckingDevice(const nvr::Image& image, std::map<std::uint32_t, const lift::LoweredBlock*> lowered,
                   std::map<std::uint32_t, const nvr::Block*> blocks)
        : steps(image.graphs.size(), 0), image_(image), lowered_(std::move(lowered)), blocks_(std::move(blocks)) {}

    const nvr::Image& image() const override { return image_; }
    std::size_t batch_hint() const override { return 1; }
    nvm::GraphDeviceStatistics statistics() const override { return {}; }

    void evaluate(std::span<nvm::GraphJob> jobs) override {
        for (auto& job : jobs) {
            const auto& graph = image_.graphs[job.graph];
            ++steps[job.graph];
            job.evaluated = !nvm::evaluate_outputs(graph, job.inputs, scratch_, job.outputs).has_value();
            const auto it = lowered_.find(job.graph);
            if (it == lowered_.end() || !job.evaluated) {
                if (it != lowered_.end()) mismatch = "the reference overflowed on a lifted graph";
                continue;
            }
            const auto& lowered = *it->second;
            std::vector<std::uint64_t> values;
            std::size_t lane = 0;
            for (const auto w : lowered.input_widths) {
                std::uint64_t value = 0;
                for (unsigned b = 0; b < w; ++b) value |= static_cast<std::uint64_t>(job.inputs[lane++] & 1) << b;
                values.push_back(value);
            }
            outputs_.resize(lowered.outputs.size());
            const auto exit = lift::execute(lowered, values, outputs_);
            ++checked;
            std::uint32_t expected = 0;
            if (lowered.exit_count > 1) {
                for (unsigned j = 0; j < nvr::selector_lanes(lowered.exit_count); ++j) {
                    expected |= static_cast<std::uint32_t>(job.outputs[lowered.selector_start + j]) << j;
                }
            }
            if (!exit || *exit != expected) {
                mismatch = "graph " + std::to_string(job.graph) + ": exit differs during the run";
                continue;
            }
            if (check_trap(*blocks_.at(job.graph), *exit)) continue;
            lanes_.assign(graph.outputs.size(), -1);
            lift::write_lanes(lowered, *exit, outputs_, lanes_);
            if (!std::equal(lanes_.begin(), lanes_.end(), job.outputs.begin())) {
                mismatch = "graph " + std::to_string(job.graph) + ": lanes differ during the run";
            }
        }
    }

    std::vector<std::uint64_t> steps;
    std::uint64_t checked = 0;
    std::string mismatch;

private:
    const nvr::Image& image_;
    std::map<std::uint32_t, const lift::LoweredBlock*> lowered_;
    std::map<std::uint32_t, const nvr::Block*> blocks_;
    std::vector<std::int64_t> scratch_;
    std::vector<std::uint64_t> outputs_;
    std::vector<std::int64_t> lanes_;
};

// A copy of `form` in which node `from` is replaced by `to` wherever it is
// used, keeping every other node shared as in the original.
LiftedForm substitute(const LiftedForm& form, const Expression* from, const ExpressionPtr& to) {
    std::map<const Expression*, ExpressionPtr> done;
    const std::function<ExpressionPtr(const ExpressionPtr&)> copy = [&](const ExpressionPtr& e) -> ExpressionPtr {
        if (!e) return e;
        if (e.get() == from) return to;
        if (const auto it = done.find(e.get()); it != done.end()) return it->second;
        auto node = std::make_shared<Expression>(*e);
        bool changed = false;
        for (auto& operand : node->operands) {
            auto replaced = copy(operand);
            changed = changed || replaced != operand;
            operand = std::move(replaced);
        }
        ExpressionPtr result = changed ? ExpressionPtr(std::move(node)) : e;
        done.emplace(e.get(), result);
        return result;
    };
    auto out = form;
    for (auto& e : out.schedule) e = copy(e);
    for (auto& [lane, e] : out.values) e = copy(e);
    for (auto& [lane, e] : out.order) e = copy(e);
    out.condition = copy(out.condition);
    return out;
}

// Every node of a form, operands before users.
std::vector<const Expression*> form_nodes(const LiftedForm& form) {
    std::vector<const Expression*> nodes;
    std::set<const Expression*> seen;
    const std::function<void(const Expression*)> visit = [&](const Expression* e) {
        if (!e || !seen.insert(e).second) return;
        for (const auto& operand : e->operands) visit(operand.get());
        nodes.push_back(e);
    };
    for (const auto& e : form.schedule) visit(e.get());
    for (const auto& [lane, e] : form.values) visit(e.get());
    visit(form.condition.get());
    return nodes;
}

std::string percent(double part, double whole) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << (whole > 0 ? 100.0 * part / whole : 0.0) << '%';
    return out.str();
}

} // namespace

// ---------------------------------------------------------------------------

NEURA_TEST(proof, regenerated_blocks_are_accepted_and_edits_are_not) {
    auto image = op_image();
    LiftedForm form;
    const auto value = lift::binary("+", I32, lift::input(I32, 0), lift::input(I32, 1));
    check(construct(image, {I32, I32}, value, nullptr, form), "the block assembles");
    const auto& block = image.functions[0].blocks[0];
    const auto& graph = image.graphs[0];
    check(lift::prove_structure(image, block, graph, form), "the regenerated block proves");
    auto accepted = lift::lift_and_prove(image, 0, 0, &form);
    check(accepted.block != nullptr, "the hint is accepted: " + accepted.rejection);

    // One weight changed: the structural proof fails, by hint and by search.
    std::size_t edited = 0;
    for (std::size_t n = 0; n < graph.nodes.size() && edited == 0; ++n) {
        if (graph.nodes[n].edges.empty()) continue;
        auto corrupted = graph;
        corrupted.nodes[n].edges.front().weight += 1;
        check(!lift::prove_structure(image, block, corrupted, form), "an edited weight fails the proof");
        auto edited_image = image;
        edited_image.graphs[0] = corrupted;
        const auto by_hint = lift::lift_and_prove(edited_image, 0, 0, &form);
        check(!by_hint.block, "an edited graph is not accepted from a hint");
        const auto by_search = lift::lift_and_prove(edited_image, 0, 0);
        check(!by_search.block, "an edited graph is not accepted by search");
        ++edited;
    }
    check(edited == 1, "the graph has an edge to edit");
    // A changed bias fails too.
    auto biased = graph;
    biased.nodes.back().bias += 1;
    check(!lift::prove_structure(image, block, biased, form), "an edited bias fails the proof");

    // Forged hints: another operation, swapped operands, a missing input,
    // and an operand of another width.
    auto forged = form;
    forged.values[0] = lift::binary("-", I32, lift::input(I32, 0), lift::input(I32, 1));
    check(!lift::lift_and_prove(image, 0, 0, &forged).block, "a forged operation is rejected");
    forged.values[0] = lift::binary("+~", I32, lift::input(I32, 0), lift::input(I32, 1));
    check(!lift::lift_and_prove(image, 0, 0, &forged).block, "an unchecked addition is not a checked one");
    forged.values[0] = lift::binary("+", I32, lift::input(I32, 0), lift::input(I32, 7));
    const auto missing = lift::lift_and_prove(image, 0, 0, &forged);
    check(!missing.block && missing.rejection.find("ill-typed") != std::string::npos,
          "a hint naming a missing input is rejected before replay: " + missing.rejection);
    forged.values[0] = lift::binary("+", I32, lift::input(I32, 0), lift::immediate(I64, 3));
    check(!lift::lift_and_prove(image, 0, 0, &forged).block, "operands of different widths are rejected");
    forged.values.clear();
    check(!lift::lift_and_prove(image, 0, 0, &forged).block, "a hint without the value is rejected");
}

NEURA_TEST(proof, corpus_forms_reprove_and_edits_fail) {
    // Every block the recognizer lifts in a compiled program re-proves from
    // its form, and changing any one weight of its graph breaks the proof.
    const auto programs = corpus_programs(environment);
    const auto it = std::find_if(programs.begin(), programs.end(),
                                 [](const Program& p) { return p.label == "programs/computation.nra"; });
    check(it != programs.end(), "the corpus has the computation program");
    const auto image = build(*it);
    std::size_t proven = 0;
    std::size_t edits = 0;
    for (std::uint32_t f = 0; f < image.functions.size(); ++f) {
        for (std::uint32_t b = 0; b < image.functions[f].blocks.size(); ++b) {
            const auto recognition = lift::recognize(image, f, b);
            if (recognition.status != lift::Recognition::Status::Lifted) continue;
            const auto& block = image.functions[f].blocks[b];
            const auto& graph = image.graphs[block.graph];
            check(lift::prove_structure(image, block, graph, recognition.form), "a recognized form re-proves");
            ++proven;
            for (std::size_t n = 0; n < graph.nodes.size(); n += 7) {
                for (std::size_t e = 0; e < graph.nodes[n].edges.size(); e += 5) {
                    auto corrupted = graph;
                    corrupted.nodes[n].edges[e].weight -= 2;
                    check(!lift::prove_structure(image, block, corrupted, recognition.form),
                          "an edited weight fails the proof of a corpus block");
                    ++edits;
                }
            }
        }
    }
    check(proven > 20 && edits > 100, "the corpus exercises the proof");
    std::cout << "  " << proven << " recognized blocks re-proved, " << edits << " edited graphs rejected\n";
}

NEURA_TEST(overflow, intervals_bound_reference_evaluation) {
    // relu(a + b - 1): a bit.
    nvr::Graph conjunction;
    conjunction.input_lanes = 2;
    conjunction.nodes.push_back({nvr::Activation::Relu, -1, {{0, 1}, {1, 1}}});
    conjunction.outputs = {2};
    auto proof = lift::prove_no_overflow(conjunction);
    check(proof.safe, "a conjunction cannot overflow");
    const std::vector<std::uint32_t> lane0{0};
    check(lift::lanes_are_bits(proof, conjunction, lane0), "a conjunction is a bit");

    // The bias at the maximum plus an input: overflows when the input is 1.
    nvr::Graph sum;
    sum.input_lanes = 1;
    sum.nodes.push_back({nvr::Activation::Identity, std::numeric_limits<std::int64_t>::max(), {{0, 1}}});
    sum.outputs = {1};
    proof = lift::prove_no_overflow(sum);
    check(!proof.safe && proof.node == 0, "a sum past the maximum is not excluded");
    // The same at one below the maximum is safe, and not a bit.
    sum.nodes[0].bias -= 1;
    proof = lift::prove_no_overflow(sum);
    check(proof.safe, "a sum reaching the maximum is safe");
    check(!lift::lanes_are_bits(proof, sum, lane0), "a large wire is not a bit");

    // A product that overflows: weight 2^62 times a wire that reaches 2.
    nvr::Graph product;
    product.input_lanes = 2;
    product.nodes.push_back({nvr::Activation::Identity, 0, {{0, 1}, {1, 1}}});
    product.nodes.push_back({nvr::Activation::Identity, 0, {{2, std::int64_t{1} << 62}}});
    product.outputs = {3};
    proof = lift::prove_no_overflow(product);
    check(!proof.safe && proof.node == 1, "a product past the range is not excluded");

    // A partial sum that overflows although the final sum would not: the
    // reference checks every running sum.
    nvr::Graph partial;
    partial.input_lanes = 2;
    partial.nodes.push_back({nvr::Activation::Identity, std::numeric_limits<std::int64_t>::max(),
                             {{0, 1}, {1, -1}}});
    partial.outputs = {2};
    proof = lift::prove_no_overflow(partial);
    check(!proof.safe, "a running sum past the maximum is not excluded");

    // A negative ReLU input clamps to zero.
    nvr::Graph clamp;
    clamp.input_lanes = 1;
    clamp.nodes.push_back({nvr::Activation::Relu, -5, {{0, 2}}});
    clamp.outputs = {1};
    proof = lift::prove_no_overflow(clamp);
    check(proof.safe && proof.wires[1].lo == 0 && proof.wires[1].hi == 0, "ReLU clamps the interval");
}

NEURA_TEST(overflow, unrefined_and_bounded_proofs_agree_with_the_full_proof) {
    // The products of an 8-bit multiplier need the refined analysis. With
    // too little work it stops undecided; with enough, or none given, it
    // proves what the unbounded analysis proves.
    auto image = op_image();
    LiftedForm form;
    check(construct(image, {U8, U8}, lift::binary("*", U8, lift::input(U8, 0), lift::input(U8, 1)), nullptr, form),
          "the block assembles");
    const auto& graph = image.graphs[0];
    std::vector<std::uint32_t> lanes(graph.outputs.size());
    for (std::uint32_t i = 0; i < lanes.size(); ++i) lanes[i] = i;
    const auto full = lift::prove_no_overflow(graph);
    check(full.safe && lift::lanes_are_bits(full, graph, lanes), "the full analysis proves the multiplier");
    const auto quick = lift::prove_no_overflow(graph, false);
    check(quick.safe, "the unrefined analysis excludes overflow");
    std::uint64_t none = 0;
    const auto starved = lift::prove_no_overflow(graph, true, &none);
    check(!starved.safe && starved.exhausted, "no work decides nothing");
    std::uint64_t plenty = std::numeric_limits<std::uint64_t>::max();
    const auto bounded = lift::prove_no_overflow(graph, true, &plenty);
    check(bounded.safe && !bounded.exhausted, "enough work proves it");
    check(bounded.wires.size() == full.wires.size(), "the bounded proof is the full proof");
    for (std::size_t w = 0; w < full.wires.size(); ++w) {
        check(bounded.wires[w].lo == full.wires[w].lo && bounded.wires[w].hi == full.wires[w].hi,
              "wire " + std::to_string(w) + " has the same interval");
    }
    // Resumed in small pieces, the analysis settles a few nodes at a time
    // and ends with the proof the unbounded analysis finds.
    // A node that needs more than a piece makes no progress until the
    // pieces grow, as the executor's allowances do.
    lift::OverflowProof partial;
    std::size_t pieces = 0;
    std::uint64_t size = 64;
    for (; pieces < 10000; ++pieces) {
        std::uint64_t piece = size;
        auto next = lift::prove_no_overflow(graph, true, &piece, pieces ? &partial : nullptr);
        if (!next.exhausted) {
            partial = std::move(next);
            break;
        }
        check(pieces == 0 || next.node >= partial.node, "a resumed analysis does not go back");
        if (pieces != 0 && next.node == partial.node) size *= 2;
        partial = std::move(next);
    }
    check(partial.safe && pieces > 2, "the analysis finishes in pieces");
    check(partial.wires.size() == full.wires.size(), "the resumed proof covers every wire");
    for (std::size_t w = 0; w < full.wires.size() && w < partial.wires.size(); ++w) {
        check(partial.wires[w].lo == full.wires[w].lo && partial.wires[w].hi == full.wires[w].hi,
              "wire " + std::to_string(w) + " has the same interval when resumed");
    }
    lift::GraphProofs proofs;
    check_equal(proofs.verdict(graph, 0, 0), std::string(lift::exhausted_work), "a starved verdict is not final");
    check(proofs.verdict(graph, 0).empty(), "an unbounded verdict proves the graph");
    check(proofs.verdict(graph, 0, 0).empty(), "a proven graph stays proven");
}

NEURA_TEST(overflow, bounded_work_counts_the_evaluations_made_and_resumes_to_the_full_proof) {
    // The case analysis charges the caller's work for the nodes it
    // evaluates: an enumeration it has already made costs nothing again, and
    // after the first assignment only the nodes that depend on a variable
    // that changed are evaluated again. One unit per node per assignment
    // would need 678,585 units for the 8-bit multiplier; the analysis proves
    // it with 500,000. With any bound, a proof found, at once or resumed in
    // pieces, is the proof found without one: same intervals, same groups.
    const auto same = [](const lift::OverflowProof& a, const lift::OverflowProof& b) {
        if (a.wires.size() != b.wires.size() || a.groups.size() != b.groups.size() || a.group_of != b.group_of) {
            return false;
        }
        for (std::size_t w = 0; w < a.wires.size(); ++w) {
            if (a.wires[w].lo != b.wires[w].lo || a.wires[w].hi != b.wires[w].hi) return false;
        }
        for (std::size_t g = 0; g < a.groups.size(); ++g) {
            if (a.groups[g].members != b.groups[g].members || a.groups[g].range.lo != b.groups[g].range.lo ||
                a.groups[g].range.hi != b.groups[g].range.hi) {
                return false;
            }
        }
        return true;
    };
    const auto lanes_of = [](const nvr::Graph& graph) {
        std::vector<std::uint32_t> lanes(graph.outputs.size());
        std::iota(lanes.begin(), lanes.end(), 0U);
        return lanes;
    };
    {
        auto image = op_image();
        LiftedForm form;
        check(construct(image, {U8, U8}, lift::binary("*", U8, lift::input(U8, 0), lift::input(U8, 1)), nullptr, form),
              "the multiplier assembles");
        const auto& graph = image.graphs[0];
        const auto full = lift::prove_no_overflow(graph);
        std::uint64_t work = 500'000;
        const auto bounded = lift::prove_no_overflow(graph, true, &work);
        check(bounded.safe && !bounded.exhausted, "500,000 units prove the multiplier");
        check(same(bounded, full), "the bounded proof is the full proof");
        check(lift::lanes_are_bits(bounded, graph, lanes_of(graph), nullptr, true, &work),
              "the remaining work proves the lanes");
    }
    // Float addition proves its lanes by cases and narrows its wires by
    // elimination and by enumerations that repeat across rounds: resumed
    // from every point where its work ran out, it ends with the full proof.
    auto image = op_image();
    LiftedForm form;
    check(construct(image, {F32, F32}, lift::binary("+", F32, lift::input(F32, 0), lift::input(F32, 1)), nullptr,
                    form),
          "the float32 addition assembles");
    const auto& graph = image.graphs[0];
    const auto full = lift::prove_no_overflow(graph);
    check(full.safe && lift::lanes_are_bits(full, graph, lanes_of(graph)), "the full analysis proves the addition");
    lift::OverflowProof partial;
    std::size_t pieces = 0;
    std::uint64_t size = 64;
    for (; pieces < 100000; ++pieces) {
        std::uint64_t piece = size;
        auto next = lift::prove_no_overflow(graph, true, &piece, pieces ? &partial : nullptr);
        if (!next.exhausted) {
            partial = std::move(next);
            break;
        }
        check(pieces == 0 || next.node >= partial.node, "a resumed analysis does not go back");
        if (pieces != 0 && next.node == partial.node) size *= 2;
        partial = std::move(next);
    }
    check(partial.safe && pieces > 2, "the addition's analysis finishes in pieces");
    check(same(partial, full), "the resumed proof is the full proof");
}

NEURA_TEST(proof, hint_sections_round_trip_and_malformed_ones_are_ignored) {
    // A shared form: x = a + b feeds both values, and a discarded product
    // is computed for its check only.
    auto image = op_image();
    const auto a = lift::input(I32, 0);
    const auto b = lift::input(I32, 1);
    const auto sum = lift::binary("+", I32, a, b);
    LiftedForm form;
    form.shared = true;
    form.schedule = {sum, lift::binary("*", I32, a, b), lift::binary("<", Bool, sum, lift::immediate(I32, 7))};
    form.values[1] = sum;
    form.condition = form.schedule.back();
    form.exit_when_true = 0;
    std::vector<lift::BlockHint> hints{{0, 3, form}, {0, 1, form}, {2, 0, form}};
    const auto section = lift::encode_hints(hints);
    check(section.tag == lift::hint_tag, "the section is tagged LIFT");
    const auto decoded = lift::decode_hints(std::span<const std::uint8_t>(section.data));
    check_equal(decoded.size(), std::size_t{3}, "every entry decodes");
    const auto& back = *decoded.at({0, 1});
    check(back.shared && back.schedule.size() == 3 && back.values.size() == 1 && back.condition &&
              back.exit_when_true == 0,
          "the form decodes whole");
    check(back.values.at(1) == back.schedule[0], "a shared node stays shared");
    check(back.condition == back.schedule[2] && back.schedule[2]->operands[0] == back.schedule[0],
          "the condition reads the shared sum");
    check(decoded.at({0, 1}) == decoded.at({0, 3}), "identical forms are stored once");
    check(lift::encode_hints(hints).data == section.data, "the encoding is deterministic");

    // Every truncation, and a byte too many, is ignored whole.
    for (std::size_t n = 0; n < section.data.size(); ++n) {
        const std::vector<std::uint8_t> cut(section.data.begin(), section.data.begin() + static_cast<std::ptrdiff_t>(n));
        check(lift::decode_hints(std::span<const std::uint8_t>(cut)).empty(), "a truncated section is ignored");
    }
    auto longer = section.data;
    longer.push_back(0);
    check(lift::decode_hints(std::span<const std::uint8_t>(longer)).empty(), "trailing bytes are ignored");
    // An operand that names a later node would make a cycle.
    auto forward = section.data;
    // The first form's third node (a binary) starts after the header (8),
    // the node count (4), and two input nodes (9 each).
    const std::size_t third = 8 + 4 + 9 + 9;
    check(forward[third] == 3, "the third node is a binary operation");
    forward[third + 6] = 9;
    check(lift::decode_hints(std::span<const std::uint8_t>(forward)).empty(), "a forward operand is ignored");
}

NEURA_TEST(proof, recorded_constructions_prove_and_mutated_hints_do_not) {
    // Blocks of compiled programs whose LIFT hints describe the length
    // narrowing, the loops of generated functions (concatenation, text and
    // value members), the nil tests of value members, the hash steps, and
    // float negation. Each genuine hint is accepted; a hint with a wrong operand, a
    // wrong constant, a wrong width or signedness, another operation, or the
    // exits swapped is rejected.
    const auto root = environment.source_dir;
    const std::vector<fs::path> files{
        root / "tests" / "benchmarks" / "strings.nra",
        root / "tests" / "backend" / "programs" / "ranges-and-variadics.nra",
        root / "tests" / "backend" / "programs" / "class-nil.nra",
        root / "tests" / "backend" / "programs" / "stdlib-primitives.nra",
        root / "tests" / "bench-wall" / "neura" / "nbody.nra",
        root / "tests" / "bench-wall" / "neura" / "hash-map.nra",
    };
    std::map<std::string, std::size_t> found;
    std::size_t mutants = 0;
    for (const auto& file : files) {
        const auto image = build({file.filename().string(), {file}, {}, ""});
        const auto type_is = [&](const Expression* e, nvr::TypeKind kind, unsigned width) {
            const auto& t = image.types.at(e->type);
            return t.kind == kind && (width == 0 || t.width == width);
        };
        const auto find_type = [&](nvr::TypeKind kind, unsigned width) -> std::optional<std::uint32_t> {
            for (std::uint32_t t = 0; t < image.types.size(); ++t) {
                if (image.types[t].kind == kind && image.types[t].width == width) return t;
            }
            return std::nullopt;
        };
        const auto is_input = [&](const ExpressionPtr& e, nvr::TypeKind kind, unsigned width) {
            return e->kind == Expression::Kind::Input && type_is(e.get(), kind, width);
        };
        const auto is_immediate = [](const ExpressionPtr& e) { return e->kind == Expression::Kind::Immediate; };
        for (const auto& [key, hinted] : lift::decode_hints(image)) {
            const auto [f, b] = key;
            const auto& form = *hinted;
            const auto where = file.filename().string() + " function " + std::to_string(f) + " block " +
                               std::to_string(b);
            std::vector<std::pair<std::string, LiftedForm>> wrong;
            const auto replace = [&](std::string what, const Expression* node, ExpressionPtr by) {
                wrong.emplace_back(std::move(what), substitute(form, node, std::move(by)));
            };
            const auto swap_exits = [&](const std::string& what) {
                auto swapped = form;
                swapped.exit_when_true = 1 - *form.exit_when_true;
                wrong.emplace_back(what + ": exits swapped", std::move(swapped));
            };
            const auto is_condition = [&](const Expression* e) { return form.condition.get() == e; };
            std::string label;
            for (const auto* e : form_nodes(form)) {
                const auto& ops = e->operands;
                if (e->kind == Expression::Kind::Convert && type_is(e, nvr::TypeKind::Signed, 32) &&
                    is_input(ops[0], nvr::TypeKind::Unsigned, 64)) {
                    label = "length narrowing";
                    if (const auto u32 = find_type(nvr::TypeKind::Unsigned, 32)) {
                        replace(label + ": unsigned target", e, lift::convert(*u32, ops[0]));
                    }
                    replace(label + ": constant operand", e, lift::convert(e->type, lift::immediate(ops[0]->type, 3)));
                    if (form.exit_when_true) swap_exits(label);
                } else if (e->kind == Expression::Kind::Binary && e->op == "<" && is_condition(e) &&
                           is_input(ops[0], nvr::TypeKind::Unsigned, 64) && is_input(ops[1], nvr::TypeKind::Unsigned, 64) &&
                           ops[0]->input != ops[1]->input) {
                    label = "index loop test";
                    replace(label + ": operands swapped", e, lift::binary("<", e->type, ops[1], ops[0]));
                    replace(label + ": '<='", e, lift::binary("<=", e->type, ops[0], ops[1]));
                    swap_exits(label);
                } else if (e->kind == Expression::Kind::Binary && e->op == "+~" &&
                           is_input(ops[0], nvr::TypeKind::Unsigned, 64) && is_immediate(ops[1]) && ops[1]->bits == 1) {
                    label = "index advance";
                    replace(label + ": constant 2", e, lift::binary("+~", e->type, ops[0], lift::immediate(ops[1]->type, 2)));
                    replace(label + ": checked", e, lift::binary("+", e->type, ops[0], ops[1]));
                    if (const auto u32 = find_type(nvr::TypeKind::Unsigned, 32)) {
                        replace(label + ": 32 bits", e,
                                lift::binary("+~", *u32, lift::convert(*u32, ops[0]), lift::immediate(*u32, 1)));
                    }
                } else if (e->kind == Expression::Kind::Binary && e->op == "==" && is_condition(e) &&
                           is_input(ops[0], nvr::TypeKind::Unsigned, 64) && is_input(ops[1], nvr::TypeKind::Unsigned, 64)) {
                    label = "length equality";
                    replace(label + ": '!='", e, lift::binary("!=", e->type, ops[0], ops[1]));
                    replace(label + ": one operand twice", e, lift::binary("==", e->type, ops[0], ops[0]));
                    swap_exits(label);
                } else if (e->kind == Expression::Kind::Binary && e->op == "!=" && is_condition(e) &&
                           is_input(ops[0], nvr::TypeKind::Unsigned, 64) && is_immediate(ops[1])) {
                    label = "nonzero index";
                    replace(label + ": constant 1", e, lift::binary("!=", e->type, ops[0], lift::immediate(ops[1]->type, 1)));
                    swap_exits(label);
                } else if (e->kind == Expression::Kind::Binary && e->op == "==" && ops[0]->kind == Expression::Kind::Input &&
                           nvr::is_reference(image.types.at(ops[0]->type).kind) && is_immediate(ops[1])) {
                    label = "nil test";
                    replace(label + ": another handle", e,
                            lift::binary("==", e->type, ops[0], lift::immediate(ops[1]->type, 1)));
                    replace(label + ": '!='", e, lift::binary("!=", e->type, ops[0], ops[1]));
                    for (const auto* user : form_nodes(form)) {
                        if (user->kind == Expression::Kind::Binary && (user->op == "&&" || user->op == "||")) {
                            replace(label + ": other connective", user,
                                    lift::binary(user->op == "&&" ? "||" : "&&", user->type, user->operands[0],
                                                 user->operands[1]));
                        }
                    }
                    if (form.exit_when_true) swap_exits(label);
                } else if (e->kind == Expression::Kind::Binary && e->op == "+~" && type_is(e, nvr::TypeKind::Unsigned, 32) &&
                           ops[0]->kind == Expression::Kind::Binary && ops[0]->op == "-~") {
                    label = "hash step";
                    const auto& difference = ops[0];
                    const auto& shifted = difference->operands[0];
                    const auto& hash = difference->operands[1];
                    replace(label + ": checked subtraction", difference.get(),
                            lift::binary("-", difference->type, shifted, hash));
                    replace(label + ": addition for the subtraction", difference.get(),
                            lift::binary("+~", difference->type, shifted, hash));
                    replace(label + ": operands of the subtraction swapped", difference.get(),
                            lift::binary("-~", difference->type, hash, shifted));
                    replace(label + ": the hash added", e, lift::binary("+~", e->type, difference, hash));
                    if (shifted->kind == Expression::Kind::Binary && shifted->op == "<<") {
                        replace(label + ": shifted by 4", shifted.get(),
                                lift::binary("<<", shifted->type, shifted->operands[0],
                                             lift::immediate(shifted->operands[1]->type, 4)));
                    }
                    if (const auto u64 = find_type(nvr::TypeKind::Unsigned, 64)) {
                        replace(label + ": 64 bits", e,
                                lift::convert(e->type, lift::binary("+~", *u64, lift::convert(*u64, difference),
                                                                    lift::convert(*u64, ops[1]))));
                    }
                } else if (e->kind == Expression::Kind::Unary && e->op == "hash" && ops[0]->kind == Expression::Kind::Input) {
                    label = "value hash";
                    replace(label + ": constant operand", e, lift::unary("hash", e->type, lift::immediate(ops[0]->type, 0)));
                    replace(label + ": result changed", e, lift::binary("^", e->type, lift::unary("hash", e->type, ops[0]), lift::immediate(e->type, 1)));
                    if (is_input(ops[0], nvr::TypeKind::Signed, 0) || is_input(ops[0], nvr::TypeKind::Unsigned, 0)) {
                        replace(label + ": a conversion", e, lift::convert(e->type, ops[0]));
                    }
                    if (is_input(ops[0], nvr::TypeKind::Bool, 0)) {
                        replace(label + ": negated operand", e, lift::unary("hash", e->type, lift::unary("!", ops[0]->type, ops[0])));
                    }
                } else if (e->kind == Expression::Kind::Unary && e->op == "neg" && is_input(ops[0], nvr::TypeKind::Float, 0)) {
                    label = "float negation";
                    replace(label + ": the operand itself", e, ops[0]);
                    if (const auto f32 = find_type(nvr::TypeKind::Float, 32); f32 && !type_is(e, nvr::TypeKind::Float, 32)) {
                        replace(label + ": another width", e, lift::unary("neg", *f32, lift::input(*f32, ops[0]->input)));
                    }
                } else {
                    continue;
                }
                break;
            }
            if (label.empty()) continue;
            const auto genuine = lift::lift_and_prove(image, f, b, &form);
            check(genuine.block != nullptr, where + " (" + label + "): the genuine hint is rejected: " + genuine.rejection);
            ++found[label];
            for (const auto& [what, mutant] : wrong) {
                const auto result = lift::lift_and_prove(image, f, b, &mutant);
                check(!result.block, where + ": a mutated hint is accepted (" + what + ")");
                ++mutants;
            }
        }
    }
    for (const auto* label : {"length narrowing", "index loop test", "index advance", "length equality",
                              "nonzero index", "float negation",
                              "nil test", "hash step", "value hash"}) {
        check(found[label] > 0, std::string("no hinted block of the construction: ") + label);
        std::cout << "  " << label << ": " << found[label] << " blocks\n";
    }
    std::cout << "  " << mutants << " mutated hints rejected\n";
}

NEURA_TEST(overflow, exclusive_indicators_sum_to_a_bit) {
    // Equality tests of the same 24 lanes against different constants are
    // exclusive, so their sum is a bit although its interval is [0, 3], and
    // the lanes are too many for case analysis: the dispatch selector shape.
    nvr::Graph graph;
    graph.input_lanes = 24;
    for (const std::uint32_t constant : {5U, 6U, 0xabcdefU}) {
        nvr::Node test{nvr::Activation::Relu, 1, {}};
        for (std::uint32_t lane = 0; lane < 24; ++lane) {
            const bool one = ((constant >> lane) & 1U) != 0;
            test.edges.push_back({lane, one ? 1 : -1});
            test.bias -= one ? 1 : 0;
        }
        graph.nodes.push_back(test);
    }
    graph.nodes.push_back({nvr::Activation::Identity, 0, {{24, 1}, {25, 1}, {26, 1}}});
    graph.outputs = {27};
    auto proof = lift::prove_no_overflow(graph);
    const std::vector<std::uint32_t> lane0{0};
    lift::BitEvidence evidence;
    check(proof.safe && proof.wires[27].hi == 3, "the interval of the sum is [0, 3]");
    check(lift::lanes_are_bits(proof, graph, lane0, &evidence) && evidence.one_hot == 1,
          "exclusive patterns sum to a bit");
    // Two equal patterns can hold together.
    graph.nodes[1] = graph.nodes[0];
    proof = lift::prove_no_overflow(graph);
    check(!lift::lanes_are_bits(proof, graph, lane0), "overlapping indicators are not a bit");

    // Trap selection as the block synthesizer builds it: the second check
    // fires only when the first is clear, which case analysis establishes.
    nvr::Graph traps;
    traps.input_lanes = 3;  // condition a, condition b, exit condition c
    traps.nodes.push_back({nvr::Activation::Relu, 0, {{0, -1}, {2, 1}}});     // 3: clear = !a (and c)
    traps.nodes.push_back({nvr::Activation::Relu, -1, {{1, 1}, {3, 1}}});     // 4: b && clear
    traps.nodes.push_back({nvr::Activation::Identity, 0, {{0, 1}, {4, 1}}});  // 5: a + (b && clear)
    traps.outputs = {5};
    proof = lift::prove_no_overflow(traps);
    check(proof.safe && proof.wires[5].lo == 0 && proof.wires[5].hi == 1,
          "case analysis narrows the trap selector to a bit");
    check(lift::lanes_are_bits(proof, traps, lane0), "the trap selector is a bit");
    // Without the exclusion (b alone instead of b && clear) the sum reaches 2.
    traps.nodes[2] = {nvr::Activation::Identity, 0, {{0, 1}, {1, 1}}};
    proof = lift::prove_no_overflow(traps);
    check(!lift::lanes_are_bits(proof, traps, lane0), "independent indicators are not a bit");

    // A ripple-carry sum lane: a + b + c - 2 * carry, where the carry is the
    // step pair relu(a + b + c - 1) - relu(a + b + c - 2).
    nvr::Graph adder;
    adder.input_lanes = 3;
    adder.nodes.push_back({nvr::Activation::Relu, -1, {{0, 1}, {1, 1}, {2, 1}}});  // 3: P
    adder.nodes.push_back({nvr::Activation::Relu, -2, {{0, 1}, {1, 1}, {2, 1}}});  // 4: Q
    adder.nodes.push_back({nvr::Activation::Identity, 0, {{0, 1}, {1, 1}, {2, 1}, {3, -2}, {4, 2}}});
    adder.nodes.push_back({nvr::Activation::Identity, 0, {{3, 1}, {4, -1}}});  // the carry
    adder.outputs = {5, 6};
    proof = lift::prove_no_overflow(adder);
    check(proof.safe && proof.group_of[3] != nvr::absent && proof.group_of[3] == proof.group_of[4],
          "the carry is a step pair");
    const std::vector<std::uint32_t> both{0, 1};
    check(lift::lanes_are_bits(proof, adder, both), "the sum and carry lanes are bits");
}

NEURA_TEST(execute, operations_match_the_reference) {
    // Each operation as the converter emits it, over boundary, random, and
    // (up to 16 input lanes) every input value.
    std::size_t vectors = 0;
    std::size_t lowered_cases = 0;
    std::vector<std::string> refused;
    std::vector<std::string> unproven;
    std::vector<std::string> unexpected;
    for (const auto& c : op_cases()) {
        auto image = op_image();
        LiftedForm form;
        if (!construct(image, c.inputs, c.value, c.condition, form)) {
            fail(c.label + ": the circuit library does not assemble it");
        }
        const auto result = lift::lift_and_prove(image, 0, 0, &form);
        if (!result.block) {
            // The 0/1 lane arguments do not yet cover every float circuit
            // (float addition, subtraction, and multiplication, and the
            // wider float-to-integer conversions); such a block stays on
            // the reference path. Everything else must be proven.
            const bool float_case = std::any_of(c.inputs.begin(), c.inputs.end(), [](std::uint32_t t) {
                return t == F32 || t == F64 || t == F16 || t == BF16;
            });
            if (!c.expect_lowered) {
                refused.push_back(c.label + " (" + result.rejection + ")");
            } else if (float_case && result.rejection == "an output lane not proven 0 or 1") {
                unproven.push_back(c.label);
            } else {
                unexpected.push_back(c.label + ": " + result.rejection);
            }
            continue;
        }
        check(c.expect_lowered, c.label + ": lowered although its result is not bit-exact by hardware");
        ++lowered_cases;
        const auto& block = image.functions[0].blocks[0];
        const bool large = image.graphs[0].nodes.size() > 20000;
        const auto inputs = input_vectors(image, c.inputs, large ? 48 : 300, std::hash<std::string>{}(c.label));
        const auto comparison = compare(image, block, *result.block, inputs);
        check(comparison.mismatch.empty(), c.label + ": " + comparison.mismatch);
        vectors += comparison.vectors;
    }
    std::cout << "  " << lowered_cases << " operations lowered and matched on " << vectors << " input vectors\n";
    std::cout << "  refused, staying on the reference path:\n";
    for (const auto& label : refused) std::cout << "    " << label << '\n';
    std::cout << "  lowerable, but an output lane is not proven 0 or 1, so on the reference path:\n";
    for (const auto& label : unproven) std::cout << "    " << label << '\n';
    for (const auto& label : unexpected) std::cout << "  not lifted: " << label << '\n';
    check(unproven.size() <= 20, "the 0/1 lane proof covers most float operations");
    check(unexpected.empty(), std::to_string(unexpected.size()) + " operations were not lifted");
}

NEURA_TEST(execute, wrapping_subtraction_matches_the_reference) {
    // "-~" of every integer type: exhaustive at 8 bits, and at the other
    // widths the boundary values crossed with each other and 100,000 seeded
    // random pairs, each lane against the reference evaluator.
    std::size_t vectors = 0;
    for (const auto t : {I8, U8, I16, U16, I32, U32, I64, U64}) {
        auto image = op_image();
        LiftedForm form;
        const auto value = lift::binary("-~", t, lift::input(t, 0), lift::input(t, 1));
        check(construct(image, {t, t}, value, nullptr, form), type_name(t) + " -~: the block assembles");
        const auto result = lift::lift_and_prove(image, 0, 0, &form);
        check(result.block != nullptr, type_name(t) + " -~: not lifted: " + result.rejection);
        check(result.block->code.size() == 1 && result.block->code[0].op == lift::Opcode::SubtractWrapping,
              type_name(t) + " -~: lowered to one wrapping subtraction");
        std::vector<std::vector<std::uint64_t>> inputs;
        if (nvr::lane_count(image.types[t]) == 8) {
            inputs = input_vectors(image, {t, t}, 0, 0);
            check_equal(inputs.size(), std::size_t{65536}, "every pair of bytes");
        } else {
            inputs = input_vectors(image, {t, t}, 100000, 0x5eb7 + t);
        }
        const auto comparison = compare(image, image.functions[0].blocks[0], *result.block, inputs);
        check(comparison.mismatch.empty(), type_name(t) + " -~: " + comparison.mismatch);
        vectors += comparison.vectors;
        // The circuit of "-~" is that of "-" without its check.
        auto checked = op_image();
        LiftedForm checked_form;
        check(construct(checked, {t, t}, lift::binary("-", t, lift::input(t, 0), lift::input(t, 1)), nullptr,
                        checked_form),
              "the checked subtraction assembles");
        check(!lift::prove_structure(image, image.functions[0].blocks[0], image.graphs[0], checked_form),
              type_name(t) + ": a checked subtraction does not prove the wrapping block");
        check(!lift::prove_structure(checked, checked.functions[0].blocks[0], checked.graphs[0], form),
              type_name(t) + ": a wrapping subtraction does not prove the checked block");
    }
    std::cout << "  " << vectors << " input vectors matched\n";
}

NEURA_TEST(execute, value_hash_matches_the_reference) {
    // "hash" of every scalar type, as hash_code builds it: exhaustive where
    // the operand has at most 16 lanes (bool, 8- and 16-bit integers, and
    // both 16-bit float formats, every NaN included), and otherwise the
    // boundary values (NaNs with several payloads among them) and 100,000
    // seeded random values, each lane against the reference evaluator.
    std::size_t vectors = 0;
    for (const auto t : {Bool, I8, U8, I16, U16, I32, U32, I64, U64, F16, BF16, F32, F64}) {
        auto image = op_image();
        LiftedForm form;
        const auto value = lift::unary("hash", U32, lift::input(t, 0));
        check(construct(image, {t}, value, nullptr, form), type_name(t) + " hash: the block assembles");
        const auto result = lift::lift_and_prove(image, 0, 0, &form);
        check(result.block != nullptr, type_name(t) + " hash: not lifted: " + result.rejection);
        check(result.block->code.size() == 1 && result.block->code[0].op == lift::Opcode::ValueHash &&
                  !result.block->uses_float,
              type_name(t) + " hash: lowered to one value hash that needs no float environment");
        const auto lanes = nvr::lane_count(image.types[t]);
        auto inputs = input_vectors(image, {t}, lanes <= 16 ? 0 : 100000, 0x4a5b + t);
        if (lanes <= 16) check_equal(inputs.size(), std::size_t{1} << lanes, type_name(t) + ": every value");
        const auto comparison = compare(image, image.functions[0].blocks[0], *result.block, inputs);
        check(comparison.mismatch.empty(), type_name(t) + " hash: " + comparison.mismatch);
        vectors += comparison.vectors;
    }
    // The hash is not a conversion: a conversion's block does not prove the
    // hash's, nor the other way round.
    auto image = op_image();
    LiftedForm form;
    check(construct(image, {I64}, lift::unary("hash", U32, lift::input(I64, 0)), nullptr, form), "int64 hash");
    auto other = op_image();
    LiftedForm converted;
    check(construct(other, {I64}, lift::convert(U32, lift::input(I64, 0)), nullptr, converted), "int64 as uint32");
    check(!lift::prove_structure(image, image.functions[0].blocks[0], image.graphs[0], converted),
          "a conversion does not prove the hash block");
    check(!lift::prove_structure(other, other.functions[0].blocks[0], other.graphs[0], form),
          "a hash does not prove the conversion block");
    std::cout << "  " << vectors << " input vectors matched\n";
}

NEURA_TEST(corpus, blocks_match_the_reference_and_coverage_is_reported) {
    const auto start = std::chrono::steady_clock::now();
    std::map<std::string, std::size_t> reasons;
    std::size_t total_blocks = 0;
    std::size_t total_lifted = 0;
    std::size_t total_vectors = 0;
    std::cout << "  program: blocks, trivial, lifted (non-trivial), lift time per block; steps on lifted graphs\n";
    for (const auto& program : corpus_programs(environment)) {
        const auto image = build(program);
        lift::LiftCache cache(image);
        std::size_t blocks = 0;
        std::size_t lifted = 0;
        std::size_t trivial = 0;
        double lift_seconds = 0;
        double max_seconds = 0;
        // Per graph: whether every block using it lifted, and one lowered
        // block when all of them share it.
        std::map<std::uint32_t, int> graph_state;  // 1 all lifted, 0 none, 2 mixed
        std::map<std::uint32_t, std::string> graph_key;
        std::map<std::uint32_t, const lift::LoweredBlock*> unique;
        std::map<std::uint32_t, const nvr::Block*> unique_block;
        std::set<const lift::LoweredBlock*> compared;
        for (std::uint32_t f = 0; f < image.functions.size(); ++f) {
            for (std::uint32_t b = 0; b < image.functions[f].blocks.size(); ++b) {
                const auto& block = image.functions[f].blocks[b];
                ++blocks;
                const auto recognized_trivial =
                    image.graphs[block.graph].nodes.empty() &&
                    std::none_of(block.exits.begin(), block.exits.end(), [](const nvr::Exit& e) {
                        return std::any_of(e.arguments.begin(), e.arguments.end(),
                                           [](const nvr::Value& v) { return v.kind == nvr::ValueKind::Lanes; }) ||
                               std::any_of(e.assignments.begin(), e.assignments.end(), [](const nvr::Assignment& a) {
                                   return a.value.kind == nvr::ValueKind::Lanes;
                               });
                    }) &&
                    block.exits.size() <= 1;
                const auto before = std::chrono::steady_clock::now();
                const auto size_before = cache.size();
                const auto& result = cache.get(f, b);
                if (cache.size() != size_before) {
                    const double seconds =
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
                    lift_seconds += seconds;
                    max_seconds = std::max(max_seconds, seconds);
                }
                const bool ok = result.block != nullptr;
                if (ok && recognized_trivial) ++trivial;
                if (ok && !recognized_trivial) ++lifted;
                if (!ok) ++reasons[result.rejection];
                auto& state = graph_state.try_emplace(block.graph, ok ? 1 : 0).first->second;
                if ((state == 1) != ok) state = 2;
                const auto key = lift::lift_key(image, block);
                const auto [k, first] = graph_key.emplace(block.graph, key);
                if (ok && first) {
                    unique[block.graph] = result.block.get();
                    unique_block[block.graph] = &block;
                } else if (!first && k->second != key) {
                    unique.erase(block.graph);
                }
                if (ok && compared.insert(result.block.get()).second) {
                    const auto inputs =
                        input_vectors(image, input_types(block), image.graphs[block.graph].nodes.size() > 20000 ? 24 : 96,
                                      0x5eed + f * 7919 + b);
                    const auto comparison = compare(image, block, *result.block, inputs);
                    check(comparison.mismatch.empty(), program.label + " function " + std::to_string(f) + " block " +
                                                           std::to_string(b) + ": " + comparison.mismatch);
                    total_vectors += comparison.vectors;
                }
            }
        }
        for (auto it = unique.begin(); it != unique.end();) {
            it = graph_state[it->first] == 1 ? std::next(it) : unique.erase(it);
        }
        total_blocks += blocks;
        total_lifted += lifted + trivial;
        std::cout << "  " << program.label << ": " << blocks << ", " << trivial << ", " << lifted << " ("
                  << percent(static_cast<double>(lifted), static_cast<double>(blocks - trivial)) << "), "
                  << std::fixed << std::setprecision(2) << 1000.0 * lift_seconds / std::max<std::size_t>(cache.size(), 1)
                  << " ms mean, " << 1000.0 * max_seconds << " ms max";
        if (program.size.empty()) {
            std::cout << '\n';
            continue;
        }
        // Run the benchmark, checking every lifted step against the reference.
        CheckingDevice device(image, unique, unique_block);
        nvm::RunLimits limits;
        limits.graph_device = &device;
        nvm::StringSink out;
        nvm::StringSink err;
        nvm::StringSource in("");
        nvm::Capabilities capabilities;
        capabilities.clock = true;
        capabilities.env = true;
        nvm::StandardServices services(out, err, in, capabilities);
        nvm::ConsoleWriteLine console(out);
        nvm::HostRegistry registry;
        registry.add(console);
        services.register_all(registry);
        const std::vector<std::string> arguments{program.size};
        const auto run = nvm::run(image, nvm::bind(image, registry), arguments, limits);
        check(run.completed && out.contents() == "ok\n", program.label + ": the benchmark runs");
        check(device.mismatch.empty(), program.label + ": " + device.mismatch);
        std::uint64_t graph_steps = 0;
        std::uint64_t lifted_steps = 0;
        std::uint64_t mixed_steps = 0;
        for (std::uint32_t g = 0; g < device.steps.size(); ++g) {
            graph_steps += device.steps[g];
            if (graph_state.contains(g) && graph_state[g] == 1) lifted_steps += device.steps[g];
            if (graph_state.contains(g) && graph_state[g] == 2) mixed_steps += device.steps[g];
        }
        std::cout << "; steps " << run.steps << ", on graphs with neurons " << graph_steps << ", lifted "
                  << lifted_steps << " (" << percent(static_cast<double>(lifted_steps), static_cast<double>(graph_steps))
                  << "), on mixed graphs " << mixed_steps << ", checked in the run " << device.checked << '\n';
    }
    std::cout << "  all: " << total_blocks << " blocks, " << total_lifted << " lifted or trivial, " << total_vectors
              << " input vectors compared\n  rejections:\n";
    std::vector<std::pair<std::size_t, std::string>> sorted;
    for (const auto& [reason, count] : reasons) sorted.emplace_back(count, reason);
    std::sort(sorted.rbegin(), sorted.rend());
    for (const auto& [count, reason] : sorted) std::cout << "    " << count << "  " << reason << '\n';
    std::cout << "  " << std::fixed << std::setprecision(1)
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
    check(total_lifted > 0, "the corpus has lifted blocks");
}

// ---------------------------------------------------------------------------
// Float arithmetic: the proofs and the
// hardware lowering against the reference, lane by lane.

struct FloatShape {
    unsigned width;
    unsigned fraction;
    unsigned exponent() const { return width - 1 - fraction; }
    std::uint64_t bias() const { return (std::uint64_t{1} << (exponent() - 1)) - 1; }
    std::uint64_t sign() const { return std::uint64_t{1} << (width - 1); }
    std::uint64_t infinity() const { return ((std::uint64_t{1} << exponent()) - 1) << fraction; }
    std::uint64_t ulp_mask() const { return (std::uint64_t{1} << fraction) - 1; }
    // 2^power * (1 + mantissa / 2^fraction) for a normal power.
    std::uint64_t value(std::int64_t power, std::uint64_t mantissa) const {
        return (static_cast<std::uint64_t>(static_cast<std::int64_t>(bias()) + power) << fraction) | (mantissa & ulp_mask());
    }
};

// The dense boundary set of a format, both signs: zeros, the smallest and
// largest subnormals, the smallest normal, 1 and its neighbours, the
// largest finite value, infinities, quiet and signalling NaNs with several
// payloads, powers that put exponents 0, 1, 2, p - 1, p, and p + 1 apart (p
// the significand's width), values that make rounding ties and exact
// cancellation, and products near overflow and underflow.
std::vector<std::uint64_t> dense_float_set(const FloatShape& s) {
    const auto f = static_cast<std::int64_t>(s.fraction);
    const auto top = static_cast<std::int64_t>(s.bias());
    const auto half = std::uint64_t{1} << (s.fraction - 1);
    const auto ones = s.ulp_mask();
    std::set<std::uint64_t> magnitudes{
        0, 1, 2, 3, ones, ones - 1, std::uint64_t{1} << s.fraction, (std::uint64_t{1} << s.fraction) + 1,
        s.value(0, 0), s.value(0, 1), s.value(0, 2), s.value(0, ones), s.value(0, half), s.value(0, half + 1),
        s.value(-1, ones), s.value(1, 0), s.value(1, 1), s.value(top, ones), s.value(top, ones - 1), s.value(top, 0),
        s.value(top - 1, ones), s.infinity(),
        // NaNs: quiet with payloads, signalling with payloads.
        s.infinity() | half, s.infinity() | half | 1, s.infinity() | half | 0x123, s.infinity() | ones,
        s.infinity() | 1, s.infinity() | 0x2a, s.infinity() | (half >> 1),
        // Products near overflow and into the subnormal range.
        s.value(top / 2, 0), s.value(top / 2, ones), s.value(top / 2 + 1, 0), s.value(-(top / 2), 0),
        s.value(-(top / 2) - f / 2, 0), s.value(1 - top, ones), s.value(2 - top, 0), s.value(f, 0), s.value(f + 1, 1),
        // A thirds pattern and other dense mantissas.
        s.value(0, 0x5555555555555555ULL), s.value(-2, 0x5555555555555555ULL), s.value(3, 0x2aaaaaaaaaaaaaaaULL),
    };
    // Exponents 0, 1, 2, p - 1, p, and p + 1 below 1 (p = fraction + 1),
    // with mantissas that make ties (1 + 2^-p), round-up ties (with an odd
    // neighbour), and near-cancellation.
    for (const std::int64_t d : {std::int64_t{0}, std::int64_t{1}, std::int64_t{2}, f, f + 1, f + 2}) {
        for (const std::uint64_t m : {std::uint64_t{0}, std::uint64_t{1}, half, ones, half | 1}) {
            magnitudes.insert(s.value(-d, m));
        }
    }
    std::vector<std::uint64_t> values;
    for (const auto m : magnitudes) {
        values.push_back(m);
        values.push_back(m | s.sign());
    }
    return values;
}

// A seeded random pair, by one of several strategies: random bit patterns,
// exponents close together (cancellation and alignment), small exponents
// (subnormal results), exponents summing near the range's ends (products
// that overflow or underflow), and a boundary value against a random one.
std::pair<std::uint64_t, std::uint64_t> random_float_pair(std::mt19937_64& random, const FloatShape& s,
                                                         const std::vector<std::uint64_t>& dense) {
    const auto mask = lift::width_mask(s.width);
    const auto exponent_field = (std::uint64_t{1} << s.exponent()) - 1;
    const auto make = [&](std::uint64_t sign, std::uint64_t exponent, std::uint64_t mantissa) {
        return ((sign & 1U) << (s.width - 1)) | ((exponent & exponent_field) << s.fraction) | (mantissa & s.ulp_mask());
    };
    const auto a = random();
    const auto b = random();
    switch (random() % 5) {
    case 0: return {a & mask, b & mask};
    case 1: {
        const auto e = random() % (exponent_field + 1);
        const auto d = random() % (s.fraction + 4);
        const auto other = random() & 1U ? e + d : (e >= d ? e - d : 0);
        auto y = random() & s.ulp_mask();
        // Often a neighbour of a's mantissa, for cancellation.
        if (random() & 1U) y = (a + (random() % 5) - 2) & s.ulp_mask();
        return {make(a >> 63, e, a), make(b >> 63, other, y)};
    }
    case 2: return {make(a >> 63, random() % 4, a), make(b >> 63, random() % (s.fraction + 3), b)};
    case 3: {
        const auto e = random() % (exponent_field + 1);
        const auto target = random() & 1U ? s.bias() * 2 + (random() % 8) : s.bias() - s.fraction - (random() % 8);
        const auto other = target > e ? target - e : random() % 4;
        return {make(a >> 63, e, a), make(b >> 63, other, b)};
    }
    default: {
        const auto fixed = dense[random() % dense.size()];
        return random() & 1U ? std::make_pair(fixed, b & mask) : std::make_pair(a & mask, fixed);
    }
    }
}

NEURA_TEST(floats, arithmetic_is_proven_and_matches_the_reference) {
    // NEURA_LIFT_FLOAT_PAIRS lowers the count of random pairs for a quick
    // local run; the test's own count is a million per operation and width.
    std::size_t random_pairs = 1'000'000;
    if (const char* text = std::getenv("NEURA_LIFT_FLOAT_PAIRS")) random_pairs = std::strtoull(text, nullptr, 10);
    const unsigned workers = 3;
    for (const auto t : {F32, F64}) {
        const FloatShape shape{t == F32 ? 32U : 64U, t == F32 ? 23U : 52U};
        const auto dense = dense_float_set(shape);
        for (const auto* op : {"+", "-", "*", "/"}) {
            const auto label = type_name(t) + " " + op;
            auto image = op_image();
            LiftedForm form;
            check(construct(image, {t, t}, lift::binary(op, t, lift::input(t, 0), lift::input(t, 1)), nullptr, form),
                  label + ": the circuit library assembles it");
            const auto started = std::chrono::steady_clock::now();
            const auto result = lift::lift_and_prove(image, 0, 0, &form);
            const auto proof_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            check(result.block != nullptr, label + ": proven (" + result.rejection + ")");
            if (!result.block) continue;
            // Every output lane is 0 or 1 by one of the arguments, and the
            // elimination settles the lanes intervals alone do not.
            const auto& graph = image.graphs[0];
            const auto overflow = lift::prove_no_overflow(graph);
            std::vector<std::uint32_t> lanes(graph.outputs.size());
            std::iota(lanes.begin(), lanes.end(), 0U);
            lift::BitEvidence evidence;
            check(overflow.safe && lift::lanes_are_bits(overflow, graph, lanes, &evidence), label + ": lanes are bits");
            const auto& block = image.functions[0].blocks[0];
            std::vector<std::vector<std::uint64_t>> pairs;
            for (const auto x : dense) {
                for (const auto y : dense) pairs.push_back({x, y});
            }
            const auto dense_count = pairs.size();
            std::mt19937_64 random(std::hash<std::string>{}(label) ^ 0xf10a7);
            // Division, a larger circuit outside this test's main subject, gets a
            // tenth of the random pairs.
            const auto pairs_here = std::string(op) == "/" ? random_pairs / 10 : random_pairs;
            for (std::size_t n = 0; n < pairs_here; ++n) {
                const auto [x, y] = random_float_pair(random, shape, dense);
                pairs.push_back({x, y});
            }
            std::vector<Comparison> parts(workers);
            std::vector<std::thread> threads;
            for (unsigned w = 0; w < workers; ++w) {
                threads.emplace_back([&, w] {
                    const auto first = pairs.size() * w / workers;
                    const auto last = pairs.size() * (w + 1) / workers;
                    parts[w] = compare(image, block, *result.block,
                                       std::vector<std::vector<std::uint64_t>>(pairs.begin() + static_cast<std::ptrdiff_t>(first),
                                                                               pairs.begin() + static_cast<std::ptrdiff_t>(last)));
                });
            }
            for (auto& thread : threads) thread.join();
            std::size_t compared = 0;
            for (const auto& part : parts) {
                check(part.mismatch.empty(), label + ": " + part.mismatch);
                compared += part.vectors;
            }
            check_equal(compared, pairs.size(), label + ": every pair compared");
            std::cout << "  " << label << ": proven in " << std::fixed << std::setprecision(3) << proof_seconds
                      << " s (" << graph.nodes.size() << " neurons; lanes by intervals " << evidence.intervals
                      << ", exclusive tests " << evidence.one_hot << ", cases " << evidence.cases << ", elimination "
                      << evidence.eliminated << "); " << dense_count << " boundary pairs and " << pairs_here
                      << " random pairs match the reference\n";
        }
    }
}

NEURA_TEST(overflow, elimination_proves_small_float_circuits_and_accepts_nothing_false) {
    // Float addition, subtraction, multiplication, and division in small
    // formats (two or three exponent and fraction bits), whose inputs can be
    // enumerated: each graph's proof is checked against every input, and
    // so are graphs with one node's bias or one weight changed by one.
    // Whenever the proofs accept a graph, the reference evaluation must not
    // overflow and every output lane must be 0 or 1 on every input. A proof
    // that accepted something false would show up here.
    std::size_t graphs = 0;
    std::size_t accepted = 0;
    std::size_t edited_accepted = 0;
    std::size_t edited_rejected_wrong = 0;
    std::mt19937_64 random(0x5eedf10a7);
    const auto exhaustive = [](const nvr::Graph& graph) {
        // True when no input overflows and every output lane is a bit.
        std::vector<std::int64_t> inputs(graph.input_lanes);
        std::vector<std::int64_t> scratch;
        std::vector<std::int64_t> lanes(graph.outputs.size());
        for (std::uint64_t bits = 0; bits < (std::uint64_t{1} << graph.input_lanes); ++bits) {
            for (std::uint32_t i = 0; i < graph.input_lanes; ++i) inputs[i] = static_cast<std::int64_t>((bits >> i) & 1U);
            if (nvm::evaluate_outputs(graph, inputs, scratch, lanes)) return false;
            for (const auto lane : lanes) {
                if (lane != 0 && lane != 1) return false;
            }
        }
        return true;
    };
    const auto proven = [](const nvr::Graph& graph) {
        std::vector<std::uint32_t> lanes(graph.outputs.size());
        std::iota(lanes.begin(), lanes.end(), 0U);
        // Bounded as the executor bounds it; running out decides nothing.
        std::uint64_t work = std::uint64_t{1} << 22;
        const auto proof = lift::prove_no_overflow(graph, true, &work);
        return proof.safe && lift::lanes_are_bits(proof, graph, lanes, nullptr, true, &work);
    };
    for (const FloatFormat format : {FloatFormat{2, 2}, FloatFormat{3, 2}, FloatFormat{2, 3}}) {
        for (int op = 0; op < 4; ++op) {
            backend::Circuit circuit;
            backend::Word a;
            backend::Word b;
            for (unsigned i = 0; i < format.width(); ++i) a.push_back(circuit.input());
            for (unsigned i = 0; i < format.width(); ++i) b.push_back(circuit.input());
            const auto result = op == 0   ? backend::float_add(circuit, a, b, format)
                                : op == 1 ? backend::float_subtract(circuit, a, b, format)
                                : op == 2 ? backend::float_multiply(circuit, a, b, format)
                                          : backend::float_divide(circuit, a, b, format);
            for (const auto& lane : result) circuit.output(lane);
            const auto graph = circuit.finish();
            ++graphs;
            const auto label = std::to_string(format.exponent_bits) + "/" + std::to_string(format.fraction_bits) +
                               " op " + std::to_string(op);
            check(exhaustive(graph), label + ": the circuit's lanes are bits");
            const bool ok = proven(graph);
            check(ok, label + ": proven");
            accepted += ok ? 1 : 0;
            for (int edit = 0; edit < 30 && !graph.nodes.empty(); ++edit) {
                auto changed = graph;
                auto& node = changed.nodes[random() % changed.nodes.size()];
                if (node.edges.empty() || random() % 2 == 0) {
                    node.bias += random() % 2 ? 1 : -1;
                } else {
                    node.edges[random() % node.edges.size()].weight += random() % 2 ? 1 : -1;
                }
                const bool truth = exhaustive(changed);
                if (proven(changed)) {
                    check(truth, label + ": an edited graph was accepted although a lane is not a bit");
                    ++edited_accepted;
                } else if (truth) {
                    ++edited_rejected_wrong;
                }
            }
        }
    }
    std::cout << "  " << accepted << " of " << graphs << " small float graphs proven; edited graphs: "
              << edited_accepted << " accepted (all true), " << edited_rejected_wrong
              << " true but not proven\n";
}

NEURA_TEST(overflow, advice_finds_proofs_and_never_makes_a_false_one) {
    // The gadget advice the circuit library records only tells the analysis
    // which cut to try first. Small float circuits, their advice, and
    // graphs edited to overflow or to give a lane outside [0, 1] are proven
    // with the true advice and with advice that is wrong in every way it can
    // be: a cut missing a real dependency, signals of several wires named as
    // single wires, advice of the neighbouring neuron, and random wires and
    // instances, some outside the graph. Whatever is accepted must hold on
    // every input; and the unedited circuits are proven with true advice.
    const auto exhaustive = [](const nvr::Graph& graph) {
        std::vector<std::int64_t> inputs(graph.input_lanes);
        std::vector<std::int64_t> scratch;
        std::vector<std::int64_t> lanes(graph.outputs.size());
        for (std::uint64_t bits = 0; bits < (std::uint64_t{1} << graph.input_lanes); ++bits) {
            for (std::uint32_t i = 0; i < graph.input_lanes; ++i) inputs[i] = static_cast<std::int64_t>((bits >> i) & 1U);
            if (nvm::evaluate_outputs(graph, inputs, scratch, lanes)) return false;
            for (const auto lane : lanes) {
                if (lane != 0 && lane != 1) return false;
            }
        }
        return true;
    };
    // Proven by every use of the advice, or by the runtime's chain of them
    // (GraphProofs).
    const auto proven = [](const nvr::Graph& graph, const lift::ProofAdvice* advice) {
        std::vector<std::uint32_t> lanes(graph.outputs.size());
        std::iota(lanes.begin(), lanes.end(), 0U);
        bool any = false;
        for (const auto use : {lift::AdviceUse::decisive, lift::AdviceUse::first, lift::AdviceUse::none}) {
            std::uint64_t work = std::uint64_t{1} << 22;
            const auto proof = lift::prove_no_overflow(graph, true, &work, nullptr, advice, use);
            any = any || (proof.safe && lift::lanes_are_bits(proof, graph, lanes, nullptr, true, &work, advice));
        }
        lift::GraphProofs proofs;
        return any || proofs.verdict(graph, 0, std::uint64_t{1} << 22, nullptr, advice).empty();
    };
    std::mt19937_64 random(0xad71ce);
    // The wrong advice derived from the true one.
    const auto variants = [&](const lift::ProofAdvice& truth, const nvr::Graph& graph) {
        std::vector<lift::ProofAdvice> out;
        auto missing = truth;
        for (auto& inputs : missing.inputs) {
            for (auto& signal : inputs) {
                if (!signal.empty()) signal.pop_back();
            }
        }
        out.push_back(missing);
        auto single = truth;
        for (auto& inputs : single.inputs) {
            std::vector<std::vector<std::uint32_t>> split;
            for (const auto& signal : inputs) {
                for (const auto wire : signal) split.push_back({wire});
            }
            inputs = split;
        }
        out.push_back(single);
        auto shifted = truth;
        if (!shifted.instance.empty()) std::rotate(shifted.instance.begin(), shifted.instance.begin() + 1, shifted.instance.end());
        out.push_back(shifted);
        auto garbage = truth;
        const auto wires = graph.input_lanes + static_cast<std::uint32_t>(graph.nodes.size());
        for (auto& instance : garbage.instance) instance = static_cast<std::uint32_t>(random() % (garbage.inputs.size() + 2));
        for (auto& inputs : garbage.inputs) {
            for (auto& signal : inputs) {
                for (auto& wire : signal) wire = static_cast<std::uint32_t>(random() % (wires + 3));
            }
        }
        out.push_back(garbage);
        return out;
    };
    std::size_t graphs = 0;
    std::size_t accepted_false = 0;
    std::size_t unsafe_graphs = 0;
    std::size_t truth_proven = 0;
    for (const FloatFormat format : {FloatFormat{2, 2}, FloatFormat{3, 2}, FloatFormat{2, 3}}) {
        for (int op = 0; op < 4; ++op) {
            backend::Circuit circuit;
            circuit.record_gadgets();
            backend::Word a;
            backend::Word b;
            for (unsigned i = 0; i < format.width(); ++i) a.push_back(circuit.input());
            for (unsigned i = 0; i < format.width(); ++i) b.push_back(circuit.input());
            const auto result = op == 0   ? backend::float_add(circuit, a, b, format)
                                : op == 1 ? backend::float_subtract(circuit, a, b, format)
                                : op == 2 ? backend::float_multiply(circuit, a, b, format)
                                          : backend::float_divide(circuit, a, b, format);
            for (const auto& lane : result) circuit.output(lane);
            const auto graph = circuit.finish();
            const auto truth = circuit.gadget_advice();
            check(truth.instance.size() == graph.nodes.size(), "the advice covers every neuron");
            const auto label = std::to_string(format.exponent_bits) + "/" + std::to_string(format.fraction_bits) +
                               " op " + std::to_string(op);
            {
                std::vector<std::uint32_t> lanes(graph.outputs.size());
                std::iota(lanes.begin(), lanes.end(), 0U);
                const auto proof = lift::prove_no_overflow(graph, true, nullptr, nullptr, &truth,
                                                           lift::AdviceUse::decisive);
                check(proof.safe && lift::lanes_are_bits(proof, graph, lanes, nullptr, true, nullptr, &truth),
                      label + ": proven with its advice standing");
            }
            truth_proven += 1;
            // Edited graphs: one bias or weight changed by one, and one
            // node that overflows when two lanes are 1 together.
            std::vector<nvr::Graph> edited;
            for (int edit = 0; edit < 8 && !graph.nodes.empty(); ++edit) {
                auto changed = graph;
                auto& node = changed.nodes[random() % changed.nodes.size()];
                if (node.edges.empty() || random() % 2 == 0) {
                    node.bias += random() % 2 ? 1 : -1;
                } else {
                    node.edges[random() % node.edges.size()].weight += random() % 2 ? 1 : -1;
                }
                edited.push_back(std::move(changed));
            }
            auto overflowing = graph;
            overflowing.nodes.push_back({nvr::Activation::Identity, 0,
                                         {{graph.outputs[0], std::int64_t{1} << 62}, {graph.outputs[1], std::int64_t{1} << 62}}});
            std::sort(overflowing.nodes.back().edges.begin(), overflowing.nodes.back().edges.end(),
                      [](const nvr::Edge& x, const nvr::Edge& y) { return x.source < y.source; });
            overflowing.outputs.push_back(overflowing.input_lanes + static_cast<std::uint32_t>(graph.nodes.size()));
            edited.push_back(overflowing);
            for (const auto& changed : edited) {
                ++graphs;
                const bool truth_holds = exhaustive(changed);
                unsafe_graphs += truth_holds ? 0 : 1;
                auto advices = variants(truth, changed);
                advices.push_back(truth);
                // The advice of the original graph, and of a graph one node
                // longer, whatever the edit.
                auto longer = truth;
                longer.instance.push_back(nvr::absent);
                advices.push_back(longer);
                for (const auto& advice : advices) {
                    if (proven(changed, &advice) && !truth_holds) {
                        ++accepted_false;
                        check(false, label + ": a graph whose lanes are not all bits was accepted with advice");
                    }
                }
            }
        }
    }
    check(unsafe_graphs >= 12, "the edits include graphs that overflow or give a lane outside [0, 1]");
    check(accepted_false == 0, "advice never makes a false proof");
    std::cout << "  " << truth_proven << " small float graphs proven with their advice; " << graphs
              << " edited graphs (" << unsafe_graphs << " not all bits) tried with 6 kinds of advice, none falsely accepted\n";
}

NEURA_TEST(overflow, enumerations_are_shared_only_between_identical_computations) {
    // The case analysis keeps each enumeration's range under a key in which
    // wires are renamed, so that isomorphic gadgets share one result. Four
    // copies of an exclusive-or gadget, s = x + y - k * relu(x + y - b),
    // over separate inputs: two identical (k = 2, b = 1), one whose weight
    // differs (k = 3) and one whose bias differs (b = 0). Each sum must get
    // its own exact range: [0, 1], [0, 1], [-1, 1], and [-2, 0].
    nvr::Graph graph;
    graph.input_lanes = 8;
    const auto gadget = [&](std::uint32_t x, std::int64_t k, std::int64_t b) {
        const auto conjunction = graph.input_lanes + static_cast<std::uint32_t>(graph.nodes.size());
        graph.nodes.push_back({nvr::Activation::Relu, -b, {{x, 1}, {x + 1, 1}}});
        graph.nodes.push_back({nvr::Activation::Identity, 0, {{x, 1}, {x + 1, 1}, {conjunction, -k}}});
        return conjunction + 1;
    };
    const auto first = gadget(0, 2, 1);
    const auto same = gadget(2, 2, 1);
    const auto weight = gadget(4, 3, 1);
    const auto bias = gadget(6, 2, 0);
    graph.outputs = {first, same, weight, bias};
    const auto proof = lift::prove_no_overflow(graph);
    check(proof.safe, "the gadgets cannot overflow");
    const auto expect = [&](std::uint32_t wire, std::int64_t lo, std::int64_t hi, const std::string& what) {
        check(proof.wires[wire].lo == lo && proof.wires[wire].hi == hi,
              what + ": [" + std::to_string(proof.wires[wire].lo) + ", " + std::to_string(proof.wires[wire].hi) +
                  "], expected [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
    };
    expect(first, 0, 1, "the first gadget");
    expect(same, 0, 1, "an identical gadget");
    expect(weight, -1, 1, "a gadget with another weight");
    expect(bias, -2, 0, "a gadget with another bias");
}

// ---------------------------------------------------------------------------
// The overflow proof against exhaustive evaluation.
//
// Graphs small enough to enumerate (at most 14 input lanes) are evaluated
// for every assignment of their input lanes, wire by wire, in stored order
// with checked 64-bit arithmetic computed in 128 bits, independently of the
// analysis. Every proof the analysis returns is then checked against that
// evaluation: a safe proof must belong to a graph that never overflows, its
// interval of every wire must contain every value of the wire, and every
// group's weighted sum must stay in the group's range; a lane
// lanes_are_bits accepts must be 0 or 1; a proof that ran out of work must
// already be right about every wire and group it keeps, because a resumed
// analysis builds on them; and GraphProofs::verdict must never accept a
// graph that overflows or has a lane outside [0, 1]. Each graph is tried
// with its true gadget advice and with advice wrong in every way listed in
// advice_variants, with every AdviceUse, unbounded, with the runtime's
// bound, and with small amounts of work resumed until the analysis ends.
//
// NEURA_OVERFLOW_FUZZ_SCALE (default 1) multiplies the number of generated
// graphs; the default run is the one CTest runs.

namespace overflow_fuzz {

// Exact products and sums of 64-bit values.
__extension__ typedef __int128 Wide;

constexpr std::uint32_t max_lanes = 14;
constexpr std::int64_t p61 = std::int64_t{1} << 61;
constexpr std::int64_t p62 = std::int64_t{1} << 62;

bool checked_add_ok(std::int64_t a, std::int64_t b) {
    std::int64_t r = 0;
    return lift::checked_add(a, b, r);
}

unsigned scale() {
    if (const char* text = std::getenv("NEURA_OVERFLOW_FUZZ_SCALE")) {
        const auto value = std::strtoul(text, nullptr, 10);
        if (value >= 1 && value <= 100000) return static_cast<unsigned>(value);
    }
    return 1;
}

// Reference evaluation of every wire for one assignment: false on overflow.
bool evaluate_wires(const nvr::Graph& graph, std::uint64_t bits, std::vector<std::int64_t>& values) {
    const auto inputs = graph.input_lanes;
    values.resize(std::size_t{inputs} + graph.nodes.size());
    for (std::uint32_t i = 0; i < inputs; ++i) values[i] = static_cast<std::int64_t>((bits >> i) & 1U);
    constexpr Wide low = std::numeric_limits<std::int64_t>::min();
    constexpr Wide high = std::numeric_limits<std::int64_t>::max();
    for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
        const auto& node = graph.nodes[n];
        Wide sum = node.bias;
        for (const auto& edge : node.edges) {
            const Wide product = static_cast<Wide>(values[edge.source]) * edge.weight;
            if (product < low || product > high) return false;
            sum += product;
            if (sum < low || sum > high) return false;
        }
        const auto value = static_cast<std::int64_t>(sum);
        values[inputs + n] = node.activation == nvr::Activation::Relu ? std::max<std::int64_t>(value, 0) : value;
    }
    return true;
}

// What exhaustive evaluation says about a graph.
struct Truth {
    bool overflow = false;
    bool lanes_bits = true;
    std::vector<std::uint8_t> lane_bit;
    std::vector<std::int64_t> lo;
    std::vector<std::int64_t> hi;
    // Every wire's value by assignment (assignment * wires + wire), when
    // the graph does not overflow.
    std::vector<std::int64_t> values;
    std::uint64_t assignments = 0;
    std::size_t wires = 0;
    bool unsound() const { return overflow || !lanes_bits; }
    std::int64_t value(std::uint64_t assignment, std::uint32_t wire) const { return values[assignment * wires + wire]; }
};

Truth exhaust(const nvr::Graph& graph) {
    Truth truth;
    truth.wires = std::size_t{graph.input_lanes} + graph.nodes.size();
    truth.assignments = std::uint64_t{1} << graph.input_lanes;
    truth.lo.assign(truth.wires, std::numeric_limits<std::int64_t>::max());
    truth.hi.assign(truth.wires, std::numeric_limits<std::int64_t>::min());
    truth.lane_bit.assign(graph.outputs.size(), 1);
    truth.values.reserve(truth.assignments * truth.wires);
    std::vector<std::int64_t> values;
    std::vector<std::int64_t> inputs(graph.input_lanes);
    std::vector<std::int64_t> scratch;
    std::vector<std::int64_t> lanes(graph.outputs.size());
    for (std::uint64_t bits = 0; bits < truth.assignments; ++bits) {
        const bool ok = evaluate_wires(graph, bits, values);
        // The runtime's reference evaluation agrees on overflow and lanes.
        for (std::uint32_t i = 0; i < graph.input_lanes; ++i) inputs[i] = values[i];
        const bool reference_ok = !nvm::evaluate_outputs(graph, inputs, scratch, lanes).has_value();
        check(ok == reference_ok, "the fuzzer's evaluation and the reference disagree on overflow");
        if (!ok) {
            truth.overflow = true;
            truth.values.clear();
            return truth;
        }
        for (std::size_t w = 0; w < truth.wires; ++w) {
            truth.lo[w] = std::min(truth.lo[w], values[w]);
            truth.hi[w] = std::max(truth.hi[w], values[w]);
        }
        for (std::size_t l = 0; l < graph.outputs.size(); ++l) {
            const auto v = values[graph.outputs[l]];
            check(v == lanes[l], "the fuzzer's evaluation and the reference disagree on a lane");
            if (v != 0 && v != 1) {
                truth.lane_bit[l] = 0;
                truth.lanes_bits = false;
            }
        }
        truth.values.insert(truth.values.end(), values.begin(), values.end());
    }
    return truth;
}

std::string describe(const nvr::Graph& graph) {
    std::ostringstream out;
    out << "graph: " << graph.input_lanes << " input lanes";
    for (std::size_t n = 0; n < graph.nodes.size() && n < 80; ++n) {
        const auto& node = graph.nodes[n];
        out << "\n    w" << graph.input_lanes + n << " = " << (node.activation == nvr::Activation::Relu ? "relu" : "id")
            << "(" << node.bias;
        for (const auto& edge : node.edges) out << " + " << edge.weight << "*w" << edge.source;
        out << ")";
    }
    if (graph.nodes.size() > 80) out << "\n    ...";
    out << "\n    outputs:";
    for (const auto o : graph.outputs) out << " w" << o;
    return out.str();
}

std::string describe(const lift::ProofAdvice& advice) {
    std::ostringstream out;
    out << "advice: instance";
    for (std::size_t i = 0; i < advice.instance.size() && i < 80; ++i) out << " " << static_cast<std::int64_t>(advice.instance[i] == nvr::absent ? -1 : advice.instance[i]);
    for (std::size_t i = 0; i < advice.inputs.size() && i < 40; ++i) {
        out << "\n    instance " << i << ":";
        for (const auto& signal : advice.inputs[i]) {
            out << " {";
            for (const auto wire : signal) out << " " << wire;
            out << " }";
        }
    }
    return out.str();
}

// One graph under test, with its truth and the groups already checked.
struct Subject {
    std::string generator;
    nvr::Graph graph;
    lift::ProofAdvice advice;
    Truth truth;
    std::set<std::vector<std::int64_t>> groups_checked;
    // Some node reads a later wire or does not name strictly ascending
    // sources: no proof of the graph may be safe.
    bool malformed = false;
};

// Empty when every claim `proof` makes holds; otherwise what fails. A
// proof that ran out of work claims the intervals it keeps and its groups.
std::string audit(Subject& subject, const lift::OverflowProof& proof) {
    const auto& truth = subject.truth;
    const auto& graph = subject.graph;
    if (proof.safe && truth.overflow) return "a graph that overflows was proven safe";
    if (proof.safe && subject.malformed) return "a graph whose edges are not in strictly ascending order was proven safe";
    if (!proof.safe && !proof.exhausted) return {};
    if (truth.overflow) return {};
    const auto settled = proof.safe ? truth.wires : std::size_t{graph.input_lanes} + proof.node;
    if (proof.wires.size() != settled) {
        if (proof.safe || !proof.wires.empty()) {
            return "the proof keeps " + std::to_string(proof.wires.size()) + " intervals, expected " +
                   std::to_string(settled);
        }
    }
    for (std::size_t w = 0; w < proof.wires.size() && w < truth.wires; ++w) {
        if (truth.lo[w] < proof.wires[w].lo || truth.hi[w] > proof.wires[w].hi) {
            return "wire " + std::to_string(w) + " claimed [" + std::to_string(proof.wires[w].lo) + ", " +
                   std::to_string(proof.wires[w].hi) + "] but takes values in [" + std::to_string(truth.lo[w]) + ", " +
                   std::to_string(truth.hi[w]) + "]";
        }
    }
    for (std::size_t g = 0; g < proof.groups.size(); ++g) {
        const auto& group = proof.groups[g];
        std::vector<std::int64_t> key{group.range.lo, group.range.hi};
        for (const auto& [wire, coefficient] : group.members) {
            if (wire >= truth.wires) return "group " + std::to_string(g) + " names wire " + std::to_string(wire);
            key.push_back(wire);
            key.push_back(coefficient);
        }
        if (subject.groups_checked.contains(key)) continue;
        for (std::uint64_t a = 0; a < truth.assignments; ++a) {
            Wide sum = 0;
            for (const auto& [wire, coefficient] : group.members) {
                sum += static_cast<Wide>(truth.value(a, wire)) * coefficient;
            }
            if (sum < group.range.lo || sum > group.range.hi) {
                std::ostringstream out;
                out << "group " << g << " (";
                for (const auto& [wire, coefficient] : group.members) out << " " << coefficient << "*w" << wire;
                out << " ) claimed [" << group.range.lo << ", " << group.range.hi << "] but sums to "
                    << static_cast<std::int64_t>(sum) << " on assignment " << a;
                return out.str();
            }
        }
        subject.groups_checked.insert(std::move(key));
    }
    return {};
}

// The lanes lanes_are_bits accepted must be bits.
std::string audit_lanes(const Subject& subject, std::span<const std::uint32_t> lanes) {
    if (subject.truth.overflow) return "lanes accepted on a graph that overflows";
    if (subject.malformed) return "lanes accepted on a graph whose edges are not in strictly ascending order";
    for (const auto lane : lanes) {
        if (!subject.truth.lane_bit[lane]) return "lane " + std::to_string(lane) + " accepted as a bit but is not one";
    }
    return {};
}

struct Counts {
    std::uint64_t cases = 0;
    std::uint64_t safe = 0;
    std::uint64_t unsound = 0;
    std::uint64_t false_accepted = 0;
};

class Harness {
public:
    Harness(std::uint64_t seed, bool thorough) : random(seed), thorough(thorough) {}

    std::mt19937_64 random;
    // The larger run: more work values resumed and each lane alone.
    bool thorough;

    std::map<std::string, Counts> counts;
    std::vector<std::string> failures;
    std::uint64_t combinations = 0;
    std::uint64_t graphs = 0;
    std::uint64_t unsound_graphs = 0;
    std::uint64_t proven_graphs = 0;

    // Records one (graph, advice, use, work) combination.
    void tally(const Subject& subject, const std::string& advice, const std::string& use, bool safe,
               const std::string& failure) {
        ++combinations;
        for (const auto& key : {"generator " + subject.generator, "advice " + advice, "use " + use}) {
            auto& c = counts[key];
            ++c.cases;
            c.safe += safe ? 1 : 0;
            c.unsound += subject.truth.unsound() ? 1 : 0;
            c.false_accepted += failure.empty() ? 0 : 1;
        }
    }

    void fail_with(const Subject& subject, const lift::ProofAdvice* advice, const std::string& context,
                   const std::string& failure) {
        if (failures.size() >= 5) {
            failures.emplace_back();
            return;
        }
        std::ostringstream out;
        out << "[" << subject.generator << "] " << context << ": " << failure << "\n  " << describe(subject.graph);
        if (advice) out << "\n  " << describe(*advice);
        failures.push_back(out.str());
    }

    std::string report() const {
        std::ostringstream out;
        out << "  " << graphs << " graphs (" << unsound_graphs << " overflow or have a lane outside [0, 1], "
            << proven_graphs << " proven by some combination), " << combinations << " combinations\n";
        out << "  " << std::left << std::setw(34) << "class" << std::right << std::setw(10) << "cases" << std::setw(10)
            << "safe" << std::setw(10) << "unsound" << std::setw(8) << "false" << "\n";
        for (const auto& [key, c] : counts) {
            out << "  " << std::left << std::setw(34) << key << std::right << std::setw(10) << c.cases << std::setw(10)
                << c.safe << std::setw(10) << c.unsound << std::setw(8) << c.false_accepted << "\n";
        }
        return out.str();
    }

    // Every combination for one graph and one advice.
    bool run(Subject& subject, const std::string& kind, const lift::ProofAdvice* advice, bool with_none);
};

std::string use_name(lift::AdviceUse use) {
    return use == lift::AdviceUse::decisive ? "decisive" : use == lift::AdviceUse::first ? "first" : "none";
}

bool Harness::run(Subject& subject, const std::string& kind, const lift::ProofAdvice* advice, bool with_none) {
    const auto& graph = subject.graph;
    std::vector<std::uint32_t> lanes(graph.outputs.size());
    std::iota(lanes.begin(), lanes.end(), 0U);
    bool proven = false;
    const bool small = graph.nodes.size() <= 64;
    std::vector<lift::AdviceUse> uses{lift::AdviceUse::decisive, lift::AdviceUse::first};
    if (with_none) uses.push_back(lift::AdviceUse::none);
    for (const auto use : uses) {
        const auto use_label = use_name(use);
        const auto context = [&](const std::string& what) { return kind + " advice, use " + use_label + ", " + what; };
        // Unbounded, and with the runtime's bound.
        for (const bool bounded : {false, true}) {
            std::uint64_t work = std::uint64_t{1} << 22;
            const auto proof = lift::prove_no_overflow(graph, true, bounded ? &work : nullptr, nullptr, advice, use);
            auto failure = audit(subject, proof);
            bool lanes_ok = false;
            if (failure.empty() && proof.safe) {
                lanes_ok = lift::lanes_are_bits(proof, graph, lanes, nullptr, true, bounded ? &work : nullptr, advice);
                if (lanes_ok) failure = audit_lanes(subject, lanes);
                // Each lane alone, which reaches the case analysis of lanes
                // after one that fails.
                for (std::uint32_t lane = 0; failure.empty() && small && thorough && lane < lanes.size(); ++lane) {
                    const std::uint32_t one[] = {lane};
                    if (lift::lanes_are_bits(proof, graph, one, nullptr, true, nullptr, advice)) {
                        failure = audit_lanes(subject, one);
                    }
                }
            }
            proven = proven || (proof.safe && lanes_ok);
            tally(subject, kind, use_label, proof.safe, failure);
            if (!failure.empty()) fail_with(subject, advice, context(bounded ? "bounded" : "unbounded"), failure);
        }
        // Small work, resumed until the analysis ends; work doubles when a
        // step settles no node, as the runtime retries with more.
        for (const std::uint64_t start : !small ? std::vector<std::uint64_t>{512}
                                                 : thorough ? std::vector<std::uint64_t>{1, 6, 41}
                                                            : std::vector<std::uint64_t>{6}) {
            std::optional<lift::OverflowProof> earlier;
            std::uint64_t given = start;
            for (int step = 0; step < 4000; ++step) {
                std::uint64_t work = given;
                const auto proof = lift::prove_no_overflow(graph, true, &work, earlier ? &*earlier : nullptr, advice, use);
                auto failure = audit(subject, proof);
                if (failure.empty() && proof.safe) {
                    std::uint64_t lane_work = start;
                    if (lift::lanes_are_bits(proof, graph, lanes, nullptr, true, &lane_work, advice)) {
                        failure = audit_lanes(subject, lanes);
                    }
                }
                tally(subject, kind, use_label, proof.safe, failure);
                if (!failure.empty()) {
                    fail_with(subject, advice,
                              context("resumed from work " + std::to_string(start) + ", step " + std::to_string(step)),
                              failure);
                    break;
                }
                if (!proof.exhausted) break;
                if (earlier && earlier->node == proof.node) given = std::min<std::uint64_t>(given * 2, std::uint64_t{1} << 30);
                earlier = proof;
            }
        }
    }
    // The runtime's chain of uses, with small work resumed.
    for (const std::uint64_t start : !small ? std::vector<std::uint64_t>{2048}
                                                 : thorough ? std::vector<std::uint64_t>{1, 23}
                                                            : std::vector<std::uint64_t>{23}) {
        lift::GraphProofs proofs;
        std::uint64_t given = start;
        double last = -1;
        for (int step = 0; step < 4000; ++step) {
            double progress = 1;
            const auto verdict = proofs.verdict(graph, 0, given, &progress, advice);
            std::string failure;
            if (verdict.empty() && (subject.truth.unsound() || subject.malformed)) failure = "GraphProofs accepted the graph";
            proven = proven || verdict.empty();
            tally(subject, kind, "verdict chain", verdict.empty(), failure);
            if (!failure.empty()) fail_with(subject, advice, kind + " advice, verdict from work " + std::to_string(start), failure);
            if (verdict != lift::exhausted_work) break;
            if (progress == last) given = std::min<std::uint64_t>(given * 2, std::uint64_t{1} << 30);
            last = progress;
        }
    }
    return proven;
}

// ---------------------------------------------------------------------------
// Advice.

std::uint32_t wire_count(const nvr::Graph& graph) {
    return graph.input_lanes + static_cast<std::uint32_t>(graph.nodes.size());
}

// Advice for a graph without recorded advice: each neuron its own
// instance, given its sources as one signal.
lift::ProofAdvice plausible_advice(const nvr::Graph& graph) {
    lift::ProofAdvice advice;
    for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
        advice.instance.push_back(static_cast<std::uint32_t>(n));
        std::vector<std::uint32_t> signal;
        for (const auto& edge : graph.nodes[n].edges) signal.push_back(edge.source);
        advice.inputs.push_back({signal});
    }
    return advice;
}

// The lowest wire each instance built, or the wire count.
std::vector<std::uint32_t> first_wire(const nvr::Graph& graph, const lift::ProofAdvice& advice) {
    std::vector<std::uint32_t> first(advice.inputs.size(), wire_count(graph));
    for (std::size_t n = 0; n < advice.instance.size() && n < graph.nodes.size(); ++n) {
        const auto i = advice.instance[n];
        if (i < first.size()) first[i] = std::min(first[i], graph.input_lanes + static_cast<std::uint32_t>(n));
    }
    return first;
}

std::vector<std::pair<std::string, lift::ProofAdvice>> advice_variants(const nvr::Graph& graph,
                                                                       const lift::ProofAdvice& truth,
                                                                       const lift::ProofAdvice& other,
                                                                       std::mt19937_64& random) {
    std::vector<std::pair<std::string, lift::ProofAdvice>> out;
    const auto wires = wire_count(graph);
    const auto inputs = graph.input_lanes;
    const auto nodes = static_cast<std::uint32_t>(graph.nodes.size());
    const auto below = [&](std::uint32_t limit) { return limit == 0 ? 0U : static_cast<std::uint32_t>(random() % limit); };
    out.emplace_back("true", truth);
    out.emplace_back("other graph", other);
    {
        auto permuted = truth;
        std::shuffle(permuted.instance.begin(), permuted.instance.end(), random);
        out.emplace_back("permuted", std::move(permuted));
    }
    {
        // Cuts through random wires below each instance: plausible, in range.
        auto cuts = truth;
        const auto first = first_wire(graph, truth);
        for (std::size_t i = 0; i < cuts.inputs.size(); ++i) {
            auto& signals = cuts.inputs[i];
            signals.clear();
            const auto count = 1 + random() % 3;
            for (std::size_t s = 0; s < count; ++s) {
                std::vector<std::uint32_t> signal;
                const auto size = 1 + random() % 3;
                for (std::size_t k = 0; k < size; ++k) signal.push_back(below(first[i]));
                std::sort(signal.begin(), signal.end());
                signal.erase(std::unique(signal.begin(), signal.end()), signal.end());
                signals.push_back(std::move(signal));
            }
        }
        out.emplace_back("random cuts", std::move(cuts));
    }
    {
        // One dependency removed from every instance.
        auto missing = truth;
        for (auto& signals : missing.inputs) {
            if (signals.empty()) continue;
            auto& signal = signals[random() % signals.size()];
            if (signal.size() > 1) {
                signal.erase(signal.begin() + static_cast<std::ptrdiff_t>(random() % signal.size()));
            } else {
                signals.erase(signals.begin() + static_cast<std::ptrdiff_t>(&signal - signals.data()));
            }
        }
        out.emplace_back("one dependency removed", std::move(missing));
    }
    {
        // Signals naming the neuron itself and later wires.
        auto later = truth;
        for (std::uint32_t n = 0; n < nodes && n < later.instance.size(); ++n) {
            const auto i = later.instance[n];
            if (i >= later.inputs.size()) continue;
            const auto wire = inputs + n;
            later.inputs[i].push_back({wire});
            later.inputs[i].push_back({wire, std::min(wire + 1 + below(4), wires - 1)});
            if (wire + 1 < wires) later.inputs[i].push_back({wire + 1 + below(wires - wire - 1)});
        }
        out.emplace_back("self and later wires", std::move(later));
    }
    {
        // Instances that name each other in a cycle through multi-wire
        // signals, and one that names its own wires.
        lift::ProofAdvice cycle;
        const auto k = std::max<std::uint32_t>(1, std::min<std::uint32_t>(nodes, 2 + below(4)));
        cycle.inputs.resize(k);
        std::vector<std::vector<std::uint32_t>> built(k);
        for (std::uint32_t n = 0; n < nodes; ++n) {
            cycle.instance.push_back(n % k);
            built[n % k].push_back(inputs + n);
        }
        for (std::uint32_t i = 0; i < k; ++i) {
            const auto& next = built[(i + 1) % k];
            std::vector<std::uint32_t> signal;
            for (std::size_t j = 0; j < next.size() && j < 3; ++j) signal.push_back(next[next.size() - 1 - j]);
            std::sort(signal.begin(), signal.end());
            if (signal.size() == 1 && inputs > 0) signal.insert(signal.begin(), 0);
            cycle.inputs[i].push_back(signal);
            if (built[i].size() >= 2) cycle.inputs[i].push_back({built[i][0], built[i][1]});
        }
        out.emplace_back("cycle", std::move(cycle));
    }
    {
        // Duplicate wires, duplicate signals, and empty signals.
        auto duplicates = truth;
        for (auto& signals : duplicates.inputs) {
            for (auto& signal : signals) {
                if (!signal.empty()) signal.push_back(signal.front());
            }
            if (!signals.empty()) signals.push_back(signals.front());
            signals.emplace_back();
        }
        out.emplace_back("duplicate and empty", std::move(duplicates));
    }
    {
        // More than 64 instances deep, and more than 64 wires.
        lift::ProofAdvice huge;
        const auto chain = 70U;
        huge.inputs.resize(chain + 1);
        for (std::uint32_t n = 0; n < nodes; ++n) huge.instance.push_back(n % chain);
        for (std::uint32_t i = 0; i < chain; ++i) {
            // Instance i names two wires built by instance i + 1, when it
            // built any.
            std::vector<std::uint32_t> signal;
            for (std::uint32_t n = (i + 1) % chain; n < nodes && signal.size() < 2; n += chain) signal.push_back(inputs + n);
            if (signal.size() < 2) signal = {below(wires), below(wires)};
            std::sort(signal.begin(), signal.end());
            huge.inputs[i].push_back(signal);
        }
        for (std::uint32_t n = 0; n < 70; ++n) huge.inputs[chain].push_back({below(wires)});
        if (!huge.instance.empty()) huge.instance.back() = chain;
        out.emplace_back("over 64", std::move(huge));
    }
    {
        // Indices far out of range.
        auto range = truth;
        for (auto& instance : range.instance) {
            switch (random() % 4) {
            case 0: instance = nvr::absent; break;
            case 1: instance = static_cast<std::uint32_t>(range.inputs.size() + random() % 3); break;
            case 2: instance = 0xfffffffeU; break;
            default: break;
            }
        }
        for (auto& signals : range.inputs) {
            for (auto& signal : signals) {
                if (signal.empty() || random() % 3 != 0) continue;
                signal[random() % signal.size()] = random() % 2 ? wires + static_cast<std::uint32_t>(random() % 4) : 0xffffffffU;
            }
        }
        out.emplace_back("out of range", std::move(range));
        auto shorter = truth;
        if (!shorter.instance.empty()) shorter.instance.pop_back();
        out.emplace_back("wrong length", std::move(shorter));
    }
    {
        // Single wires of every signal named as one signal per instance of
        // the whole graph: the widest plausible cut.
        lift::ProofAdvice whole;
        whole.inputs.resize(1);
        whole.instance.assign(nodes, 0);
        for (std::uint32_t i = 0; i < inputs; ++i) whole.inputs[0].push_back({i});
        out.emplace_back("inputs only", std::move(whole));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Generators.

struct Built {
    std::string name;
    nvr::Graph graph;
    lift::ProofAdvice advice;
};

Built build(const std::string& name, const std::function<void(backend::Circuit&)>& body) {
    backend::Circuit circuit;
    circuit.record_gadgets();
    body(circuit);
    Built built{name, circuit.finish(), {}};
    built.advice = circuit.gadget_advice();
    return built;
}

backend::Word word(backend::Circuit& circuit, unsigned width) {
    backend::Word out;
    for (unsigned i = 0; i < width; ++i) out.push_back(circuit.input());
    return out;
}

void output(backend::Circuit& circuit, const backend::Word& value) {
    for (const auto& lane : value) circuit.output(lane);
}

void output(backend::Circuit& circuit, const backend::Checked& value) {
    output(circuit, value.value);
    circuit.output(value.failure);
}

// Circuit-library gadgets at small widths, with their recorded advice.
std::vector<Built> library_gadgets() {
    std::vector<Built> out;
    for (unsigned width = 2; width <= 6; ++width) {
        for (const bool is_signed : {false, true}) {
            const auto tag = std::string(is_signed ? "i" : "u") + std::to_string(width);
            const auto binary = [&](const std::string& op, auto f) {
                out.push_back(build(op + " " + tag, [&](backend::Circuit& c) {
                    const auto a = word(c, width);
                    const auto b = word(c, width);
                    f(c, a, b);
                }));
            };
            binary("add", [&](auto& c, auto& a, auto& b) { output(c, backend::add(c, a, b, is_signed)); });
            binary("subtract", [&](auto& c, auto& a, auto& b) { output(c, backend::subtract(c, a, b, is_signed)); });
            binary("multiply", [&](auto& c, auto& a, auto& b) { output(c, backend::multiply(c, a, b, is_signed)); });
            binary("divide", [&](auto& c, auto& a, auto& b) { output(c, backend::divide(c, a, b, is_signed)); });
            binary("remainder", [&](auto& c, auto& a, auto& b) { output(c, backend::remainder(c, a, b, is_signed)); });
            binary("less", [&](auto& c, auto& a, auto& b) { c.output(backend::less(c, a, b, is_signed)); });
            if (!is_signed) {
                binary("equal", [&](auto& c, auto& a, auto& b) { c.output(backend::equal(c, a, b)); });
                binary("add with carry", [&](auto& c, auto& a, auto& b) {
                    output(c, backend::add_with_carry(c, a, b, c.input()));
                });
                binary("multiply full", [&](auto& c, auto& a, auto& b) { output(c, backend::multiply_full(c, a, b)); });
            }
            out.push_back(build("negate " + tag, [&](backend::Circuit& c) {
                output(c, backend::negate(c, word(c, width), is_signed));
            }));
            out.push_back(build("convert " + tag, [&](backend::Circuit& c) {
                output(c, backend::convert_integer(c, word(c, width), is_signed, width == 2 ? 4 : width - 1, !is_signed));
            }));
            for (const bool right : {false, true}) {
                out.push_back(build(std::string(right ? "shift right " : "shift left ") + tag, [&](backend::Circuit& c) {
                    const auto value = word(c, width);
                    const auto count = word(c, 3);
                    output(c, backend::shift(c, value, count, right, is_signed && right));
                }));
            }
        }
        // choose chains: s0 ? x0 : (s1 ? x1 : ... : y).
        for (unsigned k = 1; k <= 3 && k + (k + 1) * std::min(width, 3U) <= max_lanes; ++k) {
            const auto lanes = std::min(width, 3U);
            out.push_back(build("choose chain " + std::to_string(k) + "x" + std::to_string(lanes), [&](backend::Circuit& c) {
                std::vector<backend::Signal> selects;
                for (unsigned i = 0; i < k; ++i) selects.push_back(c.input());
                std::vector<backend::Word> words;
                for (unsigned i = 0; i <= k; ++i) words.push_back(word(c, lanes));
                auto result = words.back();
                for (unsigned i = k; i-- > 0;) {
                    for (unsigned b = 0; b < lanes; ++b) result[b] = c.choose(selects[i], words[i][b], result[b]);
                }
                output(c, result);
            }));
        }
    }
    for (const FloatFormat format : {FloatFormat{2, 2}, FloatFormat{3, 2}, FloatFormat{2, 3}}) {
        const auto tag = std::to_string(format.exponent_bits) + "/" + std::to_string(format.fraction_bits);
        const auto binary = [&](const std::string& op, auto f) {
            out.push_back(build(op + " f" + tag, [&](backend::Circuit& c) {
                const auto a = word(c, format.width());
                const auto b = word(c, format.width());
                f(c, a, b);
            }));
        };
        binary("float add", [&](auto& c, auto& a, auto& b) { output(c, backend::float_add(c, a, b, format)); });
        binary("float subtract", [&](auto& c, auto& a, auto& b) { output(c, backend::float_subtract(c, a, b, format)); });
        binary("float multiply", [&](auto& c, auto& a, auto& b) { output(c, backend::float_multiply(c, a, b, format)); });
        binary("float divide", [&](auto& c, auto& a, auto& b) { output(c, backend::float_divide(c, a, b, format)); });
        for (int op = 0; op < 6; ++op) {
            binary("float compare " + std::to_string(op), [&](auto& c, auto& a, auto& b) {
                c.output(backend::float_compare(c, a, b, format, static_cast<backend::FloatComparison>(op)));
            });
        }
        for (const bool is_signed : {false, true}) {
            out.push_back(build("float from " + std::string(is_signed ? "i4 " : "u4 ") + tag, [&](backend::Circuit& c) {
                output(c, backend::float_from_integer(c, word(c, 4), is_signed, format));
            }));
            out.push_back(build("float to " + std::string(is_signed ? "i4 " : "u4 ") + tag, [&](backend::Circuit& c) {
                output(c, backend::float_to_integer(c, word(c, format.width()), format, 4, is_signed));
            }));
        }
        const FloatFormat to = format.exponent_bits == 2 ? FloatFormat{3, 2} : FloatFormat{2, 2};
        out.push_back(build("float convert " + tag, [&](backend::Circuit& c) {
            output(c, backend::float_convert(c, word(c, format.width()), format, to));
        }));
    }
    // Compositions of two operations.
    out.push_back(build("(a + b) * c u3", [&](backend::Circuit& c) {
        const auto a = word(c, 3);
        const auto b = word(c, 3);
        const auto d = word(c, 3);
        const auto sum = backend::add(c, a, b, false);
        auto product = backend::multiply(c, sum.value, d, false);
        product.failure = c.logical_or(product.failure, sum.failure);
        output(c, product);
    }));
    out.push_back(build("a * b < c i3", [&](backend::Circuit& c) {
        const auto a = word(c, 3);
        const auto b = word(c, 3);
        const auto d = word(c, 3);
        const auto product = backend::multiply(c, a, b, true);
        c.output(backend::less(c, product.value, d, true));
        c.output(product.failure);
    }));
    out.push_back(build("(a - b) / c i3", [&](backend::Circuit& c) {
        const auto a = word(c, 3);
        const auto b = word(c, 3);
        const auto d = word(c, 3);
        const auto difference = backend::subtract(c, a, b, true);
        output(c, backend::divide(c, difference.value, d, true));
    }));
    out.push_back(build("min(a, b) i4", [&](backend::Circuit& c) {
        const auto a = word(c, 4);
        const auto b = word(c, 4);
        const auto select = backend::less(c, a, b, true);
        backend::Word result;
        for (unsigned i = 0; i < 4; ++i) result.push_back(c.choose(select, a[i], b[i]));
        output(c, result);
    }));
    out.push_back(build("(a % b) << c u4", [&](backend::Circuit& c) {
        const auto a = word(c, 4);
        const auto b = word(c, 4);
        const auto count = word(c, 2);
        const auto r = backend::remainder(c, a, b, false);
        output(c, backend::shift(c, r.value, count, false, false));
    }));
    const FloatFormat tiny{2, 2};
    out.push_back(build("(a + b) < a f2/2", [&](backend::Circuit& c) {
        const auto a = word(c, tiny.width());
        const auto b = word(c, tiny.width());
        const auto sum = backend::float_add(c, a, b, tiny);
        c.output(backend::float_compare(c, sum, a, tiny, backend::FloatComparison::Less));
    }));
    out.push_back(build("(a + b) * a f2/2", [&](backend::Circuit& c) {
        const auto a = word(c, tiny.width());
        const auto b = word(c, tiny.width());
        output(c, backend::float_multiply(c, backend::float_add(c, a, b, tiny), a, tiny));
    }));
    out.push_back(build("int(a * b) f2/2", [&](backend::Circuit& c) {
        const auto a = word(c, tiny.width());
        const auto b = word(c, tiny.width());
        output(c, backend::float_to_integer(c, backend::float_multiply(c, a, b, tiny), tiny, 3, true));
    }));
    out.push_back(build("float(a + b) i3", [&](backend::Circuit& c) {
        const auto a = word(c, 3);
        const auto b = word(c, 3);
        output(c, backend::float_from_integer(c, backend::add(c, a, b, true).value, true, FloatFormat{3, 2}));
    }));
    std::erase_if(out, [](const Built& b) { return b.graph.input_lanes > max_lanes || b.graph.nodes.empty(); });
    return out;
}

void sort_edges(nvr::Node& node) {
    std::sort(node.edges.begin(), node.edges.end(), [](const nvr::Edge& x, const nvr::Edge& y) { return x.source < y.source; });
}

bool has_source(const nvr::Node& node, std::uint32_t source) {
    return std::any_of(node.edges.begin(), node.edges.end(), [&](const nvr::Edge& e) { return e.source == source; });
}

// Inserts a copy of node n right after it; later wires move up by one.
void duplicate_node(nvr::Graph& graph, lift::ProofAdvice& advice, std::size_t n) {
    const auto at = graph.input_lanes + static_cast<std::uint32_t>(n) + 1;
    const auto shift = [&](std::uint32_t& wire) {
        if (wire >= at && wire != nvr::absent) ++wire;
    };
    auto copy = graph.nodes[n];
    graph.nodes.insert(graph.nodes.begin() + static_cast<std::ptrdiff_t>(n) + 1, copy);
    for (std::size_t m = n + 2; m < graph.nodes.size(); ++m) {
        for (auto& edge : graph.nodes[m].edges) shift(edge.source);
    }
    for (auto& o : graph.outputs) shift(o);
    graph.outputs.push_back(at);
    if (advice.instance.size() + 1 == graph.nodes.size()) {
        advice.instance.insert(advice.instance.begin() + static_cast<std::ptrdiff_t>(n) + 1, advice.instance[n]);
    }
    for (auto& signals : advice.inputs) {
        for (auto& signal : signals) {
            for (auto& wire : signal) shift(wire);
        }
    }
}

// Wires that are bits on every input, and whether two are ever 1 together.
struct BitWires {
    std::vector<std::uint32_t> wires;
};

std::vector<std::uint32_t> bit_wires(const Truth& truth, std::uint32_t inputs) {
    std::vector<std::uint32_t> out;
    if (truth.overflow) return out;
    for (std::uint32_t w = inputs; w < truth.wires; ++w) {
        if (truth.lo[w] == 0 && truth.hi[w] == 1) out.push_back(w);
    }
    return out;
}

bool together(const Truth& truth, std::span<const std::uint32_t> wires) {
    for (std::uint64_t a = 0; a < truth.assignments; ++a) {
        if (std::all_of(wires.begin(), wires.end(), [&](std::uint32_t w) { return truth.value(a, w) == 1; })) return true;
    }
    return false;
}

// Mutations of a gadget graph, each with advice adjusted where wires move.
std::vector<Built> mutations(const Built& base, const Truth& truth, std::mt19937_64& random, unsigned count) {
    std::vector<Built> out;
    const auto inputs = base.graph.input_lanes;
    const auto nodes = base.graph.nodes.size();
    const auto pick = [&] { return random() % nodes; };
    for (unsigned i = 0; i < count; ++i) {
        Built m = base;
        auto& graph = m.graph;
        const auto n = pick();
        auto& node = graph.nodes[n];
        switch (random() % 7) {
        case 0: {
            const std::int64_t delta = random() % 2 ? 1 : std::int64_t{1} << (random() % 63);
            const auto sign = random() % 2 ? 1 : -1;
            if (node.edges.empty() || random() % 2) {
                if (!checked_add_ok(node.bias, sign * delta)) continue;
                node.bias += sign * delta;
                m.name += " bias";
            } else {
                auto& weight = node.edges[random() % node.edges.size()].weight;
                if (!checked_add_ok(weight, sign * delta) || weight + sign * delta == 0) continue;
                weight += sign * delta;
                m.name += " weight";
            }
            break;
        }
        case 1: {
            if (node.edges.empty()) continue;
            const auto wire = inputs + static_cast<std::uint32_t>(n);
            const auto source = static_cast<std::uint32_t>(random() % wire);
            if (has_source(node, source)) continue;
            node.edges[random() % node.edges.size()].source = source;
            sort_edges(node);
            m.name += " retarget";
            break;
        }
        case 2:
            node.activation = node.activation == nvr::Activation::Relu ? nvr::Activation::Identity : nvr::Activation::Relu;
            m.name += " activation";
            break;
        case 3:
            duplicate_node(graph, m.advice, n);
            m.name += " duplicate";
            break;
        default: {
            // A reader with weights near 2^62 on two or three bit wires:
            // exclusive ones need their correlation proven, others must fail.
            const auto bits = bit_wires(truth, inputs);
            if (bits.size() < 2) continue;
            const bool three = bits.size() >= 3 && random() % 2;
            const bool want_exclusive = random() % 2;
            std::vector<std::uint32_t> chosen;
            for (int attempt = 0; attempt < 64; ++attempt) {
                chosen.clear();
                while (chosen.size() < (three ? 3U : 2U)) {
                    const auto w = bits[random() % bits.size()];
                    if (std::find(chosen.begin(), chosen.end(), w) == chosen.end()) chosen.push_back(w);
                }
                std::sort(chosen.begin(), chosen.end());
                if (together(truth, chosen) != want_exclusive) break;
            }
            nvr::Node reader;
            reader.activation = random() % 2 ? nvr::Activation::Relu : nvr::Activation::Identity;
            if (random() % 2) {
                // Through a sum node, whose interval case analysis narrows
                // when the wires are exclusive: the reader is then provable,
                // and overflows exactly when they are all 1.
                nvr::Node sum{nvr::Activation::Identity, 0, {}};
                for (const auto w : chosen) sum.edges.push_back({w, 1});
                graph.nodes.push_back(sum);
                const auto t = inputs + static_cast<std::uint32_t>(graph.nodes.size()) - 1;
                reader.bias = p62 - 1;
                reader.edges.push_back({t, three ? p61 : p62});
                m.advice.instance.push_back(static_cast<std::uint32_t>(m.advice.inputs.size()));
                std::vector<std::vector<std::uint32_t>> signals;
                for (const auto w : chosen) signals.push_back({w});
                m.advice.inputs.push_back(signals);
                m.name += " via sum";
            } else if (three) {
                reader.bias = p62 - 1;
                for (const auto w : chosen) reader.edges.push_back({w, p61});
            } else if (random() % 2) {
                reader.bias = 0;
                for (const auto w : chosen) reader.edges.push_back({w, p62});
            } else {
                reader.bias = -1;
                for (const auto w : chosen) reader.edges.push_back({w, -p62});
            }
            graph.nodes.push_back(reader);
            const auto wire = inputs + static_cast<std::uint32_t>(graph.nodes.size()) - 1;
            graph.outputs.push_back(wire);
            // Its advice: the read wires as single signals (a cut a wrongly
            // trusted analysis would accept), or one signal of all of them.
            m.advice.instance.push_back(static_cast<std::uint32_t>(m.advice.inputs.size()));
            if (random() % 2) {
                std::vector<std::vector<std::uint32_t>> signals;
                for (const auto w : chosen) signals.push_back({w});
                m.advice.inputs.push_back(signals);
            } else {
                m.advice.inputs.push_back({chosen});
            }
            m.name += together(truth, chosen) ? " reader (not exclusive)" : " reader (exclusive)";
            break;
        }
        }
        out.push_back(std::move(m));
    }
    return out;
}

// Random feed-forward graphs, most ReLU, small weights, some near 2^62.
Built random_graph(std::mt19937_64& random) {
    Built built;
    built.name = "random graph";
    auto& graph = built.graph;
    graph.input_lanes = 4 + static_cast<std::uint32_t>(random() % 7);
    const auto count = 5 + random() % 36;
    const bool steep = random() % 3 == 0;
    for (std::size_t n = 0; n < count; ++n) {
        const auto wire = graph.input_lanes + static_cast<std::uint32_t>(n);
        nvr::Node node;
        node.activation = random() % 5 == 0 ? nvr::Activation::Identity : nvr::Activation::Relu;
        node.bias = static_cast<std::int64_t>(random() % 7) - 3;
        const auto edges = random() % 9 == 0 ? 0 : 1 + random() % 4;
        for (std::size_t e = 0; e < edges; ++e) {
            // Mostly recent wires, so depth builds up.
            const auto source = random() % 3 == 0 ? static_cast<std::uint32_t>(random() % wire)
                                                  : wire - 1 - static_cast<std::uint32_t>(random() % std::min<std::uint32_t>(wire, 6));
            if (has_source(node, source)) continue;
            std::int64_t weight = static_cast<std::int64_t>(random() % 7) - 3;
            if (weight == 0) weight = 1;
            if (steep && random() % 6 == 0) {
                const std::int64_t big[] = {p62, -p62, p62 - 1, p61, -p61, p61 + 1, p62 / 3};
                weight = big[random() % 7];
            }
            node.edges.push_back({source, weight});
        }
        if (steep && random() % 10 == 0) node.bias = random() % 2 ? p62 : -p62;
        sort_edges(node);
        graph.nodes.push_back(std::move(node));
    }
    const auto wires = wire_count(graph);
    graph.outputs.push_back(wires - 1);
    for (int o = 0; o < 3; ++o) graph.outputs.push_back(graph.input_lanes + static_cast<std::uint32_t>(random() % count));
    built.advice = plausible_advice(graph);
    return built;
}

// Random graphs of circuit-library bit gadgets over a pool of signals.
Built random_circuit(std::mt19937_64& random) {
    return build("random circuit", [&](backend::Circuit& c) {
        std::vector<backend::Signal> pool;
        const auto inputs = 4 + random() % 9;
        for (std::size_t i = 0; i < inputs; ++i) pool.push_back(c.input());
        const auto any = [&] { return pool[random() % pool.size()]; };
        const auto steps = 4 + random() % 14;
        for (std::size_t s = 0; s < steps; ++s) {
            switch (random() % 7) {
            case 0: pool.push_back(c.logical_and(any(), any())); break;
            case 1: pool.push_back(c.logical_or(any(), any())); break;
            case 2: pool.push_back(c.logical_xor(any(), any())); break;
            case 3: pool.push_back(c.choose(any(), any(), any())); break;
            case 4: {
                std::vector<backend::Signal> bits;
                for (std::size_t k = 0; k < 2 + random() % 3; ++k) bits.push_back(any());
                pool.push_back(random() % 2 ? c.all(bits) : c.any(bits));
                break;
            }
            default: {
                backend::Signal sum;
                std::int64_t total = 0;
                for (std::size_t k = 0; k < 2 + random() % 3; ++k) {
                    const std::int64_t weight = 1 + static_cast<std::int64_t>(random() % 3);
                    sum = sum + weight * any();
                    total += weight;
                }
                pool.push_back(c.at_least(sum, 1 + static_cast<std::int64_t>(random() % static_cast<std::uint64_t>(total))));
                break;
            }
            }
        }
        for (std::size_t o = 0; o < 4; ++o) c.output(pool[pool.size() - 1 - random() % std::min<std::size_t>(pool.size(), 8)]);
    });
}

// Copies of one gadget that differ in exactly one thing the enumeration
// memo's key must tell apart. Each copy is wide enough that its sum is
// narrowed by case analysis in the same analysis as the others.
Built memo_copies(std::mt19937_64& random) {
    Built built;
    auto& graph = built.graph;
    const char* names[] = {"weight", "bias", "activation", "edge weights swapped", "source", "constant", "shared inputs"};
    const auto kind = random() % 7;
    built.name = std::string("memo copies (") + names[kind] + ")";
    const bool shared = kind == 6;
    const auto arity = 2 + random() % 2;                       // inputs per copy
    const auto copies = shared ? 6 : std::min<std::size_t>(5, 12 / arity);
    graph.input_lanes = static_cast<std::uint32_t>(shared ? 3 * arity : copies * arity);
    const auto add = [&](nvr::Node node) {
        sort_edges(node);
        graph.nodes.push_back(std::move(node));
        return graph.input_lanes + static_cast<std::uint32_t>(graph.nodes.size()) - 1;
    };
    // A constant neuron read in place of an input by the "constant" copies.
    std::vector<std::uint32_t> constants;
    if (kind == 5) {
        for (std::int64_t c : {0, 1, 2, -1}) constants.push_back(add({nvr::Activation::Identity, c, {}}));
    }
    const auto changed = random() % copies;
    const auto shape = random() % 3;
    for (std::size_t k = 0; k < copies; ++k) {
        const bool odd = k == changed || (shared && k % 2 == 1);
        std::vector<std::uint32_t> x;
        for (std::size_t i = 0; i < arity; ++i) {
            x.push_back(static_cast<std::uint32_t>(shared ? (k % 3) * arity + i : k * arity + i));
        }
        if (kind == 5) x[0] = constants[k % constants.size()];
        if (kind == 4 && odd) x[0] = x[0] == 0 ? 1 : 0;  // reads another earlier wire
        if (kind == 4 && odd && std::find(x.begin() + 1, x.end(), x[0]) != x.end()) x[0] = graph.input_lanes - 1;
        std::int64_t k_weight = (kind == 0 && odd) ? 3 : 2;
        std::int64_t b = (kind == 1 && odd) ? 0 : 1;
        auto act = (kind == 2 && odd) ? nvr::Activation::Identity : nvr::Activation::Relu;
        nvr::Node conj{act, -b, {}};
        for (std::size_t i = 0; i < arity; ++i) {
            const std::int64_t w = (kind == 3 && odd) ? static_cast<std::int64_t>(arity - i) : static_cast<std::int64_t>(i + 1);
            if (std::none_of(conj.edges.begin(), conj.edges.end(), [&](const nvr::Edge& e) { return e.source == x[i]; })) {
                conj.edges.push_back({x[i], shape == 0 ? 1 : w});
            }
        }
        const auto c = add(conj);
        nvr::Node sum{nvr::Activation::Identity, 0, {}};
        for (const auto& e : conj.edges) sum.edges.push_back(e);
        sum.edges.push_back({c, -k_weight});
        const auto s = add(sum);
        if (shape == 2) {
            // A step pair over the copy's sum, read whole.
            const auto p = add({nvr::Activation::Relu, 0, {{s, 1}}});
            const auto q = add({nvr::Activation::Relu, -1, {{s, 1}}});
            graph.outputs.push_back(add({nvr::Activation::Identity, 0, {{p, 1}, {q, -1}, {s, 1}}}));
        } else {
            graph.outputs.push_back(s);
        }
    }
    built.advice = plausible_advice(graph);
    return built;
}

// ---------------------------------------------------------------------------
// The edges of the input space: weights and biases at the ends of the
// 64-bit range mixed with small ones, wires whose intervals are 2^62 to 2^64
// wide, step pairs and selection halves read with such weights, and nodes
// whose edges are duplicated or out of order, which no proof may accept.

constexpr std::int64_t min64 = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t max64 = std::numeric_limits<std::int64_t>::max();

std::int64_t extreme_value(std::mt19937_64& random) {
    constexpr std::int64_t values[] = {min64, min64 + 1, -p62, -1, 1, p62, max64 - 1, max64};
    return values[random() % 8];
}

// An extreme value one time in three, else one in [-3, 3]; a weight is
// never 0.
std::int64_t mixed_value(std::mt19937_64& random, bool weight) {
    if (random() % 3 == 0) return extreme_value(random);
    const auto value = static_cast<std::int64_t>(random() % 7) - 3;
    return weight && value == 0 ? 1 : value;
}

// Whether every node reads earlier wires, each once, in ascending order:
// what the loader requires, decided here without the analysis.
bool well_formed(const nvr::Graph& graph) {
    for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
        const auto& edges = graph.nodes[n].edges;
        for (std::size_t e = 0; e < edges.size(); ++e) {
            if (edges[e].source >= graph.input_lanes + n || (e > 0 && edges[e].source <= edges[e - 1].source)) return false;
        }
    }
    return true;
}

std::uint32_t add_node(nvr::Graph& graph, nvr::Node node) {
    sort_edges(node);
    graph.nodes.push_back(std::move(node));
    return wire_count(graph) - 1;
}

// `count` distinct input lanes (at most the graph's).
std::vector<std::uint32_t> distinct_inputs(const nvr::Graph& graph, std::size_t count, std::mt19937_64& random) {
    std::vector<std::uint32_t> lanes(graph.input_lanes);
    std::iota(lanes.begin(), lanes.end(), 0U);
    std::shuffle(lanes.begin(), lanes.end(), random);
    lanes.resize(std::min<std::size_t>(count, lanes.size()));
    return lanes;
}

// A node reading 1 to `most` distinct earlier wires, mostly recent ones,
// with mixed weights and bias.
nvr::Node mixed_node(const nvr::Graph& graph, std::mt19937_64& random, std::size_t most) {
    nvr::Node node;
    node.activation = random() % 3 == 0 ? nvr::Activation::Identity : nvr::Activation::Relu;
    node.bias = mixed_value(random, false);
    const auto wires = wire_count(graph);
    const auto edges = 1 + random() % most;
    for (std::size_t e = 0; e < edges; ++e) {
        const auto source = random() % 2 ? static_cast<std::uint32_t>(random() % wires)
                                         : wires - 1 - static_cast<std::uint32_t>(random() % std::min<std::uint32_t>(wires, 4));
        if (!has_source(node, source)) node.edges.push_back({source, mixed_value(random, true)});
    }
    return node;
}

Built extreme_graph(std::mt19937_64& random) {
    Built built;
    auto& graph = built.graph;
    graph.input_lanes = 3 + static_cast<std::uint32_t>(random() % 5);
    const char* names[] = {"weights", "wide wires", "step pair", "selection", "wide step pair"};
    const auto kind = random() % 5;
    built.name = std::string("extreme: ") + names[kind];
    const auto bit = [&] { return static_cast<std::uint32_t>(random() % graph.input_lanes); };
    std::vector<std::uint32_t> readers;
    switch (kind) {
    case 0: {
        const auto count = 3 + random() % 10;
        for (std::size_t n = 0; n < count; ++n) add_node(graph, mixed_node(graph, random, 3));
        break;
    }
    case 1: {
        // Wires whose intervals are 2^62, 2^63, 2^64 - 2 and 2^64 - 1 wide,
        // none of which overflows, and readers of them.
        std::vector<std::uint32_t> wide;
        const auto shapes = 1 + random() % 3;
        for (std::size_t i = 0; i < shapes; ++i) {
            const auto x = distinct_inputs(graph, 2, random);
            switch (random() % 5) {
            case 0: wide.push_back(add_node(graph, {nvr::Activation::Identity, 0, {{x[0], p62}}})); break;
            case 1: wide.push_back(add_node(graph, {nvr::Activation::Identity, 0, {{x[0], p62}, {x[1], -p62}}})); break;
            case 2: wide.push_back(add_node(graph, {nvr::Activation::Identity, -p62, {{x[0], p62}, {x[1], p62}}})); break;
            case 3: wide.push_back(add_node(graph, {nvr::Activation::Identity, min64, {{x[0], max64}, {x[1], max64}}})); break;
            default: wide.push_back(add_node(graph, {nvr::Activation::Identity, 0, {{x[0], max64}, {x[1], min64}}})); break;
            }
        }
        const auto readings = 1 + random() % 3;
        for (std::size_t i = 0; i < readings; ++i) {
            const auto w = wide[random() % wide.size()];
            nvr::Node reader{random() % 2 ? nvr::Activation::Relu : nvr::Activation::Identity, 0, {}};
            switch (random() % 5) {
            case 0: reader.edges.push_back({w, 1}); break;
            case 1: reader.edges = {{bit(), static_cast<std::int64_t>(random() % 3) + 1}, {w, 1}}; break;
            case 2: reader.edges.push_back({w, -1}); break;
            case 3: {
                const auto v = wide[random() % wide.size()];
                reader.edges.push_back({w, 1});
                if (v != w) reader.edges.push_back({v, random() % 2 ? 1 : -1});
                break;
            }
            default:
                reader.bias = mixed_value(random, false);
                reader.edges.push_back({w, mixed_value(random, true)});
                break;
            }
            readers.push_back(add_node(graph, reader));
        }
        break;
    }
    case 2:
    case 4: {
        // P = relu(L + b) and Q = relu(L + b - 1), read with extreme weights.
        nvr::Node sum{nvr::Activation::Relu, static_cast<std::int64_t>(random() % 5) - 2, {}};
        for (const auto x : distinct_inputs(graph, 1 + random() % 3, random)) {
            sum.edges.push_back({x, kind == 4 ? mixed_value(random, true) : static_cast<std::int64_t>(random() % 5) - 2});
            if (sum.edges.back().weight == 0) sum.edges.back().weight = 1;
        }
        if (kind == 4 && random() % 2) sum.bias = extreme_value(random);
        if (sum.bias == min64) sum.bias = min64 + 1;
        const auto p = add_node(graph, sum);
        if (random() % 3 == 0) add_node(graph, mixed_node(graph, random, 2));
        sum.bias -= 1;
        const auto q = add_node(graph, sum);
        const auto readings = 1 + random() % 3;
        for (std::size_t i = 0; i < readings; ++i) {
            nvr::Node reader{random() % 2 ? nvr::Activation::Relu : nvr::Activation::Identity, mixed_value(random, false), {}};
            const auto k = mixed_value(random, true);
            switch (random() % 5) {
            case 0: reader.edges = {{p, k}, {q, k == min64 ? min64 : -k}}; break;
            case 1: reader.edges = {{p, k}, {q, k}}; break;
            case 2:
                reader.edges = {{q, min64}};
                reader.bias = 0;
                break;
            case 3: reader.edges = {{p, k}}; break;
            default: reader.edges = {{p, k}, {q, mixed_value(random, true)}}; break;
            }
            if (random() % 3 == 0) reader.edges.push_back({bit(), mixed_value(random, true)});
            readers.push_back(add_node(graph, reader));
        }
        break;
    }
    default: {
        // The halves of a selection, relu(s + t - 1) and relu(f - s), read
        // with equal extreme weights, adjacent or not.
        const auto x = distinct_inputs(graph, 3, random);
        const auto a = add_node(graph, {nvr::Activation::Relu, -1, {{x[0], 1}, {x[1], 1}}});
        if (random() % 3 == 0) add_node(graph, mixed_node(graph, random, 2));
        const auto b = add_node(graph, {nvr::Activation::Relu, 0, {{x[0], -1}, {x[2], 1}}});
        const auto readings = 1 + random() % 3;
        for (std::size_t i = 0; i < readings; ++i) {
            const auto k = extreme_value(random);
            nvr::Node reader{random() % 2 ? nvr::Activation::Relu : nvr::Activation::Identity, mixed_value(random, false),
                             {{a, k}, {b, random() % 4 == 0 ? mixed_value(random, true) : k}}};
            if (random() % 3 == 0) reader.edges.push_back({x[random() % 3], mixed_value(random, true)});
            readers.push_back(add_node(graph, reader));
        }
        break;
    }
    }
    // A reader of the readers, so that their sources are cut below it.
    if (readers.size() >= 2 && random() % 2) {
        nvr::Node top{nvr::Activation::Identity, mixed_value(random, false), {}};
        for (const auto r : readers) top.edges.push_back({r, random() % 2 ? 1 : mixed_value(random, true)});
        add_node(graph, top);
    }
    if (random() % 3 == 0) add_node(graph, mixed_node(graph, random, 3));
    // The last wire as a lane, or the bit relu(w) - relu(w - 1) of it, a
    // step pair over a wide sum read whole; sometimes another wire too.
    const auto last = wire_count(graph) - 1;
    if (random() % 3 != 0) {
        const auto p = add_node(graph, {nvr::Activation::Relu, 0, {{last, 1}}});
        const auto q = add_node(graph, {nvr::Activation::Relu, -1, {{last, 1}}});
        graph.outputs.push_back(add_node(graph, {nvr::Activation::Identity, 0, {{p, 1}, {q, -1}}}));
    } else {
        graph.outputs.push_back(last);
    }
    if (random() % 3 == 0) {
        graph.outputs.push_back(graph.input_lanes + static_cast<std::uint32_t>(random() % graph.nodes.size()));
    }
    built.advice = plausible_advice(graph);
    // One graph in four made malformed: an edge duplicated (as a node that
    // reads P twice), or two edges swapped.
    if (random() % 4 == 0) {
        std::vector<std::size_t> with_edges;
        for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
            if (!graph.nodes[n].edges.empty()) with_edges.push_back(n);
        }
        auto& node = graph.nodes[random() % 2 ? with_edges.back() : with_edges[random() % with_edges.size()]];
        if (node.edges.size() >= 2 && random() % 2) {
            const auto e = random() % (node.edges.size() - 1);
            std::swap(node.edges[e], node.edges[e + 1]);
            built.name = "extreme: malformed (edges out of order)";
        } else {
            const auto e = random() % node.edges.size();
            auto copy = node.edges[e];
            if (random() % 2) copy.weight = mixed_value(random, true);
            node.edges.insert(node.edges.begin() + static_cast<std::ptrdiff_t>(e) + 1, copy);
            built.name = "extreme: malformed (duplicate edge)";
        }
    }
    return built;
}

} // namespace overflow_fuzz

NEURA_TEST(overflow, exhaustive_fuzzing_finds_no_false_proof) {
    // Library gadgets with their recorded advice, their mutations, random
    // graphs, and copies of one gadget that the enumeration memo must tell
    // apart, each tried with every kind of advice and use and with small
    // work resumed (see overflow_fuzz above).
    using namespace overflow_fuzz;
    const auto factor = scale();
    Harness harness(0x0f1a7e5eedULL, factor > 1);
    auto& random = harness.random;
    const auto start = std::chrono::steady_clock::now();
    lift::ProofAdvice previous;
    std::size_t library = 0;
    std::size_t library_proven = 0;
    const bool trace = std::getenv("NEURA_OVERFLOW_FUZZ_TRACE") != nullptr;
    const auto try_graph = [&](Built built, const std::string& generator, bool all_advice) {
        const auto begun = std::chrono::steady_clock::now();
        Subject subject{generator, std::move(built.graph), std::move(built.advice), {}, {}};
        subject.truth = exhaust(subject.graph);
        ++harness.graphs;
        harness.unsound_graphs += subject.truth.unsound() ? 1 : 0;
        bool proven = false;
        // AdviceUse::none ignores advice: once per graph.
        proven = harness.run(subject, "no", nullptr, true) || proven;
        auto variants = advice_variants(subject.graph, subject.advice, previous, random);
        if (!all_advice) {
            // A sample of the kinds, always with the true advice.
            std::shuffle(variants.begin() + 1, variants.end(), random);
            variants.resize(4);
        }
        for (const auto& [kind, advice] : variants) proven = harness.run(subject, kind, &advice, false) || proven;
        harness.proven_graphs += proven ? 1 : 0;
        if (generator == "library gadget") {
            ++library;
            library_proven += proven ? 1 : 0;
            if (!proven) std::cout << "  not proven: " << built.name << "\n";
        }
        previous = subject.advice;
        if (trace) {
            std::cout << "    " << generator << " / " << built.name << ": " << subject.graph.nodes.size() << " nodes, "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begun).count()
                      << " ms" << std::endl;
        }
        return subject.truth;
    };
    // 1 and 2: library gadgets and their mutations.
    const auto gadgets = library_gadgets();
    std::size_t index = 0;
    for (const auto& gadget : gadgets) {
        const bool big = gadget.graph.nodes.size() > 120;
        // The default run takes every gadget of at most 120 nodes, every
        // third one of at most 200, and the larger ones only once (the float
        // tests above prove those with their advice).
        const auto nodes = gadget.graph.nodes.size();
        if (big && factor == 1 && (nodes > 200 || index++ % 3 != 0)) continue;
        const auto truth = try_graph(gadget, "library gadget", factor > 1 || nodes <= 64);
        const auto count = factor == 1 ? (big ? 0U : 1U) : (big ? 2U : 6U) * factor;
        for (auto& mutant : mutations(gadget, truth, random, count)) {
            const auto generator = mutant.name.find("reader") != std::string::npos
                                       ? std::string("mutation: reader ") + (mutant.name.find("not exclusive") != std::string::npos ? "(not exclusive)" : "(exclusive)")
                                       : "mutation: " + mutant.name.substr(mutant.name.rfind(' ') + 1);
            try_graph(std::move(mutant), generator, factor > 1 && !big);
        }
    }
    // 3: random graphs, plain and from library bit gadgets.
    for (unsigned i = 0; i < (factor == 1 ? 20U : 30 * factor); ++i) try_graph(random_graph(random), "random graph", factor > 1 && i % 4 == 0);
    for (unsigned i = 0; i < 40 * factor; ++i) try_graph(random_circuit(random), "random circuit", i % 2 == 0);
    // 4: memo copies.
    for (unsigned i = 0; i < 50 * factor; ++i) try_graph(memo_copies(random), "memo copies", true);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << harness.report() << "  " << elapsed << " ms\n";
    for (const auto& failure : harness.failures) {
        if (!failure.empty()) std::cout << "  FALSE PROOF " << failure << "\n";
    }
    check(harness.failures.empty(), harness.failures.empty() ? "" : harness.failures.front());
    // The fuzzer tests something only if both classes are well represented.
    std::cout << "  " << library_proven << " of " << library << " library gadgets proven\n";
    check(library_proven * 10 >= library * 9, "at least 90% of the library gadgets are proven");
    check(harness.unsound_graphs * 20 > harness.graphs, "at least 5% of the graphs are unsound");
    check(harness.proven_graphs * 4 > harness.graphs, "at least a quarter of the graphs are proven");
}

// ---------------------------------------------------------------------------
// Inputs on which the overflow proof is wrong, crashes, or does not end.

namespace overflow_defects {

// Runs `body` in a child process, so that a crash or a loop that does not
// end fails the test instead of the suite: empty when the body returned
// within `seconds`, else how the child ended.
std::string in_child(const std::function<void()>& body, unsigned seconds) {
#if defined(__unix__) || defined(__APPLE__)
    std::cout.flush();
    const auto child = fork();
    if (child < 0) return "fork failed";
    if (child == 0) {
        alarm(seconds);
        body();
        _exit(0);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child) return "waitpid failed";
    if (WIFSIGNALED(status)) {
        const auto signal = WTERMSIG(status);
        return signal == SIGALRM ? "did not end within " + std::to_string(seconds) + " s"
               : signal == SIGFPE ? std::string("killed by SIGFPE")
                                  : "killed by signal " + std::to_string(signal);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return "exited with status " + std::to_string(WEXITSTATUS(status));
    return {};
#else
    body();
    (void)seconds;
    return {};
#endif
}

} // namespace overflow_defects

NEURA_TEST(overflow, a_node_reading_one_wire_twice_is_not_proven_from_the_wires_group) {
    // P = relu(x0 + 2 x1 + 4 x2) and Q = relu(x0 + 2 x1 + 4 x2 - 1) form a
    // step pair, the group P - Q in [0, 1]. R reads P twice with weight 1,
    // so R = 2P takes every even value in [0, 14]. factor() counts R's two
    // edges from P as the group's two members, so R is read as 1 * (P - Q)
    // and given [0, 1]. The loader rejects a node whose edges do not name
    // ascending, unique sources, but prove_no_overflow does not, and accepts
    // R as a bit.
    nvr::Graph graph;
    graph.input_lanes = 3;
    graph.nodes.push_back({nvr::Activation::Relu, 0, {{0, 1}, {1, 2}, {2, 4}}});
    graph.nodes.push_back({nvr::Activation::Relu, -1, {{0, 1}, {1, 2}, {2, 4}}});
    graph.nodes.push_back({nvr::Activation::Identity, 0, {{3, 1}, {3, 1}}});
    graph.outputs = {5};
    const auto proof = lift::prove_no_overflow(graph);
    const std::uint32_t lanes[] = {0};
    const bool bits = proof.safe && lift::lanes_are_bits(proof, graph, lanes);
    std::string claimed = "not safe";
    if (proof.safe) {
        claimed = "[" + std::to_string(proof.wires[5].lo) + ", " + std::to_string(proof.wires[5].hi) + "]";
    }
    check(!proof.safe || proof.wires[5].hi >= 14,
          "wire 5 (R = 2P, values 0 to 14) claimed " + claimed + (bits ? ", and its lane accepted as a bit" : ""));
}

NEURA_TEST(overflow, the_most_negative_weight_on_a_step_pair_member_does_not_crash_the_analysis) {
    // P = relu(x0) and Q = relu(x0 - 1) form a step pair with coefficients
    // +1 and -1. R = INT64_MIN * Q never overflows (Q is 0). grouped_sum()
    // calls factor(), which computes INT64_MIN % -1 and INT64_MIN / -1:
    // undefined behavior, a SIGFPE on x86-64. Both the unrefined and the
    // refined analysis reach it, before any advice is consulted.
    nvr::Graph graph;
    graph.input_lanes = 1;
    graph.nodes.push_back({nvr::Activation::Relu, 0, {{0, 1}}});
    graph.nodes.push_back({nvr::Activation::Relu, -1, {{0, 1}}});
    graph.nodes.push_back({nvr::Activation::Identity, 0, {{2, std::numeric_limits<std::int64_t>::min()}}});
    graph.outputs = {3};
    const auto outcome = overflow_defects::in_child([&] { (void)lift::prove_no_overflow(graph, false); }, 20);
    check(outcome.empty(), "prove_no_overflow on a graph that cannot overflow " + outcome);
}

NEURA_TEST(overflow, a_wire_wider_than_2_63_does_not_escape_the_work_bound) {
    // N = 2^62 x0 - 2^62 x1 never overflows; its interval [-2^62, 2^62] is
    // 2^63 wide, so interval.hi - interval.lo overflows (undefined) in
    // range() and exact_range(), which wrap it to a negative width and take
    // N as a free variable of 2^63 + 1 values. T = N is then refined by
    // exact_range(), whose eliminate() sums out a variable no cone node reads
    // by iterating over every value without charging work: the analysis
    // does not end, though it was given 2^22 units of work.
    nvr::Graph graph;
    graph.input_lanes = 2;
    graph.nodes.push_back({nvr::Activation::Identity, 0, {{0, std::int64_t{1} << 62}, {1, -(std::int64_t{1} << 62)}}});
    graph.nodes.push_back({nvr::Activation::Identity, 0, {{2, 1}}});
    graph.outputs = {3};
    const auto outcome = overflow_defects::in_child(
        [&] {
            std::uint64_t work = std::uint64_t{1} << 22;
            (void)lift::prove_no_overflow(graph, true, &work);
        },
        5);
    check(outcome.empty(), "prove_no_overflow with 2^22 units of work " + outcome);
}

NEURA_TEST(overflow, exhaustive_fuzzing_of_extreme_values_and_malformed_graphs) {
    // The extreme_graph class of overflow_fuzz, audited as the other
    // classes are (every interval contains the truth, no safe proof of a
    // graph that overflows or is malformed, accepted lanes are bits), with
    // no advice and with plausible advice, unbounded, with the runtime's
    // bound and with small work resumed. It runs in a child process, so
    // that a crash or a loop that ignores the work bound fails the test.
    using namespace overflow_fuzz;
    const auto factor = scale();
    const auto outcome = overflow_defects::in_child(
        [&] {
            int status = 0;
            try {
                Harness harness(0xe7d3e5eedULL, factor > 1);
                std::uint64_t malformed = 0;
                std::uint64_t overflow_free = 0;
                std::uint64_t safe = 0;
                std::uint64_t proven = 0;
                const auto start = std::chrono::steady_clock::now();
                for (unsigned i = 0; i < 3000 * factor; ++i) {
                    auto built = extreme_graph(harness.random);
                    Subject subject{built.name, std::move(built.graph), std::move(built.advice), {}, {}};
                    subject.malformed = !well_formed(subject.graph);
                    subject.truth = exhaust(subject.graph);
                    ++harness.graphs;
                    harness.unsound_graphs += subject.truth.unsound() ? 1 : 0;
                    malformed += subject.malformed ? 1 : 0;
                    overflow_free += subject.truth.overflow ? 0 : 1;
                    safe += lift::prove_no_overflow(subject.graph).safe ? 1 : 0;
                    bool found = harness.run(subject, "no", nullptr, true);
                    found = harness.run(subject, "true", &subject.advice, false) || found;
                    harness.proven_graphs += found ? 1 : 0;
                    proven += found ? 1 : 0;
                }
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
                std::cout << harness.report() << "  " << malformed << " malformed, " << overflow_free
                          << " without overflow, " << safe << " proven safe without advice, " << proven
                          << " accepted, " << elapsed << " ms\n";
                for (const auto& failure : harness.failures) {
                    if (!failure.empty()) std::cout << "  FALSE PROOF " << failure << "\n";
                }
                if (!harness.failures.empty()) status = 1;
                // The class tests something only if each kind is well
                // represented.
                if (malformed * 10 < harness.graphs || overflow_free * 5 < harness.graphs ||
                    safe * 10 < harness.graphs || proven * 20 < harness.graphs) {
                    std::cout << "  too few malformed, overflow-free, safe or accepted graphs\n";
                    status = 1;
                }
            } catch (const std::exception& error) {
                std::cout << "  " << error.what() << "\n";
                status = 1;
            }
            std::cout.flush();
            _exit(status);
        },
        120 * factor);
    check(outcome.empty(), "the extreme-value fuzzer " + outcome);
}

} // namespace neura::testing
