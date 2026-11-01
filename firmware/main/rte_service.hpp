// rte_service.hpp — NVS persistence and the exit-button service: glues
// the pure RteAuthority, EventRing and ServedGuard to GPIO, flash and
// the Control stream. Without an RTE GPIO or the evlog partition (rev 2,
// older partition tables) it stays disabled; the served guard still runs.

#pragma once

#include <atomic>
#include <cstdint>
#include <esp_partition.h>
#include <functional>
#include <memory>
#include <mutex>

#include "gate_rpc/control_client.hpp"
#include "gate_state_machine/event_ring.hpp"
#include "gate_state_machine/rte_authority.hpp"
#include "gate_state_machine/served_guard.hpp"
#include "gate_state_machine/stored_config.hpp"

namespace gate::app {

inline constexpr std::uint32_t kCapEventLog = 4;

using state_machine::StoredConfig;

class Persist {
public:
    void init();
    [[nodiscard]] bool load_lockdown();
    void save_lockdown(bool latched);
    [[nodiscard]] StoredConfig load_config();
    [[nodiscard]] StoredConfig load_cached();  // thread-safe, no flash access
    bool save_config(const gate_v1_GateConfig& c);
    [[nodiscard]] std::uint32_t next_boot_id();
    void load_served(state_machine::ServedGuard::Persisted& p);
    void save_served(const state_machine::ServedGuard::Persisted& p);

private:
    std::mutex mu_;
    StoredConfig cached_{};
};

class RteService {
public:
    struct Hooks {
        std::function<bool()> link_up;
        std::function<bool(const gate_v1_ControlEnvelope&)> send;
        std::function<void()> open;
        std::function<void()> close;
        std::function<void()> local_reset;
        std::function<std::uint8_t()> wire_state;
    };

    explicit RteService(Persist& persist) noexcept : persist_(persist) {}

    void start(int gpio, std::uint32_t offline_close_s, Hooks hooks);
    [[nodiscard]] bool enabled() const { return ring_ready_.load(); }  // event log capability
    [[nodiscard]] std::uint32_t pending() const { return pending_.load(); }

    // nullptr to accept; otherwise a static refusal. Marks the id served.
    const char* admit_open(const gate_v1_GateCommand& cmd);
    void on_reply(const gate_v1_OpenReply& r);
    void on_event_ack(const gate_v1_EventAck& a);

private:
    static void task_entry(void* arg);
    void task();
    void sync_events(std::uint64_t now_ms);
    void send_unlogged_alert();

    Persist& persist_;
    Hooks hooks_;
    std::mutex mu_;  // guards guard_ and authority_
    state_machine::ServedGuard guard_;
    std::unique_ptr<state_machine::FlashIo> flash_;
    std::unique_ptr<state_machine::EventRing> ring_;
    std::unique_ptr<state_machine::RteAuthority> authority_;
    int gpio_ = -1;
    std::uint32_t boot_id_ = 0;
    std::uint32_t offline_close_ms_ = 30'000;
    state_machine::LocalCloser closer_;
    gate_v1_ControlEnvelope out_env_ = gate_v1_ControlEnvelope_init_zero;  // too big for the stack
    std::uint64_t next_sync_ = 0;
    std::atomic<bool> ring_ready_{false};
    std::atomic<std::uint32_t> pending_{0};
    std::atomic<std::uint32_t> acked_{0};
};

}  // namespace gate::app
