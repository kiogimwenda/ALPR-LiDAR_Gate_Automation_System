// ota_manifest_test.cpp — hex decoding and the OTA accept/reject rules.

#include "gate_ota/ota_manifest.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>

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

TEST_CASE("versions compare by SemVer precedence") {
    bool ok = false;
    CHECK(ota::compare_versions("0.2.0", "0.1.9", ok) > 0);
    CHECK(ok);
    CHECK(ota::compare_versions("0.10.0", "0.9.0", ok) > 0);         // numeric, not lexical
    CHECK(ota::compare_versions("1.0.0", "1.0.0+abc123", ok) == 0);  // build metadata ignored
    CHECK(ota::compare_versions("1.0.0-rc1", "1.0.0", ok) < 0);      // prerelease < release
    CHECK(ota::compare_versions("1.0.0-rc2", "1.0.0-rc1", ok) > 0);
    CHECK(ota::compare_versions("0.0.1+deadbee", "0.0.2", ok) < 0);
    for (const char* bad : {"", "1", "1.2", "1.2.x", "v1.2.3", "1.2.3-", "1.2.3 "}) {
        INFO(bad);
        ota::compare_versions(bad, "1.0.0", ok);
        CHECK_FALSE(ok);
    }
}

TEST_CASE("older target versions are refused (anti-rollback)") {
    auto m = good();  // 0.2.0
    const char* err = ota::validate(m, "0.3.1", kSlotSize, 120);
    REQUIRE(err != nullptr);
    CHECK(std::strstr(err, "downgrade") != nullptr);
    CHECK(ota::validate(m, "0.2.0+abc", kSlotSize, 120) != nullptr);  // same release
    CHECK(ota::validate(m, "0.1.0+abc", kSlotSize, 120) == nullptr);
    std::strcpy(m.target_version, "latest");
    CHECK(ota::validate(m, "0.1.0", kSlotSize, 120) != nullptr);
}

TEST_CASE("the signed statement binds version, size and digest") {
    auto m = good();
    for (std::size_t i = 0; i < m.image_sha256.size(); ++i) {
        m.image_sha256[i] = static_cast<std::uint8_t>(i * 7);
    }
    char buf[160];
    const std::size_t n = ota::signed_message(m, buf, sizeof(buf));
    REQUIRE(n > 0);
    const std::string expect =
        "gate-ota/v1\n0.2.0\n480000\n"
        "00070e151c232a31383f464d545b626970777e858c939aa1a8afb6bdc4cbd2d9\n";
    CHECK(std::string(buf, n) == expect);
    char tiny[16];
    CHECK(ota::signed_message(m, tiny, sizeof(tiny)) == 0);
}

#ifdef GATE_SOURCE_DIR
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

// The release scripts and the firmware must agree byte-for-byte on what
// is signed. Sign a dummy image with the shipped scripts, rebuild the
// statement with signed_message() from the manifest JSON fields, and
// check the signature with openssl against the generated public key.
TEST_CASE("sign-ota-manifest.sh signs exactly signed_message()", "[ota][scripts]") {
    namespace fs = std::filesystem;
    const fs::path dir =
        fs::temp_directory_path() / ("gate-ota-" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    {
        std::ofstream bin{dir / "fw.bin", std::ios::binary};
        for (int i = 0; i < 4096; ++i) {
            bin.put(static_cast<char>(i * 31));
        }
    }
    const std::string src = GATE_SOURCE_DIR;
    const std::string d = dir.string();
    const std::string sign =
        "bash '" + src + "/scripts/gen-ota-keys.sh' '" + d + "/keys' >/dev/null 2>&1 && bash '" +
        src + "/scripts/sign-ota-manifest.sh' --bin '" + d + "/fw.bin' --version 1.4.2 --url " +
        "http://h/fw.bin --key '" + d + "/keys/ota-signing.key' --out '" + d +
        "/manifest.json' >/dev/null 2>&1";
    REQUIRE(std::system(sign.c_str()) == 0);

    std::ifstream in{dir / "manifest.json"};
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string json = ss.str();
    auto field = [&](const std::string& key) {
        const auto k = json.find("\"" + key + "\"");
        REQUIRE(k != std::string::npos);
        auto v = json.find(':', k) + 1;
        while (json[v] == ' ' || json[v] == '"') {
            ++v;
        }
        auto e = v;
        while (e < json.size() && json[e] != '"' && json[e] != ',' && json[e] != '\n') {
            ++e;
        }
        return json.substr(v, e - v);
    };

    ota::Manifest m;
    std::strcpy(m.target_version, field("target_version").c_str());
    m.image_size = static_cast<std::uint32_t>(std::stoul(field("image_size")));
    REQUIRE(ota::hex_decode(field("image_sha256").c_str(), m.image_sha256.data(), 32));
    REQUIRE(ota::hex_decode(field("ed25519_signature").c_str(), m.signature.data(), 64));
    char msg[160];
    const std::size_t n = ota::signed_message(m, msg, sizeof(msg));
    REQUIRE(n > 0);
    {
        std::ofstream f{dir / "msg.bin", std::ios::binary};
        f.write(msg, static_cast<std::streamsize>(n));
        std::ofstream s{dir / "sig.bin", std::ios::binary};
        s.write(reinterpret_cast<const char*>(m.signature.data()), 64);
    }
    const std::string verify = "openssl pkeyutl -verify -rawin -pubin -inkey '" + d +
                               "/keys/ota-signing.pub' -in '" + d + "/msg.bin' -sigfile '" + d +
                               "/sig.bin' >/dev/null 2>&1";
    CHECK(std::system(verify.c_str()) == 0);

    // A relabelled version must not verify under the same signature.
    std::strcpy(m.target_version, "9.9.9");
    const std::size_t n2 = ota::signed_message(m, msg, sizeof(msg));
    {
        std::ofstream f{dir / "msg.bin", std::ios::binary | std::ios::trunc};
        f.write(msg, static_cast<std::streamsize>(n2));
    }
    CHECK(std::system(verify.c_str()) != 0);
    fs::remove_all(dir);
}
#endif
