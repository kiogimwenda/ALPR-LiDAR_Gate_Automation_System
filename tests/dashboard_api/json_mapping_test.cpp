// json_mapping_test.cpp — the dashboard's presentation schema.
//
// These tests are the contract the frontend codes against: field
// names, enum short-forms, and the dual timestamp encoding. A proto
// change that would silently reshape the UI's data breaks here first.

#include "dash_api/json_mapping.hpp"

#include <catch2/catch_test_macros.hpp>

namespace dash = gate::dash_api;

TEST_CASE("timestamps serialise as ISO-8601 UTC plus epoch millis") {
    google::protobuf::Timestamp ts;
    ts.set_seconds(1'751'700'000);  // 2025-07-05T07:20:00Z
    ts.set_nanos(250'000'000);
    const auto v = dash::to_json(ts);
    CHECK(v["ts"].asString() == "2025-07-05T07:20:00.250Z");
    CHECK(v["tsMs"].asInt64() == 1'751'700'000'250LL);
}

TEST_CASE("enum names lose their proto prefixes") {
    CHECK(dash::gate_state_name(gate::v1::GATE_STATE_OPEN) == "OPEN");
    CHECK(dash::gate_state_name(gate::v1::GATE_STATE_UNKNOWN) == "UNKNOWN");
    CHECK(dash::verdict_name(gate::v1::AUTH_VERDICT_AUTHORIZED) == "AUTHORIZED");
    // Out-of-range values render as the raw integer, not a crash.
    CHECK(dash::gate_state_name(static_cast<gate::v1::GateState>(250)) == "250");
}

TEST_CASE("telemetry maps to the camelCase snapshot the UI expects") {
    gate::v1::Telemetry t;
    t.set_gate_id("gate-01");
    t.mutable_sent_ts()->set_seconds(1'751'700'000);
    t.set_gate_state(gate::v1::GATE_STATE_CLOSED);
    t.set_limit_closed(true);
    t.set_safety_beam_clear(true);
    t.set_uptime_sec(3600);
    t.set_firmware_version("0.0.1");
    t.set_seq(41);

    const auto v = dash::to_json(t);
    CHECK(v["gateId"].asString() == "gate-01");
    CHECK(v["state"].asString() == "CLOSED");
    CHECK(v["limitClosed"].asBool());
    CHECK_FALSE(v["limitOpen"].asBool());
    CHECK(v["beamClear"].asBool());
    CHECK(v["uptimeSec"].asUInt() == 3600);
    CHECK(v["fwVersion"].asString() == "0.0.1");
    CHECK(v["seq"].asUInt64() == 41);
    CHECK(v["sent"]["tsMs"].asInt64() == 1'751'700'000'000LL);
}

TEST_CASE("decision events carry verdict, plate, and confidence") {
    gate::v1::AuthDecision d;
    d.set_decision_id("dec-1");
    d.set_gate_id("gate-01");
    d.set_verdict(gate::v1::AUTH_VERDICT_AUTHORIZED);
    d.set_matched_plate("KDA123X");
    d.set_matched_class(gate::v1::VEHICLE_CLASS_SEDAN);
    d.set_combined_conf(0.93F);
    d.set_reason_text("allowlist match");

    const auto v = dash::to_json(d);
    CHECK(v["verdict"].asString() == "AUTHORIZED");
    CHECK(v["matchedPlate"].asString() == "KDA123X");
    CHECK(v["matchedClass"].asString() == "SEDAN");
    CHECK(v["confidence"].asFloat() > 0.92F);
    CHECK(v["reason"].asString() == "allowlist match");
}

TEST_CASE("dashboard events dispatch the payload oneof into type + body") {
    gate::v1::DashboardEvent ev;
    ev.set_event_id(77);
    ev.mutable_event_ts()->set_seconds(1'751'700'000);
    ev.mutable_telemetry()->set_gate_id("gate-02");
    ev.mutable_telemetry()->set_gate_state(gate::v1::GATE_STATE_OPENING);

    const auto v = dash::to_json(ev);
    CHECK(v["eventId"].asUInt64() == 77);
    CHECK(v["type"].asString() == "telemetry");
    CHECK(v["telemetry"]["gateId"].asString() == "gate-02");
    CHECK(v["telemetry"]["state"].asString() == "OPENING");

    gate::v1::DashboardEvent empty;
    CHECK(dash::to_json(empty)["type"].asString() == "unknown");
}

TEST_CASE("allowlist entries round-trip through the presentation schema") {
    Json::Value in;
    in["plate"] = "KDA123X";
    in["ownerName"] = "A. Resident";
    in["ownerUnit"] = "B-12";
    in["allowedClasses"].append("SEDAN");
    Json::Value w;
    w["startMinute"] = 360;
    w["endMinute"] = 1080;
    w["daysMask"] = 31;  // Mon–Fri
    in["timeWindows"].append(w);

    gate::v1::Allowlistentry entry;
    std::string error;
    REQUIRE(dash::allowlist_entry_from_json(in, entry, error));
    CHECK(entry.plate_text() == "KDA123X");
    REQUIRE(entry.allowed_classes_size() == 1);
    CHECK(entry.allowed_classes(0) == gate::v1::VEHICLE_CLASS_SEDAN);
    REQUIRE(entry.time_windows_size() == 1);
    CHECK(entry.time_windows(0).start_minute_of_day() == 360);
    CHECK(entry.time_windows(0).days_of_week_mask() == 31);

    const auto out = dash::to_json(entry);
    CHECK(out["plate"].asString() == "KDA123X");
    CHECK(out["allowedClasses"][0].asString() == "SEDAN");
    CHECK(out["timeWindows"][0]["endMinute"].asUInt() == 1080);
    CHECK(out["ownerUnit"].asString() == "B-12");
}

TEST_CASE("allowlist parsing rejects the bodies the API must refuse") {
    gate::v1::Allowlistentry entry;
    std::string error;

    Json::Value no_plate;
    no_plate["ownerName"] = "x";
    CHECK_FALSE(dash::allowlist_entry_from_json(no_plate, entry, error));
    CHECK(error.find("plate") != std::string::npos);

    Json::Value bad_class;
    bad_class["plate"] = "KAA111A";
    bad_class["allowedClasses"].append("HOVERCRAFT");
    CHECK_FALSE(dash::allowlist_entry_from_json(bad_class, entry, error));
    CHECK(error.find("vehicle class") != std::string::npos);

    Json::Value bad_window;
    bad_window["plate"] = "KAA111A";
    Json::Value w;
    w["startMinute"] = 2000;  // > 1439
    bad_window["timeWindows"].append(w);
    CHECK_FALSE(dash::allowlist_entry_from_json(bad_window, entry, error));
    CHECK(error.find("1439") != std::string::npos);
}

TEST_CASE("command kinds parse from their short names") {
    gate::v1::CommandKind kind{};
    REQUIRE(dash::command_kind_from_string("OPEN_GATE", kind));
    CHECK(kind == gate::v1::COMMAND_KIND_OPEN_GATE);
    REQUIRE(dash::command_kind_from_string("LATCH_CLOSE", kind));
    CHECK(kind == gate::v1::COMMAND_KIND_LATCH_CLOSE);
    CHECK_FALSE(dash::command_kind_from_string("MAKE_COFFEE", kind));
    CHECK_FALSE(dash::command_kind_from_string("UNSPECIFIED", kind));
}

TEST_CASE("acks and faults keep their diagnostic fields") {
    gate::v1::CommandAck a;
    a.set_command_id("cmd-9");
    a.set_completed(true);
    a.set_success(false);
    a.set_error_text("MotorTimeout");
    a.set_state_after(gate::v1::GATE_STATE_FAULT);
    const auto av = dash::to_json(a);
    CHECK(av["completed"].asBool());
    CHECK_FALSE(av["success"].asBool());
    CHECK(av["error"].asString() == "MotorTimeout");
    CHECK(av["stateAfter"].asString() == "FAULT");

    gate::v1::FaultEvent f;
    f.set_code("MOTOR_TIMEOUT");
    f.set_severity(gate::v1::FAULT_SEVERITY_ERROR);
    (*f.mutable_context())["uptime"] = "120";
    const auto fv = dash::to_json(f);
    CHECK(fv["code"].asString() == "MOTOR_TIMEOUT");
    CHECK(fv["severity"].asString() == "ERROR");
    CHECK(fv["context"]["uptime"].asString() == "120");
}
