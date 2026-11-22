#pragma once

// A write to a socket whose peer has gone raises SIGPIPE unless the call
// can say otherwise (MSG_NOSIGNAL, SO_NOSIGPIPE). OpenSSL's socket BIO and
// sendfile(2) cannot, and `neura run` and native executables keep the
// default action, which ends the process. SocketWrites blocks SIGPIPE on the
// calling thread for its lifetime, then consumes only a signal those writes
// raised: one that was already pending stays pending. The write itself still
// fails with EPIPE, and errno is kept for the caller.

#if !defined(_WIN32)
#include <sys/socket.h>
#endif
#if !defined(_WIN32) && !defined(SO_NOSIGPIPE)
#include <cerrno>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#endif

namespace neura::nvm::provider {

class SocketWrites {
public:
#if !defined(_WIN32) && !defined(SO_NOSIGPIPE)
    SocketWrites() {
        sigemptyset(&blocked_);
        sigaddset(&blocked_, SIGPIPE);
        changed_ = pthread_sigmask(SIG_BLOCK, &blocked_, &saved_) == 0;
        sigset_t pending;
        pending_ = sigpending(&pending) != 0 || sigismember(&pending, SIGPIPE) == 1;
    }

    ~SocketWrites() {
        if (!changed_) return;
        // The caller reads the write's errno after this scope ends.
        const int error = errno;
        if (!pending_) {
            timespec zero{};
            while (sigtimedwait(&blocked_, nullptr, &zero) < 0 && errno == EINTR) {}
        }
        pthread_sigmask(SIG_SETMASK, &saved_, nullptr);
        errno = error;
    }

private:
    sigset_t blocked_{};
    sigset_t saved_{};
    bool changed_ = false;
    bool pending_ = false;
#else
    SocketWrites() = default;
#endif
    SocketWrites(const SocketWrites&) = delete;
    SocketWrites& operator=(const SocketWrites&) = delete;
};

} // namespace neura::nvm::provider
