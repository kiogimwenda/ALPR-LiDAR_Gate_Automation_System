// main.cpp — gate-server daemon entry point.
//
// Composition root: parses CLI args, opens the allowlist database,
// constructs the fusion engine and event broadcaster, registers the
// gRPC services, listens for SIGINT/SIGTERM, and drains cleanly on
// shutdown.

#include <CLI/CLI.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>

#include "auth/allowlist_store.hpp"
#include "dash/event_broadcaster.hpp"
#include "fusion/fusion_engine.hpp"
#include "rpc/server.hpp"

namespace {

// The handler runs in a true signal context — only async-signal-safe
// operations are permitted. Setting a sig_atomic_t is the canonical
// safe operation; we poll it from main() at a 100ms cadence.
volatile std::sig_atomic_t g_shutdown_requested = 0;

void handle_signal(int /*sig*/) {
    g_shutdown_requested = 1;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"gate-automation server: gRPC + ALPR + LiDAR fusion daemon"};

    std::string listen_address = "0.0.0.0:50051";
    std::string site_id = "default";
    std::string db_path = "allowlist.db";
    std::string log_level = "info";
    std::uint32_t shutdown_deadline_sec = 5;

    app.add_option("--listen", listen_address,
                   "gRPC listen address (host:port). Use 0.0.0.0:50051 in production.")
        ->default_str(listen_address);
    app.add_option("--site-id", site_id,
                   "Default site identifier stamped on dashboard events that lack one.")
        ->default_str(site_id);
    app.add_option("--db-path", db_path,
                   "SQLite database path for the allowlist + blocklist store.")
        ->default_str(db_path);
    app.add_option("--log-level", log_level,
                   "spdlog level: trace, debug, info, warn, error, critical, off.")
        ->default_str(log_level);
    app.add_option("--shutdown-deadline", shutdown_deadline_sec,
                   "Seconds to wait for in-flight RPCs during graceful shutdown.")
        ->default_str(std::to_string(shutdown_deadline_sec));

    CLI11_PARSE(app, argc, argv);

    spdlog::set_level(spdlog::level::from_str(log_level));
    spdlog::info("gate-server starting (listen={}, site_id={}, db={})", listen_address, site_id,
                 db_path);

    gate::auth::AllowlistStore store = gate::auth::AllowlistStore::open(db_path);
    gate::fusion::FusionEngine fusion{store};
    gate::dash::EventBroadcaster bus;

    gate::rpc::ServerConfig cfg;
    cfg.listen_address = listen_address;
    cfg.default_site_id = site_id;
    gate::rpc::Server server{store, fusion, bus, cfg};

    if (!server.start()) {
        spdlog::error("Failed to start gRPC server on {}", listen_address);
        return 1;
    }
    spdlog::info("gRPC server listening on {}", server.bound_address());

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    while (g_shutdown_requested == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    spdlog::info("Shutdown requested — draining...");
    bus.stop();  // Wakes every Subscribe RPC so they return cleanly.
    server.shutdown(std::chrono::seconds{shutdown_deadline_sec});
    spdlog::info("gate-server stopped.");
    return 0;
}
