// ota_manifest_test.cpp — hex decoding and the OTA accept/reject rules.

#include "gate_ota/ota_manifest.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

namespace ota = gate::ota;

namespace {

constexpr std::uint32_t kSlotSize = 0x180000;  // matches partitions.csv ota_1

// A manifest that passes every rule; tests break one field at a time.
ota::Manifest good() {
    ota::Manifest m;
    std::strcpy(m.target_version, "0.2.0");
    std::strcpy(m.image_url, "http://192.168.1.10:8080/fw/gate_firmware.bin");
    m.image_size = 480'000;
    m.has_sha256 = true;
    m.has_signature = true;
    m.min_uptime_sec = 60;
    return m;
}

}  // namespace

TEST_CASE("hex_decode round-trips and rejects malformed input") {
    std::uint8_t out[4] = {};
    REQUIRE(ota::hex_decode("DEadBE0f", out, 4));
    CHECK(out[0] == 0xDE);
    CHECK(out[1] == 0xAD);
    CHECK(out[2] == 0xBE);
    CHECK(out[3] == 0x0F);

    CHECK_FALSE(ota::hex_decode(nullptr, out, 4));
    CHECK_FALSE(ota::hex_decode("DEadBE", out, 4));      // too short
    CHECK_FALSE(ota::hex_decode("DEadBE0f00", out, 4));  // trailing garbage
    CHECK_FALSE(ota::hex_decode("DEadBEzz", out, 4));    // non-hex char
}

TEST_CASE("a fully-populated manifest for a new version is accepted") {
    const auto m = good();
    CHECK(ota::validate(m, "0.1.0", kSlotSize, 120) == nullptr);
}

TEST_CASE("re-flashing the running version is refused") {
    const auto m = good();
    const char* err = ota::validate(m, "0.2.0", kSlotSize, 120);
    REQUIRE(err != nullptr);
    CHECK(std::strstr(err, "equals running") != nullptr);
}

TEST_CASE("missing or non-http image_url is refused") {
    auto m = good();
    m.image_url[0] = '\0';
    CHECK(ota::validate(m, "0.1.0", kSlotSize, 120) != nullptr);
    std::strcpy(m.image_url, "ftp://server/fw.bin");
    CHECK(ota::validate(m, "0.1.0", kSlotSize, 120) != nullptr);
    std::strcpy(m.image_url, "https://server/fw.bin");
    CHECK(ota::validate(m, "0.1.0", kSlotSize, 120) == nullptr);
}

TEST_CASE("image larger than the passive slot is refused") {
    auto m = good();
    m.image_size = kSlotSize + 1;
    const char* err = ota::validate(m, "0.1.0", kSlotSize, 120);
    REQUIRE(err != nullptr);
    CHECK(std::strstr(err, "larger than") != nullptr);
    m.image_size = kSlotSize;  // exactly full slot is legal
    CHECK(ota::validate(m, "0.1.0", kSlotSize, 120) == nullptr);
}

TEST_CASE("missing digest or signature is refused") {
    auto m = good();
    m.has_sha256 = false;
    CHECK(ota::validate(m, "0.1.0", kSlotSize, 120) != nullptr);
    m.has_sha256 = true;
    m.has_signature = false;
    CHECK(ota::validate(m, "0.1.0", kSlotSize, 120) != nullptr);
}

TEST_CASE("uptime below the manifest's min_uptime gate is refused") {
    const auto m = good();  // min_uptime_sec = 60
    const char* err = ota::validate(m, "0.1.0", kSlotSize, 59);
    REQUIRE(err != nullptr);
    CHECK(std::strstr(err, "uptime") != nullptr);
    CHECK(ota::validate(m, "0.1.0", kSlotSize, 60) == nullptr);
}
