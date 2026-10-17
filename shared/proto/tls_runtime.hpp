// tls_runtime.hpp — process-level OpenSSL setup for the gRPC binaries.
//
// By default OpenSSL registers an atexit() handler that tears down its
// global state. gRPC shuts its event-engine threads down asynchronously,
// so when main() returns those threads can still be freeing their
// OpenSSL thread-local state while the atexit handler frees the same
// list. The result is an intermittent "double free or corruption" or
// SIGSEGV at exit (seen in gate-sim and gate-vision under the TLS e2e).
// The OS reclaims everything at exit anyway, so skip the cleanup.
//
// Call it first thing in main(). It must be OpenSSL's first
// initialisation: once the atexit handler is registered it stays.

#pragma once

#include <openssl/crypto.h>

namespace gate::proto {

inline void init_tls_runtime() noexcept {
    OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, nullptr);
}

}  // namespace gate::proto
