#include "secret_services.hpp"

#include "provider_internal.hpp"

#include <cerrno>
#include <cstring>
#include <map>

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#else
#include "platform/windows.hpp"

#include <mutex>
#include <werapi.h>
#endif

namespace neura::nvm {

using namespace provider;

namespace {

// Writes zeroes the compiler cannot remove as dead stores.
void clear(void* data, std::size_t size) {
#if defined(__GLIBC__) || defined(__OpenBSD__) || defined(__FreeBSD__)
    ::explicit_bzero(data, size);
#elif defined(_WIN32)
    ::SecureZeroMemory(data, size);
#else
    volatile auto* bytes = static_cast<volatile unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) bytes[i] = 0;
#endif
}

#ifdef _WIN32
// VirtualLock may lock at most the process's minimum working set, less a
// few pages the system keeps, so each buffer raises the minimum and maximum
// by its size while it lives (W20). The working set belongs to the process:
// one lock serializes the changes of every execution.
std::mutex working_set_lock;

// Grows (or, with a negative change, shrinks) the working set limits. A
// failure leaves them as they were and returns the Win32 error.
DWORD change_working_set(std::ptrdiff_t change) {
    const std::lock_guard lock(working_set_lock);
    const HANDLE process = ::GetCurrentProcess();
    SIZE_T minimum = 0;
    SIZE_T maximum = 0;
    DWORD flags = 0;
    if (!::GetProcessWorkingSetSizeEx(process, &minimum, &maximum, &flags)) return ::GetLastError();
    const auto bytes = static_cast<SIZE_T>(change < 0 ? -change : change);
    if (change < 0) {
        minimum = minimum > bytes ? minimum - bytes : minimum;
        maximum = maximum > bytes ? maximum - bytes : maximum;
    } else {
        minimum += bytes;
        maximum += bytes;
    }
    if (!::SetProcessWorkingSetSizeEx(process, minimum, maximum, flags)) return ::GetLastError();
    return ERROR_SUCCESS;
}

// The answer for a failure to raise the working set or to lock: over a
// quota or out of memory is too_large, as RLIMIT_MEMLOCK is on Linux.
HostCompletion lock_failure(DWORD error) {
    switch (error) {
    case ERROR_WORKING_SET_QUOTA:
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_NOT_ENOUGH_QUOTA:
    case ERROR_NO_SYSTEM_RESOURCES:
    case ERROR_COMMITMENT_LIMIT:
    case ERROR_PRIVILEGE_NOT_HELD:
        return fail(host_error::too_large);
    default:
        return fail(host_error::unsupported);
    }
}
#endif

} // namespace

struct SecretServices::State {
    struct Buffer {
        unsigned char* data = nullptr;
        std::size_t size = 0;   // What the program asked for.
        std::size_t mapped = 0; // Whole pages.
    };
    bool granted = false;
    bool recording = false;
    std::map<std::int64_t, Buffer> buffers;
    std::int64_t next = 1; // Handles are never reused, so a stale one is unknown.
    std::size_t locked = 0;

    ~State() { release_all(); }

    void release(Buffer& buffer) {
#ifndef _WIN32
        clear(buffer.data, buffer.mapped);
        ::munlock(buffer.data, buffer.mapped);
        ::munmap(buffer.data, buffer.mapped);
#else
        clear(buffer.data, buffer.mapped);
        ::VirtualUnlock(buffer.data, buffer.mapped);
        ::WerUnregisterExcludedMemoryBlock(buffer.data);
        ::VirtualFree(buffer.data, 0, MEM_RELEASE);
        change_working_set(-static_cast<std::ptrdiff_t>(buffer.mapped));
#endif
        locked -= buffer.mapped;
        buffer.data = nullptr;
    }

    void release_all() {
        for (auto& [handle, buffer] : buffers) release(buffer);
        buffers.clear();
    }

    Buffer* find(const HostValue& handle) {
        const auto found = buffers.find(as_int64(handle));
        return found == buffers.end() ? nullptr : &found->second;
    }

    HostCompletion create(std::int32_t size) {
#if defined(_WIN32)
        if (size < 1 || size > max_buffer_bytes) return fail(host_error::invalid_input);
        if (recording) return fail(host_error::unsupported);
        SYSTEM_INFO system{};
        ::GetSystemInfo(&system);
        const auto page = static_cast<std::size_t>(system.dwPageSize);
        const auto mapped = (static_cast<std::size_t>(size) + page - 1) / page * page;
        if (locked + mapped > max_locked_bytes) return fail(host_error::too_large);
        void* memory = ::VirtualAlloc(nullptr, mapped, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (memory == nullptr) return fail(host_error::too_large);
        // Excluded from Windows Error Reporting's dumps before any secret byte
        // is written, the counterpart of MADV_DONTDUMP; there is no fork, so
        // nothing corresponds to MADV_WIPEONFORK. Then locked, so it is never
        // written to the page file.
        if (FAILED(::WerRegisterExcludedMemoryBlock(memory, static_cast<DWORD>(mapped)))) {
            ::VirtualFree(memory, 0, MEM_RELEASE);
            return fail(host_error::unsupported);
        }
        if (const DWORD error = change_working_set(static_cast<std::ptrdiff_t>(mapped)); error != ERROR_SUCCESS) {
            ::WerUnregisterExcludedMemoryBlock(memory);
            ::VirtualFree(memory, 0, MEM_RELEASE);
            return lock_failure(error);
        }
        if (!::VirtualLock(memory, mapped)) {
            // Never a silent fallback.
            const DWORD error = ::GetLastError();
            ::WerUnregisterExcludedMemoryBlock(memory);
            ::VirtualFree(memory, 0, MEM_RELEASE);
            change_working_set(-static_cast<std::ptrdiff_t>(mapped));
            return lock_failure(error);
        }
        const auto handle = next++;
        buffers[handle] = Buffer{static_cast<unsigned char*>(memory), static_cast<std::size_t>(size), mapped};
        locked += mapped;
        return HostCompletion::ok(int64_value(handle));
#elif !defined(MADV_DONTDUMP) || !defined(MADV_WIPEONFORK)
        // Without both exclusions the promise of these services cannot be
        // kept: macOS and the BSDs answer unsupported.
        (void)size;
        return fail(host_error::unsupported);
#else
        if (size < 1 || size > max_buffer_bytes) return fail(host_error::invalid_input);
        if (recording) return fail(host_error::unsupported);
        const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
        const auto mapped = (static_cast<std::size_t>(size) + page - 1) / page * page;
        if (locked + mapped > max_locked_bytes) return fail(host_error::too_large);
        void* memory = ::mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory == MAP_FAILED) return fail(host_error::too_large);
        // Excluded from core dumps and from a forked child before any secret
        // byte is written; then locked, so it is never written to swap.
        if (::madvise(memory, mapped, MADV_DONTDUMP) != 0 || ::madvise(memory, mapped, MADV_WIPEONFORK) != 0) {
            ::munmap(memory, mapped);
            return fail(host_error::unsupported);
        }
        if (::mlock(memory, mapped) != 0) {
            // ENOMEM and EPERM: over RLIMIT_MEMLOCK. Never a silent fallback.
            const int error = errno;
            ::munmap(memory, mapped);
            return fail(error == ENOMEM || error == EPERM || error == EAGAIN ? host_error::too_large
                                                                             : host_error::unsupported);
        }
        const auto handle = next++;
        buffers[handle] = Buffer{static_cast<unsigned char*>(memory), static_cast<std::size_t>(size), mapped};
        locked += mapped;
        return HostCompletion::ok(int64_value(handle));
#endif
    }
};

SecretServices::SecretServices(bool granted) : state_(std::make_shared<State>()) {
    state_->granted = granted;
    const auto state = state_;
    const auto add = [this](HostSignature signature, FunctionService::Handler handler) {
        services_.push_back(std::make_unique<FunctionService>(std::move(signature), std::move(handler)));
    };
    const auto range = [](const State::Buffer& buffer, std::int64_t offset, std::int64_t count) {
        return offset >= 0 && count >= 0 && static_cast<std::uint64_t>(offset) + static_cast<std::uint64_t>(count) <=
                                                buffer.size;
    };

    add(signature("neura.memory.lock.create", {int32()}, int64()), [state](const HostRequest& request) {
        if (!state->granted) return denied();
        return state->create(as_int32(request.arguments.at(0)));
    });

    add(signature("neura.memory.lock.write", {int64(), int32(), byte_list()}, void_type()),
        [state, range](const HostRequest& request) {
            if (!state->granted) return denied();
            auto* buffer = state->find(request.arguments.at(0));
            const auto offset = as_int32(request.arguments.at(1));
            const auto& bytes = request.arguments.at(2).bytes;
            if (!buffer || !range(*buffer, offset, static_cast<std::int64_t>(bytes.size()))) {
                return fail(host_error::invalid_input);
            }
            if (!bytes.empty()) std::memcpy(buffer->data + offset, bytes.data(), bytes.size());
            return HostCompletion::ok();
        });

    add(signature("neura.memory.lock.read", {int64(), int32(), int32()}, byte_list()),
        [state, range](const HostRequest& request) {
            if (!state->granted) return denied();
            auto* buffer = state->find(request.arguments.at(0));
            const auto offset = as_int32(request.arguments.at(1));
            const auto count = as_int32(request.arguments.at(2));
            if (!buffer || !range(*buffer, offset, count)) return fail(host_error::invalid_input);
            return HostCompletion::ok(bytes_value(std::string(reinterpret_cast<const char*>(buffer->data) + offset,
                                                              static_cast<std::size_t>(count))));
        });

    // Compares every byte of the buffer whatever the input, so the time
    // depends on the buffer's length only.
    add(signature("neura.memory.lock.equal", {int64(), byte_list()}, boolean()), [state](const HostRequest& request) {
        if (!state->granted) return denied();
        auto* buffer = state->find(request.arguments.at(0));
        if (!buffer) return fail(host_error::invalid_input);
        const auto& bytes = request.arguments.at(1).bytes;
        unsigned difference = bytes.size() == buffer->size ? 0u : 1u;
        for (std::size_t i = 0; i < buffer->size; ++i) {
            const auto other = i < bytes.size() ? static_cast<unsigned char>(bytes[i]) : 0u;
            difference |= static_cast<unsigned>(buffer->data[i] ^ other);
        }
        return HostCompletion::ok(scalar(difference == 0 ? 1 : 0));
    });

    add(signature("neura.memory.lock.release", {int64()}, void_type()), [state](const HostRequest& request) {
        if (!state->granted) return denied();
        const auto found = state->buffers.find(as_int64(request.arguments.at(0)));
        if (found == state->buffers.end()) return fail(host_error::invalid_input);
        state->release(found->second);
        state->buffers.erase(found);
        return HostCompletion::ok();
    });
}

SecretServices::~SecretServices() = default;

void SecretServices::register_all(HostRegistry& registry) {
    for (auto& service : services_) registry.add(*service);
}

void SecretServices::set_recording(bool recording) { state_->recording = recording; }
void SecretServices::release_all() { state_->release_all(); }
std::size_t SecretServices::live_buffers() const { return state_->buffers.size(); }
std::size_t SecretServices::locked_bytes() const { return state_->locked; }

} // namespace neura::nvm
