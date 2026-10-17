// main.cpp — gate-dashboard backend entry point (ADR-003: Drogon).
//
// A thin bridge process: REST + WebSocket southbound to the Gate Control
// app (app/, ADR-013), one gRPC channel northbound to the gate-server. Phase 4.7.1 boots
// the app with /api/health; the REST API (4.7.2) and the live event
// stream (4.7.3) hang off the same skeleton.
//
//   gate-dashboard --listen 0.0.0.0 --port 8080
//                  --server 127.0.0.1:50051 --site-id site-01

#include <CLI/CLI.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <drogon/drogon.h>
#include <fstream>
#include <memory>
#include <string>

#include "dash_api/auth.hpp"
#include "dash_api/event_stream.hpp"
#include "dash_api/grpc_bridge.hpp"
#include "dash_api/history_store.hpp"

int main(int argc, char** argv) {
    CLI::App app{"gate-dashboard — dashboard backend (Drogon ↔ gRPC bridge)"};

    std::string listen_host = "0.0.0.0";
    std::uint16_t listen_port = 8080;
    int threads = 2;
    gate::dash_api::GrpcBridge::Config bridge_cfg;

    app.add_option("--listen", listen_host, "HTTP listen address")->capture_default_str();
    app.add_option("--port", listen_port, "HTTP listen port")->capture_default_str();
    app.add_option("--server", bridge_cfg.server, "gate-server gRPC address")
        ->capture_default_str();
    app.add_option("--site-id", bridge_cfg.site_id, "site identifier")->capture_default_str();
    app.add_option("--threads", threads, "Drogon IO threads")->capture_default_str();
    app.add_option("--tls-ca", bridge_cfg.tls_ca_path,
                   "CA bundle (PEM) — switches the upstream gRPC channel to TLS");
    app.add_option("--tls-cert", bridge_cfg.tls_cert_path,
                   "client certificate (PEM) presented to an mTLS server")
        ->needs(app.get_option("--tls-ca"));
    app.add_option("--tls-key", bridge_cfg.tls_key_path, "client private key (PEM)")
        ->needs(app.get_option("--tls-cert"));

    gate::dash_api::AuthService::Config auth_cfg;
    std::string jwt_secret_file;
    app.add_option("--admin-user", auth_cfg.admin_user, "admin login username")
        ->capture_default_str();
    app.add_option("--admin-password-hash", auth_cfg.password_hash,
                   "pbkdf2-sha256 admin credential (scripts/gen-admin-hash.sh); "
                   "setting it turns admin auth ON");
    app.add_option("--jwt-secret-file", jwt_secret_file,
                   "file holding the JWT signing secret — pins sessions across "
                   "restarts (default: random per-process secret)")
        ->needs(app.get_option("--admin-password-hash"));
    app.add_option("--token-ttl-min", auth_cfg.token_ttl_min, "admin session lifetime, minutes")
        ->capture_default_str();

    std::string history_db = "dashboard-history.db";
    app.add_option("--history-db", history_db,
                   "SQLite file for decision history, alerts, users and gate configuration")
        ->capture_default_str();

    CLI11_PARSE(app, argc, argv);

    if (!jwt_secret_file.empty()) {
        std::ifstream in(jwt_secret_file);
        std::getline(in, auth_cfg.jwt_secret);
        if (auth_cfg.jwt_secret.empty()) {
            std::fprintf(stderr, "gate-dashboard: cannot read a JWT secret from %s\n",
                         jwt_secret_file.c_str());
            return 1;  // asked-for auth never silently degrades
        }
    }
    gate::dash_api::set_auth(std::make_shared<gate::dash_api::AuthService>(auth_cfg));

    if (history_db.empty()) {
        // An env file from before HISTORY_DB existed expands to "": use
        // systemd's StateDirectory (/var/lib/gate) when running as a unit.
        const char* state = std::getenv("STATE_DIRECTORY");
        history_db = std::string(state != nullptr ? state : ".") + "/dashboard-history.db";
    }
    std::shared_ptr<gate::dash_api::HistoryStore> history;
    try {
        history = gate::dash_api::HistoryStore::open(history_db);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "gate-dashboard: %s\n", e.what());
        return 1;
    }
    // First start with a configured admin credential: seed the user
    // table with it, so a site upgrading from the single-admin backend
    // signs in exactly as before and can then add operators.
    if (auth_cfg.enabled() && history->user_count() == 0) {
        gate::dash_api::UserRecord admin;
        admin.username = auth_cfg.admin_user;
        admin.full_name = "Administrator";
        admin.role = gate::dash_api::Role::kAdmin;
        admin.password_hash = auth_cfg.password_hash;
        std::string error;
        if (!history->save_user(admin, error)) {
            std::fprintf(stderr, "gate-dashboard: cannot seed admin user: %s\n", error.c_str());
            return 1;
        }
    }
    gate::dash_api::set_history(history);

    auto bridge = std::make_shared<gate::dash_api::GrpcBridge>(bridge_cfg);
    gate::dash_api::set_bridge(bridge);
    auto stream = std::make_shared<gate::dash_api::EventStream>(
        bridge, gate::dash_api::EventStream::Config{});
    gate::dash_api::set_event_stream(stream);
    stream->cache().add_listener([history](const gate::v1::DashboardEvent& ev) {
        try {
            history->ingest(ev);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "gate-dashboard: history ingest failed: %s\n", e.what());
        }
    });
    stream->start();

    std::printf("gate-dashboard listening on %s:%u (server=%s, site=%s)\n", listen_host.c_str(),
                static_cast<unsigned>(listen_port), bridge_cfg.server.c_str(),
                bridge_cfg.site_id.c_str());
    if (history->user_count() > 0) {
        std::printf("auth: ON (%zu user accounts in %s, ttl=%umin)\n", history->user_count(),
                    history_db.c_str(), auth_cfg.token_ttl_min);
    } else if (auth_cfg.enabled()) {
        std::printf("admin auth: ON (user=%s, ttl=%umin, secret=%s)\n", auth_cfg.admin_user.c_str(),
                    auth_cfg.token_ttl_min, jwt_secret_file.empty() ? "per-process" : "pinned");
    } else {
        std::printf(
            "admin auth: DISABLED — allowlist + gate commands are open; "
            "production wants --admin-password-hash (Phase 4.10.2)\n");
    }

    // Retention (Phase 1 decision 9: 90 days by default, set in the app):
    // purge hourly, and once at startup.
    const auto purge = [history] {
        try {
            const auto days =
                static_cast<std::uint32_t>(std::stoul(history->setting("retention_days", "90")));
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
            history->purge(days, now);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "gate-dashboard: retention purge failed: %s\n", e.what());
        }
    };
    purge();
    drogon::app().getLoop()->runEvery(3600.0, purge);

    drogon::app()
        .setLogLevel(trantor::Logger::kWarn)
        .addListener(listen_host, listen_port)
        .setThreadNum(static_cast<std::size_t>(threads))
        .run();

    stream->stop();  // run() returned (SIGINT/SIGTERM) — cancel + join
    return 0;
}
