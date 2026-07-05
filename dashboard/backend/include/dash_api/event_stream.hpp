// event_stream.hpp — the Subscribe consumer.
//
// One background thread owns a DashboardService::Subscribe stream for
// the life of the process: read → EventCache::ingest → (cache fans
// out to WebSocket sinks). When the stream dies — server restart,
// network blip — the loop reconnects with 1 s → 30 s backoff and
// passes the cache's last_event_id back as since_event_id, so the
// server's replay ring fills any gap instead of the dashboard
// silently losing events (or re-painting duplicates).
//
// Same global-accessor hand-off as GrpcBridge: controllers are
// created reflectively, main wires set_event_stream() before run().

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include "dash_api/event_cache.hpp"
#include "dash_api/grpc_bridge.hpp"

namespace gate::dash_api {

class EventStream {
public:
    struct Config {
        std::uint32_t reconnect_min_ms = 1'000;
        std::uint32_t reconnect_max_ms = 30'000;
        std::size_t ring_capacity = 256;
    };

    // No default for cfg: GCC rejects a {} default argument for a
    // nested aggregate with NSDMIs inside the enclosing class.
    EventStream(std::shared_ptr<GrpcBridge> bridge, Config cfg);
    ~EventStream();
    EventStream(const EventStream&) = delete;
    EventStream& operator=(const EventStream&) = delete;

    void start();
    void stop();  // cancels the in-flight stream; joins the thread

    [[nodiscard]] EventCache& cache() { return cache_; }
    // True while the Subscribe stream is up; /api/status reports it as
    // streamConnected so the UI can badge staleness honestly.
    [[nodiscard]] bool connected() const { return connected_.load(std::memory_order_relaxed); }

private:
    void run();

    std::shared_ptr<GrpcBridge> bridge_;
    Config cfg_;
    EventCache cache_;

    std::thread thread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stop_{false};
    std::mutex ctx_mutex_;
    grpc::ClientContext* active_ctx_ = nullptr;  // guarded by ctx_mutex_
    std::condition_variable backoff_cv_;
    std::mutex backoff_mutex_;
};

// Process-wide accessor for controllers (same pattern as bridge()).
void set_event_stream(std::shared_ptr<EventStream> s);
std::shared_ptr<EventStream> event_stream();

}  // namespace gate::dash_api
