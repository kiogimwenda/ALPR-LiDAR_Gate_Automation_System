// stored_config.hpp — the controller's persisted config and how an
// incoming config merges over it. Pure so the rules are host-tested.

#pragma once

#include <cstdint>

namespace gate::state_machine {

struct StoredConfig {
    std::uint32_t version = 0;
    std::uint32_t status = 0;  // ConfigStatus
    std::uint32_t type = 0;
    std::uint32_t lane_mode = 0;
    std::uint32_t auto_close_s = 0;
    std::uint32_t offline_close_s = 30;
    std::uint32_t bolt_hold_open_s = 45;
    std::uint32_t maglock_shed_s = 3'600;
};

// `in` is a complete config from the server. Zero means "keep" for the
// timers that must never be zero; auto_close_s 0 is a real value (off).
[[nodiscard]] inline StoredConfig merge_config(StoredConfig base, const StoredConfig& in) noexcept {
    const auto keep = [](std::uint32_t incoming, std::uint32_t current) {
        return incoming != 0 ? incoming : current;
    };
    base.version = in.version;
    base.status = in.status;
    base.type = in.type;
    base.lane_mode = in.lane_mode;
    base.auto_close_s = in.auto_close_s;
    base.offline_close_s = keep(in.offline_close_s, base.offline_close_s);
    base.bolt_hold_open_s = keep(in.bolt_hold_open_s, base.bolt_hold_open_s);
    base.maglock_shed_s = keep(in.maglock_shed_s, base.maglock_shed_s);
    return base;
}

}  // namespace gate::state_machine
