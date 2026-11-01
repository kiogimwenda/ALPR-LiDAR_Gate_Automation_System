// event_ring.hpp — durable store-and-forward log, spec section 9.4.
// Pure logic over a flash interface so power cuts can be tested on host.
// 16 sectors of 4 KB, each with a 32-byte header and 63 64-byte records;
// the sector after the head is always erased, so capacity is 945.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gate::state_machine {

// EventRecord::kind values; the server matches them (GateDispatch).
inline constexpr std::uint8_t kEventOfflineOpen = 1;

class FlashIo {
public:
    virtual ~FlashIo() = default;
    virtual void read(std::size_t offset, std::uint8_t* out, std::size_t len) = 0;
    // NOR semantics: programming only clears bits.
    virtual void program(std::size_t offset, const std::uint8_t* data, std::size_t len) = 0;
    virtual void erase_sector(std::size_t sector) = 0;
};

struct EventRecord {
    std::uint32_t seq = 0;  // assigned by append
    std::uint32_t boot_id = 0;
    std::uint64_t uptime_ms = 0;
    std::int64_t unix_ms = 0;
    std::uint8_t kind = 0;
    std::uint8_t source = 0;
    std::uint8_t side = 0;
    std::uint8_t result = 0;
    std::uint8_t state_after = 0;
    std::array<std::uint8_t, 16> request_id{};
};

class EventRing {
public:
    static constexpr std::size_t kSectors = 16;
    static constexpr std::size_t kSectorSize = 4096;
    static constexpr std::size_t kHeaderSize = 32;
    static constexpr std::size_t kRecordSize = 64;
    static constexpr std::size_t kPerSector = 63;
    static constexpr std::size_t kCapacity = (kSectors - 1) * kPerSector;

    explicit EventRing(FlashIo& flash) noexcept : flash_(flash) {}

    // Scans flash; ignores any record with a bad CRC or no commit marker.
    void mount();
    std::uint32_t append(EventRecord r);
    // Oldest unacked records, at most `max`. Acks live in RAM: after a
    // reboot records are resent and the server's unique key drops them.
    [[nodiscard]] std::vector<EventRecord> pending(std::size_t max);
    void ack(std::uint32_t last_seq) noexcept { acked_ = last_seq; }
    [[nodiscard]] std::uint32_t highest_boot_id();
    [[nodiscard]] std::uint32_t dropped() const noexcept { return dropped_; }

private:
    static constexpr std::size_t kSlots = kSectors * kPerSector;
    [[nodiscard]] std::size_t offset(std::size_t slot) const noexcept;
    std::optional<EventRecord> read_slot(std::size_t slot);
    [[nodiscard]] bool slot_blank(std::size_t slot);
    [[nodiscard]] bool sector_blank(std::size_t sector);
    void enter_slot(std::size_t slot);

    FlashIo& flash_;
    std::size_t head_ = 0;
    std::uint32_t next_seq_ = 1;
    std::uint32_t acked_ = 0;
    std::uint32_t dropped_ = 0;
};

}  // namespace gate::state_machine
