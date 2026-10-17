// ota_manifest.hpp — OTA manifest model + validation (pure C++20).
//
// ADR-009: updates come from a self-hosted static file server as a
// JSON manifest plus an image binary. The manifest schema (all fields
// required unless noted):
//
//   {
//     "target_version":    "0.2.0",
//     "image_url":         "http://192.168.1.10:8080/fw/gate_firmware.bin",
//     "image_size":        482944,
//     "image_sha256":      "<64 hex chars>",
//     "ed25519_signature": "<128 hex chars>",   // over signed_message() below
//     "signing_key_id":    "deploy-2026",       // optional, audit only
//     "min_uptime_sec":    60                   // optional, default 0
//   }
//
// What is signed (scheme "gate-ota/v1") is not the bare digest but a
// statement binding the image to its version and size:
//
//     "gate-ota/v1\n" <target_version> "\n" <image_size> "\n" <sha256 hex, lowercase> "\n"
//
// A digest-only signature let anyone replay an old, validly signed
// image under a new version label. Now the version is signed, and
// validate() refuses any target_version that is not newer than the
// running one, so a captured manifest cannot be used to downgrade a gate.
//
// JSON extraction (cJSON) happens on-target in ota_updater.cpp; this
// header holds the extracted struct, the hex decoding, and every
// accept/reject rule — pure logic, host-tested in tests/ota_manifest/.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gate::ota {

struct Manifest {
    char target_version[24] = {};
    char image_url[128] = {};
    std::uint32_t image_size = 0;
    std::array<std::uint8_t, 32> image_sha256{};
    bool has_sha256 = false;
    std::array<std::uint8_t, 64> signature{};
    bool has_signature = false;
    char signing_key_id[32] = {};
    std::uint32_t min_uptime_sec = 0;
};

// Decode exactly `out_len` bytes of lowercase/uppercase hex into out.
// Returns false on odd/short/long input or a non-hex character.
bool hex_decode(const char* hex, std::uint8_t* out, std::size_t out_len) noexcept;

// Semantic-version precedence of a vs b: <0, 0 or >0. Accepts
// MAJOR.MINOR.PATCH with an optional "-prerelease" (lower than the
// release) and "+build" (ignored, per SemVer 2.0). `ok` is false if
// either string does not parse.
int compare_versions(const char* a, const char* b, bool& ok) noexcept;

// Writes the signed statement for `m` (see the header comment) into
// `out` and returns its length, or 0 if it does not fit in `cap`.
std::size_t signed_message(const Manifest& m, char* out, std::size_t cap) noexcept;

// Accept/reject rules. Returns nullptr when the manifest is
// acceptable, else a static human-readable reason (which lands in the
// CommandAck error_text). `partition_size` is the passive OTA slot;
// `uptime_sec` implements the manifest's own min_uptime gate
// (production safety: refuse to update a device that is crash-looping
// its way through short uptimes).
const char* validate(const Manifest& m, const char* running_version, std::uint32_t partition_size,
                     std::uint32_t uptime_sec) noexcept;

}  // namespace gate::ota
