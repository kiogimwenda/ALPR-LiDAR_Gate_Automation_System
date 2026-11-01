// gate_persist.hpp — what the controller keeps across reboots, over a
// small key-value interface so the rules run on host against a fake.

#pragma once

#include <cstddef>
#include <cstdint>

#include "gate_state_machine/served_guard.hpp"
#include "gate_state_machine/stored_config.hpp"

namespace gate::state_machine {

class KvStore {
public:
    virtual ~KvStore() = default;
    // False when the key is absent or its size differs from `len`.
    virtual bool get(const char* key, void* out, std::size_t len) = 0;
    virtual bool set(const char* key, const void* data, std::size_t len) = 0;
};

class GatePersist {
public:
    explicit GatePersist(KvStore& kv) noexcept : kv_(kv) {}

    [[nodiscard]] bool load_lockdown() {
        std::uint8_t v = 0;
        return kv_.get("lockdown", &v, sizeof(v)) && v != 0;
    }
    bool save_lockdown(bool latched) {
        const std::uint8_t v = latched ? 1 : 0;
        return kv_.set("lockdown", &v, sizeof(v));
    }
    // Absent or wrong-sized data falls back to defaults.
    [[nodiscard]] StoredConfig load_config() {
        StoredConfig c;
        if (!kv_.get("config", &c, sizeof(c))) {
            c = StoredConfig{};
        }
        return c;
    }
    [[nodiscard]] bool save_config(const StoredConfig& base, const StoredConfig& incoming,
                                   StoredConfig& merged) {
        merged = merge_config(base, incoming);
        return kv_.set("config", &merged, sizeof(merged));
    }
    [[nodiscard]] std::uint32_t next_boot_id() {
        std::uint32_t id = 0;
        (void)kv_.get("boot_id", &id, sizeof(id));
        ++id;
        (void)kv_.set("boot_id", &id, sizeof(id));
        return id;
    }
    void load_served(ServedGuard::Persisted& p) {
        if (!kv_.get("served", &p, sizeof(p))) {
            p = {};
        }
    }
    bool save_served(const ServedGuard::Persisted& p) { return kv_.set("served", &p, sizeof(p)); }

private:
    KvStore& kv_;
};

}  // namespace gate::state_machine
