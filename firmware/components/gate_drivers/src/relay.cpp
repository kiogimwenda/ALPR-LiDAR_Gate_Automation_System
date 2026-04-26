// relay.cpp

#include "gate_drivers/relay.hpp"

#include <esp_err.h>
#include <esp_log.h>

namespace gate::drivers {

namespace {
constexpr const char* kTag = "relay";
}

Relay::Relay(const Config& cfg) : cfg_(cfg) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << static_cast<int>(cfg_.gpio);
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));

    esp_timer_create_args_t targs = {};
    targs.callback = &Relay::timer_cb;
    targs.arg = this;
    targs.dispatch_method = ESP_TIMER_TASK;
    targs.name = "relay_pulse";
    ESP_ERROR_CHECK(esp_timer_create(&targs, &pulse_timer_));

    set(cfg_.initial_state);
    ESP_LOGI(kTag, "GPIO%d configured (active=%s, initial=%s)", static_cast<int>(cfg_.gpio),
             cfg_.active_high ? "high" : "low", cfg_.initial_state ? "on" : "off");
}

Relay::~Relay() {
    if (pulse_timer_ != nullptr) {
        esp_timer_stop(pulse_timer_);
        esp_timer_delete(pulse_timer_);
        pulse_timer_ = nullptr;
    }
    // Drive to OFF before releasing the pin so the relay doesn't latch
    // open across a config reload.
    write_gpio(false);
    gpio_reset_pin(cfg_.gpio);
}

void Relay::set(bool on) {
    esp_timer_stop(pulse_timer_);
    state_ = on;
    write_gpio(on);
}

bool Relay::state() const {
    return state_;
}

void Relay::pulse(std::chrono::milliseconds duration) {
    esp_timer_stop(pulse_timer_);
    state_ = true;
    write_gpio(true);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    esp_timer_start_once(pulse_timer_, static_cast<uint64_t>(us));
}

void Relay::timer_cb(void* arg) {
    auto* self = static_cast<Relay*>(arg);
    self->state_ = false;
    self->write_gpio(false);
}

void Relay::write_gpio(bool on) {
    const int level = (on == cfg_.active_high) ? 1 : 0;
    gpio_set_level(cfg_.gpio, level);
}

}  // namespace gate::drivers
