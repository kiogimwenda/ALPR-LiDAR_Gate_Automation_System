// server.cpp

#include "rpc/server.hpp"

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/server_builder.h>

namespace gate::rpc {

Server::Server(gate::auth::AllowlistStore& store, gate::fusion::FusionEngine& fusion,
               gate::dash::EventBroadcaster& bus, ServerConfig cfg)
    : cfg_(std::move(cfg)), admin_(store), dashboard_(bus, fusion, cfg_.default_site_id) {
    dashboard_.set_subscribe_poll_interval(cfg_.subscribe_poll_interval);
}

Server::~Server() {
    if (server_)
        shutdown();
}

bool Server::start() {
    grpc::EnableDefaultHealthCheckService(true);
    grpc::ServerBuilder builder;
    int selected_port = 0;
    builder.AddListeningPort(cfg_.listen_address, grpc::InsecureServerCredentials(),
                             &selected_port);
    builder.RegisterService(&admin_);
    builder.RegisterService(&dashboard_);
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
