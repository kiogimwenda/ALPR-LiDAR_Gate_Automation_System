// main.cpp — gate-sim: a virtual gate field controller.
//
// Runs the firmware's pure core (state machine + command tracker)
// under simulated physics and connects to a real gate-server over
// real gRPC, so the server, dashboard, and fusion pipeline can be
// developed and demoed against a live-feeling gate with no hardware
// on the desk. See simulation/README notes in the root README's
// Phase 4.6 section.
//
//   gate-sim --server 192.168.1.10:50051 --gate-id gate-sim-01
//            --travel-ms 12000 --auto-close-ms 8000 --interactive
//
// Interactive commands (with --interactive): open, close, stop, trip,
// clear, fault-clear, reboot, status, quit.

#include <CLI/CLI.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#include "gate_sim/sim_client.hpp"
#include "gate_sim/virtual_gate.hpp"

namespace {

std::atomic<bool> g_shutdown{false};

void handle_signal(int) {
    g_shutdown.store(true);
}

void interactive_loop(gate::sim::VirtualGate& gate) {
    std::string line;
    while (!g_shutdown.load() && std::getline(std::cin, line)) {
        if (line == "open") {
            gate.local_open();
        } else if (line == "close") {
            gate.local_close();
        } else if (line == "stop") {
            gate.operator_stop();
        } else if (line == "trip") {
            gate.trip_beam();
        } else if (line == "clear") {
            gate.clear_beam();
        } else if (line == "fault-clear") {
            gate.clear_fault();
        } else if (line == "reboot") {
            gate.reboot();
        } else if (line == "status") {
            std::printf("[sim] %s\n", gate.status_line().c_str());
            continue;
        } else if (line == "quit") {
            g_shutdown.store(true);
            break;
        } else if (!line.empty()) {
            std::printf(
                "[sim] ? commands: open close stop trip clear fault-clear reboot status quit\n");
            continue;
        }
        std::printf("[sim] %s\n", gate.status_line().c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"gate-sim — virtual gate field controller (Phase 4.6)"};

    gate::sim::SimClient::Config client_cfg;
    gate::sim::VirtualGate::Config gate_cfg;
    bool interactive = false;

    app.add_option("--server", client_cfg.server, "gate-server gRPC address")
        ->capture_default_str();
    app.add_option("--gate-id", client_cfg.gate_id, "gate identifier in telemetry")
        ->capture_default_str();
    app.add_option("--travel-ms", gate_cfg.travel_ms, "full-stroke travel time")
        ->capture_default_str();
    app.add_option("--motor-timeout-ms", gate_cfg.motor_timeout_ms, "motor watchdog budget")
        ->capture_default_str();
    app.add_option("--auto-close-ms", gate_cfg.auto_close_ms, "auto-close dwell (0 = off)")
        ->capture_default_str();
    app.add_flag("!--no-reverse-on-beam", gate_cfg.reverse_on_beam,
                 "disable the reverse-on-beam policy");
    app.add_option("--telemetry-ms", client_cfg.telemetry_period_ms, "telemetry period")
        ->capture_default_str();
    app.add_flag("-i,--interactive", interactive,
                 "read operator commands from stdin (open/close/stop/trip/…)");
    app.add_option("--tls-ca", client_cfg.tls_ca_path,
                   "CA bundle (PEM) — switches the Control channel to TLS");
    app.add_option("--tls-cert", client_cfg.tls_cert_path,
                   "client certificate (PEM) for mTLS servers")
        ->needs(app.get_option("--tls-ca"));
    app.add_option("--tls-key", client_cfg.tls_key_path, "client private key (PEM)")
        ->needs(app.get_option("--tls-cert"));

    CLI11_PARSE(app, argc, argv);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    gate::sim::VirtualGate gate(gate_cfg);
    gate.boot_closed();
    std::printf("[sim] %s\n", gate.status_line().c_str());

    std::thread stdin_thread;
    if (interactive) {
        stdin_thread = std::thread(interactive_loop, std::ref(gate));
    }

    gate::sim::SimClient client(client_cfg, gate);
    while (!g_shutdown.load()) {
        const bool reached_server = client.run_session(g_shutdown);
        if (g_shutdown.load()) {
            break;
        }
        // Physics keep running while disconnected — an offline gate
        // still moves — but at the reconnect cadence, not the tick.
        const auto backoff = std::chrono::milliseconds(reached_server ? 1'000 : 3'000);
        gate.advance(static_cast<std::uint32_t>(backoff.count()));
        std::this_thread::sleep_for(backoff);
    }

    if (stdin_thread.joinable()) {
        stdin_thread.detach();  // blocked in getline; process exit reaps it
    }
    std::printf("[sim] bye\n");
    return 0;
}
