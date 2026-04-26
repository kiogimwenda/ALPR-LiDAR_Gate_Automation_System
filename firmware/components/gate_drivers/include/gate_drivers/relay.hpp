// relay.hpp — single-output relay driver.
//
// Operates in two modes:
//
//   - latched : `set(true)` / `set(false)` drives the output until the next
//               call. Used for `LATCH_OPEN` / `LATCH_CLOSE` commands from
//               the proto's CommandKind enum.
//   - pulsed  : `pulse(duration)` drives the output ON, then OFF after
//               `duration` via an esp_timer one-shot. Used for the
//               `PULSE_RELAY` and `OPEN_GATE`/`CLOSE_GATE` commands which
//               typically map to a momentary trigger input on commercial
//               gate openers.
//
// `active_high=false` flips the GPIO polarity for opto-isolated relay
// boards that energise the coil when the input pulls low.
//
// Non-copyable, non-movable: the esp_timer captures `this` and needs a
// stable address for the relay's lifetime. Construct in place (static
// storage or aggregate member) — the typical embedded pattern.

#pragma once

#include <chrono>
#include <driver/gpio.h>
#include <esp_timer.h>

namespace gate::drivers {

class Relay {
public:
    struct Config {
        gpio_num_t gpio;
        bool active_high = true;
        bool initial_state = false;
    };

    explicit Relay(const Config& cfg);
    ~Relay();
    Relay(const Relay&) = delete;
    Relay& operator=(const Relay&) = delete;
    Relay(Relay&&) = delete;
    Relay& operator=(Relay&&) = delete;

    // Drive the relay to the requested state. Cancels any in-flight pulse.
    void set(bool on);

    // Last commanded logical state (independent of GPIO polarity).
    bool state() const;

    // Fire a non-blocking one-shot pulse. Calls within the active window
    // restart the timer with the new duration ("re-arm rather than stack").
    void pulse(std::chrono::milliseconds duration);

private:
    static void timer_cb(void* arg);
    void write_gpio(bool on);

    Config cfg_;
    bool state_ = false;
    esp_timer_handle_t pulse_timer_ = nullptr;
};

}  // namespace gate::drivers
