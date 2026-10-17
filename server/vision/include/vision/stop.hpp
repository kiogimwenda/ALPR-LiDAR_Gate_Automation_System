// stop.hpp — process-wide shutdown request for gate-vision.
//
// SIGINT/SIGTERM set the flag (a lock-free atomic store is
// async-signal-safe); every place that waits — scenario pacing, camera
// cadence, LiDAR replay, submit backoff — waits through sleep_until()
// below, which wakes within ~50 ms of a stop request. Without this a
// scripted scenario could hold `systemctl stop` for the length of its
// longest gap.

#pragma once

#include <atomic>
#include <chrono>
#include <thread>

namespace gate::vision {

namespace detail {
inline std::atomic<bool>& stop_flag() {
    static std::atomic<bool> flag{false};
    static_assert(std::atomic<bool>::is_always_lock_free, "signal handler needs a lock-free flag");
    return flag;
}
}  // namespace detail

inline void request_stop() noexcept {
    detail::stop_flag().store(true, std::memory_order_relaxed);
}

inline bool stop_requested() noexcept {
    return detail::stop_flag().load(std::memory_order_relaxed);
}

// Tests reset the flag between cases.
inline void reset_stop() noexcept {
    detail::stop_flag().store(false, std::memory_order_relaxed);
}

// Sleep until `tp` or until a stop is requested, whichever comes first.
// Returns false if woken by a stop request.
template <class Clock, class Duration>
bool sleep_until(std::chrono::time_point<Clock, Duration> tp) {
    constexpr auto kSlice = std::chrono::milliseconds{50};
    while (!stop_requested()) {
        const auto now = Clock::now();
        if (now >= tp) {
            return true;
        }
        const auto left = tp - now;
        std::this_thread::sleep_for(
            left < kSlice ? std::chrono::duration_cast<std::chrono::nanoseconds>(left)
                          : std::chrono::duration_cast<std::chrono::nanoseconds>(kSlice));
    }
    return false;
}

template <class Rep, class Period>
bool sleep_for(std::chrono::duration<Rep, Period> d) {
    return sleep_until(std::chrono::steady_clock::now() + d);
}

}  // namespace gate::vision
