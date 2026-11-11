#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neura::nvm::tier2 {
struct Region;
}

namespace neura::jit {

// The native code generator of tier 2 (docs/runtime/NVM.md, "Native
// compilation"). It compiles a region program (src/nvm/tier2.hpp) to machine
// code that does exactly what the region interpreter (tier2_steps.inc) does:
// the same operations in the same order, the same deoptimization points,
// budget checks, write-backs, and commit order. Whatever it does not compile
// inline it hands to a helper that runs the interpreter's own code. A region
// it cannot compile at all runs on the interpreter.

// How a run uses the native code generator: `Off` runs every region on the
// interpreter; `On` compiles them and makes it an error when this platform
// cannot; `Auto` compiles them where the generator is enabled by default
// and silently uses the interpreter elsewhere or when executable memory
// cannot be had.
enum class Mode : std::uint8_t { Off, On, Auto };

// "off", "on", "auto"; nothing for anything else.
bool parse_mode(const std::string& text, Mode& mode);
const char* mode_name(Mode mode);

// NEURA_JIT=off|on|auto in the environment, read once per process. Unset
// (or any other value) is Auto. It combines with a run's own mode as
// combine() says.
Mode environment_mode();
// The mode a run uses given its own setting and the environment's: Off if
// either is Off, else On if either is On, else Auto.
Mode combine(Mode run, Mode environment);

enum class Arch : std::uint8_t { X86_64, AArch64 };

// Whether this build has a backend for the processor it runs on, and
// which.
bool host_has_backend();
Arch host_arch();

// What a run with `mode` gets on this host: native code or not, and why
// not. For On, `error` is set when the platform cannot (no backend for the
// processor or operating system, or no executable memory). Executable
// memory is probed once per process with a tiny program.
struct Availability {
    bool enabled = false;
    std::string error;   // On only: why native code is impossible here
    std::string reason;  // Auto: why it stays off (for --tier-stats)
};
Availability availability(Mode mode);

struct Context;

// A helper the generated code calls: (context, immediate, a, b). It
// returns its value and sets `context->status`: 0 success, 1 a failure the
// region deoptimizes for, 2 an exception the executor rethrows.
using Helper = std::uint64_t (*)(Context*, std::uint32_t, std::uint64_t, std::uint64_t);

// What the generated code reads and writes. The executor fills it before
// each call; the code reaches every helper through it, so the code holds
// no address of the binary and runs unchanged in the static launcher.
struct Context {
    std::uint64_t* registers = nullptr;           // 0: the region registers
    std::uint8_t* initialized = nullptr;          // 8: their initialized flags
    const std::uint64_t* steps = nullptr;         // 16: the run's step count
    std::uint64_t max_steps = 0;                  // 24
    std::uint64_t allowance = 0;                  // 32: the caller's allowance
    std::uint64_t* polls = nullptr;               // 40: the scheduler's polls, or null
    std::uint64_t quantum = 0;                    // 48: the task quantum
    std::uint64_t done = 0;                       // 56: out: blocks completed
    std::uint32_t next_block = 0;                 // 64: out: the block reached
    std::uint32_t outcome = 0;                    // 68: out: an Outcome
    std::uint8_t status = 0;                      // 72: helper result
    std::uint8_t padding[7] = {};
    Helper lift = nullptr;                        // 80: (index, a, b) -> value; status 1 failed
    Helper check_reference = nullptr;             // 88: (type, bits) -> status 1 failed, 2 exception
    Helper state = nullptr;                       // 96: (pc) -> result; status 1 trapped, 2 exception
    Helper poll = nullptr;                        // 104: () -> status 2 exception
    const nvm::tier2::Region* region = nullptr;   // 112
    void* owner = nullptr;                        // 120: the executor
};

namespace offsets {
inline constexpr std::uint32_t registers = 0;
inline constexpr std::uint32_t initialized = 8;
inline constexpr std::uint32_t steps = 16;
inline constexpr std::uint32_t max_steps = 24;
inline constexpr std::uint32_t allowance = 32;
inline constexpr std::uint32_t polls = 40;
inline constexpr std::uint32_t quantum = 48;
inline constexpr std::uint32_t done = 56;
inline constexpr std::uint32_t next_block = 64;
inline constexpr std::uint32_t outcome = 68;
inline constexpr std::uint32_t status = 72;
inline constexpr std::uint32_t lift = 80;
inline constexpr std::uint32_t check_reference = 88;
inline constexpr std::uint32_t state = 96;
inline constexpr std::uint32_t poll = 104;
} // namespace offsets

// How the generated code ended: Leave (the region left to `next_block`),
// a deoptimization at `next_block` for one of the interpreter's reasons, or
// an exception a helper caught, which the executor rethrows.
enum class Outcome : std::uint32_t {
    Leave,
    Budget,
    Checks,
    Selectors,
    References,
    StateTraps,
    Exits,
    Exception,
};

// The default lift helper: lift::evaluate on context->region's instruction
// `index`, as the interpreter calls it.
std::uint64_t evaluate_helper(Context* context, std::uint32_t index, std::uint64_t a, std::uint64_t b);

// Executable code, read-only and executable for its whole life (W^X: it is
// written while not executable, then made executable and never writable).
class Program {
public:
    ~Program();
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    void run(Context& context) const { entry_(&context); }
    std::size_t size() const { return size_; }

    // Copies `code` into fresh executable memory, or explains why it could
    // not (mmap or mprotect failed, or the platform has no executable
    // memory for us).
    static std::unique_ptr<Program> load(const std::vector<std::uint8_t>& code, std::string& failure);

private:
    Program() = default;
    void* memory_ = nullptr;
    std::size_t mapped_ = 0;
    std::size_t size_ = 0;
    void (*entry_)(Context*) = nullptr;
};

// Generates the machine code of `region` for `arch`, or explains why not
// (an op the generator does not handle, or code too large for its
// branches). Deterministic: the same region gives the same bytes.
struct Generated {
    std::vector<std::uint8_t> code;
    std::string failure;
    std::uint32_t pinned = 0;  // region registers kept in machine registers
};
Generated generate(const nvm::tier2::Region& region, Arch arch);

// generate() for the host, then load(). The region must outlive the program.
struct Compiled {
    std::unique_ptr<Program> program;
    std::string failure;
};
Compiled compile(const nvm::tier2::Region& region);

// Tests: makes every later Program::load fail as a failed mmap would (the
// probe is not affected once it has run).
void set_load_failure_for_testing(bool fail);

// NEURA_JIT_DUMP=1: each compiled region's code is written, as hex, to
// standard error, or to the file NEURA_JIT_DUMP names when it is not "1".
void dump(const nvm::tier2::Region& region, const std::vector<std::uint8_t>& code);

} // namespace neura::jit
