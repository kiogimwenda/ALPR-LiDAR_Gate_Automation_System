// sim_client.cpp — one Control-stream session over grpc++.

#include "gate_sim/sim_client.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <grpcpp/grpcpp.h>
#include <mutex>
#include <sstream>
#include <thread>

#include "gate_service.grpc.pb.h"

namespace gate::sim {

namespace sm = gate::state_machine;

namespace {

gate::v1::GateState to_wire(sm::State s) {
    switch (s) {
        case sm::State::Closed:
            return gate::v1::GATE_STATE_CLOSED;
        case sm::State::Opening:
            return gate::v1::GATE_STATE_OPENING;
        case sm::State::Open:
            return gate::v1::GATE_STATE_OPEN;
        case sm::State::Closing:
            return gate::v1::GATE_STATE_CLOSING;
        case sm::State::Faulted:
            return gate::v1::GATE_STATE_FAULT;
        default:
            return gate::v1::GATE_STATE_UNKNOWN;
    }
}

void stamp_now(google::protobuf::Timestamp* ts) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    ts->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(now).count());
    ts->set_nanos(static_cast<std::int32_t>(
        (std::chrono::duration_cast<std::chrono::nanoseconds>(now) % std::chrono::seconds(1))
            .count()));
}

using Stream = grpc::ClientReaderWriter<gate::v1::ControlEnvelope, gate::v1::ControlEnvelope>;

// grpc streams allow one in-flight Write; the session loop and the
// command handler both send, so every Write goes through here.
class LockedWriter {
public:
    explicit LockedWriter(Stream* stream) : stream_(stream) {}

    bool write(const gate::v1::ControlEnvelope& env) {
        const std::lock_guard<std::mutex> lock(mutex_);
        return stream_->Write(env);
    }

private:
    Stream* stream_;
    std::mutex mutex_;
};

void write_ack_received(LockedWriter& w, const gate::v1::GateCommand& cmd,
                        gate::v1::GateState state) {
    gate::v1::ControlEnvelope env;
    auto* ack = env.mutable_ack();
    ack->set_command_id(cmd.command_id());
    stamp_now(ack->mutable_received_ts());
    ack->set_completed(false);
    ack->set_state_after(state);
    w.write(env);
}

void write_ack_completed(LockedWriter& w, const std::string& command_id, bool success,
                         const std::string& error, gate::v1::GateState state) {
    gate::v1::ControlEnvelope env;
    auto* ack = env.mutable_ack();
    ack->set_command_id(command_id);
    stamp_now(ack->mutable_completed_ts());
    ack->set_completed(true);
    ack->set_success(success);
    if (!success) {
        ack->set_error_text(error);
    }
    ack->set_state_after(state);
    std::printf("[sim] -> ack %s: %s%s%s\n", command_id.c_str(), success ? "ok" : "failed",
                success ? "" : " — ", success ? "" : error.c_str());
    w.write(env);
}

}  // namespace

namespace {

std::string read_pem(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

bool SimClient::run_session(const std::atomic<bool>& shutdown) {
    std::shared_ptr<grpc::ChannelCredentials> creds;
    if (cfg_.tls_ca_path.empty()) {
        creds = grpc::InsecureChannelCredentials();
    } else {
        // Mirrors the firmware's eventual posture (4.10.3): verify the
        // server against the site CA and present a device identity.
        grpc::SslCredentialsOptions opts;
        opts.pem_root_certs = read_pem(cfg_.tls_ca_path);
        if (!cfg_.tls_cert_path.empty()) {
            opts.pem_cert_chain = read_pem(cfg_.tls_cert_path);
            opts.pem_private_key = read_pem(cfg_.tls_key_path);
        }
        creds = grpc::SslCredentials(opts);
    }
    auto channel = grpc::CreateChannel(cfg_.server, creds);
    const auto deadline =
        std::chrono::system_clock::now() + std::chrono::milliseconds(cfg_.connect_timeout_ms);
    if (!channel->WaitForConnected(deadline)) {
        std::printf("[sim] no server at %s\n", cfg_.server.c_str());
        return false;
    }

    auto stub = gate::v1::FieldControllerService::NewStub(channel);
    grpc::ClientContext ctx;
    auto stream = stub->Control(&ctx);
    LockedWriter writer(stream.get());
    std::printf("[sim] Control stream open to %s as %s\n", cfg_.server.c_str(),
                cfg_.gate_id.c_str());

    std::atomic<bool> stream_open{true};
    std::thread reader([&] {
        gate::v1::ControlEnvelope env;
        while (stream->Read(&env)) {
            if (!env.has_command()) {
                continue;
            }
            const auto& cmd = env.command();
            std::printf("[sim] <- command kind=%d id=%s actor=%s\n", static_cast<int>(cmd.kind()),
                        cmd.command_id().c_str(), cmd.actor().c_str());
            write_ack_received(writer, cmd, to_wire(gate_.state()));
            switch (cmd.kind()) {
                case gate::v1::COMMAND_KIND_OPEN_GATE:
                    gate_.issue(cmd.command_id(), VirtualGate::Command::Open);
                    break;
                case gate::v1::COMMAND_KIND_CLOSE_GATE:
                    gate_.issue(cmd.command_id(), VirtualGate::Command::Close);
                    break;
                case gate::v1::COMMAND_KIND_LED_PATTERN:
                    std::printf("[sim] (led pattern %d rendered)\n",
                                static_cast<int>(cmd.led_pattern()));
                    write_ack_completed(writer, cmd.command_id(), true, "", to_wire(gate_.state()));
                    break;
                case gate::v1::COMMAND_KIND_REBOOT:
                    gate_.reboot();
                    write_ack_completed(writer, cmd.command_id(), true, "", to_wire(gate_.state()));
                    break;
                case gate::v1::COMMAND_KIND_BEGIN_OTA:
                    write_ack_completed(writer, cmd.command_id(), false,
                                        "simulator has no flash to update", to_wire(gate_.state()));
                    break;
                default:
                    write_ack_completed(writer, cmd.command_id(), false,
                                        "unsupported on the simulated residential profile",
                                        to_wire(gate_.state()));
                    break;
            }
        }
        stream_open.store(false);
    });

    const auto started = std::chrono::steady_clock::now();
    std::uint64_t since_telemetry_ms = cfg_.telemetry_period_ms;  // send one immediately
    while (stream_open.load() && !shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.tick_ms));
        gate_.advance(cfg_.tick_ms);

        for (const auto& a : gate_.drain_acks()) {
            write_ack_completed(writer, a.command_id, a.success, a.error, to_wire(gate_.state()));
        }

        since_telemetry_ms += cfg_.tick_ms;
        if (since_telemetry_ms >= cfg_.telemetry_period_ms) {
            since_telemetry_ms = 0;
            gate::v1::ControlEnvelope env;
            auto* t = env.mutable_telemetry();
            t->set_gate_id(cfg_.gate_id);
            stamp_now(t->mutable_sent_ts());
            t->set_gate_state(to_wire(gate_.state()));
            t->set_limit_open(gate_.limit_open());
            t->set_limit_closed(gate_.limit_closed());
            t->set_safety_beam_clear(gate_.beam_clear());
            t->set_uptime_sec(
                static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                               std::chrono::steady_clock::now() - started)
                                               .count()));
            t->set_firmware_version(cfg_.fw_version);
            t->set_seq(telemetry_seq_++);
            if (!writer.write(env)) {
                break;
            }
        }
    }

    ctx.TryCancel();  // unblocks the reader if the server is quiet
    reader.join();
    const grpc::Status status = stream->Finish();
    std::printf("[sim] session ended (%s)\n", status.ok() ? "ok" : status.error_message().c_str());
    return true;
}

}  // namespace gate::sim
