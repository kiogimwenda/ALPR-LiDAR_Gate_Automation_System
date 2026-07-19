// server.cpp

#include "rpc/server.hpp"

#include <fstream>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>
#include <sstream>

namespace gate::rpc {

namespace {

// Read a PEM file whole; empty result means unreadable (the caller
// treats that as a hard configuration error — a server that silently
// fell back to plaintext after a cert typo would be worse than one
// that refuses to start).
std::string read_pem(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::shared_ptr<grpc::ServerCredentials> make_credentials(const TlsConfig& tls, bool& ok) {
    ok = true;
    if (!tls.enabled())
        return grpc::InsecureServerCredentials();

    grpc::SslServerCredentialsOptions opts(
        tls.require_client_cert ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
        : tls.ca_path.empty()   ? GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE
                                : GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
    const auto cert = read_pem(tls.cert_path);
    const auto key = read_pem(tls.key_path);
    if (cert.empty() || key.empty()) {
        ok = false;
        return nullptr;
    }
    if (!tls.ca_path.empty()) {
        opts.pem_root_certs = read_pem(tls.ca_path);
        if (opts.pem_root_certs.empty()) {
            ok = false;
            return nullptr;
        }
    }
    opts.pem_key_cert_pairs.push_back({key, cert});
    return grpc::SslServerCredentials(opts);
}

}  // namespace

Server::Server(gate::auth::AllowlistStore& store, gate::fusion::FusionEngine& fusion,
               gate::dash::EventBroadcaster& bus, ServerConfig cfg)
    : cfg_(std::move(cfg)),
      admin_(store),
      dashboard_(bus, fusion, cfg_.default_site_id),
      field_(bus, fusion, cfg_.default_site_id) {
    dashboard_.set_subscribe_poll_interval(cfg_.subscribe_poll_interval);
    field_.set_subscribe_poll_interval(cfg_.subscribe_poll_interval);
}

Server::~Server() {
    if (server_)
        shutdown();
}

bool Server::start() {
    grpc::EnableDefaultHealthCheckService(true);
    grpc::ServerBuilder builder;
    int selected_port = 0;
    bool creds_ok = false;
    auto credentials = make_credentials(cfg_.tls, creds_ok);
    if (!creds_ok) {
        return false;  // unreadable cert/key/CA — refuse to start, never downgrade
    }
    builder.AddListeningPort(cfg_.listen_address, std::move(credentials), &selected_port);
    builder.RegisterService(&admin_);
    builder.RegisterService(&dashboard_);
    builder.RegisterService(&field_);
    server_ = builder.BuildAndStart();
    if (!server_)
        return false;
    if (selected_port > 0) {
        // Resolve "0.0.0.0:0" to the actual bound port for tests.
        const auto colon = cfg_.listen_address.rfind(':');
        const auto host =
            colon == std::string::npos ? cfg_.listen_address : cfg_.listen_address.substr(0, colon);
        bound_address_ = host + ":" + std::to_string(selected_port);
    } else {
        bound_address_ = cfg_.listen_address;
    }
    return true;
}

void Server::wait() {
    if (server_)
        server_->Wait();
}

void Server::shutdown(std::chrono::milliseconds deadline) {
    if (!server_)
        return;
    const auto t = std::chrono::system_clock::now() + deadline;
    server_->Shutdown(t);
    server_.reset();
}

std::string Server::bound_address() const {
    return bound_address_;
}

}  // namespace gate::rpc
