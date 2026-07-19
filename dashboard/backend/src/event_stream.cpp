// event_stream.cpp — connect → read → ingest → reconnect, forever.

#include "dash_api/event_stream.hpp"

#include <chrono>
#include <cstdio>
#include <utility>

namespace gate::dash_api {

namespace {

std::mutex g_stream_mutex;
std::shared_ptr<EventStream> g_stream;

}  // namespace

EventStream::EventStream(std::shared_ptr<GrpcBridge> bridge, Config cfg)
    : bridge_(std::move(bridge)), cfg_(cfg), cache_(cfg.ring_capacity) {}

EventStream::~EventStream() {
    stop();
}

void EventStream::start() {
    if (thread_.joinable()) {
        return;
    }
    stop_.store(false);
    thread_ = std::thread([this] { run(); });
}

void EventStream::stop() {
    if (!thread_.joinable()) {
        return;
    }
    stop_.store(true);
    {
        const std::lock_guard<std::mutex> lock(ctx_mutex_);
        if (active_ctx_ != nullptr) {
            active_ctx_->TryCancel();
        }
    }
    backoff_cv_.notify_all();
    thread_.join();
}

void EventStream::run() {
    std::uint32_t backoff_ms = cfg_.reconnect_min_ms;
    while (!stop_.load()) {
        gate::v1::DashboardSubscription sub;
        sub.set_site_id(bridge_->config().site_id);
        sub.set_include_decisions(true);
        sub.set_include_telemetry(true);
        sub.set_include_faults(true);
        sub.set_include_ota(true);
        sub.set_since_event_id(cache_.last_event_id());

        grpc::ClientContext ctx;
        {
            const std::lock_guard<std::mutex> lock(ctx_mutex_);
            active_ctx_ = &ctx;
        }
        auto reader = bridge_->subscribe(ctx, sub);

        gate::v1::DashboardEvent ev;
        bool got_any = false;
        while (!stop_.load() && reader->Read(&ev)) {
            connected_.store(true, std::memory_order_relaxed);
            got_any = true;
            backoff_ms = cfg_.reconnect_min_ms;
            cache_.ingest(ev);
        }
        connected_.store(false, std::memory_order_relaxed);
        const grpc::Status status = reader->Finish();
        {
            const std::lock_guard<std::mutex> lock(ctx_mutex_);
            active_ctx_ = nullptr;
        }
        if (stop_.load()) {
            break;
        }
        std::printf("[dash] event stream ended (%s%s) — retrying in %ums\n",
                    status.error_message().empty() ? "eof" : status.error_message().c_str(),
                    got_any ? ", resuming" : "", static_cast<unsigned>(backoff_ms));

        std::unique_lock<std::mutex> lock(backoff_mutex_);
        backoff_cv_.wait_for(lock, std::chrono::milliseconds(backoff_ms),
                             [this] { return stop_.load(); });
        backoff_ms =
            backoff_ms * 2 > cfg_.reconnect_max_ms ? cfg_.reconnect_max_ms : backoff_ms * 2;
    }
}

void set_event_stream(std::shared_ptr<EventStream> s) {
    const std::lock_guard<std::mutex> lock(g_stream_mutex);
    g_stream = std::move(s);
}

std::shared_ptr<EventStream> event_stream() {
    const std::lock_guard<std::mutex> lock(g_stream_mutex);
    return g_stream;
}

}  // namespace gate::dash_api
