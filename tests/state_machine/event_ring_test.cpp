// event_ring_test.cpp — capacity, ack, ghost-grant guard and power cuts.

#include "gate_state_machine/event_ring.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <vector>

#include "gate_state_machine/rte_authority.hpp"
#include "gate_state_machine/served_guard.hpp"

namespace sm = gate::state_machine;
using sm::EventRing;

namespace {

class FakeFlash final : public sm::FlashIo {
public:
    std::vector<std::uint8_t> mem =
        std::vector<std::uint8_t>(EventRing::kSectors * EventRing::kSectorSize, 0xFF);
    std::size_t budget = std::numeric_limits<std::size_t>::max();  // bytes until the power cut

    void read(std::size_t o, std::uint8_t* out, std::size_t n) override {
        std::copy_n(mem.begin() + static_cast<std::ptrdiff_t>(o), n, out);
    }
    void program(std::size_t o, const std::uint8_t* d, std::size_t n) override {
        for (std::size_t i = 0; i < n && budget > 0; ++i, --budget) {
            mem[o + i] &= d[i];
        }
    }
    void erase_sector(std::size_t s) override {
        std::fill_n(mem.begin() + static_cast<std::ptrdiff_t>(s * EventRing::kSectorSize),
                    EventRing::kSectorSize, 0xFF);
    }
};

sm::EventRecord rec(std::uint8_t kind) {
    sm::EventRecord r;
    r.kind = kind;
    r.boot_id = 7;
    r.request_id[15] = kind;
    return r;
}

}  // namespace

TEST_CASE("records round trip and survive remount", "[event_ring]") {
    FakeFlash f;
    EventRing r(f);
    r.mount();
    r.append(rec(1));
    r.append(rec(2));
    EventRing again(f);
    again.mount();
    const auto p = again.pending(16);
    REQUIRE(p.size() == 2);
    CHECK(p[1].kind == 2);
    CHECK(p[1].request_id[15] == 2);
    CHECK(again.highest_boot_id() == 7);
    CHECK(again.append(rec(3)) == 3);
}

TEST_CASE("ack frees records; batches stay oldest first", "[event_ring]") {
    FakeFlash f;
    EventRing r(f);
    r.mount();
    for (int i = 0; i < 20; ++i) {
        r.append(rec(static_cast<std::uint8_t>(i)));
    }
    const auto a = r.pending(16);
    REQUIRE(a.size() == 16);
    r.ack(a.back().seq);
    CHECK(r.pending(16).size() == 4);
}

TEST_CASE("overflow drops the oldest and counts it", "[event_ring]") {
    FakeFlash f;
    EventRing r(f);
    r.mount();
    for (int i = 0; i < 2'000; ++i) {
        r.append(rec(1));
    }
    const auto p = r.pending(5'000);
    CHECK(p.size() >= EventRing::kCapacity - EventRing::kPerSector);
    CHECK(p.size() <= EventRing::kCapacity);
    CHECK(p.back().seq == 2'000);
    CHECK(r.dropped() > 0);
}

TEST_CASE("a power cut at every byte loses at most that record", "[event_ring]") {
    for (std::size_t cut = 0; cut <= EventRing::kRecordSize + 2; ++cut) {
        FakeFlash f;
        EventRing r(f);
        r.mount();
        r.append(rec(1));
        f.budget = cut;
        r.append(rec(2));
        f.budget = std::numeric_limits<std::size_t>::max();

        EventRing again(f);
        again.mount();
        const auto p = again.pending(16);
        INFO("cut " << cut);
        REQUIRE(p.size() == (cut >= EventRing::kRecordSize + 2 ? 2U : 1U));
        again.append(rec(3));
        const auto q = again.pending(16);
        REQUIRE(q.size() == p.size() + 1);
        CHECK(q.back().kind == 3);
    }
}

TEST_CASE("a ghost late grant is refused, across a reboot", "[served_guard]") {
    sm::ServedGuard g;
    const sm::ServedGuard::Id id{1, 2, 3};
    using V = sm::ServedGuard::Verdict;
    CHECK(g.check(id, 0, 0, false) == V::Accept);
    g.mark_served(id);
    sm::ServedGuard rebooted;
    rebooted.restore(g.persisted());
    CHECK(rebooted.check(id, 0, 0, false) == V::AlreadyServed);
    const sm::ServedGuard::Id other{9};
    CHECK(rebooted.check(other, 100, 200, true) == V::Expired);
    CHECK(rebooted.check(other, 100, 200, false) == V::Accept);
    CHECK(rebooted.check(sm::ServedGuard::Id{}, 0, 0, true) == V::Accept);
}

namespace {

struct RteRig {
    FakeFlash flash;
    EventRing ring{flash};
    sm::ServedGuard guard;
    sm::RteAuthority rte{{}, ring, guard};
    std::uint64_t now = 1;
    const sm::ServedGuard::Id id{7, 7};
    RteRig() { ring.mount(); }
    sm::RteAuthority::Output step(bool pressed, bool link, std::uint32_t ms = 20) {
        sm::RteAuthority::Output last = sm::RteAuthority::Output::None;
        for (std::uint32_t t = 0; t < ms; t += 20) {
            now += 20;
            const auto o = rte.tick(now, pressed, link, id, {});
            if (o != sm::RteAuthority::Output::None) {
                last = o;
            }
        }
        return last;
    }
};

}  // namespace

using Out = sm::RteAuthority::Output;

TEST_CASE("RTE offline opens locally, logged, and refuses the ghost grant", "[rte]") {
    RteRig r;
    CHECK(r.step(true, false, 100) == Out::OpenLocal);
    const auto log = r.ring.pending(16);
    REQUIRE(log.size() == 1);
    CHECK(log[0].kind == sm::kEventOfflineOpen);
    CHECK(log[0].request_id == r.id);
    CHECK(r.guard.check(r.id, 0, 0, false) == sm::ServedGuard::Verdict::AlreadyServed);
}

TEST_CASE("RTE online waits for the server, then falls back", "[rte]") {
    RteRig r;
    CHECK(r.step(true, true, 100) == Out::SendRequest);
    r.step(false, true, 40);
    SECTION("granted: no local open") {
        r.rte.on_reply(sm::RteAuthority::Reply::Granted, r.now);
        CHECK(r.step(false, true, 3'000) == Out::None);
        CHECK(r.ring.pending(16).empty());
    }
    SECTION("silence: local after the grant timeout") {
        CHECK(r.step(false, true, 1'600) == Out::OpenLocal);
    }
    SECTION("queued extends the wait") {
        r.rte.on_reply(sm::RteAuthority::Reply::Queued, r.now);
        CHECK(r.step(false, true, 5'000) == Out::None);
        CHECK(r.step(false, true, 16'000) == Out::OpenLocal);
    }
    SECTION("the link dropping goes local at once") {
        CHECK(r.step(false, false, 20) == Out::OpenLocal);
    }
}

TEST_CASE("RTE hold, stuck and rate limit", "[rte]") {
    RteRig r;
    CHECK(r.step(true, false, 100) == Out::OpenLocal);
    CHECK(r.step(true, false, 5'000) == Out::LocalReset);
    CHECK(r.step(true, false, 5'000) == Out::Stuck);
    r.step(false, false, 40);
    RteRig q;
    q.step(true, false, 100);
    q.step(false, false, 100);
    CHECK(q.step(true, false, 100) == Out::None);  // inside 3 s
    q.step(false, false, 3'000);
    CHECK(q.step(true, false, 100) == Out::OpenLocal);
}
