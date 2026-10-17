// command_guard_test.cpp — firmware-side replay protection for GateCommands.

#include "gate_rpc/command_guard.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using gate::rpc::CommandGuard;
using V = CommandGuard::Verdict;

namespace {
constexpr std::int64_t kNow = 1'790'000'000'000LL;  // 2026, unix ms
}

TEST_CASE("a fresh command is accepted exactly once", "[command_guard]") {
    CommandGuard g;
    CHECK(g.admit("cmd-1", kNow - 500, kNow) == V::kAccept);
    CHECK(g.admit("cmd-1", kNow - 500, kNow + 10) == V::kDuplicate);
    CHECK(g.admit("cmd-2", kNow, kNow) == V::kAccept);
}

TEST_CASE("stale and future-dated commands are refused", "[command_guard]") {
    CommandGuard g{{.max_age_ms = 30'000, .max_future_ms = 5'000}};
    CHECK(g.admit("old", kNow - 30'001, kNow) == V::kStale);
    CHECK(g.admit("edge", kNow - 30'000, kNow) == V::kAccept);
    CHECK(g.admit("ahead", kNow + 5'001, kNow) == V::kFromFuture);
    CHECK(g.admit("skewed", kNow + 4'000, kNow) == V::kAccept);
}

TEST_CASE("a refused command is not remembered", "[command_guard]") {
    CommandGuard g;
    CHECK(g.admit("x", kNow - 120'000, kNow) == V::kStale);
    // Same id with a sane timestamp is a decision for the age rule, not
    // a duplicate: nothing executed the first time.
    CHECK(g.admit("x", kNow, kNow) == V::kAccept);
}

TEST_CASE("unknown clocks still dedupe but skip the age check", "[command_guard]") {
    CommandGuard g;
    CHECK(g.admit("a", kNow - 999'999, 0) == V::kAcceptUnchecked);  // no SNTP yet
    CHECK(g.admit("a", kNow, kNow) == V::kDuplicate);
    CHECK(g.admit("b", 0, kNow) == V::kAcceptUnchecked);  // server omitted issued_ts
    CHECK(CommandGuard::accepted(V::kAcceptUnchecked));
}

TEST_CASE("empty command ids are refused", "[command_guard]") {
    CommandGuard g;
    CHECK(g.admit("", kNow, kNow) == V::kMissingId);
    CHECK(g.admit(nullptr, kNow, kNow) == V::kMissingId);
    CHECK_FALSE(CommandGuard::accepted(V::kMissingId));
    CHECK(std::string{CommandGuard::describe(V::kStale)}.find("stale") != std::string::npos);
}

TEST_CASE("history is a bounded ring", "[command_guard]") {
    CommandGuard g;
    for (std::size_t i = 0; i < CommandGuard::kHistory; ++i) {
        REQUIRE(g.admit(("id-" + std::to_string(i)).c_str(), kNow, kNow) == V::kAccept);
    }
    CHECK(g.admit("id-0", kNow, kNow) == V::kDuplicate);
    // One more evicts the oldest entry (id-0).
    CHECK(g.admit("id-new", kNow, kNow) == V::kAccept);
    CHECK(g.admit("id-0", kNow, kNow) == V::kAccept);
    CHECK(g.admit("id-2", kNow, kNow) == V::kDuplicate);
}
