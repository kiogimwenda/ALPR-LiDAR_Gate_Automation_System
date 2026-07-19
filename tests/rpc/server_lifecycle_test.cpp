// server_lifecycle_test.cpp — gRPC Server start / bind / shutdown.
//
// Spins up a real gRPC server on an ephemeral port (`:0`), checks the
// resolved bound address, then shuts it down cleanly. End-to-end RPC
// traffic over the channel lives in 4.3.6 integration tests.

#include <catch2/catch_test_macros.hpp>

#include "rpc/server.hpp"

using gate::auth::AllowlistStore;
using gate::dash::EventBroadcaster;
using gate::fusion::FusionEngine;
using gate::rpc::Server;
using gate::rpc::ServerConfig;

TEST_CASE("rpc::Server: starts on ephemeral port and reports bound address", "[rpc][server]") {
    AllowlistStore store = AllowlistStore::open(":memory:");
    FusionEngine fusion{store};
    EventBroadcaster bus;

    ServerConfig cfg;
    cfg.listen_address = "127.0.0.1:0";  // ephemeral
    cfg.subscribe_poll_interval = std::chrono::milliseconds{50};
    Server server{store, fusion, bus, cfg};
    REQUIRE(server.start());
    const auto bound = server.bound_address();
    REQUIRE(bound.starts_with("127.0.0.1:"));
    REQUIRE(bound != "127.0.0.1:0");  // The :0 was resolved to a real port.

    server.shutdown();
}

TEST_CASE("rpc::Server: start() fails on an unbindable address", "[rpc][server][error]") {
    AllowlistStore store = AllowlistStore::open(":memory:");
    FusionEngine fusion{store};
    EventBroadcaster bus;

    // Port 1 requires root; binding fails unprivileged.
    ServerConfig cfg;
    cfg.listen_address = "127.0.0.1:1";
    Server server{store, fusion, bus, cfg};
    REQUIRE_FALSE(server.start());
}
