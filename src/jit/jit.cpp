#include "jit.hpp"

#include "assembler.hpp"

#include "lift/evaluate.hpp"
#include "nvm/tier2.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

namespace neura::jit {

bool parse_mode(const std::string& text, Mode& mode) {
    if (text == "off") {
        mode = Mode::Off;
    } else if (text == "on") {
        mode = Mode::On;
    } else if (text == "auto") {
        mode = Mode::Auto;
    } else {
        return false;
    }
    return true;
}

const char* mode_name(Mode mode) {
    switch (mode) {
    case Mode::Off: return "off";
    case Mode::On: return "on";
    case Mode::Auto: return "auto";
    }
    return "auto";
}

Mode environment_mode() {
    static const Mode mode = [] {
        Mode parsed = Mode::Auto;
        const char* text = std::getenv("NEURA_JIT");
        if (text && !parse_mode(text, parsed)) parsed = Mode::Auto;
        return parsed;
    }();
    return mode;
}

Mode combine(Mode run, Mode environment) {
    if (run == Mode::Off || environment == Mode::Off) return Mode::Off;
    if (run == Mode::On || environment == Mode::On) return Mode::On;
    return Mode::Auto;
}

bool host_has_backend() {
#if defined(__x86_64__) && (defined(__linux__) || defined(__APPLE__))
    return true;
#elif defined(__aarch64__) && (defined(__linux__) || defined(__APPLE__))
    return true;
#else
    return false;
#endif
}

Arch host_arch() {
#if defined(__aarch64__)
    return Arch::AArch64;
#else
    return Arch::X86_64;
#endif
}

namespace {

// Where `auto` turns the generator on: only where its code has been run
// and checked (docs/runtime/NVM.md, "Native compilation"). Elsewhere `on`
// enables it.
const char* not_default_reason() {
#if defined(__x86_64__) && defined(__linux__)
    return nullptr;
#elif defined(__aarch64__) && defined(__linux__)
    return "not enabled by default on aarch64 Linux (use --jit=on)";
#elif defined(__aarch64__) && defined(__APPLE__)
    return "not enabled by default on aarch64 macOS (use --jit=on)";
#elif defined(__x86_64__) && defined(__APPLE__)
    return "not enabled by default on x86-64 macOS (use --jit=on)";
#else
    return "no code generator for this platform";
#endif
}

std::atomic<bool> fail_loads{false};

std::size_t page_size() {
#if defined(__linux__) || defined(__APPLE__)
    const auto size = sysconf(_SC_PAGESIZE);
    return size > 0 ? static_cast<std::size_t>(size) : 4096;
#else
    return 4096;
#endif
}

// A program that only records that it ran: it checks executable memory and
// the calling convention of the generated code once per process.
struct Probe {
    bool ok = false;
    std::string failure;
};

Probe probe() {
    Probe result;
    if (!host_has_backend()) {
        result.failure = "no code generator for this platform";
        return result;
    }
    std::vector<std::uint8_t> code;
    try {
        auto masm = host_arch() == Arch::X86_64 ? make_x86_64() : make_aarch64();
        masm->pin({0});
        masm->prologue();
        masm->move_immediate(Scratch::T0, 0x5A5A5A5A5A5AULL);
        masm->store(0, Scratch::T0);
        masm->add_done(3);
        masm->store_context32(offsets::outcome, 0x5A5AU);
        masm->epilogue();
        code = masm->finish();
    } catch (const GenerationError& error) {
        result.failure = error.what();
        return result;
    }
    auto program = Program::load(code, result.failure);
    if (!program) return result;
    std::uint64_t registers[1] = {7};
    std::uint8_t initialized[1] = {0};
    Context context;
    context.registers = registers;
    context.initialized = initialized;
    program->run(context);
    if (context.outcome != 0x5A5AU || context.done != 3 || registers[0] != 0x5A5A5A5A5A5AULL) {
        result.failure = "the probe program did not run as generated";
        return result;
    }
    result.ok = true;
    return result;
}

const Probe& process_probe() {
    static const Probe result = probe();
    return result;
}

} // namespace

Availability availability(Mode mode) {
    Availability result;
    if (mode == Mode::Off) {
        result.reason = "off";
        return result;
    }
    if (mode == Mode::Auto) {
        if (const auto* reason = not_default_reason()) {
            result.reason = reason;
            return result;
        }
        const auto& checked = process_probe();
        if (!checked.ok) {
            result.reason = "no executable memory: " + checked.failure;
            return result;
        }
        result.enabled = true;
        return result;
    }
    if (!host_has_backend()) {
        result.error = "--jit=on: there is no native code generator for this platform";
        return result;
    }
    const auto& checked = process_probe();
    if (!checked.ok) {
        result.error = "--jit=on: native code cannot run here: " + checked.failure;
        return result;
    }
    result.enabled = true;
    return result;
}

void set_load_failure_for_testing(bool fail) { fail_loads.store(fail); }

Program::~Program() {
#if defined(__linux__) || defined(__APPLE__)
    if (memory_) munmap(memory_, mapped_);
#endif
}

std::unique_ptr<Program> Program::load(const std::vector<std::uint8_t>& code, std::string& failure) {
    if (fail_loads.load()) {
        failure = "mmap failed (simulated)";
        return nullptr;
    }
#if defined(__linux__) || defined(__APPLE__)
    if (code.empty()) {
        failure = "no code";
        return nullptr;
    }
    const auto page = page_size();
    const auto mapped = (code.size() + page - 1) / page * page;
#if defined(__APPLE__) && defined(__aarch64__)
    // Apple silicon: a MAP_JIT mapping, writable or executable per thread,
    // never both at once for this thread (pthread_jit_write_protect_np).
    void* memory = mmap(nullptr, mapped, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (memory == MAP_FAILED) {
        failure = std::string("mmap failed: ") + std::strerror(errno);
        return nullptr;
    }
    pthread_jit_write_protect_np(0);
    std::memcpy(memory, code.data(), code.size());
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(memory, code.size());
#else
    // Written while read-write, then read-execute for good.
    void* memory = mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) {
        failure = std::string("mmap failed: ") + std::strerror(errno);
        return nullptr;
    }
    std::memcpy(memory, code.data(), code.size());
#if defined(__aarch64__)
    __builtin___clear_cache(static_cast<char*>(memory), static_cast<char*>(memory) + code.size());
#endif
    if (mprotect(memory, mapped, PROT_READ | PROT_EXEC) != 0) {
        failure = std::string("mprotect failed: ") + std::strerror(errno);
        munmap(memory, mapped);
        return nullptr;
    }
#endif
    std::unique_ptr<Program> program(new Program());
    program->memory_ = memory;
    program->mapped_ = mapped;
    program->size_ = code.size();
    program->entry_ = reinterpret_cast<void (*)(Context*)>(memory);
    return program;
#else
    (void)code;
    failure = "no executable memory on this platform";
    return nullptr;
#endif
}

std::uint64_t evaluate_helper(Context* context, std::uint32_t index, std::uint64_t a, std::uint64_t b) {
    bool failed = false;
    const auto value = lift::evaluate(context->region->instructions[index], a, b, failed);
    context->status = failed ? 1 : 0;
    return value;
}

void dump(const nvm::tier2::Region& region, const std::vector<std::uint8_t>& code) {
    const char* target = std::getenv("NEURA_JIT_DUMP");
    if (!target || !*target) return;
    static std::mutex mutex;
    const std::lock_guard lock(mutex);
    std::FILE* out = std::strcmp(target, "1") == 0 ? stderr : std::fopen(target, "a");
    if (!out) return;
    std::fprintf(out, "jit: region function %u entry %u: %zu bytes\n", region.function, region.entry, code.size());
    for (std::size_t i = 0; i < code.size(); ++i) {
        std::fprintf(out, "%02x%s", code[i], (i % 32 == 31 || i + 1 == code.size()) ? "\n" : " ");
    }
    if (out != stderr) std::fclose(out);
}

Compiled compile(const nvm::tier2::Region& region) {
    Compiled result;
    if (!host_has_backend()) {
        result.failure = "no code generator for this platform";
        return result;
    }
    auto generated = generate(region, host_arch());
    if (!generated.failure.empty()) {
        result.failure = generated.failure;
        return result;
    }
    dump(region, generated.code);
    result.program = Program::load(generated.code, result.failure);
    return result;
}

} // namespace neura::jit
