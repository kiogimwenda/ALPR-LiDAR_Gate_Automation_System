// ota_updater.cpp — fetch → validate → download → verify → finish.

#include "gate_ota/ota_updater.hpp"

#include <algorithm>
#include <cJSON.h>
#include <cstdlib>
#include <cstring>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sodium.h>

#include "gate_ota/ota_manifest.hpp"

namespace gate::ota {

namespace {

constexpr const char* kTag = "gate-ota";

constexpr std::uint32_t kTaskStackBytes = 8192;
constexpr UBaseType_t kTaskPriority = 3;  // below gate_rpc (4) and gate_ctrl (5)
constexpr int kHttpTimeoutMs = 10'000;
constexpr std::size_t kManifestMax = 2048;
constexpr std::size_t kReadbackChunk = 4096;

// GET the manifest into a NUL-terminated heap buffer. Returns nullptr
// on success and fills *out (caller frees), else a static error.
// `ca_pem` verifies https URLs (ignored for plain http).
const char* fetch_json(const char* url, const char* ca_pem, char** out) {
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.cert_pem = ca_pem;
    cfg.timeout_ms = kHttpTimeoutMs;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        return "manifest: http client init failed";
    }
    const char* err = nullptr;
    char* buf = nullptr;
    do {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            err = "manifest: connection failed";
            break;
        }
        (void)esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGW(kTag, "manifest GET returned %d", status);
            err = "manifest: http status not 200";
            break;
        }
        buf = static_cast<char*>(std::malloc(kManifestMax));
        if (buf == nullptr) {
            err = "manifest: out of memory";
            break;
        }
        const int n = esp_http_client_read_response(client, buf, kManifestMax - 1);
        if (n <= 0) {
            err = "manifest: empty response";
            break;
        }
        buf[n] = '\0';
    } while (false);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != nullptr) {
        std::free(buf);
        return err;
    }
    *out = buf;
    return nullptr;
}

void copy_json_str(char* dst, std::size_t cap, const cJSON* item) {
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
        const std::size_t n = strnlen(item->valuestring, cap - 1);
        std::memcpy(dst, item->valuestring, n);
        dst[n] = '\0';
    }
}

const char* parse_manifest(const char* json, Manifest& m) {
    cJSON* root = cJSON_Parse(json);
    if (root == nullptr) {
        return "manifest: invalid JSON";
    }
    copy_json_str(m.target_version, sizeof(m.target_version),
                  cJSON_GetObjectItemCaseSensitive(root, "target_version"));
    copy_json_str(m.image_url, sizeof(m.image_url),
                  cJSON_GetObjectItemCaseSensitive(root, "image_url"));
    copy_json_str(m.signing_key_id, sizeof(m.signing_key_id),
                  cJSON_GetObjectItemCaseSensitive(root, "signing_key_id"));
    const cJSON* size = cJSON_GetObjectItemCaseSensitive(root, "image_size");
    if (cJSON_IsNumber(size) && size->valuedouble > 0) {
        m.image_size = static_cast<std::uint32_t>(size->valuedouble);
    }
    const cJSON* uptime = cJSON_GetObjectItemCaseSensitive(root, "min_uptime_sec");
    if (cJSON_IsNumber(uptime) && uptime->valuedouble >= 0) {
        m.min_uptime_sec = static_cast<std::uint32_t>(uptime->valuedouble);
    }
    const cJSON* sha = cJSON_GetObjectItemCaseSensitive(root, "image_sha256");
    if (cJSON_IsString(sha) && sha->valuestring != nullptr) {
        m.has_sha256 = hex_decode(sha->valuestring, m.image_sha256.data(), m.image_sha256.size());
    }
    const cJSON* sig = cJSON_GetObjectItemCaseSensitive(root, "ed25519_signature");
    if (cJSON_IsString(sig) && sig->valuestring != nullptr) {
        m.has_signature = hex_decode(sig->valuestring, m.signature.data(), m.signature.size());
    }
    cJSON_Delete(root);
    return nullptr;
}

// SHA-256 the first `len` bytes of the freshly-written passive
// partition. Readback-from-flash (rather than hash-while-downloading)
// verifies what was actually written, catching flash write corruption
// for free.
const char* readback_sha256(const esp_partition_t* part, std::uint32_t len,
                            std::uint8_t digest[32]) {
    auto* buf = static_cast<std::uint8_t*>(std::malloc(kReadbackChunk));
    if (buf == nullptr) {
        return "verify: out of memory";
    }
    crypto_hash_sha256_state st;
    crypto_hash_sha256_init(&st);
    const char* err = nullptr;
    for (std::uint32_t off = 0; off < len;) {
        const std::uint32_t n = std::min<std::uint32_t>(kReadbackChunk, len - off);
        if (esp_partition_read(part, off, buf, n) != ESP_OK) {
            err = "verify: partition readback failed";
            break;
        }
        crypto_hash_sha256_update(&st, buf, n);
        off += n;
    }
    std::free(buf);
    if (err == nullptr) {
        crypto_hash_sha256_final(&st, digest);
    }
    return err;
}

}  // namespace

esp_err_t OtaUpdater::begin(DoneCallback done, ProgressCallback progress) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return ESP_ERR_INVALID_STATE;
    }
    done_ = std::move(done);
    progress_ = std::move(progress);
    if (xTaskCreate(&OtaUpdater::task_entry, "gate_ota", kTaskStackBytes, this, kTaskPriority,
                    nullptr) != pdPASS) {
        running_.store(false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void OtaUpdater::task_entry(void* arg) {
    static_cast<OtaUpdater*>(arg)->run();
    vTaskDelete(nullptr);
}

void OtaUpdater::run() {
    const char* err = run_inner();
    if (err == nullptr) {
        ESP_LOGI(kTag, "update staged — passive slot is now the boot slot");
    } else {
        ESP_LOGE(kTag, "update failed: %s", err);
    }
    if (done_) {
        done_(err == nullptr, err == nullptr ? "" : err);
    }
    running_.store(false);
}

const char* OtaUpdater::run_inner() {
    // 1. Manifest.
    char* json = nullptr;
    const char* err = fetch_json(cfg_.manifest_url, cfg_.server_ca_pem, &json);
    if (err != nullptr) {
        return err;
    }
    Manifest m;
    err = parse_manifest(json, m);
    std::free(json);
    if (err != nullptr) {
        return err;
    }

    // 2. Accept/reject rules (pure; tests/ota_manifest/).
    const esp_partition_t* passive = esp_ota_get_next_update_partition(nullptr);
    if (passive == nullptr) {
        return "no passive OTA partition";
    }
    const auto uptime_sec = static_cast<std::uint32_t>(esp_timer_get_time() / 1'000'000LL);
    err = validate(m, cfg_.running_version, passive->size, uptime_sec);
    if (err != nullptr) {
        return err;
    }
    ESP_LOGI(kTag, "updating %s -> %s (%u bytes, key_id=%s)", cfg_.running_version,
             m.target_version, static_cast<unsigned>(m.image_size), m.signing_key_id);

    // 3. Stream the image into the passive slot.
    esp_http_client_config_t http_cfg = {};
    http_cfg.url = m.image_url;
    http_cfg.cert_pem = cfg_.server_ca_pem;
    http_cfg.timeout_ms = kHttpTimeoutMs;
    http_cfg.keep_alive_enable = true;
    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;
    esp_https_ota_handle_t handle = nullptr;
    if (esp_https_ota_begin(&ota_cfg, &handle) != ESP_OK) {
        return "download: esp_https_ota_begin failed";
    }
    std::uint8_t last_decile = 0;
    esp_err_t rv;
    while ((rv = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        const int got = esp_https_ota_get_image_len_read(handle);
        const auto pct =
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(got) * 100U) / m.image_size);
        if (pct / 10 != last_decile) {
            last_decile = pct / 10;
            ESP_LOGI(kTag, "download %u%%", static_cast<unsigned>(pct));
            if (progress_) {
                progress_(pct);
            }
        }
    }
    if (rv != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        esp_https_ota_abort(handle);
        return "download: transfer failed or truncated";
    }

    // 4. Checksum the bytes that actually hit flash.
    std::uint8_t digest[32];
    err = readback_sha256(passive, m.image_size, digest);
    if (err != nullptr) {
        esp_https_ota_abort(handle);
        return err;
    }
    if (std::memcmp(digest, m.image_sha256.data(), sizeof(digest)) != 0) {
        esp_https_ota_abort(handle);
        return "verify: image sha256 mismatch";
    }

    // 5. Signature over the digest (ADR-009). The deployment keypair
    // is generated in Phase 4.8; until a public key is provisioned we
    // proceed on the checksum alone — loudly.
    std::uint8_t pubkey[crypto_sign_PUBLICKEYBYTES];
    if (cfg_.pubkey_hex != nullptr && cfg_.pubkey_hex[0] != '\0') {
        if (!hex_decode(cfg_.pubkey_hex, pubkey, sizeof(pubkey))) {
            esp_https_ota_abort(handle);
            return "verify: provisioned public key is malformed";
        }
        if (crypto_sign_verify_detached(m.signature.data(), digest, sizeof(digest), pubkey) != 0) {
            esp_https_ota_abort(handle);
            return "verify: ed25519 signature rejected";
        }
        ESP_LOGI(kTag, "ed25519 signature verified (key_id=%s)", m.signing_key_id);
    } else {
        ESP_LOGW(kTag,
                 "no OTA public key provisioned — accepting on sha256 alone "
                 "(deployment keypair lands in Phase 4.8)");
    }

    // 6. Validate the image header and flip the boot slot.
    if (esp_https_ota_finish(handle) != ESP_OK) {
        return "finish: image validation failed";
    }
    return nullptr;
}

}  // namespace gate::ota
