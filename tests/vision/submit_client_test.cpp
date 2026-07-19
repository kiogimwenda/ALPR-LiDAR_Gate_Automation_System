// submit_client_test.cpp — the credentials ladder fails closed.
//
// No live gRPC here (the wire is the next milestone's E2E pass); these
// tests pin the constructor's refusal paths — every way an operator
// typo could silently weaken the transport must throw instead.

#include "vision/submit_client.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using gate::vision::SubmitClient;

using Catch::Matchers::ContainsSubstring;

namespace {

// Self-cleaning PEM fixture. Contents don't need to be a valid cert —
// the constructor only reads bytes; gRPC parses them at handshake time,
// which no unit test reaches.
class TempPem {
public:
    explicit TempPem(const std::string& name, const std::string& body = "-----BEGIN-----\nx\n") {
        path_ = std::filesystem::temp_directory_path() / name;
        std::ofstream out(path_);
        out << body;
    }
    ~TempPem() { std::filesystem::remove(path_); }
    std::string str() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("no TLS config builds a plaintext client", "[vision][submit_client]") {
    SubmitClient::Config cfg;
    // Plaintext is legal (dev/bench) — construction must succeed; the
    // loud spdlog warning is the posture, not an error.
    SubmitClient client{cfg};
    CHECK(client.config().server == "127.0.0.1:50051");
}

TEST_CASE("readable CA (and cert pair) builds a TLS client", "[vision][submit_client]") {
    TempPem ca("gate_vision_test_ca.pem");
    SubmitClient::Config cfg;
    cfg.tls_ca_path = ca.str();
    CHECK_NOTHROW(SubmitClient{cfg});

    TempPem cert("gate_vision_test_cert.pem");
    TempPem key("gate_vision_test_key.pem");
    cfg.tls_cert_path = cert.str();
    cfg.tls_key_path = key.str();
    CHECK_NOTHROW(SubmitClient{cfg});
}

TEST_CASE("unreadable configured PEMs refuse to construct",
          "[vision][submit_client][fail-closed]") {
    SubmitClient::Config cfg;
    cfg.tls_ca_path = "/nonexistent/ca.pem";
    CHECK_THROWS_WITH(SubmitClient{cfg}, ContainsSubstring("cannot read CA PEM"));

    TempPem ca("gate_vision_test_ca2.pem");
    cfg.tls_ca_path = ca.str();
    cfg.tls_cert_path = "/nonexistent/cert.pem";
    cfg.tls_key_path = "/nonexistent/key.pem";
    CHECK_THROWS_WITH(SubmitClient{cfg}, ContainsSubstring("cannot read client cert PEM"));
}

TEST_CASE("an empty PEM is as refused as a missing one", "[vision][submit_client][fail-closed]") {
    // A zero-byte file (truncated copy, interrupted provisioning) would
    // make gRPC negotiate with no root — the exact silent downgrade the
    // ladder forbids.
    TempPem ca("gate_vision_test_empty_ca.pem", "");
    SubmitClient::Config cfg;
    cfg.tls_ca_path = ca.str();
    CHECK_THROWS_WITH(SubmitClient{cfg}, ContainsSubstring("empty CA PEM"));
}

TEST_CASE("half a client identity is refused", "[vision][submit_client][fail-closed]") {
    TempPem ca("gate_vision_test_ca3.pem");
    TempPem cert("gate_vision_test_cert3.pem");

    SubmitClient::Config cfg;
    cfg.tls_ca_path = ca.str();
    cfg.tls_cert_path = cert.str();  // key deliberately unset
    CHECK_THROWS_WITH(SubmitClient{cfg}, ContainsSubstring("must be set together"));
}

TEST_CASE("a client cert without a CA is refused", "[vision][submit_client][fail-closed]") {
    TempPem cert("gate_vision_test_cert4.pem");
    TempPem key("gate_vision_test_key4.pem");

    SubmitClient::Config cfg;  // tls_ca_path deliberately unset
    cfg.tls_cert_path = cert.str();
    cfg.tls_key_path = key.str();
    CHECK_THROWS_WITH(SubmitClient{cfg}, ContainsSubstring("without tls_ca_path"));
}
