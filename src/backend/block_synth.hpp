#pragma once

#include "circuit.hpp"
#include "nvr/image.hpp"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace neura::backend {

// A value while a block is synthesized. Immediates and constants never need
// a block; slot operands name committed frame state; bit operands live in the
// graph of the block that computed them.
struct Operand {
    enum class Kind { Void, Immediate, Constant, Slot, Bits };

    Kind kind = Kind::Void;
    std::uint32_t type = 0;
    // Constant or slot index.
    std::uint32_t index = 0;
    // Value of an immediate Boolean or integer.
    std::uint64_t bits = 0;
    Word word;
    // Block generation that computed a Bits operand, and the temporary slot
    // that holds it once that block has closed. Only the converter's
    // function compiler uses these.
    std::uint64_t generation = 0;
    std::uint32_t home = nvr::absent;
};

struct ExitSpec {
    nvr::ExitKind kind = nvr::ExitKind::Continue;
    // Bit that selects this exit. The indicators of a block's exits are
    // mutually exclusive and cover every case in which no check fails.
    Signal when = Signal::of(1);
    // Assignments made only by this exit, after the block's pending ones.
    std::vector<std::pair<std::uint32_t, Operand>> assignments;
    std::uint32_t target = 0;
    nvr::StateOp operation = nvr::StateOp::AllocateObject;
    std::uint32_t immediate = 0;
    std::vector<Operand> arguments;
    std::uint32_t result_slot = nvr::absent;
    std::uint32_t resume = 0;
    std::uint32_t error_slot = nvr::absent;
    std::uint32_t error_resume = 0;
    std::uint32_t reason = 0;
};

// The image tables a block refers to. The converter interns into the image it
// builds; the decompiler resolves against the image it reads.
class BlockTables {
public:
    virtual ~BlockTables() = default;
    virtual const nvr::Type& type(std::uint32_t index) const = 0;
    virtual std::uint32_t constant(std::uint32_t type, std::vector<std::uint8_t> payload) = 0;
    virtual std::uint32_t graph(const nvr::Graph& graph) = 0;
};

// Little-endian payload of an immediate of scalar type `type`.
std::vector<std::uint8_t> immediate_payload(const nvr::Type& type, std::uint64_t bits);

// Assembles one NVR block: its inputs in order of first use, its checks in
// evaluation order, its pending parallel assignments, and its exits with the
// selector and value lanes of its graph. The result depends only on the
// sequence of calls, so replaying the calls reproduces the block exactly;
// the decompiler relies on that to prove a lifted block.
//
// Operands passed in must be resolved: a Bits operand must belong to this
// block.
class BlockSynthesizer {
public:
    explicit BlockSynthesizer(BlockTables& tables) : tables_(&tables) {}

    Circuit& circuit() { return circuit_; }

    // The lanes of an operand. A slot or constant becomes a block input the
    // first time it is used; later uses share its lanes.
    Word lanes(const Operand& operand);
    // The exit value of an operand. Bits are materialized as output lanes,
    // shared between identical words.
    nvr::Value value(const Operand& operand);

    // Records a check. Consecutive checks with the same reason are merged.
    // Returns false when the condition is constantly false.
    bool fail_when(const Signal& condition, std::uint32_t reason);

    // Slot assignments made by the block's statements, committed in parallel
    // by whichever non-trap, non-return exit is taken.
    std::map<std::uint32_t, Operand>& pending() { return pending_; }
    const std::map<std::uint32_t, Operand>& pending() const { return pending_; }
    const std::vector<nvr::Value>& inputs() const { return inputs_; }
    const std::vector<std::pair<std::uint32_t, Signal>>& failures() const { return failures_; }

    // Ends the block. The first failing check, in evaluation order, selects
    // its trap; the given exits apply only when no check fails.
    nvr::Block close(std::vector<ExitSpec> exits);

private:
    BlockTables* tables_;
    Circuit circuit_;
    std::vector<nvr::Value> inputs_;
    std::map<std::pair<nvr::ValueKind, std::uint32_t>, Word> input_lanes_;
    std::map<std::vector<WireId>, std::uint32_t> output_lanes_;
    std::map<std::uint32_t, Operand> pending_;
    std::vector<std::pair<std::uint32_t, Signal>> failures_;
};

} // namespace neura::backend
