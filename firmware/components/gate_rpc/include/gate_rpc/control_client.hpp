// control_client.hpp — gRPC Control-stream client over h2c (ADR-011).
//
// Speaks real gRPC to the server's FieldControllerService/Control bidi
// stream using the three-layer stack ADR-011 picked: nanopb messages,
// the gate_rpc framing codec, and nghttp2 over a plain lwIP socket
// (the server listens with InsecureServerCredentials — cleartext
// HTTP/2, client preface sent directly, no Upgrade dance).
//
// Lifecycle
// ---------
// start() spawns the "gate_rpc" task, which blocks until
// notify_network_up() (wired to Ethernet::on_got_ip in main) and then
// runs a connect → stream → pump loop. Any failure — DNS, TCP, HTTP/2
// GOAWAY, stream close, decode error — tears the session down and
// retries with exponential backoff (1 s doubling to 30 s), reset to
// the minimum after a session that reached the stream-open stage.
// notify_network_down() gates the next reconnect; the in-flight
// session discovers the dead link through socket errors.
//
// Phase 4.5.3 scope (foundation): on stream open the client sends one
// "hello" Telemetry envelope and logs every envelope the server sends
// back. The 1 Hz telemetry cadence, CommandAck flow, and dispatch of
// GateCommands into GateController land in Phase 4.5.4.
//
// Threading
// ---------
// nghttp2 sessions are not thread-safe; everything session-related
// happens on the gate_rpc task. The only cross-thread surfaces are
// send_envelope() (encodes into a fixed frame and posts it to a
// FreeRTOS queue the pump drains between poll() wakeups), the two
// notify_*() flags, and the connected() snapshot.

#pragma once

#include <atomic>
#include <cstdint>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <functional>

#include "gate_service.pb.h"
#include "gate_state_machine/state_machine.hpp"

namespace gate::rpc {

// Session-scoped pump state; defined in control_client.cpp, one
// instance per connect → stream → pump cycle.
struct ControlSession;

// Map the on-device state machine's state onto the wire enum. The
// proto has no mid-travel "stopped" value (a server-side modelling
// question deferred to 4.5.4), so Stopped* reports as UNKNOWN rather
// than lying with OPENING/CLOSING.
gate_v1_GateState to_wire_state(gate::state_machine::State s) noexcept;

class ControlClient {
public:
    // Fills the gate-specific Telemetry fields (state, limits, beam)
    // right before an envelope is encoded. Runs on the gate_rpc task.
    using TelemetryFiller = std::function<void(gate_v1_Telemetry&)>;

    struct Config {
        const char* host = "";       // server IPv4/hostname (Kconfig: GATE_SERVER_HOST)
        std::uint16_t port = 50051;  // gRPC h2c port
        const char* gate_id = "";    // echoed in every Telemetry
        const char* fw_version = "";
        std::uint32_t reconnect_min_ms = 1'000;
        std::uint32_t reconnect_max_ms = 30'000;
    };

    ControlClient(const Config& cfg, TelemetryFiller filler);
    ~ControlClient();
    ControlClient(const ControlClient&) = delete;
    ControlClient& operator=(const ControlClient&) = delete;
    ControlClient(ControlClient&&) = delete;
    ControlClient& operator=(ControlClient&&) = delete;

    // Spawn the client task. ESP_ERR_INVALID_STATE on a second call.
    esp_err_t start();

    // Network lifecycle, wired to the ethernet driver's callbacks.
    void notify_network_up();
    void notify_network_down();

    // True while the Control stream is open end-to-end.
    [[nodiscard]] bool connected() const { return connected_.load(std::memory_order_relaxed); }

    // Encode and queue an envelope for transmission. Thread-safe.
    // Fails (false, logged) when the stream is down or the queue is
    // full — Control messages are periodic state, not a reliable log;
    // the next telemetry tick supersedes a dropped one.
    bool send_envelope(const gate_v1_ControlEnvelope& env);

private:
    // Encoded gRPC frame (5-byte prefix + nanopb payload) queued for
    // the HTTP/2 data provider. Control-plane messages are small; 512
    // covers the worst-case Telemetry/Ack/Fault by a wide margin.
    static constexpr std::size_t kMaxOutFrame = 512;
    struct OutFrame {
        std::uint16_t len;
        std::uint8_t data[kMaxOutFrame];
    };

    static void task_entry(void* arg);
    void task_main();
    bool run_session();  // one connect → stream → pump cycle
    void send_hello();

    friend struct ControlSession;  // session frames use OutFrame

    Config cfg_;
    TelemetryFiller filler_;
    TaskHandle_t task_ = nullptr;
    EventGroupHandle_t net_events_ = nullptr;
    QueueHandle_t out_queue_ = nullptr;
    std::atomic<bool> connected_{false};
    std::uint64_t telemetry_seq_ = 0;
    bool started_ = false;
};

}  // namespace gate::rpc
