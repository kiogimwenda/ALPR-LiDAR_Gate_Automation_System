// main.cpp — gate-dashboard backend entry point (ADR-003: Drogon).
//
// A thin bridge process: REST + WebSocket southbound to the browser,
// one gRPC channel northbound to the gate-server. Phase 4.7.1 boots
// the app with /api/health; the REST API (4.7.2) and the live event
// stream (4.7.3) hang off the same skeleton.
//
//   gate-dashboard --listen 0.0.0.0 --port 8080
//                  --server 127.0.0.1:50051 --site-id site-01

#include <CLI/CLI.hpp>
#include <cstdio>
#include <drogon/drogon.h>
#include <fstream>
#include <memory>
#include <string>

#include "dash_api/auth.hpp"
#include "dash_api/event_stream.hpp"
#include "dash_api/grpc_bridge.hpp"

int main(int argc, char** argv) {
    CLI::App app{"gate-dashboard — dashboard backend (Drogon ↔ gRPC bridge)"};

    std::string listen_host = "0.0.0.0";
    std::uint16_t listen_port = 8080;
    int threads = 2;
    bool cors_dev = false;
    std::string www_dir;
    gate::dash_api::GrpcBridge::Config bridge_cfg;

    app.add_option("--listen", listen_host, "HTTP listen address")->capture_default_str();
    app.add_option("--port", listen_port, "HTTP listen port")->capture_default_str();
    app.add_option("--server", bridge_cfg.server, "gate-server gRPC address")
        ->capture_default_str();
    app.add_option("--site-id", bridge_cfg.site_id, "site identifier")->capture_default_str();
    app.add_option("--threads", threads, "Drogon IO threads")->capture_default_str();
    app.add_flag("--cors-dev", cors_dev,
                 "answer every origin with permissive CORS headers (Vite dev server)");
    app.add_option("--www", www_dir,
                   "serve the built SPA from this directory (production, same-origin)");
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

    auto bridge = std::make_shared<gate::dash_api::GrpcBridge>(bridge_cfg);
    gate::dash_api::set_bridge(bridge);
    auto stream = std::make_shared<gate::dash_api::EventStream>(
        bridge, gate::dash_api::EventStream::Config{});
    gate::dash_api::set_event_stream(stream);
    stream->start();

    if (cors_dev) {
        // Development only: the SvelteKit dev server runs on its own
        // origin. Production serves the built SPA from this process,
        // same-origin, and never sends these headers.
        drogon::app().registerPostHandlingAdvice(
            [](const drogon::HttpRequestPtr&, const drogon::HttpResponsePtr& resp) {
                resp->addHeader("Access-Control-Allow-Origin", "*");
                resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
                resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
            });
    }

    if (!www_dir.empty()) {
        // Serve the SvelteKit build same-origin (ADR-004 SPA mode).
        // Static files resolve from the document root; anything else
        // that isn't /api or /ws falls back to index.html so client-
        // side routes (/allowlist) survive deep links and reloads.
        drogon::app().setDocumentRoot(www_dir);
        drogon::app().setCustomErrorHandler(
            [www_dir](drogon::HttpStatusCode code,
                      const drogon::HttpRequestPtr& req) -> drogon::HttpResponsePtr {
                const auto& path = (req != nullptr) ? req->path() : std::string{};
                if (code == drogon::k404NotFound && req != nullptr && path.rfind("/api", 0) != 0 &&
                    path.rfind("/ws", 0) != 0) {
                    auto resp = drogon::HttpResponse::newFileResponse(www_dir + "/index.html");
                    resp->setStatusCode(drogon::k200OK);
                    return resp;
                }
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(code);
                return resp;
            });
    }

    std::printf("gate-dashboard listening on %s:%u (server=%s, site=%s%s%s)\n", listen_host.c_str(),
                static_cast<unsigned>(listen_port), bridge_cfg.server.c_str(),
                bridge_cfg.site_id.c_str(), cors_dev ? ", cors-dev" : "",
                www_dir.empty() ? "" : ", serving SPA");
    if (auth_cfg.enabled()) {
        std::printf("admin auth: ON (user=%s, ttl=%umin, secret=%s)\n", auth_cfg.admin_user.c_str(),
                    auth_cfg.token_ttl_min, jwt_secret_file.empty() ? "per-process" : "pinned");
    } else {
        std::printf(
            "admin auth: DISABLED — allowlist + gate commands are open; "
            "production wants --admin-password-hash (Phase 4.10.2)\n");
    }

    drogon::app()
        .setLogLevel(trantor::Logger::kWarn)
        .addListener(listen_host, listen_port)
        .setThreadNum(static_cast<std::size_t>(threads))
        .run();

    stream->stop();  // run() returned (SIGINT/SIGTERM) — cancel + join
    return 0;
}
