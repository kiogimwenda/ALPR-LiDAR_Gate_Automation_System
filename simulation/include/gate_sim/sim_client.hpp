// sim_client.hpp — the virtual gate's Control-stream session (grpc++).
//
// Where the firmware speaks gRPC through nanopb + nghttp2 (ADR-011),
// the simulator runs on a host with vcpkg and uses the full grpc++
// stack and the same generated stubs as the server — so a gate-sim ↔
// gate-server session exercises the identical wire contract from the
// opposite side of the fence the firmware uses. Command semantics
// mirror ControlClient: double acks, tracker-driven completion for
// motion commands, honest failures for kinds a gate (real or
// simulated) cannot perform.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "gate_sim/virtual_gate.hpp"

namespace gate::sim {

class SimClient {
public:
    struct Config {
        std::string server = "127.0.0.1:50051";
        std::string gate_id = "gate-sim-01";
        std::string fw_version = "sim-0.1.0";
        std::uint32_t telemetry_period_ms = 1'000;
        std::uint32_t tick_ms = 50;  // physics + write cadence
        std::uint32_t connect_timeout_ms = 3'000;
    };

    SimClient(Config cfg, VirtualGate& gate) : cfg_(std::move(cfg)), gate_(gate) {}

    // Run one connect → stream → pump session. Blocks until the stream
    // ends or `shutdown` goes true. Returns true if the channel ever
    // reached READY (used by the caller's backoff policy).
    bool run_session(const std::atomic<bool>& shutdown);

private:
    Config cfg_;
    VirtualGate& gate_;
    std::uint64_t telemetry_seq_ = 0;
};

}  // namespace gate::sim
