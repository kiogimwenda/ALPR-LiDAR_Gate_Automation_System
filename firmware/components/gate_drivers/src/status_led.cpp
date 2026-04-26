// status_led.cpp

#include "gate_drivers/status_led.hpp"

#include <esp_err.h>
#include <esp_log.h>

namespace gate::drivers {

namespace {

constexpr const char* kTag = "status-led";
constexpr std::uint64_t kTickPeriodUs = 100'000ULL;  // 100 ms

// Frame counts for the finite patterns. Repeating patterns (kFaultSlow,
// kOtaPulse) cycle on the modulo of step_ and never stop.
constexpr std::uint32_t kAuthFlashFrames = 6;  // 3× on/off pairs
constexpr std::uint32_t kDenyFlashFrames = 6;
constexpr std::uint32_t kBootOkFrames = 20;     // 20 × 100 ms = 2 s
constexpr std::uint32_t kSlowBlinkPeriod = 10;  // 10 × 100 ms = 1 s ⇒ 1 Hz

}  // namespace

StatusLed::StatusLed(const Config& cfg) : cfg_(cfg) {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << static_cast<int>(cfg_.red_gpio)) |
                      (1ULL << static_cast<int>(cfg_.green_gpio)) |
                      (1ULL << static_cast<int>(cfg_.yellow_gpio));
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));

    set_color(false, false, false);

    esp_timer_create_args_t targs = {};
    targs.callback = &StatusLed::timer_cb;
    targs.arg = this;
    targs.dispatch_method = ESP_TIMER_TASK;
    targs.name = "status_led";
    ESP_ERROR_CHECK(esp_timer_create(&targs, &timer_));

    ESP_LOGI(kTag, "configured (R=GPIO%d G=GPIO%d Y=GPIO%d active=%s)",
             static_cast<int>(cfg_.red_gpio), static_cast<int>(cfg_.green_gpio),
             static_cast<int>(cfg_.yellow_gpio), cfg_.active_high ? "high" : "low");
}

StatusLed::~StatusLed() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
        timer_ = nullptr;
    }
    set_color(false, false, false);
    gpio_reset_pin(cfg_.red_gpio);
    gpio_reset_pin(cfg_.green_gpio);
    gpio_reset_pin(cfg_.yellow_gpio);
}

void StatusLed::render(Pattern p) {
    current_.store(p);
    restart_requested_.store(true);
    if (p == Pattern::kOff) {
        // Drive everything off immediately and stop the timer; the tick
        // path would also do it but we save 100 ms of lingering output.
        set_color(false, false, false);
        stop_timer();
        return;
    }
    start_timer_if_needed();
}

StatusLed::Pattern StatusLed::current() const {
    return current_.load();
}

void StatusLed::timer_cb(void* arg) {
    static_cast<StatusLed*>(arg)->on_tick();
}

void StatusLed::on_tick() {
    if (restart_requested_.exchange(false)) {
        step_ = 0;
    }
    const auto p = current_.load();
    switch (p) {
        case Pattern::kOff:
            set_color(false, false, false);
            stop_timer();
            return;

        case Pattern::kBootOk:
            if (step_ >= kBootOkFrames) {
                set_color(false, false, false);
                stop_timer();
                return;
            }
            set_color(false, true, false);
            break;

        case Pattern::kAuthFlash:
            if (step_ >= kAuthFlashFrames) {
                set_color(false, false, false);
                stop_timer();
                return;
            }
            // step 0,2,4 → on; step 1,3,5 → off.
            set_color(false, (step_ & 1u) == 0, false);
            break;

        case Pattern::kDenyFlash:
            if (step_ >= kDenyFlashFrames) {
                set_color(false, false, false);
                stop_timer();
                return;
            }
            set_color((step_ & 1u) == 0, false, false);
            break;

        case Pattern::kFaultSlow:
            // First half of the period on, second half off — 1 Hz.
            set_color((step_ % kSlowBlinkPeriod) < (kSlowBlinkPeriod / 2), false, false);
            break;

        case Pattern::kOtaPulse:
            set_color(false, false, (step_ % kSlowBlinkPeriod) < (kSlowBlinkPeriod / 2));
            break;
    }
    ++step_;
}

void StatusLed::set_color(bool red, bool green, bool yellow) {
    const int on_level = cfg_.active_high ? 1 : 0;
    const int off_level = !cfg_.active_high ? 1 : 0;
    gpio_set_level(cfg_.red_gpio, red ? on_level : off_level);
    gpio_set_level(cfg_.green_gpio, green ? on_level : off_level);
    gpio_set_level(cfg_.yellow_gpio, yellow ? on_level : off_level);
}

void StatusLed::start_timer_if_needed() {
    if (esp_timer_is_active(timer_))
        return;
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_, kTickPeriodUs));
}

void StatusLed::stop_timer() {
    if (esp_timer_is_active(timer_)) {
        esp_timer_stop(timer_);
    }
}

}  // namespace gate::drivers
