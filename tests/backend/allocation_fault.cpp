#include "allocation_fault.hpp"

#include <cstdlib>
#include <new>

namespace {

struct FaultState {
    bool armed = false;
    bool fired = false;
    std::size_t countdown = 0;
    std::size_t count = 0;
};

thread_local FaultState state;

} // namespace

namespace neura::testing {

AllocationFault::AllocationFault(std::size_t nth) {
    state = {true, false, nth, 0};
}

AllocationFault::~AllocationFault() { state.armed = false; }
bool AllocationFault::fired() const { return state.fired; }
std::size_t AllocationFault::allocations() const { return state.count; }
void AllocationFault::disarm() { state.armed = false; }

} // namespace neura::testing

// Replaces the plain, array, nothrow, and sized forms, each through the plain
// pair below. The C++ library's own array and nothrow forms forward to the
// plain ones, but a sanitizer runtime supplies every form itself, and memory
// from its nothrow `new` (which std::stable_sort uses) must not reach the
// `free` in this file's `delete`. Aligned allocations keep the default
// implementation, whose `new` and `delete` match each other.
void* operator new(std::size_t size) {
    if (state.armed) {
        ++state.count;
        if (state.countdown != 0 && --state.countdown == 0) {
            state.armed = false;
            state.fired = true;
            throw std::bad_alloc();
        }
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return operator new(size);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](std::size_t size) { return operator new(size); }
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept { return operator new(size, tag); }

void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
