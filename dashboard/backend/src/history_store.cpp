// history_store.cpp — SQLite persistence + reporting for the operator app.

#include "dash_api/history_store.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>
#include <sqlite3.h>
#include <stdexcept>
#include <utility>
#include <variant>

namespace gate::dash_api {

namespace {

// ---- small sqlite helpers -------------------------------------------------

struct StmtDeleter {
    void operator()(sqlite3_stmt* s) const { sqlite3_finalize(s); }
};
using Stmt = std::unique_ptr<sqlite3_stmt, StmtDeleter>;

[[noreturn]] void throw_db(sqlite3* db, const std::string& what) {
    throw std::runtime_error("history: " + what + ": " + sqlite3_errmsg(db));
}

Stmt prepare(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &s, nullptr) != SQLITE_OK) {
        throw_db(db, "prepare");
    }
    return Stmt{s};
}

void exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err != nullptr ? err : "?";
        sqlite3_free(err);
        throw std::runtime_error(std::string("history: exec: ") + msg);
    }
}

// A bound parameter list built alongside dynamic WHERE clauses.
struct Binds {
    std::vector<std::variant<std::int64_t, double, std::string>> text;
    void add_text(std::string v) { text.emplace_back(std::move(v)); }
    void add_int(std::int64_t v) { text.emplace_back(v); }
    void add_real(double v) { text.emplace_back(v); }
    void apply(sqlite3_stmt* s, int first = 1) const {
        int i = first;
        for (const auto& v : text) {
            if (const auto* n = std::get_if<std::int64_t>(&v)) {
                sqlite3_bind_int64(s, i, *n);
            } else if (const auto* d = std::get_if<double>(&v)) {
                sqlite3_bind_double(s, i, *d);
            } else {
                const auto& str = std::get<std::string>(v);
                sqlite3_bind_text(s, i, str.c_str(), static_cast<int>(str.size()),
                                  SQLITE_TRANSIENT);
            }
            ++i;
        }
    }
};

std::string col_text(sqlite3_stmt* s, int i) {
    const auto* p = sqlite3_column_text(s, i);
    return p != nullptr ? reinterpret_cast<const char*>(p) : std::string{};
}

std::int64_t col_int(sqlite3_stmt* s, int i) {
    return sqlite3_column_int64(s, i);
}

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::int64_t ts_ms(const google::protobuf::Timestamp& t) {
    return t.seconds() * 1000 + t.nanos() / 1'000'000;
}

std::string short_enum(const std::string& full, const char* prefix) {
    const std::string p(prefix);
    return full.rfind(p, 0) == 0 ? full.substr(p.size()) : full;
}

// Plates are compared normalised: uppercase, no spaces / dashes.
std::string normalise_plate(const std::string& in) {
    std::string out;
    for (const char c : in) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

Json::Value stamp(std::int64_t ms) {
    Json::Value v;
    const auto secs = static_cast<std::int64_t>(ms / 1000);
    std::tm tm{};
    const auto t = static_cast<std::time_t>(secs);
    gmtime_r(&t, &tm);
    char buf[40];
    const std::size_t n = std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    std::snprintf(buf + n, sizeof(buf) - n, ".%03dZ", static_cast<int>(ms % 1000));
    v["ts"] = buf;
    v["tsMs"] = Json::Value::Int64{ms};
    return v;
}

std::string iso(std::int64_t ms) {
    return stamp(ms)["ts"].asString();
}

std::string csv_field(const std::string& s) {
    // RFC 4180 quoting, and a leading ' on formula-looking cells so a
    // spreadsheet never executes a plate string (CSV injection).
    std::string v = s;
    if (!v.empty() && (v[0] == '=' || v[0] == '+' || v[0] == '-' || v[0] == '@')) {
        v.insert(v.begin(), '\'');
    }
    if (v.find_first_of(",\"\n\r") == std::string::npos) {
        return v;
    }
    std::string out = "\"";
    for (const char c : v) {
        out += c == '"' ? std::string("\"\"") : std::string(1, c);
    }
    return out + "\"";
}

const char* severity_for_fault(gate::v1::FaultSeverity s) {
    switch (s) {
        case gate::v1::FAULT_SEVERITY_CRITICAL:
            return "critical";
        case gate::v1::FAULT_SEVERITY_ERROR:
            return "high";
        case gate::v1::FAULT_SEVERITY_WARN:
            return "medium";
        default:
            return "low";
    }
}

// "local day" / "local hour" of an epoch-ms timestamp for a UTC offset.
std::int64_t local_secs(std::int64_t ms, int tz_offset_min) {
    return ms / 1000 + static_cast<std::int64_t>(tz_offset_min) * 60;
}

constexpr const char* kDirectionJoin =
    " FROM decisions d LEFT JOIN gates g ON g.gate_id = d.gate_id ";
constexpr const char* kDirectionExpr = "COALESCE(g.direction, 'both')";

}  // namespace

const char* role_name(Role r) {
    switch (r) {
        case Role::kAdmin:
            return "admin";
        case Role::kOperator:
            return "operator";
        case Role::kViewer:
        default:
            return "viewer";
    }
}

std::optional<Role> role_from_name(const std::string& s) {
    if (s == "admin") {
        return Role::kAdmin;
    }
    if (s == "operator") {
        return Role::kOperator;
    }
    if (s == "viewer") {
        return Role::kViewer;
    }
    return std::nullopt;
}

struct HistoryStore::Impl {
    sqlite3* db = nullptr;
    std::mutex mu;
    ~Impl() {
        if (db != nullptr) {
            sqlite3_close(db);
        }
    }

    // Session reconstruction shared by stats() and sessions().
    struct Session {
        std::string plate;
        std::int64_t entry_ms = 0;
        std::int64_t exit_ms = 0;  // 0 = still on site
        std::string entry_gate, exit_gate;
    };
    std::vector<Session> build_sessions(std::int64_t to_ms, std::int64_t timeout_ms);
    std::int64_t session_timeout_ms();
    void add_alert(std::int64_t ts, const std::string& kind, const char* severity,
                   const std::string& gate, const std::string& plate, const std::string& message,
                   const std::string& ref);
    std::string where_for(const DecisionQuery& q, Binds& b);
};

HistoryStore::HistoryStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
HistoryStore::~HistoryStore() = default;

std::unique_ptr<HistoryStore> HistoryStore::open(const std::string& path) {
    auto impl = std::make_unique<Impl>();
    if (sqlite3_open(path.c_str(), &impl->db) != SQLITE_OK) {
        const std::string msg = impl->db != nullptr ? sqlite3_errmsg(impl->db) : "out of memory";
        throw std::runtime_error("history: cannot open " + path + ": " + msg);
    }
    sqlite3_busy_timeout(impl->db, 2000);
    exec(impl->db, "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;");
    exec(impl->db, R"sql(
        CREATE TABLE IF NOT EXISTS decisions (
            id INTEGER PRIMARY KEY,
            decision_id TEXT NOT NULL UNIQUE,
            ts_ms INTEGER NOT NULL,
            gate_id TEXT NOT NULL DEFAULT '',
            plate TEXT NOT NULL DEFAULT '',
            verdict TEXT NOT NULL DEFAULT '',
            deny_reason TEXT NOT NULL DEFAULT '',
            vehicle_class TEXT NOT NULL DEFAULT '',
            confidence REAL NOT NULL DEFAULT 0,
            reason TEXT NOT NULL DEFAULT '',
            actor TEXT NOT NULL DEFAULT '',
            frame_id INTEGER NOT NULL DEFAULT 0);
        CREATE INDEX IF NOT EXISTS idx_decisions_ts ON decisions(ts_ms);
        CREATE INDEX IF NOT EXISTS idx_decisions_plate ON decisions(plate, ts_ms);
        CREATE INDEX IF NOT EXISTS idx_decisions_gate ON decisions(gate_id, ts_ms);
        CREATE TABLE IF NOT EXISTS faults (
            id INTEGER PRIMARY KEY,
            fault_id TEXT NOT NULL UNIQUE,
            ts_ms INTEGER NOT NULL,
            gate_id TEXT NOT NULL DEFAULT '',
            severity TEXT NOT NULL DEFAULT '',
            code TEXT NOT NULL DEFAULT '',
            description TEXT NOT NULL DEFAULT '');
        CREATE INDEX IF NOT EXISTS idx_faults_ts ON faults(ts_ms);
        CREATE TABLE IF NOT EXISTS commands (
            id INTEGER PRIMARY KEY,
            command_id TEXT NOT NULL UNIQUE,
            ts_ms INTEGER NOT NULL,
            gate_id TEXT NOT NULL DEFAULT '',
            kind TEXT NOT NULL DEFAULT '',
            actor TEXT NOT NULL DEFAULT '',
            completed INTEGER NOT NULL DEFAULT 0,
            success INTEGER,
            error TEXT NOT NULL DEFAULT '',
            completed_ms INTEGER);
        CREATE INDEX IF NOT EXISTS idx_commands_ts ON commands(ts_ms);
        CREATE TABLE IF NOT EXISTS alerts (
            id INTEGER PRIMARY KEY,
            ref TEXT NOT NULL UNIQUE,
            ts_ms INTEGER NOT NULL,
            kind TEXT NOT NULL,
            severity TEXT NOT NULL,
            gate_id TEXT NOT NULL DEFAULT '',
            plate TEXT NOT NULL DEFAULT '',
            message TEXT NOT NULL,
            acknowledged_by TEXT,
            acknowledged_ms INTEGER);
        CREATE INDEX IF NOT EXISTS idx_alerts_ts ON alerts(ts_ms);
        CREATE INDEX IF NOT EXISTS idx_alerts_unread ON alerts(acknowledged_ms);
        CREATE TABLE IF NOT EXISTS gates (
            gate_id TEXT PRIMARY KEY,
            name TEXT NOT NULL DEFAULT '',
            location TEXT NOT NULL DEFAULT '',
            direction TEXT NOT NULL DEFAULT 'both'
                CHECK (direction IN ('entry', 'exit', 'both')),
            last_seen_ms INTEGER,
            last_state TEXT NOT NULL DEFAULT '',
            fw_version TEXT NOT NULL DEFAULT '');
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY,
            username TEXT NOT NULL UNIQUE COLLATE NOCASE,
            full_name TEXT NOT NULL DEFAULT '',
            email TEXT NOT NULL DEFAULT '',
            role TEXT NOT NULL CHECK (role IN ('viewer', 'operator', 'admin')),
            active INTEGER NOT NULL DEFAULT 1,
            password_hash TEXT NOT NULL,
            created_ms INTEGER NOT NULL,
            last_login_ms INTEGER NOT NULL DEFAULT 0);
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL);
    )sql");
    return std::unique_ptr<HistoryStore>(new HistoryStore(std::move(impl)));
}

// ---------------------------------------------------------------- ingest ---

void HistoryStore::Impl::add_alert(std::int64_t ts, const std::string& kind, const char* severity,
                                   const std::string& gate, const std::string& plate,
                                   const std::string& message, const std::string& ref) {
    auto s = prepare(db,
                     "INSERT OR IGNORE INTO alerts(ref, ts_ms, kind, severity, gate_id, plate, "
                     "message) VALUES (?,?,?,?,?,?,?)");
    Binds b;
    b.add_text(ref);
    b.add_int(ts);
    b.add_text(kind);
    b.add_text(severity);
    b.add_text(gate);
    b.add_text(plate);
    b.add_text(message);
    b.apply(s.get());
    sqlite3_step(s.get());
}

void HistoryStore::ingest(const gate::v1::DashboardEvent& ev) {
    const std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    const std::int64_t event_ms = ev.has_event_ts() ? ts_ms(ev.event_ts()) : now_ms();

    switch (ev.payload_case()) {
        case gate::v1::DashboardEvent::kDecision: {
            const auto& d = ev.decision();
            if (d.decision_id().empty()) {
                return;
            }
            const std::int64_t t = d.has_decision_ts() ? ts_ms(d.decision_ts()) : event_ms;
            const std::string verdict =
                short_enum(gate::v1::AuthVerdict_Name(d.verdict()), "AUTH_VERDICT_");
            const std::string reason =
                short_enum(gate::v1::DenyReason_Name(d.deny_reason()), "DENY_REASON_");
            const std::string cls =
                short_enum(gate::v1::VehicleClass_Name(d.matched_class()), "VEHICLE_CLASS_");
            const std::string plate = normalise_plate(d.matched_plate());
            auto s = prepare(db,
                             "INSERT OR IGNORE INTO decisions(decision_id, ts_ms, gate_id, plate, "
                             "verdict, deny_reason, vehicle_class, confidence, reason, actor, "
                             "frame_id) VALUES (?,?,?,?,?,?,?,?,?,?,?)");
            Binds b;
            b.add_text(d.decision_id());
            b.add_int(t);
            b.add_text(d.gate_id());
            b.add_text(plate);
            b.add_text(verdict);
            b.add_text(reason);
            b.add_text(cls);
            b.add_real(d.combined_conf());
            b.add_text(d.reason_text());
            b.add_text(d.actor());
            b.add_int(static_cast<std::int64_t>(d.frame_id()));
            b.apply(s.get());
            if (sqlite3_step(s.get()) != SQLITE_DONE) {
                throw_db(db, "insert decision");
            }
            if (sqlite3_changes(db) == 0) {
                return;  // replayed — alerts already derived
            }
            if (d.verdict() == gate::v1::AUTH_VERDICT_DENIED &&
                d.deny_reason() == gate::v1::DENY_REASON_BLOCKLISTED) {
                impl_->add_alert(t, "blocklisted", "critical", d.gate_id(), plate,
                                 "Blocklisted plate " + plate + " at " + d.gate_id(),
                                 "blocklisted:" + d.decision_id());
            } else if (d.verdict() == gate::v1::AUTH_VERDICT_MANUAL_REVIEW) {
                impl_->add_alert(t, "class_mismatch", "high", d.gate_id(), plate,
                                 "Plate " + plate +
                                     " is registered to a different vehicle class (" + cls +
                                     " seen) — held for review",
                                 "class:" + d.decision_id());
            }
            return;
        }
        case gate::v1::DashboardEvent::kFault: {
            const auto& f = ev.fault();
            if (f.fault_id().empty()) {
                return;
            }
            const std::int64_t t = f.has_occurred_ts() ? ts_ms(f.occurred_ts()) : event_ms;
            auto s =
                prepare(db,
                        "INSERT OR IGNORE INTO faults(fault_id, ts_ms, gate_id, severity, code, "
                        "description) VALUES (?,?,?,?,?,?)");
            Binds b;
            b.add_text(f.fault_id());
            b.add_int(t);
            b.add_text(f.gate_id());
            b.add_text(short_enum(gate::v1::FaultSeverity_Name(f.severity()), "FAULT_SEVERITY_"));
            b.add_text(f.code());
            b.add_text(f.description());
            b.apply(s.get());
            sqlite3_step(s.get());
            if (sqlite3_changes(db) > 0 && f.severity() >= gate::v1::FAULT_SEVERITY_WARN) {
                impl_->add_alert(t, "fault", severity_for_fault(f.severity()), f.gate_id(), "",
                                 f.code() + ": " + f.description(), "fault:" + f.fault_id());
            }
            return;
        }
        case gate::v1::DashboardEvent::kCommand: {
            const auto& c = ev.command();
            if (c.command_id().empty()) {
                return;
            }
            auto s =
                prepare(db,
                        "INSERT OR IGNORE INTO commands(command_id, ts_ms, gate_id, kind, actor) "
                        "VALUES (?,?,?,?,?)");
            Binds b;
            b.add_text(c.command_id());
            b.add_int(c.has_issued_ts() ? ts_ms(c.issued_ts()) : event_ms);
            b.add_text(c.gate_id());
            b.add_text(short_enum(gate::v1::CommandKind_Name(c.kind()), "COMMAND_KIND_"));
            b.add_text(c.actor());
            b.apply(s.get());
            sqlite3_step(s.get());
            return;
        }
        case gate::v1::DashboardEvent::kAck: {
            const auto& a = ev.ack();
            if (!a.completed() || a.command_id().empty()) {
                return;
            }
            const std::int64_t t = a.has_completed_ts() ? ts_ms(a.completed_ts()) : event_ms;
            auto s = prepare(db,
                             "UPDATE commands SET completed = 1, success = ?, error = ?, "
                             "completed_ms = ? WHERE command_id = ? AND completed = 0");
            Binds b;
            b.add_int(a.success() ? 1 : 0);
            b.add_text(a.error_text());
            b.add_int(t);
            b.add_text(a.command_id());
            b.apply(s.get());
            sqlite3_step(s.get());
            if (sqlite3_changes(db) > 0 && !a.success()) {
                auto q = prepare(db, "SELECT gate_id, kind FROM commands WHERE command_id = ?");
                sqlite3_bind_text(q.get(), 1, a.command_id().c_str(), -1, SQLITE_TRANSIENT);
                std::string gate, kind;
                if (sqlite3_step(q.get()) == SQLITE_ROW) {
                    gate = col_text(q.get(), 0);
                    kind = col_text(q.get(), 1);
                }
                impl_->add_alert(t, "command_failed", "medium", gate, "",
                                 kind + " failed at " + gate + ": " + a.error_text(),
                                 "ack:" + a.command_id());
            }
            return;
        }
        case gate::v1::DashboardEvent::kTelemetry: {
            const auto& tm = ev.telemetry();
            if (tm.gate_id().empty()) {
                return;
            }
            auto s = prepare(db, R"sql(
                INSERT INTO gates(gate_id, last_seen_ms, last_state, fw_version)
                VALUES (?,?,?,?)
                ON CONFLICT(gate_id) DO UPDATE SET last_seen_ms = excluded.last_seen_ms,
                    last_state = excluded.last_state, fw_version = excluded.fw_version)sql");
            Binds b;
            b.add_text(tm.gate_id());
            b.add_int(event_ms);
            b.add_text(short_enum(gate::v1::GateState_Name(tm.gate_state()), "GATE_STATE_"));
            b.add_text(tm.firmware_version());
            b.apply(s.get());
            sqlite3_step(s.get());
            return;
        }
        default:
            return;  // OTA progress is live-only
    }
}

// ---------------------------------------------------------------- sessions -

std::int64_t HistoryStore::Impl::session_timeout_ms() {
    auto s = prepare(db, "SELECT value FROM settings WHERE key = 'session_timeout_minutes'");
    std::int64_t minutes = 720;
    if (sqlite3_step(s.get()) == SQLITE_ROW) {
        try {
            minutes = std::stoll(col_text(s.get(), 0));
        } catch (...) {
        }
    }
    return std::max<std::int64_t>(minutes, 1) * 60'000;
}

std::vector<HistoryStore::Impl::Session> HistoryStore::Impl::build_sessions(
    std::int64_t to_ms, std::int64_t timeout_ms) {
    // Authorized movements through gates with a direction, oldest first.
    auto s = prepare(db,
                     "SELECT d.plate, d.ts_ms, g.direction, d.gate_id FROM decisions d "
                     "JOIN gates g ON g.gate_id = d.gate_id "
                     "WHERE d.verdict = 'AUTHORIZED' AND d.plate != '' "
                     "AND g.direction IN ('entry', 'exit') AND d.ts_ms < ? "
                     "ORDER BY d.plate, d.ts_ms");
    sqlite3_bind_int64(s.get(), 1, to_ms);
    std::vector<Session> out;
    std::map<std::string, Session> open;
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        const std::string plate = col_text(s.get(), 0);
        const std::int64_t t = col_int(s.get(), 1);
        const std::string dir = col_text(s.get(), 2);
        const std::string gate = col_text(s.get(), 3);
        auto it = open.find(plate);
        if (dir == "entry") {
            if (it != open.end()) {
                // A second entry without an exit: the camera missed the
                // exit. Close the old session as abandoned (no exit time)
                // unless it is a duplicate read within a minute.
                if (t - it->second.entry_ms < 60'000) {
                    continue;
                }
                open.erase(it);
            }
            open[plate] = Session{plate, t, 0, gate, ""};
        } else if (it != open.end()) {
            if (t - it->second.entry_ms <= timeout_ms * 4) {
                it->second.exit_ms = t;
                it->second.exit_gate = gate;
                out.push_back(it->second);
            }
            open.erase(it);
        }
    }
    const std::int64_t horizon = to_ms - timeout_ms;
    for (auto& [plate, sess] : open) {
        if (sess.entry_ms >= horizon) {
            out.push_back(sess);  // still on site
        }
    }
    return out;
}

// ---------------------------------------------------------------- reports --

Json::Value HistoryStore::stats(std::int64_t from_ms, std::int64_t to_ms, int tz_offset_min) {
    const std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    if (to_ms <= 0) {
        to_ms = now_ms();
    }
    Json::Value v;
    {
        auto s =
            prepare(db, std::string("SELECT COUNT(*), COUNT(DISTINCT NULLIF(d.plate, '')), "
                                    "SUM(d.verdict = 'AUTHORIZED'), SUM(d.verdict = 'DENIED'), "
                                    "SUM(d.verdict = 'LOW_CONFIDENCE'), "
                                    "SUM(d.verdict = 'MANUAL_REVIEW'), "
                                    "SUM(d.verdict = 'AUTHORIZED' AND ") +
                            kDirectionExpr + " = 'entry'), SUM(d.verdict = 'AUTHORIZED' AND " +
                            kDirectionExpr + " = 'exit'), AVG(d.confidence)" + kDirectionJoin +
                            "WHERE d.ts_ms >= ? AND d.ts_ms < ?");
        sqlite3_bind_int64(s.get(), 1, from_ms);
        sqlite3_bind_int64(s.get(), 2, to_ms);
        sqlite3_step(s.get());
        v["captures"] = Json::Value::Int64{col_int(s.get(), 0)};
        v["uniqueVehicles"] = Json::Value::Int64{col_int(s.get(), 1)};
        v["authorized"] = Json::Value::Int64{col_int(s.get(), 2)};
        v["denied"] = Json::Value::Int64{col_int(s.get(), 3)};
        v["lowConfidence"] = Json::Value::Int64{col_int(s.get(), 4)};
        v["manualReview"] = Json::Value::Int64{col_int(s.get(), 5)};
        v["entries"] = Json::Value::Int64{col_int(s.get(), 6)};
        v["exits"] = Json::Value::Int64{col_int(s.get(), 7)};
        v["avgConfidence"] = sqlite3_column_double(s.get(), 8);
    }
    {
        auto s = prepare(db,
                         "SELECT ((ts_ms / 1000 + ?) / 3600) % 24 AS h, COUNT(*) AS n "
                         "FROM decisions WHERE ts_ms >= ? AND ts_ms < ? "
                         "GROUP BY h ORDER BY n DESC, h LIMIT 1");
        sqlite3_bind_int64(s.get(), 1, static_cast<std::int64_t>(tz_offset_min) * 60);
        sqlite3_bind_int64(s.get(), 2, from_ms);
        sqlite3_bind_int64(s.get(), 3, to_ms);
        v["peakHour"] = sqlite3_step(s.get()) == SQLITE_ROW
                            ? Json::Value(static_cast<int>(col_int(s.get(), 0)))
                            : Json::Value(Json::nullValue);
    }
    {
        const auto sessions = impl_->build_sessions(to_ms, impl_->session_timeout_ms());
        std::int64_t on_site = 0, done = 0, total_min = 0;
        for (const auto& se : sessions) {
            if (se.exit_ms == 0) {
                ++on_site;
            } else if (se.exit_ms >= from_ms) {
                ++done;
                total_min += (se.exit_ms - se.entry_ms) / 60'000;
            }
        }
        v["currentlyOnSite"] = Json::Value::Int64{on_site};
        v["avgDwellMinutes"] =
            done > 0 ? Json::Value(static_cast<double>(total_min) / static_cast<double>(done))
                     : Json::Value(Json::nullValue);
    }
    {
        auto s = prepare(db, "SELECT COUNT(*) FROM alerts WHERE acknowledged_ms IS NULL");
        sqlite3_step(s.get());
        v["unreadAlerts"] = Json::Value::Int64{col_int(s.get(), 0)};
    }
    {
        auto s = prepare(db, "SELECT COUNT(*), SUM(last_seen_ms >= ?) FROM gates");
        sqlite3_bind_int64(s.get(), 1, now_ms() - 15'000);
        sqlite3_step(s.get());
        v["gates"] = Json::Value::Int64{col_int(s.get(), 0)};
        v["gatesOnline"] = Json::Value::Int64{col_int(s.get(), 1)};
    }
    v["from"] = stamp(from_ms);
    v["to"] = stamp(to_ms);
    return v;
}

Json::Value HistoryStore::hourly(std::int64_t from_ms, std::int64_t to_ms, int tz_offset_min) {
    const std::lock_guard lk{impl_->mu};
    if (to_ms <= 0) {
        to_ms = now_ms();
    }
    Json::Value hours(Json::arrayValue);
    for (int h = 0; h < 24; ++h) {
        Json::Value b;
        b["hour"] = h;
        b["captures"] = 0;
        b["uniqueVehicles"] = 0;
        b["authorized"] = 0;
        b["denied"] = 0;
        b["entries"] = 0;
        b["exits"] = 0;
        hours.append(b);
    }
    auto s = prepare(
        impl_->db,
        std::string("SELECT ((d.ts_ms / 1000 + ?) / 3600) % 24 AS h, COUNT(*), "
                    "COUNT(DISTINCT NULLIF(d.plate, '')), SUM(d.verdict = 'AUTHORIZED'), "
                    "SUM(d.verdict != 'AUTHORIZED'), SUM(d.verdict = 'AUTHORIZED' AND ") +
            kDirectionExpr + " = 'entry'), SUM(d.verdict = 'AUTHORIZED' AND " + kDirectionExpr +
            " = 'exit')" + kDirectionJoin + "WHERE d.ts_ms >= ? AND d.ts_ms < ? GROUP BY h");
    sqlite3_bind_int64(s.get(), 1, static_cast<std::int64_t>(tz_offset_min) * 60);
    sqlite3_bind_int64(s.get(), 2, from_ms);
    sqlite3_bind_int64(s.get(), 3, to_ms);
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        const auto h = static_cast<Json::ArrayIndex>(col_int(s.get(), 0));
        auto& b = hours[h];
        b["captures"] = Json::Value::Int64{col_int(s.get(), 1)};
        b["uniqueVehicles"] = Json::Value::Int64{col_int(s.get(), 2)};
        b["authorized"] = Json::Value::Int64{col_int(s.get(), 3)};
        b["denied"] = Json::Value::Int64{col_int(s.get(), 4)};
        b["entries"] = Json::Value::Int64{col_int(s.get(), 5)};
        b["exits"] = Json::Value::Int64{col_int(s.get(), 6)};
    }
    return hours;
}

Json::Value HistoryStore::daily(std::int64_t from_ms, std::int64_t to_ms, int tz_offset_min) {
    const std::lock_guard lk{impl_->mu};
    if (to_ms <= 0) {
        to_ms = now_ms();
    }
    const std::int64_t off = static_cast<std::int64_t>(tz_offset_min) * 60;
    std::map<std::int64_t, Json::Value> days;
    for (std::int64_t day = local_secs(from_ms, tz_offset_min) / 86400;
         day <= local_secs(to_ms - 1, tz_offset_min) / 86400 && days.size() < 400; ++day) {
        Json::Value b;
        const auto t = static_cast<std::time_t>(day * 86400);
        std::tm tm{};
        gmtime_r(&t, &tm);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        b["date"] = buf;
        b["captures"] = 0;
        b["uniqueVehicles"] = 0;
        b["authorized"] = 0;
        b["denied"] = 0;
        days[day] = b;
    }
    auto s = prepare(impl_->db,
                     "SELECT (ts_ms / 1000 + ?) / 86400 AS day, COUNT(*), "
                     "COUNT(DISTINCT NULLIF(plate, '')), SUM(verdict = 'AUTHORIZED'), "
                     "SUM(verdict != 'AUTHORIZED') FROM decisions "
                     "WHERE ts_ms >= ? AND ts_ms < ? GROUP BY day");
    sqlite3_bind_int64(s.get(), 1, off);
    sqlite3_bind_int64(s.get(), 2, from_ms);
    sqlite3_bind_int64(s.get(), 3, to_ms);
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        auto it = days.find(col_int(s.get(), 0));
        if (it == days.end()) {
            continue;
        }
        it->second["captures"] = Json::Value::Int64{col_int(s.get(), 1)};
        it->second["uniqueVehicles"] = Json::Value::Int64{col_int(s.get(), 2)};
        it->second["authorized"] = Json::Value::Int64{col_int(s.get(), 3)};
        it->second["denied"] = Json::Value::Int64{col_int(s.get(), 4)};
    }
    Json::Value out(Json::arrayValue);
    for (auto& [day, b] : days) {
        out.append(b);
    }
    return out;
}

Json::Value HistoryStore::classes(std::int64_t from_ms, std::int64_t to_ms) {
    const std::lock_guard lk{impl_->mu};
    if (to_ms <= 0) {
        to_ms = now_ms();
    }
    auto s =
        prepare(impl_->db,
                "SELECT CASE vehicle_class WHEN '' THEN 'UNKNOWN' ELSE vehicle_class END AS c, "
                "COUNT(*) AS n FROM decisions WHERE ts_ms >= ? AND ts_ms < ? "
                "GROUP BY c ORDER BY n DESC");
    sqlite3_bind_int64(s.get(), 1, from_ms);
    sqlite3_bind_int64(s.get(), 2, to_ms);
    Json::Value out(Json::arrayValue);
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        Json::Value r;
        r["vehicleClass"] = col_text(s.get(), 0);
        r["count"] = Json::Value::Int64{col_int(s.get(), 1)};
        out.append(r);
    }
    return out;
}

Json::Value HistoryStore::verdicts(std::int64_t from_ms, std::int64_t to_ms) {
    const std::lock_guard lk{impl_->mu};
    if (to_ms <= 0) {
        to_ms = now_ms();
    }
    Json::Value out;
    Json::Value by_verdict(Json::arrayValue);
    Json::Value by_reason(Json::arrayValue);
    {
        auto s = prepare(impl_->db,
                         "SELECT verdict, COUNT(*) AS n FROM decisions WHERE ts_ms >= ? AND "
                         "ts_ms < ? GROUP BY verdict ORDER BY n DESC");
        sqlite3_bind_int64(s.get(), 1, from_ms);
        sqlite3_bind_int64(s.get(), 2, to_ms);
        while (sqlite3_step(s.get()) == SQLITE_ROW) {
            Json::Value r;
            r["verdict"] = col_text(s.get(), 0);
            r["count"] = Json::Value::Int64{col_int(s.get(), 1)};
            by_verdict.append(r);
        }
    }
    {
        auto s = prepare(impl_->db,
                         "SELECT deny_reason, COUNT(*) AS n FROM decisions WHERE ts_ms >= ? AND "
                         "ts_ms < ? AND verdict != 'AUTHORIZED' GROUP BY deny_reason "
                         "ORDER BY n DESC");
        sqlite3_bind_int64(s.get(), 1, from_ms);
        sqlite3_bind_int64(s.get(), 2, to_ms);
        while (sqlite3_step(s.get()) == SQLITE_ROW) {
            Json::Value r;
            r["reason"] = col_text(s.get(), 0);
            r["count"] = Json::Value::Int64{col_int(s.get(), 1)};
            by_reason.append(r);
        }
    }
    out["byVerdict"] = by_verdict;
    out["byReason"] = by_reason;
    return out;
}

Json::Value HistoryStore::top_plates(std::int64_t from_ms, std::int64_t to_ms,
                                     std::uint32_t limit) {
    const std::lock_guard lk{impl_->mu};
    if (to_ms <= 0) {
        to_ms = now_ms();
    }
    auto s = prepare(impl_->db,
                     "SELECT plate, COUNT(*) AS n, MIN(ts_ms), MAX(ts_ms), "
                     "COUNT(DISTINCT ts_ms / 86400000), "
                     "(SELECT verdict FROM decisions d2 WHERE d2.plate = d.plate "
                     " ORDER BY ts_ms DESC LIMIT 1) "
                     "FROM decisions d WHERE plate != '' AND ts_ms >= ? AND ts_ms < ? "
                     "GROUP BY plate ORDER BY n DESC, MAX(ts_ms) DESC LIMIT ?");
    sqlite3_bind_int64(s.get(), 1, from_ms);
    sqlite3_bind_int64(s.get(), 2, to_ms);
    sqlite3_bind_int64(s.get(), 3, std::clamp<std::uint32_t>(limit, 1, 500));
    Json::Value out(Json::arrayValue);
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        Json::Value r;
        r["plate"] = col_text(s.get(), 0);
        r["visits"] = Json::Value::Int64{col_int(s.get(), 1)};
        r["firstSeen"] = stamp(col_int(s.get(), 2));
        r["lastSeen"] = stamp(col_int(s.get(), 3));
        r["days"] = Json::Value::Int64{col_int(s.get(), 4)};
        r["lastVerdict"] = col_text(s.get(), 5);
        out.append(r);
    }
    return out;
}

Json::Value HistoryStore::sessions(std::int64_t from_ms, std::int64_t to_ms) {
    const std::lock_guard lk{impl_->mu};
    if (to_ms <= 0) {
        to_ms = now_ms();
    }
    const auto all = impl_->build_sessions(to_ms, impl_->session_timeout_ms());
    // Dwell-time categories, as an operator reads them.
    struct Cat {
        const char* label;
        std::int64_t max_min;
        std::int64_t count = 0;
    };
    Cat cats[] = {{"< 15 min", 15},
                  {"15–60 min", 60},
                  {"1–4 h", 240},
                  {"4–12 h", 720},
                  {"> 12 h", INT64_MAX}};
    Json::Value completed(Json::arrayValue);
    Json::Value on_site(Json::arrayValue);
    std::vector<Impl::Session> done;
    for (const auto& se : all) {
        if (se.exit_ms == 0) {
            Json::Value r;
            r["plate"] = se.plate;
            r["entered"] = stamp(se.entry_ms);
            r["entryGate"] = se.entry_gate;
            r["minutesOnSite"] = Json::Value::Int64{(to_ms - se.entry_ms) / 60'000};
            on_site.append(r);
        } else if (se.exit_ms >= from_ms) {
            done.push_back(se);
            const std::int64_t m = (se.exit_ms - se.entry_ms) / 60'000;
            for (auto& c : cats) {
                if (m < c.max_min) {
                    ++c.count;
                    break;
                }
            }
        }
    }
    std::sort(done.begin(), done.end(), [](const auto& a, const auto& b) {
        return (a.exit_ms - a.entry_ms) > (b.exit_ms - b.entry_ms);
    });
    std::int64_t total = 0;
    for (const auto& se : done) {
        total += se.exit_ms - se.entry_ms;
        if (completed.size() < 50) {
            Json::Value r;
            r["plate"] = se.plate;
            r["entered"] = stamp(se.entry_ms);
            r["exited"] = stamp(se.exit_ms);
            r["minutes"] = Json::Value::Int64{(se.exit_ms - se.entry_ms) / 60'000};
            r["entryGate"] = se.entry_gate;
            r["exitGate"] = se.exit_gate;
            completed.append(r);
        }
    }
    Json::Value categories(Json::arrayValue);
    for (const auto& c : cats) {
        Json::Value r;
        r["category"] = c.label;
        r["count"] = Json::Value::Int64{c.count};
        categories.append(r);
    }
    Json::Value out;
    out["completed"] = Json::Value::Int64{static_cast<std::int64_t>(done.size())};
    out["avgMinutes"] =
        done.empty()
            ? Json::Value(Json::nullValue)
            : Json::Value(static_cast<double>(total) / 60'000.0 / static_cast<double>(done.size()));
    out["categories"] = categories;
    out["longest"] = completed;
    out["onSite"] = on_site;
    return out;
}

std::string HistoryStore::Impl::where_for(const DecisionQuery& q, Binds& b) {
    std::string w = " WHERE d.ts_ms >= ? AND d.ts_ms < ?";
    b.add_int(q.from_ms);
    b.add_int(q.to_ms > 0 ? q.to_ms : now_ms());
    if (const std::string p = normalise_plate(q.plate); !p.empty()) {
        w += " AND d.plate LIKE ?";
        b.add_text("%" + p + "%");
    }
    if (!q.gate_id.empty()) {
        w += " AND d.gate_id = ?";
        b.add_text(q.gate_id);
    }
    if (!q.verdict.empty()) {
        w += " AND d.verdict = ?";
        b.add_text(q.verdict);
    }
    if (!q.vehicle_class.empty()) {
        w += " AND d.vehicle_class = ?";
        b.add_text(q.vehicle_class);
    }
    if (!q.direction.empty()) {
        w += std::string(" AND ") + kDirectionExpr + " = ?";
        b.add_text(q.direction);
    }
    if (q.min_confidence) {
        w += " AND d.confidence >= ?";
        b.add_real(*q.min_confidence);
    }
    return w;
}

namespace {

constexpr const char* kDecisionColumns =
    "SELECT d.id, d.decision_id, d.ts_ms, d.gate_id, d.plate, d.verdict, d.deny_reason, "
    "d.vehicle_class, d.confidence, d.reason, d.actor, COALESCE(g.direction, 'both'), "
    "COALESCE(NULLIF(g.name, ''), d.gate_id)";

Json::Value decision_row(sqlite3_stmt* s) {
    Json::Value r;
    r["id"] = Json::Value::Int64{col_int(s, 0)};
    r["decisionId"] = col_text(s, 1);
    r["decided"] = stamp(col_int(s, 2));
    r["gateId"] = col_text(s, 3);
    r["plate"] = col_text(s, 4);
    r["verdict"] = col_text(s, 5);
    r["denyReason"] = col_text(s, 6);
    r["vehicleClass"] = col_text(s, 7);
    r["confidence"] = sqlite3_column_double(s, 8);
    r["reason"] = col_text(s, 9);
    r["actor"] = col_text(s, 10);
    r["direction"] = col_text(s, 11);
    r["gateName"] = col_text(s, 12);
    return r;
}

}  // namespace

Json::Value HistoryStore::search(const DecisionQuery& q) {
    const std::lock_guard lk{impl_->mu};
    Binds b;
    const std::string where = impl_->where_for(q, b);
    Json::Value out;
    {
        auto s = prepare(impl_->db, std::string("SELECT COUNT(*)") + kDirectionJoin + where);
        b.apply(s.get());
        sqlite3_step(s.get());
        out["total"] = Json::Value::Int64{col_int(s.get(), 0)};
    }
    auto s = prepare(impl_->db, std::string(kDecisionColumns) + kDirectionJoin + where +
                                    " ORDER BY d.ts_ms DESC LIMIT ? OFFSET ?");
    b.apply(s.get());
    const int n = static_cast<int>(b.text.size());
    sqlite3_bind_int64(s.get(), n + 1, std::clamp<std::uint32_t>(q.limit, 1, 1000));
    sqlite3_bind_int64(s.get(), n + 2, q.offset);
    Json::Value items(Json::arrayValue);
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        items.append(decision_row(s.get()));
    }
    out["items"] = items;
    out["offset"] = q.offset;
    return out;
}

std::string HistoryStore::export_csv(const DecisionQuery& q) {
    const std::lock_guard lk{impl_->mu};
    Binds b;
    const std::string where = impl_->where_for(q, b);
    auto s = prepare(impl_->db, std::string(kDecisionColumns) + kDirectionJoin + where +
                                    " ORDER BY d.ts_ms DESC LIMIT ?");
    b.apply(s.get());
    sqlite3_bind_int64(s.get(), static_cast<int>(b.text.size()) + 1,
                       std::clamp<std::uint32_t>(q.limit, 1, 100'000));
    std::string out =
        "time_utc,gate,direction,plate,verdict,deny_reason,vehicle_class,confidence,reason,actor\n";
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        char conf[16];
        std::snprintf(conf, sizeof(conf), "%.3f", sqlite3_column_double(s.get(), 8));
        out += csv_field(iso(col_int(s.get(), 2))) + "," + csv_field(col_text(s.get(), 12)) + "," +
               csv_field(col_text(s.get(), 11)) + "," + csv_field(col_text(s.get(), 4)) + "," +
               csv_field(col_text(s.get(), 5)) + "," + csv_field(col_text(s.get(), 6)) + "," +
               csv_field(col_text(s.get(), 7)) + "," + conf + "," +
               csv_field(col_text(s.get(), 9)) + "," + csv_field(col_text(s.get(), 10)) + "\n";
    }
    return out;
}

Json::Value HistoryStore::rejected(std::int64_t from_ms, std::int64_t to_ms, std::uint32_t limit,
                                   std::uint32_t offset) {
    const std::lock_guard lk{impl_->mu};
    if (to_ms <= 0) {
        to_ms = now_ms();
    }
    const std::string where =
        " WHERE d.ts_ms >= ? AND d.ts_ms < ? AND (d.verdict = 'LOW_CONFIDENCE' OR "
        "d.deny_reason = 'NO_PLATE_FOUND')";
    Json::Value out;
    {
        auto s = prepare(impl_->db, std::string("SELECT COUNT(*)") + kDirectionJoin + where);
        sqlite3_bind_int64(s.get(), 1, from_ms);
        sqlite3_bind_int64(s.get(), 2, to_ms);
        sqlite3_step(s.get());
        out["total"] = Json::Value::Int64{col_int(s.get(), 0)};
    }
    {
        auto s =
            prepare(impl_->db, std::string("SELECT d.deny_reason, COUNT(*) AS n") + kDirectionJoin +
                                   where + " GROUP BY d.deny_reason ORDER BY n DESC");
        sqlite3_bind_int64(s.get(), 1, from_ms);
        sqlite3_bind_int64(s.get(), 2, to_ms);
        Json::Value breakdown(Json::arrayValue);
        while (sqlite3_step(s.get()) == SQLITE_ROW) {
            Json::Value r;
            r["reason"] = col_text(s.get(), 0);
            r["count"] = Json::Value::Int64{col_int(s.get(), 1)};
            breakdown.append(r);
        }
        out["breakdown"] = breakdown;
    }
    auto s = prepare(impl_->db, std::string(kDecisionColumns) + kDirectionJoin + where +
                                    " ORDER BY d.ts_ms DESC LIMIT ? OFFSET ?");
    sqlite3_bind_int64(s.get(), 1, from_ms);
    sqlite3_bind_int64(s.get(), 2, to_ms);
    sqlite3_bind_int64(s.get(), 3, std::clamp<std::uint32_t>(limit, 1, 500));
    sqlite3_bind_int64(s.get(), 4, offset);
    Json::Value items(Json::arrayValue);
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        items.append(decision_row(s.get()));
    }
    out["items"] = items;
    return out;
}

Json::Value HistoryStore::plate_history(const std::string& plate_in, std::uint32_t limit) {
    const std::string plate = normalise_plate(plate_in);
    Json::Value out;
    out["plate"] = plate;
    {
        const std::lock_guard lk{impl_->mu};
        auto s = prepare(impl_->db,
                         "SELECT COUNT(*), MIN(ts_ms), MAX(ts_ms), SUM(verdict = 'AUTHORIZED'), "
                         "COUNT(DISTINCT ts_ms / 86400000) FROM decisions WHERE plate = ?");
        sqlite3_bind_text(s.get(), 1, plate.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(s.get());
        out["visits"] = Json::Value::Int64{col_int(s.get(), 0)};
        if (col_int(s.get(), 0) > 0) {
            out["firstSeen"] = stamp(col_int(s.get(), 1));
            out["lastSeen"] = stamp(col_int(s.get(), 2));
        }
        out["authorized"] = Json::Value::Int64{col_int(s.get(), 3)};
        out["days"] = Json::Value::Int64{col_int(s.get(), 4)};

        auto h = prepare(impl_->db, std::string(kDecisionColumns) + kDirectionJoin +
                                        " WHERE d.plate = ? ORDER BY d.ts_ms DESC LIMIT ?");
        sqlite3_bind_text(h.get(), 1, plate.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(h.get(), 2, std::clamp<std::uint32_t>(limit, 1, 500));
        Json::Value items(Json::arrayValue);
        while (sqlite3_step(h.get()) == SQLITE_ROW) {
            items.append(decision_row(h.get()));
        }
        out["history"] = items;
    }
    // Sessions for this plate (all time).
    Json::Value plate_sessions(Json::arrayValue);
    {
        const std::lock_guard lk{impl_->mu};
        for (const auto& se : impl_->build_sessions(now_ms(), impl_->session_timeout_ms())) {
            if (se.plate != plate) {
                continue;
            }
            Json::Value r;
            r["entered"] = stamp(se.entry_ms);
            if (se.exit_ms != 0) {
                r["exited"] = stamp(se.exit_ms);
                r["minutes"] = Json::Value::Int64{(se.exit_ms - se.entry_ms) / 60'000};
            } else {
                r["onSite"] = true;
            }
            plate_sessions.append(r);
        }
    }
    out["sessions"] = plate_sessions;
    return out;
}

// ---------------------------------------------------------------- alerts ---

Json::Value HistoryStore::alerts(bool unread_only, std::uint32_t limit, std::uint32_t offset) {
    const std::lock_guard lk{impl_->mu};
    const std::string where = unread_only ? " WHERE acknowledged_ms IS NULL" : "";
    Json::Value out;
    {
        auto s = prepare(impl_->db, "SELECT COUNT(*) FROM alerts" + where);
        sqlite3_step(s.get());
        out["total"] = Json::Value::Int64{col_int(s.get(), 0)};
    }
    auto s = prepare(impl_->db,
                     "SELECT id, ts_ms, kind, severity, gate_id, plate, message, acknowledged_by, "
                     "acknowledged_ms FROM alerts" +
                         where + " ORDER BY ts_ms DESC, id DESC LIMIT ? OFFSET ?");
    sqlite3_bind_int64(s.get(), 1, std::clamp<std::uint32_t>(limit, 1, 500));
    sqlite3_bind_int64(s.get(), 2, offset);
    Json::Value items(Json::arrayValue);
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        Json::Value r;
        r["id"] = Json::Value::Int64{col_int(s.get(), 0)};
        r["raised"] = stamp(col_int(s.get(), 1));
        r["kind"] = col_text(s.get(), 2);
        r["severity"] = col_text(s.get(), 3);
        r["gateId"] = col_text(s.get(), 4);
        r["plate"] = col_text(s.get(), 5);
        r["message"] = col_text(s.get(), 6);
        if (sqlite3_column_type(s.get(), 8) != SQLITE_NULL) {
            r["acknowledgedBy"] = col_text(s.get(), 7);
            r["acknowledged"] = stamp(col_int(s.get(), 8));
        }
        items.append(r);
    }
    out["items"] = items;
    return out;
}

std::uint32_t HistoryStore::unread_alerts() {
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, "SELECT COUNT(*) FROM alerts WHERE acknowledged_ms IS NULL");
    sqlite3_step(s.get());
    return static_cast<std::uint32_t>(col_int(s.get(), 0));
}

bool HistoryStore::acknowledge_alert(std::int64_t id, const std::string& by) {
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db,
                     "UPDATE alerts SET acknowledged_by = ?, acknowledged_ms = ? "
                     "WHERE id = ? AND acknowledged_ms IS NULL");
    sqlite3_bind_text(s.get(), 1, by.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s.get(), 2, now_ms());
    sqlite3_bind_int64(s.get(), 3, id);
    sqlite3_step(s.get());
    if (sqlite3_changes(impl_->db) > 0) {
        return true;
    }
    auto e = prepare(impl_->db, "SELECT 1 FROM alerts WHERE id = ?");
    sqlite3_bind_int64(e.get(), 1, id);
    return sqlite3_step(e.get()) == SQLITE_ROW;  // already acknowledged = fine
}

std::uint32_t HistoryStore::acknowledge_all(const std::string& by) {
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db,
                     "UPDATE alerts SET acknowledged_by = ?, acknowledged_ms = ? "
                     "WHERE acknowledged_ms IS NULL");
    sqlite3_bind_text(s.get(), 1, by.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s.get(), 2, now_ms());
    sqlite3_step(s.get());
    return static_cast<std::uint32_t>(sqlite3_changes(impl_->db));
}

// ---------------------------------------------------------------- gates ----

Json::Value HistoryStore::gates() {
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db,
                     "SELECT gate_id, name, location, direction, last_seen_ms, last_state, "
                     "fw_version FROM gates ORDER BY gate_id");
    Json::Value out(Json::arrayValue);
    const std::int64_t now = now_ms();
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        Json::Value r;
        r["gateId"] = col_text(s.get(), 0);
        r["name"] = col_text(s.get(), 1);
        r["location"] = col_text(s.get(), 2);
        r["direction"] = col_text(s.get(), 3);
        if (sqlite3_column_type(s.get(), 4) != SQLITE_NULL) {
            r["lastSeen"] = stamp(col_int(s.get(), 4));
            r["online"] = now - col_int(s.get(), 4) < 15'000;
        } else {
            r["online"] = false;
        }
        r["lastState"] = col_text(s.get(), 5);
        r["fwVersion"] = col_text(s.get(), 6);
        out.append(r);
    }
    return out;
}

bool HistoryStore::configure_gate(const std::string& gate_id, const std::string& name,
                                  const std::string& location, const std::string& direction,
                                  std::string& error) {
    if (gate_id.empty() || gate_id.size() > 64) {
        error = "gate id must be 1–64 characters";
        return false;
    }
    if (direction != "entry" && direction != "exit" && direction != "both") {
        error = "direction must be entry, exit or both";
        return false;
    }
    if (name.size() > 120 || location.size() > 120) {
        error = "name and location are limited to 120 characters";
        return false;
    }
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, R"sql(
        INSERT INTO gates(gate_id, name, location, direction) VALUES (?,?,?,?)
        ON CONFLICT(gate_id) DO UPDATE SET name = excluded.name,
            location = excluded.location, direction = excluded.direction)sql");
    Binds b;
    b.add_text(gate_id);
    b.add_text(name);
    b.add_text(location);
    b.add_text(direction);
    b.apply(s.get());
    if (sqlite3_step(s.get()) != SQLITE_DONE) {
        error = sqlite3_errmsg(impl_->db);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- users ----

namespace {

UserRecord user_row(sqlite3_stmt* s) {
    UserRecord u;
    u.id = col_int(s, 0);
    u.username = col_text(s, 1);
    u.full_name = col_text(s, 2);
    u.email = col_text(s, 3);
    u.role = role_from_name(col_text(s, 4)).value_or(Role::kViewer);
    u.active = col_int(s, 5) != 0;
    u.password_hash = col_text(s, 6);
    u.created_ms = col_int(s, 7);
    u.last_login_ms = col_int(s, 8);
    return u;
}

constexpr const char* kUserColumns =
    "SELECT id, username, full_name, email, role, active, password_hash, created_ms, "
    "last_login_ms FROM users";

bool valid_username(const std::string& u) {
    if (u.empty() || u.size() > 64) {
        return false;
    }
    return std::all_of(u.begin(), u.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' || c == '-';
    });
}

}  // namespace

std::vector<UserRecord> HistoryStore::users() {
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, std::string(kUserColumns) + " ORDER BY username");
    std::vector<UserRecord> out;
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        out.push_back(user_row(s.get()));
    }
    return out;
}

std::optional<UserRecord> HistoryStore::user_by_name(const std::string& username) {
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, std::string(kUserColumns) + " WHERE username = ?");
    sqlite3_bind_text(s.get(), 1, username.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(s.get()) == SQLITE_ROW) {
        return user_row(s.get());
    }
    return std::nullopt;
}

std::optional<UserRecord> HistoryStore::user_by_id(std::int64_t id) {
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, std::string(kUserColumns) + " WHERE id = ?");
    sqlite3_bind_int64(s.get(), 1, id);
    if (sqlite3_step(s.get()) == SQLITE_ROW) {
        return user_row(s.get());
    }
    return std::nullopt;
}

bool HistoryStore::save_user(UserRecord& u, std::string& error) {
    if (!valid_username(u.username)) {
        error = "username must be 1–64 letters, digits, '.', '_' or '-'";
        return false;
    }
    if (u.full_name.size() > 120 || u.email.size() > 254) {
        error = "name or email too long";
        return false;
    }
    const std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    // Never leave the site without an active admin.
    if (u.id != 0 && (u.role != Role::kAdmin || !u.active)) {
        auto s = prepare(
            db, "SELECT COUNT(*) FROM users WHERE role = 'admin' AND active = 1 AND id != ?");
        sqlite3_bind_int64(s.get(), 1, u.id);
        sqlite3_step(s.get());
        auto me = prepare(db, "SELECT role, active FROM users WHERE id = ?");
        sqlite3_bind_int64(me.get(), 1, u.id);
        const bool was_admin = sqlite3_step(me.get()) == SQLITE_ROW &&
                               col_text(me.get(), 0) == "admin" && col_int(me.get(), 1) != 0;
        if (was_admin && col_int(s.get(), 0) == 0) {
            error = "this is the last active admin — add another admin first";
            return false;
        }
    }
    if (u.id == 0) {
        if (u.password_hash.empty()) {
            error = "a new user needs a password";
            return false;
        }
        auto s =
            prepare(db,
                    "INSERT INTO users(username, full_name, email, role, active, password_hash, "
                    "created_ms) VALUES (?,?,?,?,?,?,?)");
        Binds b;
        b.add_text(u.username);
        b.add_text(u.full_name);
        b.add_text(u.email);
        b.add_text(role_name(u.role));
        b.add_int(u.active ? 1 : 0);
        b.add_text(u.password_hash);
        b.add_int(u.created_ms = now_ms());
        b.apply(s.get());
        if (sqlite3_step(s.get()) != SQLITE_DONE) {
            error = sqlite3_extended_errcode(db) == SQLITE_CONSTRAINT_UNIQUE
                        ? "that username is taken"
                        : sqlite3_errmsg(db);
            return false;
        }
        u.id = sqlite3_last_insert_rowid(db);
        return true;
    }
    auto s =
        prepare(db,
                "UPDATE users SET username = ?, full_name = ?, email = ?, role = ?, active = ?, "
                "password_hash = CASE WHEN ? = '' THEN password_hash ELSE ? END WHERE id = ?");
    Binds b;
    b.add_text(u.username);
    b.add_text(u.full_name);
    b.add_text(u.email);
    b.add_text(role_name(u.role));
    b.add_int(u.active ? 1 : 0);
    b.add_text(u.password_hash);
    b.add_text(u.password_hash);
    b.add_int(u.id);
    b.apply(s.get());
    if (sqlite3_step(s.get()) != SQLITE_DONE) {
        error = sqlite3_extended_errcode(db) == SQLITE_CONSTRAINT_UNIQUE ? "that username is taken"
                                                                         : sqlite3_errmsg(db);
        return false;
    }
    if (sqlite3_changes(db) == 0) {
        error = "no such user";
        return false;
    }
    return true;
}

bool HistoryStore::delete_user(std::int64_t id, std::string& error) {
    const std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    auto me = prepare(db, "SELECT role, active FROM users WHERE id = ?");
    sqlite3_bind_int64(me.get(), 1, id);
    if (sqlite3_step(me.get()) != SQLITE_ROW) {
        error = "no such user";
        return false;
    }
    if (col_text(me.get(), 0) == "admin" && col_int(me.get(), 1) != 0) {
        auto s = prepare(
            db, "SELECT COUNT(*) FROM users WHERE role = 'admin' AND active = 1 AND id != ?");
        sqlite3_bind_int64(s.get(), 1, id);
        sqlite3_step(s.get());
        if (col_int(s.get(), 0) == 0) {
            error = "this is the last active admin — add another admin first";
            return false;
        }
    }
    auto s = prepare(db, "DELETE FROM users WHERE id = ?");
    sqlite3_bind_int64(s.get(), 1, id);
    sqlite3_step(s.get());
    return true;
}

void HistoryStore::touch_login(std::int64_t id) {
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, "UPDATE users SET last_login_ms = ? WHERE id = ?");
    sqlite3_bind_int64(s.get(), 1, now_ms());
    sqlite3_bind_int64(s.get(), 2, id);
    sqlite3_step(s.get());
}

std::size_t HistoryStore::user_count() {
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, "SELECT COUNT(*) FROM users");
    sqlite3_step(s.get());
    return static_cast<std::size_t>(col_int(s.get(), 0));
}

// ---------------------------------------------------------------- settings -

std::string HistoryStore::setting(const std::string& key, const std::string& fallback) {
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, "SELECT value FROM settings WHERE key = ?");
    sqlite3_bind_text(s.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
    return sqlite3_step(s.get()) == SQLITE_ROW ? col_text(s.get(), 0) : fallback;
}

void HistoryStore::set_setting(const std::string& key, const std::string& value) {
    const std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db,
                     "INSERT INTO settings(key, value) VALUES (?, ?) "
                     "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    sqlite3_bind_text(s.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s.get(), 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(s.get());
}

std::uint64_t HistoryStore::purge(std::uint32_t retention_days, std::int64_t now) {
    const std::lock_guard lk{impl_->mu};
    const std::int64_t cutoff = now - static_cast<std::int64_t>(retention_days) * 86'400'000;
    std::uint64_t removed = 0;
    for (const char* sql :
         {"DELETE FROM decisions WHERE ts_ms < ?", "DELETE FROM faults WHERE ts_ms < ?",
          "DELETE FROM commands WHERE ts_ms < ?",
          "DELETE FROM alerts WHERE ts_ms < ? AND acknowledged_ms IS NOT NULL"}) {
        auto s = prepare(impl_->db, sql);
        sqlite3_bind_int64(s.get(), 1, cutoff);
        sqlite3_step(s.get());
        removed += static_cast<std::uint64_t>(sqlite3_changes(impl_->db));
    }
    return removed;
}

// ---------------------------------------------------------------- global ---

namespace {
std::shared_ptr<HistoryStore> g_history;  // NOLINT — set once in main()
}

void set_history(std::shared_ptr<HistoryStore> h) {
    g_history = std::move(h);
}

std::shared_ptr<HistoryStore> history() {
    return g_history;
}

}  // namespace gate::dash_api
