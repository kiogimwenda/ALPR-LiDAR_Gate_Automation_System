// limit_switch.hpp — debounced GPIO input driver for mechanical switches.
//
// Polls the input pin via a periodic esp_timer (default 5 ms = 200 Hz)
// and commits a state transition only after `debounce_ms` of stable
// readings — eliminating contact-bounce noise typical of mechanical
// switches without ISR complexity. The user callback fires on the
// committed edge, not on noisy bounces.
//
// `normally_open=true` (the common configuration) means the switch is
// open when the limit is *not* reached; the input pin reads HIGH (via
// internal pull-up) until the switch closes to ground. `is_active()`
// abstracts polarity so callers always read "true = limit reached".
//
// Threading: the periodic poll runs on the esp_timer task; the user
// callback fires from there (not from an ISR), so it's safe to take
// mutexes, post to queues, and call generic ESP-IDF APIs.
//
// Non-copyable, non-movable: the esp_timer captures `this`.

#pragma once

#include <atomic>
#include <cstdint>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <functional>

namespace gate::drivers {

class LimitSwitch {
public:
    using Callback = std::function<void(bool active)>;

    struct Config {
        gpio_num_t gpio;
        bool normally_open = true;
        std::uint32_t debounce_ms = 20;
        std::uint32_t poll_period_ms = 5;
        bool internal_pullup = true;
    };

    explicit LimitSwitch(const Config& cfg);
    ~LimitSwitch();
    LimitSwitch(const LimitSwitch&) = delete;
    LimitSwitch& operator=(const LimitSwitch&) = delete;
    LimitSwitch(LimitSwitch&&) = delete;
    LimitSwitch& operator=(LimitSwitch&&) = delete;

    // Currently committed state. `true` means the limit is asserted.
    bool is_active() const;

    // Replace (or clear, with `{}`) the edge callback. Called from the
    // esp_timer task context — not an ISR — so it can take mutexes and
    // call any ESP-IDF API.
    void set_callback(Callback cb);

private:
    static void poll_timer_cb(void* arg);
    void on_poll();
    bool read_active() const;

    Config cfg_;
    std::atomic<bool> active_{false};
    bool last_sample_ = false;
    std::uint32_t stable_samples_ = 0;
    std::uint32_t samples_to_commit_ = 1;
    Callback cb_;
    esp_timer_handle_t poll_timer_ = nullptr;
};

}  // namespace gate::drivers
