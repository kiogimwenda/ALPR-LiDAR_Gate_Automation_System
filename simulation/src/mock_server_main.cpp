// mock_server_main.cpp — gate-mock-server: a GPU-free stand-in for
// gate-server, wiring gate-sim fleets to the dashboard.
//
// The real gate-server needs CUDA/TensorRT (BUILD_SERVER gates on
// ENABLE_GPU), which makes frontend development and demos hostage to
// the GPU host. This mock implements exactly the RPC surface the rest
// of the system consumes — no inference, no fusion, no database:
//
//   FieldControllerService/Control   gate-sim (or real firmware)
//     telemetry/acks in → published as DashboardEvents; GateCommands
//     forwarded out to the addressed gate's live stream.
//   DashboardService/Subscribe       the dashboard backend's event feed
//   DashboardService/IssueCommand    browser buttons → the gate
//   AdminService                     in-memory allowlist CRUD
//
// Plus an optional synthetic AuthDecision every --decisions-period
// seconds so the feed looks alive without an ALPR camera.
//
//   gate-mock-server --listen 127.0.0.1:50051 &
//   gate-sim --server 127.0.0.1:50051 --gate-id gate-01 &
//   gate-dashboard --port 8080 --server 127.0.0.1:50051 --www …/build
//
// Demo-grade by intent: in-memory everything, no persistence, no auth.

#include <CLI/CLI.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <deque>
#include <grpcpp/grpcpp.h>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "gate_service.grpc.pb.h"

namespace {

void stamp_now(google::protobuf::Timestamp* ts) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    ts->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(now).count());
    ts->set_nanos(static_cast<std::int32_t>(
        (std::chrono::duration_cast<std::chrono::nanoseconds>(now) % std::chrono::seconds(1))
            .count()));
}

std::string fresh_id(const char* prefix) {
    static std::atomic<std::uint64_t> counter{1};
    return std::string(prefix) + "-" + std::to_string(counter++);
}

// ---------------------------------------------------------------------------
// EventBus — DashboardEvent fan-out with a small replay ring, the same
// contract the real broadcaster honours (since_event_id resume).
// ---------------------------------------------------------------------------

class EventBus {
public:
    static constexpr std::size_t kRing = 128;

    void publish(gate::v1::DashboardEvent ev) {
        const std::lock_guard<std::mutex> lock(mutex_);
        ev.set_event_id(next_id_++);
        stamp_now(ev.mutable_event_ts());
        ring_.push_back(ev);
        while (ring_.size() > kRing) {
            ring_.pop_front();
        }
        for (auto& [id, sub] : subscribers_) {
            sub->queue.push_back(ring_.back());
            sub->cv.notify_one();
        }
    }

    struct Subscriber {
        std::mutex m;
        std::condition_variable cv;
        std::deque<gate::v1::DashboardEvent> queue;
    };

    std::pair<std::uint64_t, std::shared_ptr<Subscriber>> subscribe(std::uint64_t since) {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto sub = std::make_shared<Subscriber>();
        for (const auto& ev : ring_) {
            if (ev.event_id() > since) {
                sub->queue.push_back(ev);
            }
        }
        const auto id = next_sub_id_++;
        subscribers_[id] = sub;
        return {id, sub};
    }

    void unsubscribe(std::uint64_t id) {
        const std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(id);
    }

private:
    std::mutex mutex_;
    std::deque<gate::v1::DashboardEvent> ring_;
    std::map<std::uint64_t, std::shared_ptr<Subscriber>> subscribers_;
    std::uint64_t next_id_ = 1;
    std::uint64_t next_sub_id_ = 1;
};

// ---------------------------------------------------------------------------
// Gate session registry — live Control streams, addressable by gate_id.
// ---------------------------------------------------------------------------

struct GateSession {
    std::mutex m;  // serialises Writes and guards `alive`
    grpc::ServerReaderWriter<gate::v1::ControlEnvelope, gate::v1::ControlEnvelope>* stream =
        nullptr;
    bool alive = true;

    bool send_command(const gate::v1::GateCommand& cmd) {
        const std::lock_guard<std::mutex> lock(m);
        if (!alive) {
            return false;
        }
        gate::v1::ControlEnvelope env;
        *env.mutable_command() = cmd;
        return stream->Write(env);
    }
};

class GateRegistry {
public:
    void put(const std::string& gate_id, std::shared_ptr<GateSession> s) {
        const std::lock_guard<std::mutex> lock(mutex_);
        sessions_[gate_id] = std::move(s);
    }
    void drop(const std::string& gate_id, const std::shared_ptr<GateSession>& s) {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(gate_id);
        if (it != sessions_.end() && it->second == s) {
            sessions_.erase(it);
        }
    }
    std::shared_ptr<GateSession> find(const std::string& gate_id) {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(gate_id);
        return it == sessions_.end() ? nullptr : it->second;
    }

private:
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<GateSession>> sessions_;
};

// ---------------------------------------------------------------------------
// Services
// ---------------------------------------------------------------------------

class MockField final : public gate::v1::FieldControllerService::Service {
public:
    MockField(EventBus& bus, GateRegistry& gates) : bus_(bus), gates_(gates) {}

    grpc::Status Control(grpc::ServerContext* ctx,
                         grpc::ServerReaderWriter<gate::v1::ControlEnvelope,
                                                  gate::v1::ControlEnvelope>* stream) override {
        auto session = std::make_shared<GateSession>();
        session->stream = stream;
        std::string gate_id;

        gate::v1::ControlEnvelope env;
        while (!ctx->IsCancelled() && stream->Read(&env)) {
            gate::v1::DashboardEvent ev;
            switch (env.payload_case()) {
                case gate::v1::ControlEnvelope::kTelemetry:
                    if (gate_id.empty()) {
                        gate_id = env.telemetry().gate_id();
                        gates_.put(gate_id, session);
                        std::printf("[mock] gate connected: %s\n", gate_id.c_str());
                    }
                    *ev.mutable_telemetry() = env.telemetry();
                    bus_.publish(std::move(ev));
                    break;
                case gate::v1::ControlEnvelope::kAck:
                    *ev.mutable_ack() = env.ack();
                    bus_.publish(std::move(ev));
                    break;
                case gate::v1::ControlEnvelope::kFault:
                    *ev.mutable_fault() = env.fault();
                    bus_.publish(std::move(ev));
                    break;
                default:
                    break;
            }
        }

        {
            const std::lock_guard<std::mutex> lock(session->m);
            session->alive = false;  // no further writes through this stream
        }
        if (!gate_id.empty()) {
            gates_.drop(gate_id, session);
            std::printf("[mock] gate disconnected: %s\n", gate_id.c_str());
        }
        return grpc::Status::OK;
    }

private:
    EventBus& bus_;
    GateRegistry& gates_;
};

class MockDashboard final : public gate::v1::DashboardService::Service {
public:
    MockDashboard(EventBus& bus, GateRegistry& gates) : bus_(bus), gates_(gates) {}

    grpc::Status Subscribe(grpc::ServerContext* ctx, const gate::v1::DashboardSubscription* req,
                           grpc::ServerWriter<gate::v1::DashboardEvent>* writer) override {
        auto [id, sub] = bus_.subscribe(req->since_event_id());
        std::printf("[mock] dashboard subscribed (since=%llu)\n",
                    static_cast<unsigned long long>(req->since_event_id()));
        while (!ctx->IsCancelled()) {
            std::unique_lock<std::mutex> lock(sub->m);
            sub->cv.wait_for(lock, std::chrono::milliseconds(250),
                             [&] { return !sub->queue.empty(); });
            while (!sub->queue.empty()) {
                const auto ev = sub->queue.front();
                sub->queue.pop_front();
                lock.unlock();
                if (!writer->Write(ev)) {
                    bus_.unsubscribe(id);
                    return grpc::Status::OK;
                }
                lock.lock();
            }
        }
        bus_.unsubscribe(id);
        return grpc::Status::OK;
    }

    grpc::Status IssueCommand(grpc::ServerContext* /*ctx*/, const gate::v1::GateCommand* req,
                              gate::v1::CommandAck* resp) override {
        if (req->gate_id().empty()) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "gate_id is required"};
        }
        gate::v1::GateCommand cmd{*req};
        if (cmd.command_id().empty()) {
            cmd.set_command_id(fresh_id("cmd"));
        }
        stamp_now(cmd.mutable_issued_ts());

        const auto session = gates_.find(cmd.gate_id());
        if (session == nullptr) {
            return {grpc::StatusCode::NOT_FOUND, "gate not connected: " + cmd.gate_id()};
        }

        gate::v1::DashboardEvent ev;
        *ev.mutable_command() = cmd;
        bus_.publish(std::move(ev));

        if (!session->send_command(cmd)) {
            return {grpc::StatusCode::UNAVAILABLE, "gate stream just closed"};
        }
        resp->set_command_id(cmd.command_id());
        stamp_now(resp->mutable_received_ts());
        resp->set_completed(false);
        return grpc::Status::OK;
    }

    grpc::Status Authorize(grpc::ServerContext* /*ctx*/, const gate::v1::AuthorizeRequest*,
                           gate::v1::AuthDecision*) override {
        return {grpc::StatusCode::UNIMPLEMENTED, "mock server has no fusion engine"};
    }

private:
    EventBus& bus_;
    GateRegistry& gates_;
};

class MockAdmin final : public gate::v1::AdminService::Service {
public:
    grpc::Status UpsertAllowlist(grpc::ServerContext*, const gate::v1::UpsertAllowlistRequest* req,
                                 gate::v1::UpsertAllowlistResponse* resp) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& e : req->entries()) {
            const bool existed = entries_.count(e.plate_text()) > 0;
            entries_[e.plate_text()] = e;
            existed ? resp->set_updated(resp->updated() + 1)
                    : resp->set_inserted(resp->inserted() + 1);
        }
        return grpc::Status::OK;
    }

    grpc::Status ListAllowlist(grpc::ServerContext*, const gate::v1::ListAllowlistRequest*,
                               gate::v1::ListAllowlistResponse* resp) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [plate, e] : entries_) {
            *resp->add_entries() = e;
        }
        return grpc::Status::OK;
    }

    grpc::Status DeleteAllowlist(grpc::ServerContext*, const gate::v1::DeleteAllowlistRequest* req,
                                 gate::v1::UpsertAllowlistResponse* resp) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& plate : req->plate_texts()) {
            resp->set_updated(resp->updated() + entries_.erase(plate) > 0 ? 1 : 0);
        }
        return grpc::Status::OK;
    }

    std::vector<std::string> plates() {
        const std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> out;
        for (const auto& [plate, e] : entries_) {
            out.push_back(plate);
        }
        return out;
    }

private:
    std::mutex mutex_;
    std::map<std::string, gate::v1::Allowlistentry> entries_;
};

std::atomic<bool> g_shutdown{false};
void handle_signal(int) {
    g_shutdown.store(true);
}

// Synthetic ALPR traffic: alternate an allowlisted plate (AUTHORIZED)
// with a random one (DENIED) so the feed shows both verdicts.
void decision_loop(EventBus& bus, MockAdmin& admin, std::uint32_t period_s) {
    std::mt19937 rng{std::random_device{}()};
    bool authorized_turn = true;
    while (!g_shutdown.load()) {
        for (std::uint32_t i = 0; i < period_s * 10 && !g_shutdown.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (g_shutdown.load()) {
            return;
        }
        const auto plates = admin.plates();
        gate::v1::DashboardEvent ev;
        auto* d = ev.mutable_decision();
        d->set_decision_id(fresh_id("dec"));
        stamp_now(d->mutable_decision_ts());
        d->set_gate_id("gate-sim-01");
        d->set_matched_class(gate::v1::VEHICLE_CLASS_SEDAN);
        if (authorized_turn && !plates.empty()) {
            d->set_verdict(gate::v1::AUTH_VERDICT_AUTHORIZED);
            d->set_matched_plate(plates[rng() % plates.size()]);
            d->set_combined_conf(0.90F + static_cast<float>(rng() % 10) / 100.0F);
            d->set_reason_text("allowlist match (synthetic)");
        } else {
            char plate[8];
            std::snprintf(plate, sizeof(plate), "K%c%c%03u%c", static_cast<int>('A' + rng() % 26),
                          static_cast<int>('A' + rng() % 26), static_cast<unsigned>(rng() % 1000),
                          static_cast<int>('A' + rng() % 26));
            d->set_verdict(gate::v1::AUTH_VERDICT_DENIED);
            d->set_deny_reason(gate::v1::DENY_REASON_NOT_ON_ALLOWLIST);
            d->set_matched_plate(plate);
            d->set_combined_conf(0.80F);
            d->set_reason_text("no allowlist match (synthetic)");
        }
        d->set_actor("mock-alpr");
        authorized_turn = !authorized_turn;
        bus.publish(std::move(ev));
    }
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"gate-mock-server — GPU-free gate-server stand-in for demos/dev"};
    std::string listen = "127.0.0.1:50051";
    std::uint32_t decisions_period = 15;
    app.add_option("--listen", listen, "gRPC listen address")->capture_default_str();
    app.add_option("--decisions-period", decisions_period,
                   "seconds between synthetic ALPR decisions (0 = off)")
        ->capture_default_str();
    CLI11_PARSE(app, argc, argv);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    EventBus bus;
    GateRegistry gates;
    MockField field(bus, gates);
    MockDashboard dashboard(bus, gates);
    MockAdmin admin;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen, grpc::InsecureServerCredentials());
    builder.RegisterService(&field);
    builder.RegisterService(&dashboard);
    builder.RegisterService(&admin);
    auto server = builder.BuildAndStart();
    if (server == nullptr) {
        std::fprintf(stderr, "failed to bind %s\n", listen.c_str());
        return 1;
    }
    std::printf("gate-mock-server listening on %s (synthetic decisions every %us)\n",
                listen.c_str(), decisions_period);

    std::thread decisions;
    if (decisions_period > 0) {
        decisions = std::thread(decision_loop, std::ref(bus), std::ref(admin), decisions_period);
    }

    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::printf("shutting down…\n");
    server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(2));
    if (decisions.joinable()) {
        decisions.join();
    }
    return 0;
}
