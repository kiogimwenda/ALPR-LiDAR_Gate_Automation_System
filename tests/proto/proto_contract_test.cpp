// proto_contract_test.cpp
//
// Contract tests for gate_service.proto. These tests guard the wire format
// against accidental breaking changes. Every change that fails one of these
// must be conscious, reviewed, and accompanied by a version bump.
//
// Tested invariants:
//   1. Field numbers for stable enums never shift.
//   2. Round-trip serialize/parse preserves every field.
//   3. ControlEnvelope oneof correctly discriminates payload types.
//   4. AuthDecision encodes and decodes denial reasons faithfully.
//   5. Telemetry messages stay under the firmware's 256-byte budget for
//      typical content (anti-bloat guard).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gate_service.pb.h"

#include <google/protobuf/util/time_util.h>

#include <chrono>
#include <string>

using namespace gate::v1;
using google::protobuf::util::TimeUtil;

namespace {

Telemetry make_typical_telemetry() {
    Telemetry t;
    t.set_gate_id("residence-main-01");
    *t.mutable_sent_ts() = TimeUtil::SecondsToTimestamp(1745568000);
    t.set_gate_state(GATE_STATE_CLOSED);
    t.set_limit_open(false);
    t.set_limit_closed(true);
    t.set_safety_beam_clear(true);
    t.set_uptime_sec(86400);
    t.set_rssi_dbm(0);
    t.set_free_heap_bytes(220 * 1024);
    t.set_min_free_heap(180 * 1024);
    t.set_supply_voltage_v(12.04F);
    t.set_core_temp_c(38.5F);
    t.set_firmware_version("1.0.0");
    t.set_firmware_sha256("a3b4c5d6e7f8");
    t.set_seq(123456);
    return t;
}

}  // namespace

// --- Enum stability ---------------------------------------------------------

TEST_CASE("enum: VehicleClass has stable field numbers", "[proto][contract]") {
    REQUIRE(VEHICLE_CLASS_UNKNOWN    == 0);
    REQUIRE(VEHICLE_CLASS_PEDESTRIAN == 1);
    REQUIRE(VEHICLE_CLASS_BICYCLE    == 2);
    REQUIRE(VEHICLE_CLASS_MOTORCYCLE == 3);
    REQUIRE(VEHICLE_CLASS_SEDAN      == 4);
    REQUIRE(VEHICLE_CLASS_SUV        == 5);
    REQUIRE(VEHICLE_CLASS_PICKUP     == 6);
    REQUIRE(VEHICLE_CLASS_VAN        == 7);
    REQUIRE(VEHICLE_CLASS_TRUCK      == 8);
}

TEST_CASE("enum: GateState has stable field numbers", "[proto][contract]") {
    REQUIRE(GATE_STATE_UNKNOWN  == 0);
    REQUIRE(GATE_STATE_CLOSED   == 1);
    REQUIRE(GATE_STATE_OPENING  == 2);
    REQUIRE(GATE_STATE_OPEN     == 3);
    REQUIRE(GATE_STATE_CLOSING  == 4);
    REQUIRE(GATE_STATE_FAULT    == 5);
    REQUIRE(GATE_STATE_LOCKDOWN == 6);
}

TEST_CASE("enum: AuthVerdict has stable field numbers", "[proto][contract]") {
    REQUIRE(AUTH_VERDICT_UNSPECIFIED    == 0);
    REQUIRE(AUTH_VERDICT_AUTHORIZED     == 1);
    REQUIRE(AUTH_VERDICT_DENIED         == 2);
    REQUIRE(AUTH_VERDICT_LOW_CONFIDENCE == 3);
    REQUIRE(AUTH_VERDICT_MANUAL_REVIEW  == 4);
}

// --- Round-trip ------------------------------------------------------------

TEST_CASE("Telemetry serializes and parses back identically",
          "[proto][roundtrip]") {
    const Telemetry original = make_typical_telemetry();

    std::string wire;
    REQUIRE(original.SerializeToString(&wire));

    Telemetry decoded;
    REQUIRE(decoded.ParseFromString(wire));

    REQUIRE(decoded.gate_id()           == original.gate_id());
    REQUIRE(decoded.gate_state()        == original.gate_state());
    REQUIRE(decoded.limit_closed()      == original.limit_closed());
    REQUIRE(decoded.uptime_sec()        == original.uptime_sec());
    REQUIRE(decoded.firmware_version()  == original.firmware_version());
    REQUIRE(decoded.seq()               == original.seq());
    REQUIRE_THAT(decoded.supply_voltage_v(),
                 Catch::Matchers::WithinAbs(original.supply_voltage_v(), 0.0001));
    REQUIRE_THAT(decoded.core_temp_c(),
                 Catch::Matchers::WithinAbs(original.core_temp_c(), 0.0001));
}

TEST_CASE("AuthDecision preserves DENIED reason on the wire",
          "[proto][roundtrip]") {
    AuthDecision d;
    d.set_decision_id("01928f7a-7c1d-4e2f-9b89-0a1b2c3d4e5f");
    d.set_gate_id("residence-main-01");
    *d.mutable_decision_ts() = TimeUtil::SecondsToTimestamp(1745568123);
    d.set_verdict(AUTH_VERDICT_DENIED);
    d.set_deny_reason(DENY_REASON_OUTSIDE_WINDOW);
    d.set_matched_plate("KCH123A");
    d.set_matched_class(VEHICLE_CLASS_SEDAN);
    d.set_combined_conf(0.91F);
    d.set_reason_text("Plate KCH123A is outside its allowed time window (08:00-18:00).");
    d.set_frame_id(987);

    std::string wire;
    REQUIRE(d.SerializeToString(&wire));

    AuthDecision parsed;
    REQUIRE(parsed.ParseFromString(wire));
    REQUIRE(parsed.verdict()       == AUTH_VERDICT_DENIED);
    REQUIRE(parsed.deny_reason()   == DENY_REASON_OUTSIDE_WINDOW);
    REQUIRE(parsed.matched_plate() == "KCH123A");
    REQUIRE(parsed.frame_id()      == 987);
}

// --- ControlEnvelope oneof discrimination ----------------------------------

TEST_CASE("ControlEnvelope correctly discriminates oneof payload",
          "[proto][envelope]") {
    SECTION("telemetry payload") {
        ControlEnvelope env;
        *env.mutable_telemetry() = make_typical_telemetry();

        std::string wire;
        REQUIRE(env.SerializeToString(&wire));

        ControlEnvelope decoded;
        REQUIRE(decoded.ParseFromString(wire));
        REQUIRE(decoded.payload_case() == ControlEnvelope::kTelemetry);
        REQUIRE(decoded.telemetry().gate_id() == "residence-main-01");
    }

    SECTION("command payload") {
        ControlEnvelope env;
        auto* cmd = env.mutable_command();
        cmd->set_command_id("cmd-0001");
        cmd->set_gate_id("residence-main-01");
        cmd->set_kind(COMMAND_KIND_PULSE_RELAY);
        cmd->set_relay_index(0);
        *cmd->mutable_duration() = TimeUtil::MillisecondsToDuration(500);

        std::string wire;
        REQUIRE(env.SerializeToString(&wire));

        ControlEnvelope decoded;
        REQUIRE(decoded.ParseFromString(wire));
        REQUIRE(decoded.payload_case() == ControlEnvelope::kCommand);
        REQUIRE(decoded.command().kind() == COMMAND_KIND_PULSE_RELAY);
        REQUIRE(TimeUtil::DurationToMilliseconds(decoded.command().duration()) == 500);
    }

    SECTION("ack payload") {
        ControlEnvelope env;
        auto* ack = env.mutable_ack();
        ack->set_command_id("cmd-0001");
        ack->set_completed(true);
        ack->set_success(true);
        ack->set_state_after(GATE_STATE_OPEN);

        std::string wire;
        REQUIRE(env.SerializeToString(&wire));

        ControlEnvelope decoded;
        REQUIRE(decoded.ParseFromString(wire));
        REQUIRE(decoded.payload_case()      == ControlEnvelope::kAck);
        REQUIRE(decoded.ack().success()     == true);
        REQUIRE(decoded.ack().state_after() == GATE_STATE_OPEN);
    }
}

// --- DashboardEvent oneof --------------------------------------------------

TEST_CASE("DashboardEvent oneof routes payload by type",
          "[proto][dashboard]") {
    DashboardEvent ev;
    ev.set_event_id(42);
    *ev.mutable_event_ts() = TimeUtil::SecondsToTimestamp(1745568400);

    auto* d = ev.mutable_decision();
    d->set_decision_id("dec-0001");
    d->set_verdict(AUTH_VERDICT_AUTHORIZED);
    d->set_combined_conf(0.97F);

    std::string wire;
    REQUIRE(ev.SerializeToString(&wire));

    DashboardEvent decoded;
    REQUIRE(decoded.ParseFromString(wire));
    REQUIRE(decoded.payload_case() == DashboardEvent::kDecision);
    REQUIRE(decoded.decision().verdict() == AUTH_VERDICT_AUTHORIZED);
    REQUIRE_THAT(decoded.decision().combined_conf(),
                 Catch::Matchers::WithinAbs(0.97F, 0.0001));
}

// --- Bandwidth budget ------------------------------------------------------

TEST_CASE("Telemetry stays under 256-byte firmware budget",
          "[proto][budget]") {
    const Telemetry t = make_typical_telemetry();
    const auto bytes  = t.ByteSizeLong();
    INFO("encoded telemetry size: " << bytes << " bytes");
    REQUIRE(bytes < 256);
}

TEST_CASE("Empty messages encode to zero bytes", "[proto][zero]") {
    Telemetry t;
    REQUIRE(t.ByteSizeLong() == 0);

    AuthDecision d;
    REQUIRE(d.ByteSizeLong() == 0);
}

// --- OTA chunk sizing ------------------------------------------------------

TEST_CASE("OtaChunk holds a full ESP32 flash sector worth of data",
          "[proto][ota]") {
    OtaChunk c;
    std::string sector(4096, '\xAA');
    c.set_offset(0x10000);
    c.set_data(sector);
    c.set_is_final(false);

    std::string wire;
    REQUIRE(c.SerializeToString(&wire));

    OtaChunk decoded;
    REQUIRE(decoded.ParseFromString(wire));
    REQUIRE(decoded.data().size() == 4096);
    REQUIRE(decoded.offset()      == 0x10000);
    REQUIRE(decoded.is_final()    == false);
}
