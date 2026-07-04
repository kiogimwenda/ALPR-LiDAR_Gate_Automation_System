// control_client.cpp — connect/stream/pump loop for the Control stream.
//
// One long-lived HTTP/2 session per server connection, one gRPC bidi
// stream per session. The pump is a classic nghttp2 client loop:
// poll() on the socket, feed received bytes to
// nghttp2_session_mem_recv (which fans out to the on_* callbacks),
// and let nghttp2_session_send drain what the library wants to write.
// Outbound envelopes cross the thread boundary through a FreeRTOS
// queue; the HTTP/2 data provider reads from the frame currently
// being transmitted and defers when there is none.

#include "gate_rpc/control_client.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <fcntl.h>
#include <netdb.h>
#include <nghttp2/nghttp2.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gate_rpc/grpc_framing.hpp"

namespace gate::rpc {

namespace sm = gate::state_machine;

namespace {

constexpr const char* kTag = "gate-rpc";

constexpr std::uint32_t kTaskStackBytes = 8192;
constexpr UBaseType_t kTaskPriority = 4;  // below gate_ctrl (5) — RPC yields to safety
constexpr UBaseType_t kOutQueueDepth = 8;
constexpr int kPollPeriodMs = 250;
// Largest message we accept on the Control stream. GateCommand is the
// biggest legal payload (~300 B with every string at max_size); 1 KiB
// leaves generous slack. OTA chunks arrive on their own RPC (4.5.5)
// with their own budget.
constexpr std::size_t kMaxInMessage = 1024;

constexpr EventBits_t kNetUpBit = BIT0;

nghttp2_nv make_nv(const char* name, const char* value) {
    nghttp2_nv nv;
    nv.name = reinterpret_cast<std::uint8_t*>(const_cast<char*>(name));
    nv.value = reinterpret_cast<std::uint8_t*>(const_cast<char*>(value));
    nv.namelen = std::strlen(name);
    nv.valuelen = std::strlen(value);
    nv.flags = NGHTTP2_NV_FLAG_NONE;
    return nv;
}

void copy_str(char* dst, std::size_t cap, const char* src) {
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

}  // namespace

gate_v1_GateState to_wire_state(sm::State s) noexcept {
    switch (s) {
        case sm::State::Closed:
            return gate_v1_GateState_GATE_STATE_CLOSED;
        case sm::State::Opening:
            return gate_v1_GateState_GATE_STATE_OPENING;
        case sm::State::Open:
            return gate_v1_GateState_GATE_STATE_OPEN;
        case sm::State::Closing:
            return gate_v1_GateState_GATE_STATE_CLOSING;
        case sm::State::Faulted:
            return gate_v1_GateState_GATE_STATE_FAULT;
        case sm::State::Initializing:
        case sm::State::StoppedOpen:
        case sm::State::StoppedClose:
            // No wire value for "paused mid-travel" yet (4.5.4 revisits
            // the enum); UNKNOWN is honest, OPENING/CLOSING would lie.
            return gate_v1_GateState_GATE_STATE_UNKNOWN;
    }
    return gate_v1_GateState_GATE_STATE_UNKNOWN;
}

// Session-scoped state. Lives on the gate_rpc task for exactly one
// connect → pump cycle; nghttp2 callbacks get here via user_data.
// Namespace-scope (not nested) so the free-function callbacks below
// can name it; ControlClient befriends it for the OutFrame type.
struct ControlSession {
    using OutFrame = ControlClient::OutFrame;

    ControlClient* owner = nullptr;
    int fd = -1;
    nghttp2_session* ng = nullptr;
    std::int32_t stream_id = -1;

    std::uint8_t rx_buf[kMaxInMessage];
    FrameAssembler assembler{rx_buf, sizeof(rx_buf)};

    OutFrame current{};
    std::size_t current_off = 0;
    bool has_current = false;

    bool headers_seen = false;  // response HEADERS with :status arrived
    bool closed = false;
    int grpc_status = -1;  // from trailers, if the server sent one

    void handle_message(const std::uint8_t* data, std::size_t len);
};

void ControlSession::handle_message(const std::uint8_t* data, std::size_t len) {
    gate_v1_ControlEnvelope env = gate_v1_ControlEnvelope_init_zero;
    pb_istream_t is = pb_istream_from_buffer(data, len);
    if (!pb_decode(&is, gate_v1_ControlEnvelope_fields, &env)) {
        ESP_LOGW(kTag, "envelope decode failed (%u bytes): %s", static_cast<unsigned>(len),
                 PB_GET_ERROR(&is));
        return;
    }
    switch (env.which_payload) {
        case gate_v1_ControlEnvelope_command_tag:
            // Foundation scope: log only. 4.5.4 dispatches into
            // GateController and answers with CommandAck.
            ESP_LOGI(kTag, "<- GateCommand kind=%d id=%s actor=%s (dispatch lands in 4.5.4)",
                     static_cast<int>(env.payload.command.kind), env.payload.command.command_id,
                     env.payload.command.actor);
            break;
        case gate_v1_ControlEnvelope_telemetry_tag:
        case gate_v1_ControlEnvelope_ack_tag:
        case gate_v1_ControlEnvelope_fault_tag:
            ESP_LOGW(kTag, "<- unexpected server payload (which=%d) — ignoring",
                     static_cast<int>(env.which_payload));
            break;
        default:
            ESP_LOGW(kTag, "<- empty envelope");
            break;
    }
}

// ---------- nghttp2 callbacks -------------------------------------------------

namespace {

ssize_t cb_send(nghttp2_session*, const std::uint8_t* data, std::size_t length, int,
                void* user_data) {
    auto* s = static_cast<ControlSession*>(user_data);
    const ssize_t n = send(s->fd, data, length, 0);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return n;
}

int cb_on_header(nghttp2_session*, const nghttp2_frame* frame, const std::uint8_t* name,
                 std::size_t namelen, const std::uint8_t* value, std::size_t valuelen, std::uint8_t,
                 void* user_data) {
    auto* s = static_cast<ControlSession*>(user_data);
    if (frame->hd.stream_id != s->stream_id) {
        return 0;
    }
    if (namelen == 7 && std::memcmp(name, ":status", 7) == 0) {
        s->headers_seen = true;
        if (valuelen != 3 || std::memcmp(value, "200", 3) != 0) {
            ESP_LOGW(kTag, "HTTP status %.*s on Control stream", static_cast<int>(valuelen), value);
        }
    } else if (namelen == 11 && std::memcmp(name, "grpc-status", 11) == 0) {
        s->grpc_status = std::atoi(reinterpret_cast<const char*>(value));
    } else if (namelen == 12 && std::memcmp(name, "grpc-message", 12) == 0 && valuelen > 0) {
        ESP_LOGW(kTag, "grpc-message: %.*s", static_cast<int>(valuelen), value);
    }
    return 0;
}

int cb_on_data_chunk(nghttp2_session*, std::uint8_t, std::int32_t stream_id,
                     const std::uint8_t* data, std::size_t len, void* user_data) {
    auto* s = static_cast<ControlSession*>(user_data);
    if (stream_id != s->stream_id) {
        return 0;
    }
    std::size_t off = 0;
    while (off < len) {
        std::size_t consumed = 0;
        const auto st = s->assembler.push(data + off, len - off, consumed);
        off += consumed;
        if (st == FrameAssembler::Status::MessageReady) {
            s->handle_message(s->assembler.message(), s->assembler.message_size());
        } else if (st == FrameAssembler::Status::ErrorCompressed ||
                   st == FrameAssembler::Status::ErrorOversize) {
            ESP_LOGE(kTag, "framing protocol error (%d) — dropping session", static_cast<int>(st));
            s->closed = true;
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    }
    return 0;
}

int cb_on_frame_recv(nghttp2_session*, const nghttp2_frame* frame, void* user_data) {
    auto* s = static_cast<ControlSession*>(user_data);
    if (frame->hd.stream_id == s->stream_id && (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0) {
        s->closed = true;  // server half-closed: bidi stream is over
    }
    if (frame->hd.type == NGHTTP2_GOAWAY) {
        ESP_LOGW(kTag, "GOAWAY from server (error=%u)",
                 static_cast<unsigned>(frame->goaway.error_code));
        s->closed = true;
    }
    return 0;
}

int cb_on_stream_close(nghttp2_session*, std::int32_t stream_id, std::uint32_t error_code,
                       void* user_data) {
    auto* s = static_cast<ControlSession*>(user_data);
    if (stream_id == s->stream_id) {
        if (error_code != NGHTTP2_NO_ERROR) {
            ESP_LOGW(kTag, "stream closed (h2 error=%u)", static_cast<unsigned>(error_code));
        }
        s->closed = true;
    }
    return 0;
}

// HTTP/2 data provider for the request body — the firmware→server half
// of the bidi stream. Feeds the frame currently in flight; defers when
// the outbound queue is dry (the pump resumes us when it refills).
// Never sets EOF: the stream stays open for the life of the session.
ssize_t cb_data_read(nghttp2_session*, std::int32_t, std::uint8_t* buf, std::size_t length,
                     std::uint32_t* data_flags, nghttp2_data_source* source, void*) {
    auto* s = static_cast<ControlSession*>(source->ptr);
    *data_flags = 0;
    if (!s->has_current) {
        return NGHTTP2_ERR_DEFERRED;
    }
    const std::size_t remain = s->current.len - s->current_off;
    const std::size_t take = (length < remain) ? length : remain;
    std::memcpy(buf, s->current.data + s->current_off, take);
    s->current_off += take;
    if (s->current_off == s->current.len) {
        s->has_current = false;
        s->current_off = 0;
    }
    return static_cast<ssize_t>(take);
}

int connect_to(const char* host, std::uint16_t port) {
    char port_str[8];
    std::snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(port));
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == nullptr) {
        ESP_LOGW(kTag, "DNS lookup failed for %s", host);
        return -1;
    }
    int fd = -1;
    for (addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        ESP_LOGW(kTag, "TCP connect to %s:%u failed", host, static_cast<unsigned>(port));
        return -1;
    }
    const int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

}  // namespace

// ---------- ControlClient -----------------------------------------------------

ControlClient::ControlClient(const Config& cfg, TelemetryFiller filler)
    : cfg_(cfg), filler_(std::move(filler)) {
    net_events_ = xEventGroupCreate();
    configASSERT(net_events_ != nullptr);
    out_queue_ = xQueueCreate(kOutQueueDepth, sizeof(OutFrame));
    configASSERT(out_queue_ != nullptr);
}

ControlClient::~ControlClient() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
    if (out_queue_ != nullptr) {
        vQueueDelete(out_queue_);
    }
    if (net_events_ != nullptr) {
        vEventGroupDelete(net_events_);
    }
}

esp_err_t ControlClient::start() {
    if (started_) {
        return ESP_ERR_INVALID_STATE;
    }
    started_ = true;
    if (xTaskCreate(&ControlClient::task_entry, "gate_rpc", kTaskStackBytes, this, kTaskPriority,
                    &task_) != pdPASS) {
        ESP_LOGE(kTag, "failed to create gate_rpc task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(kTag, "started (server=%s:%u, gate_id=%s)", cfg_.host,
             static_cast<unsigned>(cfg_.port), cfg_.gate_id);
    return ESP_OK;
}

void ControlClient::notify_network_up() {
    xEventGroupSetBits(net_events_, kNetUpBit);
}

void ControlClient::notify_network_down() {
    xEventGroupClearBits(net_events_, kNetUpBit);
}

bool ControlClient::send_envelope(const gate_v1_ControlEnvelope& env) {
    OutFrame frame;
    pb_ostream_t os = pb_ostream_from_buffer(frame.data + kFrameHeaderSize,
                                             sizeof(frame.data) - kFrameHeaderSize);
    if (!pb_encode(&os, gate_v1_ControlEnvelope_fields, &env)) {
        ESP_LOGE(kTag, "envelope encode failed: %s", PB_GET_ERROR(&os));
        return false;
    }
    const auto header = make_frame_header(static_cast<std::uint32_t>(os.bytes_written));
    std::memcpy(frame.data, header.data(), header.size());
    frame.len = static_cast<std::uint16_t>(kFrameHeaderSize + os.bytes_written);
    if (!connected_.load(std::memory_order_relaxed)) {
        return false;  // quiet: normal while the link is down
    }
    if (xQueueSend(out_queue_, &frame, 0) != pdTRUE) {
        ESP_LOGW(kTag, "outbound queue full — dropped envelope (which=%d)",
                 static_cast<int>(env.which_payload));
        return false;
    }
    return true;
}

void ControlClient::send_hello() {
    gate_v1_ControlEnvelope env = gate_v1_ControlEnvelope_init_zero;
    env.which_payload = gate_v1_ControlEnvelope_telemetry_tag;
    auto& t = env.payload.telemetry;
    copy_str(t.gate_id, sizeof(t.gate_id), cfg_.gate_id);
    copy_str(t.firmware_version, sizeof(t.firmware_version), cfg_.fw_version);
    t.uptime_sec = static_cast<std::uint32_t>(esp_timer_get_time() / 1'000'000LL);
    t.free_heap_bytes = esp_get_free_heap_size();
    t.min_free_heap = esp_get_minimum_free_heap_size();
    t.seq = telemetry_seq_++;
    // sent_ts stays unset until SNTP lands with the telemetry cadence
    // work in 4.5.4 — a zero timestamp is worse than an absent one.
    if (filler_) {
        filler_(t);
    }
    send_envelope(env);
}

void ControlClient::task_entry(void* arg) {
    static_cast<ControlClient*>(arg)->task_main();
}

void ControlClient::task_main() {
    std::uint32_t backoff_ms = cfg_.reconnect_min_ms;
    for (;;) {
        xEventGroupWaitBits(net_events_, kNetUpBit, pdFALSE, pdTRUE, portMAX_DELAY);
        const bool reached_stream = run_session();
        connected_.store(false, std::memory_order_relaxed);
        xQueueReset(out_queue_);  // stale frames must not greet the next session
        if (reached_stream) {
            backoff_ms = cfg_.reconnect_min_ms;
        }
        ESP_LOGI(kTag, "reconnecting in %ums", static_cast<unsigned>(backoff_ms));
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        backoff_ms =
            backoff_ms * 2 > cfg_.reconnect_max_ms ? cfg_.reconnect_max_ms : backoff_ms * 2;
    }
}

bool ControlClient::run_session() {
    ControlSession s;
    s.owner = this;

    s.fd = connect_to(cfg_.host, cfg_.port);
    if (s.fd < 0) {
        return false;
    }

    nghttp2_session_callbacks* cbs = nullptr;
    nghttp2_session_callbacks_new(&cbs);
    nghttp2_session_callbacks_set_send_callback(cbs, cb_send);
    nghttp2_session_callbacks_set_on_header_callback(cbs, cb_on_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, cb_on_data_chunk);
    nghttp2_session_callbacks_set_on_frame_recv_callback(cbs, cb_on_frame_recv);
    nghttp2_session_callbacks_set_on_stream_close_callback(cbs, cb_on_stream_close);
    nghttp2_session_client_new(&s.ng, cbs, &s);
    nghttp2_session_callbacks_del(cbs);

    nghttp2_submit_settings(s.ng, NGHTTP2_FLAG_NONE, nullptr, 0);

    char authority[64];
    std::snprintf(authority, sizeof(authority), "%s:%u", cfg_.host,
                  static_cast<unsigned>(cfg_.port));
    const nghttp2_nv hdrs[] = {
        make_nv(":method", "POST"),
        make_nv(":scheme", "http"),
        make_nv(":authority", authority),
        make_nv(":path", "/gate.v1.FieldControllerService/Control"),
        make_nv("content-type", "application/grpc"),
        make_nv("te", "trailers"),
        make_nv("user-agent", "grpc-esp32-nanopb/0.1 (gate-fw)"),
    };
    nghttp2_data_provider prd;
    prd.source.ptr = &s;
    prd.read_callback = cb_data_read;
    s.stream_id =
        nghttp2_submit_request(s.ng, nullptr, hdrs, sizeof(hdrs) / sizeof(hdrs[0]), &prd, nullptr);
    if (s.stream_id < 0) {
        ESP_LOGE(kTag, "submit_request failed: %s", nghttp2_strerror(s.stream_id));
        nghttp2_session_del(s.ng);
        close(s.fd);
        return false;
    }
    ESP_LOGI(kTag, "Control stream opening to %s (stream=%ld)", authority,
             static_cast<long>(s.stream_id));

    // The stream exists client-side the moment submit_request returns;
    // queue the hello now so it rides out right behind the HEADERS.
    connected_.store(true, std::memory_order_relaxed);
    send_hello();

    // Pump until the stream or connection dies.
    while (!s.closed && (nghttp2_session_want_read(s.ng) || nghttp2_session_want_write(s.ng))) {
        if (!s.has_current && xQueueReceive(out_queue_, &s.current, 0) == pdTRUE) {
            s.current_off = 0;
            s.has_current = true;
            nghttp2_session_resume_data(s.ng, s.stream_id);
        }

        if (nghttp2_session_want_write(s.ng)) {
            const int rv = nghttp2_session_send(s.ng);
            if (rv != 0) {
                ESP_LOGW(kTag, "session send: %s", nghttp2_strerror(rv));
                break;
            }
        }

        pollfd pfd = {};
        pfd.fd = s.fd;
        pfd.events = POLLIN;
        if (nghttp2_session_want_write(s.ng)) {
            pfd.events |= POLLOUT;
        }
        const int pr = poll(&pfd, 1, kPollPeriodMs);
        if (pr < 0) {
            ESP_LOGW(kTag, "poll failed (errno=%d)", errno);
            break;
        }
        if ((pfd.revents & (POLLERR | POLLHUP)) != 0) {
            ESP_LOGW(kTag, "socket error/hangup");
            break;
        }
        if ((pfd.revents & POLLIN) != 0) {
            std::uint8_t buf[1024];
            const ssize_t n = recv(s.fd, buf, sizeof(buf), 0);
            if (n == 0) {
                ESP_LOGW(kTag, "server closed connection");
                break;
            }
            if (n < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
                ESP_LOGW(kTag, "recv failed (errno=%d)", errno);
                break;
            }
            if (n > 0) {
                const ssize_t rv = nghttp2_session_mem_recv(s.ng, buf, static_cast<size_t>(n));
                if (rv < 0) {
                    ESP_LOGW(kTag, "session recv: %s", nghttp2_strerror(static_cast<int>(rv)));
                    break;
                }
            }
        }
    }

    if (s.grpc_status >= 0 && s.grpc_status != 0) {
        ESP_LOGW(kTag, "Control stream ended with grpc-status=%d", s.grpc_status);
    }
    const bool reached_stream = s.headers_seen;
    connected_.store(false, std::memory_order_relaxed);
    nghttp2_session_del(s.ng);
    close(s.fd);
    ESP_LOGI(kTag, "session ended (reached_stream=%d)", reached_stream ? 1 : 0);
    return reached_stream;
}

}  // namespace gate::rpc
