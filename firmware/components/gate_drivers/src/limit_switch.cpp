// limit_switch.cpp

#include "gate_drivers/limit_switch.hpp"

#include <esp_err.h>
#include <esp_log.h>
#include <utility>

namespace gate::drivers {

namespace {
constexpr const char* kTag = "limit-sw";
}

LimitSwitch::LimitSwitch(const Config& cfg) : cfg_(cfg) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << static_cast<int>(cfg_.gpio);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = cfg_.internal_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;  // we poll
    ESP_ERROR_CHECK(gpio_config(&io));

    samples_to_commit_ = (cfg_.debounce_ms + cfg_.poll_period_ms - 1) / cfg_.poll_period_ms;
    if (samples_to_commit_ == 0)
        samples_to_commit_ = 1;

    last_sample_ = read_active();
    active_.store(last_sample_);

    esp_timer_create_args_t targs = {};
    targs.callback = &LimitSwitch::poll_timer_cb;
    targs.arg = this;
    targs.dispatch_method = ESP_TIMER_TASK;
    targs.name = "limit_poll";
    ESP_ERROR_CHECK(esp_timer_create(&targs, &poll_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(poll_timer_, cfg_.poll_period_ms * 1000ULL));

    ESP_LOGI(kTag, "GPIO%d configured (%s, pullup=%s, poll=%ums, debounce=%ums = %u samples)",
             static_cast<int>(cfg_.gpio), cfg_.normally_open ? "NO" : "NC",
             cfg_.internal_pullup ? "on" : "off", static_cast<unsigned>(cfg_.poll_period_ms),
             static_cast<unsigned>(cfg_.debounce_ms), static_cast<unsigned>(samples_to_commit_));
}

LimitSwitch::~LimitSwitch() {
    if (poll_timer_ != nullptr) {
        esp_timer_stop(poll_timer_);
        esp_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    gpio_reset_pin(cfg_.gpio);
}

bool LimitSwitch::is_active() const {
    return active_.load();
}

void LimitSwitch::set_callback(Callback cb) {
    cb_ = std::move(cb);
}

void LimitSwitch::poll_timer_cb(void* arg) {
    static_cast<LimitSwitch*>(arg)->on_poll();
}

void LimitSwitch::on_poll() {
    const bool sample = read_active();

    if (sample != last_sample_) {
        // The raw level just changed — restart the stability counter
        // against the new value. This is the bounce-glitch entry path.
        last_sample_ = sample;
        stable_samples_ = 1;
        return;
    }
    if (sample == active_.load()) {
        // Stable in the already-committed state; nothing to do.
        stable_samples_ = 0;
        return;
    }
    if (++stable_samples_ < samples_to_commit_)
        return;

    // We've seen the new state for `samples_to_commit_` consecutive ticks
    // — long enough that contact bounce is over. Commit + notify.
    active_.store(sample);
    stable_samples_ = 0;
    if (cb_)
        cb_(sample);
}

bool LimitSwitch::read_active() const {
    const int raw = gpio_get_level(cfg_.gpio);
    // For NO with internal pull-up, idle = HIGH and active = LOW (switch
    // closes to ground). For NC, the inverse holds.
    return cfg_.normally_open ? (raw == 0) : (raw == 1);
}

}  // namespace gate::drivers
