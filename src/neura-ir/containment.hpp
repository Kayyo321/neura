#pragma once

#include "ir.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace neura {

// Containment of value types (docs/design/Representation030.md, "Finite
// aggregates"). After specialization, a tuple contains each of its element
// types and a payload enum contains each payload type of each variant. A
// class, list, array, string, function value or type parameter contains
// nothing: it is a reference, or its storage is separate. A value type that
// contains itself, directly or through other value types, has no finite
// inline layout.
//
// The analysis reads a specialized program. Generic declarations are skipped
// and each instance is checked on its own, so `Optional<Tree>` and a generic
// enum instantiated with itself are checked per instance.
//
// Today's NVR layout keeps every tuple and payload enum on the heap, where a
// recursive enum runs correctly, so no stage calls check_containment yet. The
// change that gives tuples and payload enums inline layouts enforces it.

// One edge of the containment graph: enum `from` contains enum `to` in the
// payload at `payload` of `variant`, inside the tuple elements that `tuple`
// lists from the outside in.
struct ContainmentStep {
    SymbolId from = no_symbol;
    SymbolId variant = no_symbol;
    std::size_t payload = 0;
    std::vector<std::size_t> tuple{};
    SymbolId to = no_symbol;
};

// A cycle starts and ends at the same enum. `steps.back()` is the payload that
// closes it.
struct ContainmentCycle {
    std::vector<ContainmentStep> steps;
};

// One cycle per strongly connected group of enums that contain each other, in
// order of their lowest symbol id. Each cycle starts at that enum and is a
// shortest path back to it, with ties broken by symbol order, so the result
// depends only on the program.
std::vector<ContainmentCycle> find_containment_cycles(const IrProgram& program);

// The path of a cycle, as in "Tree -> Tree.Branch.0: Tree". Payloads are named
// by position, because NeuraIR does not keep payload field names; a tuple
// element adds its position, as in "Pair.Two.1.0: Pair".
std::string describe_containment_cycle(const IrProgram& program, const ContainmentCycle& cycle);

// E0448 at the variant whose payload closes the cycle.
Diagnostic containment_diagnostic(const IrProgram& program, const ContainmentCycle& cycle);

// Throws the diagnostic of the first cycle, if any.
void check_containment(const IrProgram& program);

} // namespace neura
