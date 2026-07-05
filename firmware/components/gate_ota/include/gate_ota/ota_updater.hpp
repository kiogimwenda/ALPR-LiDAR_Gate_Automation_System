// ota_updater.hpp — self-hosted OTA per ADR-009, driven by BEGIN_OTA.
//
// Flow (all on a dedicated short-lived "gate_ota" task):
//
//   1. GET the JSON manifest from Config::manifest_url (esp_http_client).
//   2. Extract → gate::ota::Manifest, then run the pure validate()
//      rules (version differs, fits the passive slot, uptime gate, …).
//   3. Stream the image into the passive partition with the
//      esp_https_ota advanced API (plain http on the field LAN today —
//      TLS + provisioning arrive with Phase 4.8 deployment).
//   4. Read the written image back out of the partition, SHA-256 it,
//      and compare against the manifest digest.
//   5. Verify the manifest's ed25519 signature over that digest
//      (libsodium). If no public key is provisioned (Config::pubkey
//      empty — the deployment keypair is generated in Phase 4.8), log
//      a loud warning and continue on the checksum alone.
//   6. esp_https_ota_finish() → passive slot becomes the boot slot.
//      The caller's DoneCallback fires; main acks the command and
//      reboots. CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE makes the new
//      image PENDING_VERIFY — app_main marks it valid once boot
//      reaches steady state, else the bootloader rolls back.
//
// One update at a time: begin() while an update runs is refused. The
// updater never reboots the device itself — sequencing the completion
// ack ahead of the restart is the caller's job (same pattern as the
// REBOOT command).

#pragma once

#include <atomic>
#include <cstdint>
#include <esp_err.h>
#include <functional>

namespace gate::ota {

class OtaUpdater {
public:
    // Outcome callback; runs on the gate_ota task right before it
    // exits. `error` is a static string when !success.
    using DoneCallback = std::function<void(bool success, const char* error)>;
    // Coarse progress for LED/logging; percent is 0..100.
    using ProgressCallback = std::function<void(std::uint8_t percent)>;

    struct Config {
        const char* manifest_url = "";
        const char* running_version = "";
        // 32-byte ed25519 public key, hex-encoded (64 chars), or ""
        // until the Phase 4.8 deployment keypair exists.
        const char* pubkey_hex = "";
    };

    explicit OtaUpdater(const Config& cfg) : cfg_(cfg) {}
    OtaUpdater(const OtaUpdater&) = delete;
    OtaUpdater& operator=(const OtaUpdater&) = delete;

    // Kick off an update attempt. Returns ESP_ERR_INVALID_STATE when
    // one is already running, ESP_ERR_NO_MEM if the task can't spawn.
    esp_err_t begin(DoneCallback done, ProgressCallback progress = {});

    [[nodiscard]] bool in_progress() const { return running_.load(std::memory_order_relaxed); }

private:
    static void task_entry(void* arg);
    void run();               // fetch → validate → download → verify → finish
    const char* run_inner();  // returns nullptr on success, else the error

    Config cfg_;
    DoneCallback done_;
    ProgressCallback progress_;
    std::atomic<bool> running_{false};
};

}  // namespace gate::ota
