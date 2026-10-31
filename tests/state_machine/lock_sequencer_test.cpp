// lock_sequencer_test.cpp — section 7 timings and failure paths.

#include "gate_state_machine/lock_sequencer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <random>

namespace sm = gate::state_machine;
using Result = sm::LockSequencer::Result;

namespace {

sm::LockFeedback bolts(bool retracted, bool bonded) {
    sm::LockFeedback fb;
    fb.bolt_retracted = {retracted, retracted};
    fb.bolt_extended = {!retracted, !retracted};
    fb.mag_bonded = {bonded, bonded, bonded};
    return fb;
}

Result run(sm::LockSequencer& s, std::uint64_t& now, const sm::LockFeedback& fb,
           std::uint32_t for_ms) {
    for (std::uint32_t t = 0; t < for_ms; t += 20) {
        now += 20;
        if (const auto r = s.tick(now, fb); r != Result::None) {
            return r;
        }
    }
    return Result::None;
}

}  // namespace

TEST_CASE("release drops maglocks before pulling bolts", "[lock_sequencer]") {
    sm::LockSequencer s{sm::LockLayout::Five, {}};
    std::uint64_t now = 1;
    s.start_release(now);
    CHECK_FALSE(s.maglock_drive());
    CHECK_FALSE(s.bolt_drive());
    CHECK(run(s, now, bolts(false, true), 100) == Result::None);
    CHECK_FALSE(s.bolt_drive());
    CHECK(run(s, now, bolts(false, false), 40) == Result::None);
    CHECK(s.bolt_drive());
    CHECK(run(s, now, bolts(true, false), 500) == Result::Released);
    CHECK(s.bolt_drive());  // held through motion
}

TEST_CASE("stuck maglock retries once then fails safe", "[lock_sequencer]") {
    sm::LockSequencer s{sm::LockLayout::Four, {}};
    std::uint64_t now = 1;
    s.start_release(now);
    CHECK(run(s, now, bolts(false, true), 2'000) == Result::Failed);
    CHECK(s.fault() == sm::LockFault::MaglockStuck);
    CHECK(s.maglock_drive());
    CHECK_FALSE(s.bolt_drive());
}

TEST_CASE("jammed bolt retries twice then fails safe", "[lock_sequencer]") {
    sm::LockSequencer s{sm::LockLayout::Four, {}};
    std::uint64_t now = 1;
    s.start_release(now);
    CHECK(run(s, now, bolts(false, false), 3 * 800 + 2 * 500 + 100) == Result::Failed);
    CHECK(s.fault() == sm::LockFault::BoltJam);
    CHECK(s.maglock_drive());
    CHECK_FALSE(s.bolt_drive());
}

TEST_CASE("engage runs maglocks first and proves both", "[lock_sequencer]") {
    sm::LockSequencer s{sm::LockLayout::Five, {}};
    std::uint64_t now = 1;
    s.start_retract(now);
    CHECK(run(s, now, bolts(true, false), 400) == Result::Released);
    s.start_engage(now);
    CHECK(run(s, now, bolts(true, false), 320) == Result::None);
    CHECK(s.maglock_drive());
    CHECK(s.bolt_drive());
    CHECK(run(s, now, bolts(true, true), 40) == Result::None);
    CHECK_FALSE(s.bolt_drive());
    CHECK(run(s, now, bolts(false, true), 40) == Result::Engaged);
}

TEST_CASE("a maglock that will not bond still drops the bolts", "[lock_sequencer]") {
    sm::LockSequencer s{sm::LockLayout::Five, {}};
    std::uint64_t now = 1;
    s.start_engage(now);
    CHECK(run(s, now, bolts(true, false), 2'400) == Result::None);
    CHECK_FALSE(s.bolt_drive());
    CHECK(run(s, now, bolts(false, false), 40) == Result::Failed);
    CHECK(s.fault() == sm::LockFault::Incomplete);
}

TEST_CASE("M3 is ignored on the 4-lock layout", "[lock_sequencer]") {
    sm::LockSequencer s{sm::LockLayout::Four, {}};
    std::uint64_t now = 1;
    auto fb = bolts(false, false);
    fb.mag_bonded[2] = true;
    s.start_release(now);
    CHECK(run(s, now, fb, 40) == Result::None);
    CHECK(s.bolt_drive());
}

TEST_CASE("contradictory bolt feedback faults after 300 ms", "[lock_sequencer]") {
    sm::LockSequencer s{sm::LockLayout::Four, {}};
    std::uint64_t now = 1;
    sm::LockFeedback fb = bolts(true, false);
    fb.bolt_extended[1] = true;  // both contacts made
    s.start_retract(now);
    CHECK(run(s, now, fb, 500) == Result::Failed);
    CHECK(s.fault() == sm::LockFault::Feedback);
}

TEST_CASE("Released never reported with an unretracted bolt", "[lock_sequencer][property]") {
    std::mt19937 rng(7);
    std::bernoulli_distribution bit(0.5);
    for (int walk = 0; walk < 5'000; ++walk) {
        sm::LockSequencer s{sm::LockLayout::Five, {}};
        std::uint64_t now = 1;
        s.start_release(now);
        for (int i = 0; i < 300 && s.busy(); ++i) {
            now += 20;
            sm::LockFeedback fb;
            fb.bolt_retracted = {bit(rng), bit(rng)};
            fb.bolt_extended = {bit(rng), bit(rng)};
            fb.mag_bonded = {bit(rng), bit(rng), bit(rng)};
            if (s.tick(now, fb) == Result::Released) {
                REQUIRE(fb.bolt_retracted[0]);
                REQUIRE(fb.bolt_retracted[1]);
                REQUIRE_FALSE(fb.bolt_extended[0]);
                REQUIRE_FALSE(fb.bolt_extended[1]);
            }
        }
    }
}
