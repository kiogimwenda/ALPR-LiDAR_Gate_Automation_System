// submit_client.cpp — credentials ladder + bounded-retry unary submit.

#include "vision/submit_client.hpp"

#include <chrono>
#include <fstream>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace gate::vision {

namespace {

// Unlike the sim's read_pem, an unreadable *configured* path throws
// here instead of returning empty: gRPC would accept the empty string
// and quietly negotiate a weaker posture than the operator configured.
std::string read_pem_or_throw(const std::string& path, const char* role) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(std::string("submit_client: cannot read ") + role +
                                 " PEM: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (ss.str().empty()) {
        throw std::runtime_error(std::string("submit_client: empty ") + role + " PEM: " + path);
    }
    return ss.str();
}

std::shared_ptr<grpc::ChannelCredentials> make_credentials(const SubmitClient::Config& cfg) {
    // Half a client identity, or an identity with no CA to verify the
    // server against, is always a config mistake — refuse rather than
    // guess which half the operator meant.
    if (cfg.tls_cert_path.empty() != cfg.tls_key_path.empty()) {
        throw std::runtime_error(
            "submit_client: tls_cert_path and tls_key_path must be set together");
    }
    if (cfg.tls_ca_path.empty()) {
        if (!cfg.tls_cert_path.empty()) {
            throw std::runtime_error(
                "submit_client: client cert configured without tls_ca_path (no server "
                "verification) — refusing");
        }
        spdlog::warn("submit_client: no tls_ca_path set — using PLAINTEXT channel to {}",
                     cfg.server);
        return grpc::InsecureChannelCredentials();
    }

    grpc::SslCredentialsOptions opts;
    opts.pem_root_certs = read_pem_or_throw(cfg.tls_ca_path, "CA");
    if (!cfg.tls_cert_path.empty()) {
        opts.pem_cert_chain = read_pem_or_throw(cfg.tls_cert_path, "client cert");
        opts.pem_private_key = read_pem_or_throw(cfg.tls_key_path, "client key");
        spdlog::info("submit_client: mTLS to {} (ca={}, cert={})", cfg.server, cfg.tls_ca_path,
                     cfg.tls_cert_path);
    } else {
        spdlog::info("submit_client: TLS to {} (ca={})", cfg.server, cfg.tls_ca_path);
    }
    return grpc::SslCredentials(opts);
}

}  // namespace

SubmitClient::SubmitClient(Config cfg) : cfg_(std::move(cfg)) {
    channel_ = grpc::CreateChannel(cfg_.server, make_credentials(cfg_));
    stub_ = gate::v1::FieldControllerService::NewStub(channel_);
}

gate::v1::AuthDecision SubmitClient::submit(const gate::v1::DetectionFrame& frame) {
    const std::uint32_t attempts = cfg_.max_attempts == 0 ? 1 : cfg_.max_attempts;
    std::chrono::milliseconds backoff{cfg_.backoff_initial_ms};
    grpc::Status status;

    for (std::uint32_t attempt = 1; attempt <= attempts; ++attempt) {
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() +
                         std::chrono::milliseconds(cfg_.deadline_ms));
        gate::v1::AuthDecision decision;
        status = stub_->SubmitDetection(&ctx, frame, &decision);
        if (status.ok()) {
            return decision;
        }
        // Only UNAVAILABLE is worth retrying (server restart, transient
        // network). Anything else — INVALID_ARGUMENT, DEADLINE_EXCEEDED
        // on a stale frame, auth failures — retrying cannot fix and a
        // fresh frame is seconds away anyway.
        if (status.error_code() != grpc::StatusCode::UNAVAILABLE || attempt == attempts) {
            break;
        }
        spdlog::warn(
            "submit_client: SubmitDetection frame_id={} UNAVAILABLE (attempt {}/{}), "
            "retrying in {} ms",
            frame.frame_id(), attempt, attempts, backoff.count());
        std::this_thread::sleep_for(backoff);
        backoff *= 2;
    }

    throw std::runtime_error(
        "submit_client: SubmitDetection failed for frame_id=" + std::to_string(frame.frame_id()) +
        ": [" + std::to_string(status.error_code()) + "] " + status.error_message());
}

}  // namespace gate::vision
