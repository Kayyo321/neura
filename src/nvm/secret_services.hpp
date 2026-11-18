#pragma once

// neura.memory.lock.*: secret bytes held by the host in locked memory,
// addressed by handle (docs/runtime/NVM.md, "Secret memory"). The NVM cannot
// lock, pin or promise to clear its own heap: the collector moves and copies
// storage. These services give a program a place outside that heap: page
// memory the host maps, locks with mlock(2), excludes from core dumps
// (MADV_DONTDUMP) and from forked children (MADV_WIPEONFORK), and zeroes
// with explicit_bzero before it unmaps it, on release and at the end of the
// execution. Only the buffer has these properties: bytes a program writes
// into it, or reads back, pass through the NVM heap as ordinary copies.
//
// neura.memory.lock.create(int32 size) -> int64 handle
// neura.memory.lock.write(int64 handle, int32 offset, uint8[list] bytes) -> void
// neura.memory.lock.read(int64 handle, int32 offset, int32 count) -> uint8[list]
// neura.memory.lock.equal(int64 handle, uint8[list] bytes) -> bool
// neura.memory.lock.release(int64 handle) -> void
//
// All version 1 with a uint32 error, in the entropy group (14 without it).
// A size outside 1 to 65,536, a range outside the buffer, or a handle that
// was never created or was released answers invalid_input (4). Locked bytes
// beyond 1 MiB per execution, or beyond what RLIMIT_MEMLOCK allows, answer
// too_large (16). A platform without mlock, MADV_DONTDUMP and
// MADV_WIPEONFORK (every one but Linux), and a run that records a trace,
// answer unsupported (13): a trace would hold the secret.

#include "host.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace neura::nvm {

class SecretServices {
public:
    static constexpr std::int32_t max_buffer_bytes = 64 * 1024;
    static constexpr std::size_t max_locked_bytes = 1024 * 1024;

    explicit SecretServices(bool granted);
    ~SecretServices();
    SecretServices(const SecretServices&) = delete;
    SecretServices& operator=(const SecretServices&) = delete;

    void register_all(HostRegistry& registry);
    // A recorded run cannot have secret buffers: create answers unsupported.
    void set_recording(bool recording);
    // Zeroes and frees every buffer, as the end of an execution does.
    void release_all();
    std::size_t live_buffers() const;
    std::size_t locked_bytes() const;

    struct State;

private:
    std::vector<std::unique_ptr<HostService>> services_;
    std::shared_ptr<State> state_;
};

} // namespace neura::nvm
