// gate_dispatch.hpp — the only publisher of OPEN_GATE. Owns one arbiter
// per gate, tracks firmware capabilities, audits every open, ties guest
// passes to confirmed opens and ingests the controller's event log.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "auth/allowlist_store.hpp"
#include "dash/event_broadcaster.hpp"
#include "fusion/fusion_engine.hpp"
#include "gate_service.pb.h"
#include "rpc/gate_arbiter.hpp"

namespace gate::rpc {

class GateDispatch {
public:
    // Telemetry.capabilities bits, spec section 21. 0 = legacy firmware.
    static constexpr std::uint32_t kCapLocks = 1;
    static constexpr std::uint32_t kCapEventLog = 4;
    static constexpr std::uint32_t kCapVerify = 8;
    static constexpr std::uint32_t kCapStats = 16;
    static constexpr std::int64_t kOpenExpirySeconds = 10;
    static constexpr std::uint32_t kEventOfflineOpen = 1;
    static constexpr std::uint64_t kConfigResendMs = 30'000;

    // `store` may be null (tests of a lone service): audit and pass
    // bookkeeping are then skipped.
    GateDispatch(gate::dash::EventBroadcaster& bus, gate::auth::AllowlistStore* store,
                 gate::fusion::FusionEngine* fusion, std::string site_id);

    gate::v1::OpenReply submit(const std::string& gate_id, GateArbiter::Request r);
    gate::v1::OpenReply submit_decision(const gate::v1::AuthDecision& d,
                                        gate::v1::OpenSource source);
    gate::v1::OpenReply on_open_request(const std::string& gate_id, const gate::v1::OpenRequest& r);
    void on_telemetry(const gate::v1::Telemetry& t);
    void on_ack(const gate::v1::CommandAck& a);
    gate::v1::EventAck ingest(const std::string& gate_id, const gate::v1::EventBatch& batch);
    void publish_alerts(const gate::v1::AuthDecision& d);

    [[nodiscard]] bool supports(const std::string& gate_id, gate::v1::CommandKind kind);
    // Spec 13.1: type, layout and lane change only when shut.
    [[nodiscard]] bool configurable(const std::string& gate_id);
    void publish_config(const std::string& gate_id, const gate::v1::GateConfig& cfg);
    [[nodiscard]] bool locked(const std::string& gate_id);
    [[nodiscard]] bool hold_auto_close(const std::string& gate_id);
    void vehicle_left(const std::string& gate_id, const std::string& plate);

    // Injectable clock for tests.
    void set_clock_ms(std::uint64_t ms) { fixed_now_ms_ = ms; }

private:
    struct Gate {
        std::unique_ptr<GateArbiter> arbiter;
        std::uint32_t capabilities = 0;
        bool beam_clear = true;
        bool beam_tripped = false;  // since the current grant
        gate::v1::GateState state = gate::v1::GATE_STATE_UNKNOWN;
        std::uint64_t config_sent_ms = 0;
    };

    Gate& gate_locked(const std::string& gate_id);
    void publish_open(const std::string& gate_id, const GateArbiter::Request& r);
    void audit(const std::string& gate_id, const GateArbiter::Audit& a);
    [[nodiscard]] std::uint64_t now_ms() const;

    gate::dash::EventBroadcaster& bus_;
    gate::auth::AllowlistStore* store_;
    gate::fusion::FusionEngine* fusion_;
    std::string site_id_;
    std::mutex mu_;
    std::map<std::string, Gate> gates_;
    std::uint64_t fixed_now_ms_ = 0;
};

}  // namespace gate::rpc
