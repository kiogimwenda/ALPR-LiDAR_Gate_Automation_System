// event_ring.cpp — see the header.

#include "gate_state_machine/event_ring.hpp"

#include <algorithm>
#include <cstring>

namespace gate::state_machine {

namespace {

constexpr std::uint16_t kStart = 0x5AA5;
constexpr std::uint16_t kCommit = 0xA55A;
constexpr std::size_t kCrcOffset = 58;
constexpr std::size_t kCommitOffset = 62;

std::uint32_t crc32(const std::uint8_t* p, std::size_t n) noexcept {
    std::uint32_t c = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int b = 0; b < 8; ++b) {
            c = (c >> 1) ^ (0xEDB88320U & (0U - (c & 1U)));
        }
    }
    return ~c;
}

template <typename T>
void put(std::uint8_t* p, T v) noexcept {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        p[i] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(v) >> (8 * i));
    }
}

template <typename T>
T get(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    }
    return static_cast<T>(v);
}

}  // namespace

std::size_t EventRing::offset(std::size_t slot) const noexcept {
    return (slot / kPerSector) * kSectorSize + kHeaderSize + (slot % kPerSector) * kRecordSize;
}

std::optional<EventRecord> EventRing::read_slot(std::size_t slot) {
    std::array<std::uint8_t, kRecordSize> b{};
    flash_.read(offset(slot), b.data(), b.size());
    if (get<std::uint16_t>(&b[0]) != kStart || get<std::uint16_t>(&b[kCommitOffset]) != kCommit ||
        get<std::uint32_t>(&b[kCrcOffset]) != crc32(b.data(), kCrcOffset)) {
        return std::nullopt;
    }
    EventRecord r;
    r.seq = get<std::uint32_t>(&b[2]);
    r.boot_id = get<std::uint32_t>(&b[6]);
    r.uptime_ms = get<std::uint64_t>(&b[10]);
    r.unix_ms = get<std::int64_t>(&b[18]);
    r.kind = b[26];
    r.source = b[27];
    r.side = b[28];
    r.result = b[29];
    r.state_after = b[30];
    std::memcpy(r.request_id.data(), &b[42], r.request_id.size());
    return r;
}

bool EventRing::slot_blank(std::size_t slot) {
    std::array<std::uint8_t, kRecordSize> b{};
    flash_.read(offset(slot), b.data(), b.size());
    return std::all_of(b.begin(), b.end(), [](std::uint8_t v) { return v == 0xFF; });
}

bool EventRing::sector_blank(std::size_t sector) {
    for (std::size_t i = 0; i < kPerSector; ++i) {
        if (!slot_blank(sector * kPerSector + i)) {
            return false;
        }
    }
    return true;
}

// Entering a sector erases the one after it, dropping its oldest records.
void EventRing::enter_slot(std::size_t slot) {
    head_ = slot % kSlots;
    if (head_ % kPerSector != 0) {
        return;
    }
    const std::size_t sector = head_ / kPerSector;
    if (!sector_blank(sector)) {
        flash_.erase_sector(sector);  // finishes an interrupted erase
    }
    const std::size_t next = (sector + 1) % kSectors;
    for (std::size_t i = 0; i < kPerSector; ++i) {
        const auto r = read_slot(next * kPerSector + i);
        if (r && r->seq > acked_) {
            ++dropped_;
        }
    }
    if (!sector_blank(next)) {
        flash_.erase_sector(next);
    }
}

void EventRing::mount() {
    bool any = false;
    std::size_t last = 0;
    std::uint32_t top = 0;
    for (std::size_t s = 0; s < kSlots; ++s) {
        if (const auto r = read_slot(s); r && (!any || r->seq > top)) {
            any = true;
            top = r->seq;
            last = s;
        }
    }
    next_seq_ = any ? top + 1 : 1;
    std::size_t slot = any ? last + 1 : 0;
    // Skip torn slots: they cannot be programmed again without an erase.
    for (std::size_t i = 0; i < kSlots && !slot_blank(slot % kSlots); ++i) {
        ++slot;
        if (slot % kPerSector == 0) {
            break;  // sector entry below erases as needed
        }
    }
    enter_slot(slot);
}

std::uint32_t EventRing::append(EventRecord r) {
    r.seq = next_seq_++;
    std::array<std::uint8_t, kRecordSize> b{};
    put<std::uint16_t>(&b[0], kStart);
    put<std::uint32_t>(&b[2], r.seq);
    put<std::uint32_t>(&b[6], r.boot_id);
    put<std::uint64_t>(&b[10], r.uptime_ms);
    put<std::int64_t>(&b[18], r.unix_ms);
    b[26] = r.kind;
    b[27] = r.source;
    b[28] = r.side;
    b[29] = r.result;
    b[30] = r.state_after;
    std::memcpy(&b[42], r.request_id.data(), r.request_id.size());
    put<std::uint32_t>(&b[kCrcOffset], crc32(b.data(), kCrcOffset));
    b[kCommitOffset] = 0xFF;
    b[kCommitOffset + 1] = 0xFF;

    const std::size_t at = offset(head_);
    flash_.program(at, b.data(), b.size());
    std::array<std::uint8_t, 2> commit{};
    put<std::uint16_t>(commit.data(), kCommit);
    flash_.program(at + kCommitOffset, commit.data(), commit.size());
    enter_slot(head_ + 1);
    return r.seq;
}

std::vector<EventRecord> EventRing::pending(std::size_t max) {
    std::vector<EventRecord> out;
    for (std::size_t s = 0; s < kSlots; ++s) {
        if (const auto r = read_slot(s); r && r->seq > acked_) {
            out.push_back(*r);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const EventRecord& a, const EventRecord& b) { return a.seq < b.seq; });
    if (out.size() > max) {
        out.resize(max);
    }
    return out;
}

std::uint32_t EventRing::highest_boot_id() {
    std::uint32_t top = 0;
    for (std::size_t s = 0; s < kSlots; ++s) {
        if (const auto r = read_slot(s)) {
            top = std::max(top, r->boot_id);
        }
    }
    return top;
}

}  // namespace gate::state_machine
