// server.hpp — gRPC server lifecycle and composition root for the RPC layer.
//
// Owns the grpc::Server, the address it listens on, and the registered
// service implementations. Constructed from non-owning references to the
// AllowlistStore, FusionEngine, and EventBroadcaster, so composition is
// the caller's job (typically main.cpp in 4.3.5).
//
// Threading: start() blocks while building, then returns; the gRPC server
// runs on its own thread pool. wait() blocks the calling thread until
// shutdown(). shutdown() is safe to call from any thread, including a
// signal handler (it forwards to grpc::Server::Shutdown which is itself
// async-signal-safe-ish; production main.cpp will route SIGTERM through
// a dedicated thread).

#pragma once

#include <grpcpp/server.h>
#include <memory>
#include <string>

#include "auth/allowlist_store.hpp"
#include "dash/event_broadcaster.hpp"
#include "fusion/fusion_engine.hpp"
#include "rpc/admin_service.hpp"
#include "rpc/dashboard_service.hpp"
#include "rpc/field_controller_service.hpp"

namespace gate::rpc {

// Transport security (Phase 4.10.1). Setting cert+key enables TLS;
// adding a CA verifies client certificates, and require_client_cert
// turns that into mTLS (unverified clients are rejected at handshake).
// All paths are PEM files. Empty cert path = plaintext listener —
// legal for dev/tests, loudly logged by the daemon.
struct TlsConfig {
    std::string cert_path;  // server certificate chain
    std::string key_path;   // server private key
    std::string ca_path;    // CA for verifying client certs (mTLS)
    bool require_client_cert = false;

    [[nodiscard]] bool enabled() const { return !cert_path.empty(); }
};

struct ServerConfig {
    std::string listen_address = "0.0.0.0:50051";
    // Default site_id stamped on dashboard-published events when the RPC
    // doesn't carry one explicitly. Multi-tenant deployments override this.
    std::string default_site_id = "default";
    // Per-Subscribe poll interval (controls how often each Subscribe RPC
    // checks for cancellation). 500ms is a reasonable production default.
    std::chrono::milliseconds subscribe_poll_interval{500};
    TlsConfig tls{};
};

class Server {
public:
    Server(gate::auth::AllowlistStore& store, gate::fusion::FusionEngine& fusion,
           gate::dash::EventBroadcaster& bus, ServerConfig cfg = {});
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Build and start the gRPC server. Returns false if binding the
    // listen address failed (e.g. port already in use).
    bool start();

    // Block the calling thread until shutdown() is called from another
    // thread. Safe to call only once.
    void wait();

    // Initiate graceful shutdown: stop accepting new RPCs, drain the
    // event broadcaster, give in-flight RPCs `deadline` to finish.
    void shutdown(std::chrono::milliseconds deadline = std::chrono::seconds{2});

    // The actual address the server is listening on (with the resolved
    // port if `:0` was requested). Empty until start() succeeds.
    std::string bound_address() const;

private:
    ServerConfig cfg_;
    AdminServiceImpl admin_;
    DashboardServiceImpl dashboard_;
    FieldControllerServiceImpl field_;
    std::unique_ptr<grpc::Server> server_;
    std::string bound_address_;
};

}  // namespace gate::rpc
