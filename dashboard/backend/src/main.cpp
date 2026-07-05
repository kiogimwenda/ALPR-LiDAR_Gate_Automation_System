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
#include <memory>
#include <string>

#include "dash_api/grpc_bridge.hpp"

int main(int argc, char** argv) {
    CLI::App app{"gate-dashboard — dashboard backend (Drogon ↔ gRPC bridge)"};

    std::string listen_host = "0.0.0.0";
    std::uint16_t listen_port = 8080;
    int threads = 2;
    bool cors_dev = false;
    gate::dash_api::GrpcBridge::Config bridge_cfg;

    app.add_option("--listen", listen_host, "HTTP listen address")->capture_default_str();
    app.add_option("--port", listen_port, "HTTP listen port")->capture_default_str();
    app.add_option("--server", bridge_cfg.server, "gate-server gRPC address")
        ->capture_default_str();
    app.add_option("--site-id", bridge_cfg.site_id, "site identifier")->capture_default_str();
    app.add_option("--threads", threads, "Drogon IO threads")->capture_default_str();
    app.add_flag("--cors-dev", cors_dev,
                 "answer every origin with permissive CORS headers (Vite dev server)");

    CLI11_PARSE(app, argc, argv);

    gate::dash_api::set_bridge(std::make_shared<gate::dash_api::GrpcBridge>(bridge_cfg));

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

    std::printf("gate-dashboard listening on %s:%u (server=%s, site=%s%s)\n", listen_host.c_str(),
                static_cast<unsigned>(listen_port), bridge_cfg.server.c_str(),
                bridge_cfg.site_id.c_str(), cors_dev ? ", cors-dev" : "");

    drogon::app()
        .setLogLevel(trantor::Logger::kWarn)
        .addListener(listen_host, listen_port)
        .setThreadNum(static_cast<std::size_t>(threads))
        .run();
    return 0;
}
