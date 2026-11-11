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

#include "backend/converter.hpp"
#include "ir-optimization/support.hpp"
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
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <sstream>

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
        for (const auto* op : {"+", "-", "*", "/", "%", "+~", "&", "|", "^", "<<", ">>"}) {
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

} // namespace neura::testing
