// rte_service.cpp — see the header.

#include "rte_service.hpp"

#include <cstdio>
#include <cstring>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <sys/time.h>

namespace gate::app {

namespace sm = gate::state_machine;

namespace {

constexpr const char* kTag = "rte";
constexpr const char* kNs = "gate";
constexpr std::uint32_t kPollMs = 20;
constexpr std::uint32_t kStackBytes = 6144;  // ring scans and nanopb encode run here
constexpr std::uint64_t kSyncPeriodMs = 1'000;
constexpr std::int64_t kMinValidUnix = 1'700'000'000;

class PartitionFlash final : public sm::FlashIo {
public:
    explicit PartitionFlash(const esp_partition_t* p) noexcept : p_(p) {}
    void read(std::size_t o, std::uint8_t* out, std::size_t n) override {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_partition_read(p_, o, out, n));
    }
    void program(std::size_t o, const std::uint8_t* d, std::size_t n) override {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_partition_write(p_, o, d, n));
    }
    void erase_sector(std::size_t s) override {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_partition_erase_range(p_, s * sm::EventRing::kSectorSize,
                                                                sm::EventRing::kSectorSize));
    }

private:
    const esp_partition_t* p_;
};

struct Nvs {
    nvs_handle_t h = 0;
    bool ok = false;
    explicit Nvs(nvs_open_mode_t mode) { ok = nvs_open(kNs, mode, &h) == ESP_OK; }
    ~Nvs() {
        if (ok) {
            nvs_close(h);
        }
    }
};

std::int64_t unix_ms_now() {
    timeval tv{};
    gettimeofday(&tv, nullptr);
    return tv.tv_sec < kMinValidUnix ? 0 : tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

}  // namespace

void Persist::init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

bool NvsKv::get(const char* key, void* out, std::size_t len) {
    Nvs n(NVS_READONLY);
    std::size_t size = len;
    return n.ok && nvs_get_blob(n.h, key, out, &size) == ESP_OK && size == len;
}

bool NvsKv::set(const char* key, const void* data, std::size_t len) {
    Nvs n(NVS_READWRITE);
    return n.ok && nvs_set_blob(n.h, key, data, len) == ESP_OK && nvs_commit(n.h) == ESP_OK;
}

StoredConfig Persist::load_config() {
    const StoredConfig c = store_.load_config();
    const std::lock_guard<std::mutex> lk{mu_};
    cached_ = c;
    return c;
}

StoredConfig Persist::load_cached() {
    const std::lock_guard<std::mutex> lk{mu_};
    return cached_;
}

bool Persist::save_config(const gate_v1_GateConfig& in) {
    StoredConfig incoming;
    incoming.version = in.version;
    incoming.status = gate_v1_ConfigStatus_CONFIG_UNVERIFIED;
    incoming.type = in.type;
    incoming.lane_mode = in.lane_mode;
    incoming.auto_close_s = in.auto_close_s;
    incoming.offline_close_s = in.offline_close_s;
    incoming.bolt_hold_open_s = in.bolt_hold_open_s;
    incoming.maglock_shed_s = in.maglock_shed_s;
    StoredConfig merged;
    if (!store_.save_config(load_cached(), incoming, merged)) {
        return false;
    }
    const std::lock_guard<std::mutex> lk{mu_};
    cached_ = merged;
    return true;
}

void RteService::start(int gpio, std::uint32_t offline_close_s, Hooks hooks) {
    hooks_ = std::move(hooks);
    offline_close_ms_ = (offline_close_s != 0 ? offline_close_s : 30U) * 1000U;
    const esp_partition_t* part = esp_partition_find_first(static_cast<esp_partition_type_t>(0x40),
                                                           ESP_PARTITION_SUBTYPE_ANY, "evlog");
    if (gpio < 0) {
        core_ = std::make_unique<sm::RteCore>(persist_.store(), nullptr, offline_close_ms_);
        ESP_LOGW(kTag, "exit button disabled: no GPIO");
        return;
    }
    gpio_ = gpio;
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << gpio;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&io));

    if (part != nullptr) {
        flash_ = std::make_unique<PartitionFlash>(part);
        ring_ = std::make_unique<sm::EventRing>(*flash_);
        ring_->mount();
        boot_id_ = persist_.next_boot_id();
        // NVS lost: resume above anything already in the ring.
        if (const auto top = ring_->highest_boot_id(); boot_id_ <= top) {
            boot_id_ = top + 1;
        }
        ring_ready_.store(true);
    } else {
        ESP_LOGE(kTag, "no evlog partition: exits open but go unlogged");
    }
    core_ = std::make_unique<sm::RteCore>(persist_.store(), ring_.get(), offline_close_ms_);
    xTaskCreate(&RteService::task_entry, "gate_rte", kStackBytes, this, 4, nullptr);
}

void RteService::task_entry(void* arg) {
    static_cast<RteService*>(arg)->task();
}

void RteService::task() {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
        const std::uint64_t now = esp_timer_get_time() / 1000;
        const bool pressed = gpio_get_level(static_cast<gpio_num_t>(gpio_)) == 0;
        const bool link = hooks_.link_up();

        sm::ServedGuard::Id fresh{};
        esp_fill_random(fresh.data(), fresh.size());
        fresh[6] = static_cast<std::uint8_t>((fresh[6] & 0x0F) | 0x40);
        fresh[8] = static_cast<std::uint8_t>((fresh[8] & 0x3F) | 0x80);

        sm::EventRecord base;
        base.boot_id = boot_id_;
        base.uptime_ms = now;
        base.unix_ms = unix_ms_now();
        base.source = gate_v1_OpenSource_OPEN_SOURCE_RTE;
        base.side = gate_v1_GateSide_GATE_SIDE_EXIT;
        base.state_after = hooks_.wire_state();

        sm::RteCore::Step step;
        {
            const std::lock_guard<std::mutex> lk{mu_};
            step = core_->tick({now, pressed, link, base.state_after, fresh, base});
        }
        const auto out = step.output;
        const auto id = step.id;

        using Out = sm::RteAuthority::Output;
        if (out == Out::SendRequest) {
            out_env_ = gate_v1_ControlEnvelope_init_zero;
            out_env_.which_payload = gate_v1_ControlEnvelope_open_request_tag;
            auto& q = out_env_.payload.open_request;
            sm::ServedGuard::format_uuid(id, q.request_id);
            std::snprintf(q.gate_id, sizeof(q.gate_id), "%s", CONFIG_GATE_ID);
            q.side = gate_v1_GateSide_GATE_SIDE_EXIT;
            q.source = gate_v1_OpenSource_OPEN_SOURCE_RTE;
            (void)hooks_.send(out_env_);  // a lost send falls back on the timeout
        } else if (out == Out::OpenLocal) {
            hooks_.open();
            if (ring_ == nullptr) {
                send_unlogged_alert();
            }
        } else if (out == Out::LocalReset) {
            hooks_.local_reset();
        } else if (out == Out::Stuck) {
            ESP_LOGE(kTag, "RTE_STUCK: button held, input ignored until released");
        }

        // Retries past a blocked beam; the state machine still guards.
        if (step.post_close) {
            hooks_.close();
        }
        if (now >= next_sync_) {
            next_sync_ = now + kSyncPeriodMs;
            sync_events(now);
        }
    }
}

// One record per envelope, resent until acked: freed only on ack.
void RteService::sync_events(std::uint64_t) {
    if (ring_ == nullptr) {
        return;
    }
    ring_->ack(acked_.load());
    const auto batch = ring_->pending(1);
    pending_.store(static_cast<std::uint32_t>(ring_->pending(sm::EventRing::kCapacity).size()));
    if (batch.empty() || !hooks_.link_up()) {
        return;
    }
    const auto& r = batch.front();
    out_env_ = gate_v1_ControlEnvelope_init_zero;
    out_env_.which_payload = gate_v1_ControlEnvelope_events_tag;
    auto& b = out_env_.payload.events;
    b.dropped = ring_->dropped();
    b.events_count = 1;
    auto& e = b.events[0];
    e.seq = r.seq;
    e.boot_id = r.boot_id;
    e.uptime_ms = r.uptime_ms;
    e.unix_ms = r.unix_ms;
    e.kind = r.kind;
    e.source = static_cast<gate_v1_OpenSource>(r.source);
    e.side = static_cast<gate_v1_GateSide>(r.side);
    e.result = r.result;
    e.state_after = static_cast<gate_v1_GateState>(r.state_after);
    e.request_id.size = static_cast<pb_size_t>(r.request_id.size());
    std::memcpy(e.request_id.bytes, r.request_id.data(), r.request_id.size());
    (void)hooks_.send(out_env_);
}

void RteService::send_unlogged_alert() {
    out_env_ = gate_v1_ControlEnvelope_init_zero;
    out_env_.which_payload = gate_v1_ControlEnvelope_fault_tag;
    auto& f = out_env_.payload.fault;
    std::snprintf(f.gate_id, sizeof(f.gate_id), "%s", CONFIG_GATE_ID);
    f.severity = gate_v1_FaultSeverity_FAULT_SEVERITY_ERROR;
    std::snprintf(f.code, sizeof(f.code), "EVENT_LOG_MISSING");
    std::snprintf(f.description, sizeof(f.description), "exit opened with no event log");
    ESP_LOGE(kTag, "exit opened with no event log");
    (void)hooks_.send(out_env_);
}

const char* RteService::admit_open(const gate_v1_GateCommand& cmd) {
    const auto id = sm::ServedGuard::parse_uuid(cmd.request_id);
    const std::int64_t now = unix_ms_now();
    const std::int64_t expires = cmd.has_expires_ts ? cmd.expires_ts.seconds * 1000LL : 0;
    const std::lock_guard<std::mutex> lk{mu_};
    switch (core_->admit_open(id, expires, now, now != 0)) {
        case sm::ServedGuard::Verdict::AlreadyServed:
            return "already served";
        case sm::ServedGuard::Verdict::Expired:
            return "open command expired";
        case sm::ServedGuard::Verdict::Accept:
            break;
    }
    return nullptr;
}

void RteService::on_reply(const gate_v1_OpenReply& r) {
    using Reply = sm::RteAuthority::Reply;
    Reply reply{};
    switch (r.status) {
        case gate_v1_OpenReply_Status_GRANTED:
            reply = Reply::Granted;
            break;
        case gate_v1_OpenReply_Status_QUEUED:
            reply = Reply::Queued;
            break;
        case gate_v1_OpenReply_Status_DENIED:
            reply = Reply::Denied;
            break;
        case gate_v1_OpenReply_Status_ALREADY_SERVED:
            reply = Reply::AlreadyServed;
            break;
        default:
            return;
    }
    const std::lock_guard<std::mutex> lk{mu_};
    core_->on_reply(reply, sm::ServedGuard::parse_uuid(r.request_id), esp_timer_get_time() / 1000);
}

void RteService::on_event_ack(const gate_v1_EventAck& a) {
    acked_.store(static_cast<std::uint32_t>(a.last_seq));
}

}  // namespace gate::app
