// ota_manifest.cpp — hex decoding and the manifest accept/reject rules.

#include "gate_ota/ota_manifest.hpp"

#include <cstdio>
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

// Parses MAJOR.MINOR.PATCH[-pre][+build]; `pre` points at the
// prerelease text (empty if none), `pre_len` its length.
bool parse_semver(const char* v, unsigned long (&num)[3], const char*& pre,
                  std::size_t& pre_len) noexcept {
    if (v == nullptr) {
        return false;
    }
    const char* p = v;
    for (int i = 0; i < 3; ++i) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        unsigned long n = 0;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + static_cast<unsigned long>(*p - '0');
            if (n > 999'999UL) {
                return false;
            }
            ++p;
        }
        num[i] = n;
        if (i < 2) {
            if (*p != '.') {
                return false;
            }
            ++p;
        }
    }
    pre = "";
    pre_len = 0;
    if (*p == '-') {
        pre = ++p;
        while (*p != '\0' && *p != '+') {
            ++p;
        }
        pre_len = static_cast<std::size_t>(p - pre);
        if (pre_len == 0) {
            return false;
        }
    }
    return *p == '\0' || *p == '+';
}

}  // namespace

int compare_versions(const char* a, const char* b, bool& ok) noexcept {
    unsigned long na[3] = {}, nb[3] = {};
    const char* pa = nullptr;
    const char* pb = nullptr;
    std::size_t la = 0, lb = 0;
    ok = parse_semver(a, na, pa, la) && parse_semver(b, nb, pb, lb);
    if (!ok) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        if (na[i] != nb[i]) {
            return na[i] < nb[i] ? -1 : 1;
        }
    }
    if (la == 0 || lb == 0) {
        return la == lb ? 0 : (la == 0 ? 1 : -1);  // release > prerelease
    }
    const int c = std::strncmp(pa, pb, la < lb ? la : lb);
    if (c != 0) {
        return c < 0 ? -1 : 1;
    }
    return la == lb ? 0 : (la < lb ? -1 : 1);
}

std::size_t signed_message(const Manifest& m, char* out, std::size_t cap) noexcept {
    char hex[65];
    static constexpr char kDigits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < m.image_sha256.size(); ++i) {
        hex[2 * i] = kDigits[m.image_sha256[i] >> 4];
        hex[2 * i + 1] = kDigits[m.image_sha256[i] & 0x0F];
    }
    hex[64] = '\0';
    const int n = std::snprintf(out, cap, "gate-ota/v1\n%s\n%lu\n%s\n", m.target_version,
                                static_cast<unsigned long>(m.image_size), hex);
    if (n <= 0 || static_cast<std::size_t>(n) >= cap) {
        return 0;
    }
    return static_cast<std::size_t>(n);
}

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
    bool parsed = false;
    const int order = compare_versions(m.target_version, running_version, parsed);
    if (!parsed) {
        return "manifest: target_version or running version is not MAJOR.MINOR.PATCH";
    }
    if (order == 0) {
        return "manifest: target_version equals running version";  // differs only in +build
    }
    if (order < 0) {
        // Anti-rollback: a captured (validly signed) older manifest must
        // not be able to walk a gate back to firmware with known bugs.
        return "manifest: target_version is older than running version — downgrade refused";
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
