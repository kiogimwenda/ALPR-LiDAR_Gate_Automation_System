// submit_client.hpp — gRPC client for SubmitDetection (Phase 5.2).
//
// The gate-vision daemon's one wire dependency: push a DetectionFrame
// to FieldControllerService.SubmitDetection and hand the returned
// AuthDecision back to the caller for logging. Unary with a deadline
// plus bounded retry/backoff on UNAVAILABLE — the server restarting
// must not kill the vision daemon, but a frame is stale within seconds
// so retries are few and short, never unbounded.
//
// Transport security follows the sim client's Phase 4.10.1 ladder
// exactly: no CA configured → plaintext (allowed for bench setups but
// loudly logged); CA set → TLS with server verification; cert + key
// also set → client identity for mTLS. One hardening beyond the sim:
// any *configured* PEM path that cannot be read makes the constructor
// throw. A client that silently degraded to plaintext after a cert
// typo would defeat the site PKI — fail closed, repo non-negotiable.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "gate_service.grpc.pb.h"

namespace gate::vision {

class SubmitClient {
public:
    struct Config {
        std::string server = "127.0.0.1:50051";
        // Identity, for logs only: the frame carries gate_id; the
        // server stamps its own site scope (see SubmitDetection impl).
        std::string site_id = "site-a";
        std::string gate_id = "gate-01";

        std::uint32_t deadline_ms = 2'000;       // Per-attempt RPC deadline
        std::uint32_t max_attempts = 3;          // Total tries incl. the first
        std::uint32_t backoff_initial_ms = 200;  // Doubles per retry

        // Transport security (Phase 4.10.1 ladder — see header note).
        std::string tls_ca_path;
        std::string tls_cert_path;
        std::string tls_key_path;
    };

    // Builds credentials + channel. Throws std::runtime_error on any
    // unreadable configured PEM, or a cert/key pair with one half
    // missing, or a client cert configured without a CA (all of those
    // would otherwise silently weaken the transport).
    explicit SubmitClient(Config cfg);

    // Sends the frame; retries UNAVAILABLE with exponential backoff up
    // to cfg.max_attempts. Returns the server's AuthDecision on success;
    // throws std::runtime_error once the attempt budget is exhausted or
    // on any non-retryable status.
    gate::v1::AuthDecision submit(const gate::v1::DetectionFrame& frame);

    const Config& config() const { return cfg_; }

private:
    Config cfg_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<gate::v1::FieldControllerService::Stub> stub_;
};

}  // namespace gate::vision
