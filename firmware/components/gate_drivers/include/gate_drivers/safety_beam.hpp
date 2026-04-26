// safety_beam.hpp — photoelectric beam-break input driver.
//
// Same polling + debounce pattern as `LimitSwitch`, tuned for the safety
// requirements of beam-break sensors:
//
//   - Shorter debounce (default 5 ms vs 20 ms): a person walking through
//     the gate's swing path during close needs detection within tens of
//     ms — slower than a contact bounce but well below human-noticeable.
//   - Faster poll (default 1 ms vs 5 ms): keeps the worst-case detection
//     latency to ~6 ms (debounce + one poll).
//   - Active-when-blocked semantics: `is_blocked()` always reads "true =
//     beam interrupted" regardless of the sensor's electrical polarity.
//
// Failsafe note: typical beam receivers pull the line LOW when light is
// detected (beam clear); a disconnected wire reads HIGH (no light = treat
// as blocked) which is the safe default. `active_high_when_blocked = true`
// matches that wiring. Hardware that uses the opposite convention (a
// dedicated "fault output" that asserts when the sensor itself fails)
// should override that flag.
//
// Threading: the periodic poll runs on the esp_timer task; the user
// callback fires from there, not from an ISR — safe to take mutexes
// and call any ESP-IDF API.

#pragma once

#include <atomic>
#include <cstdint>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <functional>

namespace gate::drivers {

class SafetyBeam {
public:
    using Callback = std::function<void(bool blocked)>;

    struct Config {
        gpio_num_t gpio;
        bool active_high_when_blocked = true;
        std::uint32_t debounce_ms = 5;
        std::uint32_t poll_period_ms = 1;
        bool internal_pullup = true;
    };

    explicit SafetyBeam(const Config& cfg);
    ~SafetyBeam();
    SafetyBeam(const SafetyBeam&) = delete;
    SafetyBeam& operator=(const SafetyBeam&) = delete;
    SafetyBeam(SafetyBeam&&) = delete;
    SafetyBeam& operator=(SafetyBeam&&) = delete;

    // Currently committed state. `true` means the beam is interrupted
    // (something is in the gate's path). Lock-free read.
    bool is_blocked() const;

    // Replace (or clear, with `{}`) the edge callback. Called from the
    // esp_timer task context, not an ISR.
    void set_callback(Callback cb);

private:
    static void poll_timer_cb(void* arg);
    void on_poll();
    bool read_blocked() const;

    Config cfg_;
    std::atomic<bool> blocked_{false};
    bool last_sample_ = false;
    std::uint32_t stable_samples_ = 0;
    std::uint32_t samples_to_commit_ = 1;
    Callback cb_;
    esp_timer_handle_t poll_timer_ = nullptr;
};

}  // namespace gate::drivers
