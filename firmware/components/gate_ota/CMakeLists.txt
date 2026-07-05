# components/gate_ota — self-hosted OTA per ADR-009.
#
# Manifest fetch (esp_http_client + cJSON), pure accept/reject rules
# (ota_manifest.*, host-tested in tests/ota_manifest/), image download
# via the esp_https_ota advanced API, flash readback SHA-256 +
# ed25519 manifest signature (libsodium), A/B slot flip with
# bootloader rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).

idf_component_register(
    SRCS
        "src/ota_manifest.cpp"
        "src/ota_updater.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        app_update       # esp_ota_ops
        esp_http_client
        esp_https_ota
        esp_partition
        esp_timer
        freertos
        cjson            # espressif/cjson managed component (left IDF core in v6)
        libsodium        # ed25519 verify + sha256 (managed component)
        log
)
