// ota_manifest.cpp — hex decoding and the manifest accept/reject rules.

#include "gate_ota/ota_manifest.hpp"

#include <cstring>

namespace gate::ota {

namespace {

int hex_nibble(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

bool hex_decode(const char* hex, std::uint8_t* out, std::size_t out_len) noexcept {
    if (hex == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < out_len; ++i) {
        const int hi = hex_nibble(hex[2 * i]);
        if (hi < 0) {
            return false;  // covers early NUL too — nibble of '\0' is -1
        }
        const int lo = hex_nibble(hex[2 * i + 1]);
        if (lo < 0) {
            return false;
        }
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return hex[2 * out_len] == '\0';  // no trailing garbage
}

const char* validate(const Manifest& m, const char* running_version, std::uint32_t partition_size,
                     std::uint32_t uptime_sec) noexcept {
    if (m.target_version[0] == '\0') {
        return "manifest: target_version missing";
    }
    if (std::strcmp(m.target_version, running_version) == 0) {
        // Idempotence guard: re-flashing the running version wastes a
        // flash cycle and a reboot for zero change.
        return "manifest: target_version equals running version";
    }
    if (std::strncmp(m.image_url, "http://", 7) != 0 &&
        std::strncmp(m.image_url, "https://", 8) != 0) {
        return "manifest: image_url missing or not http(s)";
    }
    if (m.image_size == 0) {
        return "manifest: image_size missing or zero";
    }
    if (m.image_size > partition_size) {
        return "manifest: image larger than the OTA partition";
    }
    if (!m.has_sha256) {
        return "manifest: image_sha256 missing or malformed";
    }
    if (!m.has_signature) {
        return "manifest: ed25519_signature missing or malformed";
    }
    if (uptime_sec < m.min_uptime_sec) {
        return "manifest: device uptime below min_uptime_sec gate";
    }
    return nullptr;
}

}  // namespace gate::ota
