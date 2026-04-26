// status_led.hpp — three-color status LED pattern renderer.
//
// Drives three discrete LEDs (red, green, yellow) and renders animated
// patterns matching the proto's `LedPattern` enum:
//
//   kOff        — all off, timer stopped.
//   kAuthFlash  — 3× green flash @ 100 ms on/off, then off.
//   kDenyFlash  — 3× red flash   @ 100 ms on/off, then off.
//   kFaultSlow  — 1 Hz red blink, indefinite.
//   kOtaPulse   — 1 Hz yellow blink, indefinite.
//   kBootOk     — solid green for 2 s on boot, then off.
//
// `render(pattern)` is non-blocking: it stamps the new pattern, restarts
// the animation from frame 0, and returns. The 100 ms periodic esp_timer
// drives the actual LED toggles. Calling render() again interrupts the
// in-flight pattern and starts the new one cleanly.
//
// active_high=false flips polarity for common-anode wiring.
//
// Non-copyable, non-movable: the esp_timer captures `this`.

#pragma once

#include <atomic>
#include <cstdint>
#include <driver/gpio.h>
#include <esp_timer.h>

namespace gate::drivers {

class StatusLed {
public:
    enum class Pattern : std::uint8_t {
        kOff = 0,
        kAuthFlash = 1,
        kDenyFlash = 2,
        kFaultSlow = 3,
        kOtaPulse = 4,
        kBootOk = 5,
    };

    struct Config {
        gpio_num_t red_gpio;
        gpio_num_t green_gpio;
        gpio_num_t yellow_gpio;
        bool active_high = true;
    };

    explicit StatusLed(const Config& cfg);
    ~StatusLed();
    StatusLed(const StatusLed&) = delete;
    StatusLed& operator=(const StatusLed&) = delete;
    StatusLed(StatusLed&&) = delete;
    StatusLed& operator=(StatusLed&&) = delete;

    // Switch to the given pattern. Restarts animation from frame 0.
    // Thread-safe; the next 100 ms tick picks up the change.
    void render(Pattern p);

    // Currently-rendering pattern (snapshot).
    Pattern current() const;

private:
    static void timer_cb(void* arg);
    void on_tick();
    void set_color(bool red, bool green, bool yellow);
    void start_timer_if_needed();
    void stop_timer();

    Config cfg_;
    std::atomic<Pattern> current_{Pattern::kOff};
    std::atomic<bool> restart_requested_{false};
    std::uint32_t step_ = 0;
    esp_timer_handle_t timer_ = nullptr;
};

}  // namespace gate::drivers
