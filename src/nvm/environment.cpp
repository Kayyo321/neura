#include "environment.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace neura::nvm {

namespace {

std::atomic<bool> held{false};

} // namespace

void hold_environment() { held.store(true, std::memory_order_seq_cst); }

void bind_environment() { held.store(false, std::memory_order_seq_cst); }

bool environment_bound() { return !held.load(std::memory_order_seq_cst); }

void require_bound_environment(const char* variable) {
    if (!held.load(std::memory_order_seq_cst)) return;
    std::fprintf(stderr, "neura: internal error: %s was read before the launch's environment was bound\n", variable);
    std::abort();
}

} // namespace neura::nvm
