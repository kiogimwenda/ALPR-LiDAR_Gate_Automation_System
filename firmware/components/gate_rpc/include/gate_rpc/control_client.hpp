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
// Command flow (Phase 4.5.4)
// --------------------------
// Every GateCommand is acked twice, per the proto contract: once on
// receipt (completed=false) and once on completion. The dispatcher
// callback (wired in main) issues the hardware action and classifies
// the command as synchronous (LED, reboot — completed immediately),
// asynchronous (open/close — completed when the gate reaches the
// commanded terminal state, tracked by CommandTracker), or rejected
// (unsupported kinds — completed immediately with an error). Gate
// transitions reach the tracker through notify_gate_event(), wired to
// GateController's transition listener. Telemetry flows at
// telemetry_period_ms while the stream is up; sent_ts appears once
// SNTP has synced the wall clock (a zero timestamp is worse than an
// absent one).
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
#include <mutex>

#include "gate_rpc/command_tracker.hpp"
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

    // Outcome of dispatching a GateCommand to the hardware layer.
    struct DispatchResult {
        bool accepted = false;
        bool completed_now = false;  // synchronous command — final ack immediately
        // Async command whose subsystem acks for itself by calling
        // complete_command() later (e.g. OTA) — the gate-state tracker
        // is not armed and no deadline is imposed here.
        bool external_completion = false;
        // Async gate-motion commands: reaching this state completes
        // successfully (tracked, deadline-backstopped).
        gate::state_machine::State terminal_ok = gate::state_machine::State::Initializing;
        const char* error = "";  // set when !accepted; must be a literal
    };
    // Runs on the gate_rpc task; must not block (a slow dispatcher
    // stalls the HTTP/2 pump). Command entry points on GateController
    // are queue-posts, so the natural implementations are all O(1).
    using CommandDispatcher = std::function<DispatchResult(const gate_v1_GateCommand&)>;

    struct Config {
        const char* host = "";       // server IPv4/hostname (Kconfig: GATE_SERVER_HOST)
        std::uint16_t port = 50051;  // gRPC h2c port
        const char* gate_id = "";    // echoed in every Telemetry
        const char* fw_version = "";
        std::uint32_t reconnect_min_ms = 1'000;
        std::uint32_t reconnect_max_ms = 30'000;
        std::uint32_t telemetry_period_ms = 1'000;  // proto asks for ~1 Hz
        // Completion backstop for async commands: motor watchdog
        // budget plus slack, so the gate's own MotorTimeout fault
        // normally resolves the ack first with a better error.
        std::uint32_t command_deadline_ms = 40'000;
    };

    ControlClient(const Config& cfg, TelemetryFiller filler, CommandDispatcher dispatcher);
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

    // Gate transition/rejection feed for pending-command completion —
    // wire to GateController::set_transition_listener. Thread-safe
    // (called from the gate_ctrl task; tracker sits under a mutex).
    void notify_gate_event(gate::state_machine::State s, gate::state_machine::Reason r,
                           bool transitioned);

    // Completion ack for external_completion commands (see
    // DispatchResult). Thread-safe; `error` must be a static string.
    void complete_command(const char* command_id, bool success, const char* error);

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
    void send_telemetry();
    void handle_command(const gate_v1_GateCommand& cmd);  // gate_rpc task
    void send_ack_received(const gate_v1_GateCommand& cmd);
    void send_ack_completed(const char* command_id, bool success, const char* error);

    friend struct ControlSession;  // session frames use OutFrame; commands call back

    Config cfg_;
    TelemetryFiller filler_;
    CommandDispatcher dispatcher_;
    TaskHandle_t task_ = nullptr;
    EventGroupHandle_t net_events_ = nullptr;
    QueueHandle_t out_queue_ = nullptr;
    std::atomic<bool> connected_{false};
    std::uint64_t telemetry_seq_ = 0;
    std::uint32_t last_telemetry_ms_ = 0;
    bool started_ = false;

    // Last state reported on the wire — CommandAck.state_after uses
    // this so acks and telemetry never disagree about the gate.
    std::atomic<gate_v1_GateState> last_wire_state_{gate_v1_GateState_GATE_STATE_UNKNOWN};

    std::mutex tracker_mutex_;
    CommandTracker tracker_;
};

}  // namespace gate::rpc
