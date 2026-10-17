// history_store.hpp — the dashboard's durable record of what happened.
//
// gate-server's broadcaster is a 4096-event ring: perfect for "what is
// happening now", useless for "how many vehicles came in last week".
// The HistoryStore persists every decision, fault, command and ack the
// Subscribe stream delivers into SQLite and answers the reporting
// questions the operator app asks — the feature set of a production
// ALPR site console (dashboard, live feed, reports, search, alerts,
// rejected reads, users, gate configuration):
//
//   decisions  one row per AuthDecision (≈ an ALPR "capture")
//   faults     FaultEvents from the field controllers
//   commands   GateCommands (+ completion from their CommandAck)
//   alerts     derived on ingest: blocklisted plate, class mismatch
//              (swapped plate), fault, failed command — acknowledgeable
//   gates      operator configuration per gate: display name,
//              location, direction (entry / exit / both)
//   users      operator accounts with roles (viewer / operator / admin)
//   settings   key → value (retention, …)
//
// Entry/exit: a decision that AUTHORIZED a vehicle at a gate configured
// "entry" counts as an entry, at an "exit" gate as an exit. Sessions
// (dwell time, who is on site) pair a plate's entry with its next exit;
// gates left at "both" contribute captures but not sessions.
//
// Idempotent ingest: rows are keyed by the event's own UUID
// (decision_id, fault_id, command_id), so the stream's reconnect
// replays and a dashboard restart (which re-reads gate-server's ring)
// never double-count.
//
// Times are Unix milliseconds (UTC). Reports that bucket by hour or day
// take the client's UTC offset in minutes so "09:00" means local 09:00.
//
// Thread-safe: one connection guarded by a mutex (the write rate is a
// handful of events per second; reports are small aggregate queries).
// No Drogon here — host-tested in tests/dashboard_api/history_store_test.cpp.

#pragma once

#include <cstdint>
#include <json/json.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gate_service.pb.h"

namespace gate::dash_api {

enum class Role : std::uint8_t { kViewer = 0, kOperator = 1, kAdmin = 2 };

const char* role_name(Role r);
std::optional<Role> role_from_name(const std::string& s);

struct UserRecord {
    std::int64_t id = 0;
    std::string username;
    std::string full_name;
    std::string email;
    Role role = Role::kViewer;
    bool active = true;
    std::string password_hash;  // pbkdf2-sha256$…; never serialised
    std::int64_t created_ms = 0;
    std::int64_t last_login_ms = 0;
};

// Filters for search / export. Empty strings and nullopt mean "any".
struct DecisionQuery {
    std::int64_t from_ms = 0;
    std::int64_t to_ms = 0;  // exclusive; 0 = now
    std::string plate;       // substring, case-insensitive, normalised
    std::string gate_id;
    std::string verdict;        // AUTHORIZED | DENIED | LOW_CONFIDENCE | MANUAL_REVIEW
    std::string vehicle_class;  // SEDAN | SUV | …
    std::string direction;      // entry | exit (via gate configuration)
    std::optional<float> min_confidence;
    std::uint32_t limit = 100;
    std::uint32_t offset = 0;
};

class HistoryStore {
public:
    // ":memory:" for tests. Creates/migrates the schema; throws
    // std::runtime_error if the database cannot be opened.
    static std::unique_ptr<HistoryStore> open(const std::string& path);
    ~HistoryStore();
    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    // ---- ingest -----------------------------------------------------------
    void ingest(const gate::v1::DashboardEvent& ev);

    // ---- reports (JSON in the presentation schema) -------------------------
    // Headline numbers for [from, to): captures, unique plates, verdict
    // counts, entries, exits, currently on site, average dwell, peak hour,
    // average confidence, unread alerts.
    Json::Value stats(std::int64_t from_ms, std::int64_t to_ms, int tz_offset_min);
    // 24 hourly buckets (local hour): captures, unique, authorized,
    // denied, entries, exits.
    Json::Value hourly(std::int64_t from_ms, std::int64_t to_ms, int tz_offset_min);
    // One bucket per local day in range.
    Json::Value daily(std::int64_t from_ms, std::int64_t to_ms, int tz_offset_min);
    // Captures per vehicle class.
    Json::Value classes(std::int64_t from_ms, std::int64_t to_ms);
    // Captures per verdict and per deny reason.
    Json::Value verdicts(std::int64_t from_ms, std::int64_t to_ms);
    // Most frequent plates: visits, first/last seen, last verdict.
    Json::Value top_plates(std::int64_t from_ms, std::int64_t to_ms, std::uint32_t limit);
    // Completed sessions in range (entry→exit pairs), dwell categories,
    // longest sessions; plus vehicles currently on site.
    Json::Value sessions(std::int64_t from_ms, std::int64_t to_ms);
    // Filtered, paged decisions: {"total": n, "items": [...]}.
    Json::Value search(const DecisionQuery& q);
    // The same rows as CSV (header + one line per decision, no paging cap
    // beyond `q.limit`).
    std::string export_csv(const DecisionQuery& q);
    // Frames the fusion engine could not act on (LOW_CONFIDENCE and
    // NO_PLATE_FOUND), paged, with a breakdown by reason.
    Json::Value rejected(std::int64_t from_ms, std::int64_t to_ms, std::uint32_t limit,
                         std::uint32_t offset);
    // Everything known about one plate: visits, verdict history, sessions.
    Json::Value plate_history(const std::string& plate, std::uint32_t limit);

    // ---- alerts ------------------------------------------------------------
    Json::Value alerts(bool unread_only, std::uint32_t limit, std::uint32_t offset);
    std::uint32_t unread_alerts();
    // false if no such alert.
    bool acknowledge_alert(std::int64_t id, const std::string& by);
    std::uint32_t acknowledge_all(const std::string& by);

    // ---- gates -------------------------------------------------------------
    // Known gates: every gate that ever reported or was configured.
    Json::Value gates();
    // Upsert display configuration. `direction` must be entry|exit|both.
    bool configure_gate(const std::string& gate_id, const std::string& name,
                        const std::string& location, const std::string& direction,
                        std::string& error);

    // ---- users -------------------------------------------------------------
    std::vector<UserRecord> users();
    std::optional<UserRecord> user_by_name(const std::string& username);
    std::optional<UserRecord> user_by_id(std::int64_t id);
    // Inserts (id == 0) or updates. An empty password_hash on update keeps
    // the stored one. Fails on duplicate username or a last-admin demotion.
    bool save_user(UserRecord& u, std::string& error);
    bool delete_user(std::int64_t id, std::string& error);
    void touch_login(std::int64_t id);
    std::size_t user_count();

    // ---- settings / housekeeping ------------------------------------------
    std::string setting(const std::string& key, const std::string& fallback);
    void set_setting(const std::string& key, const std::string& value);
    // Deletes decisions / faults / commands / acknowledged alerts older
    // than `retention_days`. Returns rows removed.
    std::uint64_t purge(std::uint32_t retention_days, std::int64_t now_ms);

private:
    struct Impl;
    explicit HistoryStore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// Process-wide accessor (same pattern as bridge() / auth()).
void set_history(std::shared_ptr<HistoryStore> h);
std::shared_ptr<HistoryStore> history();

}  // namespace gate::dash_api
