// The containment analysis of value types (src/neura-ir/containment.hpp,
// docs/design/Representation030.md "Finite aggregates"). Each case lowers and
// specializes a program from tests/ir-optimization/containment and checks the
// cycles it closes: the path, and the code, place and text of E0448. No stage
// enforces the error yet, so these cases are its only evidence.
#include "neura-ir/containment.hpp"
#include "neura-ir/specialize.hpp"
#include "support.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace neura::testing {
namespace {

IrProgram specialized(const Environment& environment, std::string_view file) {
    auto program = lower_files({environment.source_dir / "tests" / "ir-optimization" / "containment" / file});
    specialize(program);
    return program;
}

std::vector<std::string> paths(const IrProgram& program) {
    std::vector<std::string> result;
    for (const auto& cycle : find_containment_cycles(program)) {
        result.push_back(describe_containment_cycle(program, cycle));
    }
    return result;
}

std::string joined(const std::vector<std::string>& items) {
    std::string text;
    for (const auto& item : items) text += (text.empty() ? "" : " | ") + item;
    return text;
}

void expect_paths(const Environment& environment, std::string_view file, std::vector<std::string> expected) {
    const auto program = specialized(environment, file);
    check_equal(joined(paths(program)), joined(expected), std::string(file));
}

NEURA_TEST(structure, containment_accepts_references_and_separate_storage) {
    const auto program = specialized(environment, "accepted.nra");
    // The nested instance exists, so the case checks it.
    check(std::ranges::any_of(program.symbols, [](const IrSymbol& item) { return item.name == "Box<Box<int32>>"; }),
          "Box<Box<int32>> is instantiated");
    check(find_containment_cycles(program).empty(), "accepted.nra closes no cycle: " + joined(paths(program)));
    check_containment(program);
}

NEURA_TEST(structure, containment_reports_a_direct_cycle) {
    // The first payload that closes the cycle names it; the second is the
    // same cycle and is not reported again.
    expect_paths(environment, "self.nra", {"Tree -> Tree.Branch.0: Tree"});
    const auto program = specialized(environment, "self.nra");
    const auto cycles = find_containment_cycles(program);
    check_equal(cycles.size(), std::size_t{1}, "one cycle");
    const auto error = containment_diagnostic(program, cycles.front());
    check_equal(std::string(error.code()), std::string("E0448"), "code");
    check_equal(error.span.line, std::size_t{2}, "the error is at the variant that closes the cycle");
    check_equal(std::string(error.what()), std::string("enum 'Tree' contains itself, so its values have no finite size"),
                "message");
    bool threw = false;
    try {
        check_containment(program);
    } catch (const Diagnostic& thrown) {
        threw = std::string(thrown.code()) == "E0448";
    }
    check(threw, "check_containment throws E0448");
}

NEURA_TEST(structure, containment_reports_a_mutual_cycle_once) {
    expect_paths(environment, "mutual.nra",
                 {"Expression -> Expression.Group.0: Statement -> Statement.Value.1: Expression"});
}

NEURA_TEST(structure, containment_walks_tuple_elements) {
    expect_paths(environment, "tuple.nra", {"Pair -> Pair.Two.1.1: Pair"});
}

NEURA_TEST(structure, containment_checks_each_generic_instance) {
    expect_paths(environment, "instance.nra", {"Chain<int32> -> Chain<int32>.Link.1: Chain<int32>"});
}

NEURA_TEST(structure, containment_follows_a_generic_instance) {
    expect_paths(environment, "through-generic.nra",
                 {"Tree -> Tree.Branch.1: Optional<Tree> -> Optional<Tree>.Some.0: Tree"});
}

NEURA_TEST(structure, containment_of_the_standard_library) {
    // Error and PortableError hold their cause through a link class, so only
    // the regular expression parser's Node contains itself. Result<int>
    // contains Error and closes no cycle.
    const auto program = specialized(environment, "standard-library.nra");
    std::set<std::string> starts;
    for (const auto& cycle : find_containment_cycles(program)) {
        starts.insert(program.symbols[cycle.steps.front().from].name);
    }
    std::string found;
    for (const auto& name : starts) found += (found.empty() ? "" : ", ") + name;
    check_equal(found, std::string("Node"), "standard library cycles");
}

NEURA_TEST(structure, containment_of_the_backend_programs) {
    // Every program in tests/backend/programs, each lowered on its own: only
    // aggregates.nra declares an enum that contains itself (its Tree), and
    // the others close only the standard library's cycles. Tree in
    // generic-enums.nra recurses through a list and closes none.
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(environment.source_dir / "tests" / "backend" / "programs")) {
        if (entry.is_regular_file() && entry.path().extension() == ".nra") files.push_back(entry.path());
    }
    std::ranges::sort(files);
    check(files.size() > 20, "the backend programs are found");
    std::set<std::string> found;
    for (const auto& file : files) {
        auto program = lower_files({file});
        specialize(program);
        for (const auto& cycle : find_containment_cycles(program)) {
            const auto& name = program.symbols[cycle.steps.front().from].name;
            if (name != "Error" && name != "PortableError" && name != "Node") {
                found.insert(file.filename().string() + ": " + describe_containment_cycle(program, cycle));
            }
        }
    }
    check_equal(joined({found.begin(), found.end()}), std::string("aggregates.nra: Tree -> Tree.Node.0: Tree"),
                "cycles declared by the backend programs");
}

} // namespace
} // namespace neura::testing
