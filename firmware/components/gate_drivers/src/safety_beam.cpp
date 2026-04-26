// safety_beam.cpp

#include "gate_drivers/safety_beam.hpp"

#include <esp_err.h>
#include <esp_log.h>
#include <utility>

namespace gate::drivers {

namespace {
constexpr const char* kTag = "safety-beam";
}

SafetyBeam::SafetyBeam(const Config& cfg) : cfg_(cfg) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << static_cast<int>(cfg_.gpio);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = cfg_.internal_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));

    samples_to_commit_ = (cfg_.debounce_ms + cfg_.poll_period_ms - 1) / cfg_.poll_period_ms;
    if (samples_to_commit_ == 0)
        samples_to_commit_ = 1;

    last_sample_ = read_blocked();
    blocked_.store(last_sample_);

    esp_timer_create_args_t targs = {};
    targs.callback = &SafetyBeam::poll_timer_cb;
    targs.arg = this;
    targs.dispatch_method = ESP_TIMER_TASK;
    targs.name = "safety_poll";
    ESP_ERROR_CHECK(esp_timer_create(&targs, &poll_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(poll_timer_, cfg_.poll_period_ms * 1000ULL));

    ESP_LOGI(kTag,
             "GPIO%d configured (active=%s when blocked, pullup=%s, poll=%ums, "
             "debounce=%ums = %u samples)",
             static_cast<int>(cfg_.gpio), cfg_.active_high_when_blocked ? "high" : "low",
             cfg_.internal_pullup ? "on" : "off", static_cast<unsigned>(cfg_.poll_period_ms),
             static_cast<unsigned>(cfg_.debounce_ms), static_cast<unsigned>(samples_to_commit_));
}

SafetyBeam::~SafetyBeam() {
    if (poll_timer_ != nullptr) {
        esp_timer_stop(poll_timer_);
        esp_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    gpio_reset_pin(cfg_.gpio);
}

bool SafetyBeam::is_blocked() const {
    return blocked_.load();
}

void SafetyBeam::set_callback(Callback cb) {
    cb_ = std::move(cb);
}

void SafetyBeam::poll_timer_cb(void* arg) {
    static_cast<SafetyBeam*>(arg)->on_poll();
}

void SafetyBeam::on_poll() {
    const bool sample = read_blocked();

    if (sample != last_sample_) {
        last_sample_ = sample;
        stable_samples_ = 1;
        return;
    }
    if (sample == blocked_.load()) {
        stable_samples_ = 0;
        return;
    }
    if (++stable_samples_ < samples_to_commit_)
        return;

    blocked_.store(sample);
    stable_samples_ = 0;
    if (cb_)
        cb_(sample);
}

bool SafetyBeam::read_blocked() const {
    const int raw = gpio_get_level(cfg_.gpio);
    return cfg_.active_high_when_blocked ? (raw == 1) : (raw == 0);
}

}  // namespace gate::drivers
