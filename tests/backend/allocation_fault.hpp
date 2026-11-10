#pragma once

#include <cstddef>

namespace neura::testing {

// Makes the Nth global allocation of the calling thread throw std::bad_alloc.
// The test binary replaces the global allocation functions with versions that
// only look at a thread-local counter, so a scope that is not armed costs one
// branch per allocation and behaves like malloc. Only the allocations of the
// arming thread between construction and destruction are counted. A fault
// fires once and disarms itself, so the code that handles the failure
// allocates normally.
class AllocationFault {
public:
    // nth is 1-based. Zero counts allocations and never fails any.
    explicit AllocationFault(std::size_t nth = 0);
    AllocationFault(const AllocationFault&) = delete;
    AllocationFault& operator=(const AllocationFault&) = delete;
    ~AllocationFault();

    bool fired() const;
    // Allocations made since construction, counting the one that failed.
    std::size_t allocations() const;
    void disarm();
};

} // namespace neura::testing
