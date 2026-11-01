// allowlist_store.cpp

#include "auth/allowlist_store.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <sqlite3.h>
#include <utility>

namespace gate::auth {

namespace {

// --- Schema -----------------------------------------------------------------
//
// Versioned via a 1-row schema_version table. New migrations append a new
// `upgrade_to_N` block. We never write down-migrations — operators roll
// forward with backups, not by executing speculative SQL.
constexpr int kSchemaVersion = 3;

constexpr const char* kUpgradeTo3 = R"sql(
CREATE TABLE gate_configs (
    gate_id TEXT PRIMARY KEY, config BLOB NOT NULL, version INTEGER NOT NULL,
    status INTEGER NOT NULL DEFAULT 0, updated_by TEXT NOT NULL, updated_ts INTEGER NOT NULL
);
)sql";

constexpr const char* kUpgradeTo2 = R"sql(
ALTER TABLE blocklist ADD COLUMN added_by TEXT NOT NULL DEFAULT '';
CREATE TABLE allowlist_gates (
    site_id TEXT, plate_text TEXT, gate_id TEXT,
    PRIMARY KEY (site_id, plate_text, gate_id),
    FOREIGN KEY (site_id, plate_text)
        REFERENCES allowlist(site_id, plate_text) ON DELETE CASCADE
);
CREATE TABLE guest_passes (
    pass_id TEXT PRIMARY KEY, site_id TEXT NOT NULL, plate_text TEXT NOT NULL,
    guest_name TEXT NOT NULL DEFAULT '', host TEXT NOT NULL DEFAULT '',
    kind TEXT NOT NULL CHECK (kind IN ('single','until')),
    valid_from INTEGER NOT NULL, valid_until INTEGER NOT NULL,
    entries_used INTEGER NOT NULL DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'active'
        CHECK (status IN ('active','used','expired','revoked')),
    expected_make TEXT, expected_model TEXT, expected_colour TEXT,
    created_by TEXT NOT NULL, created_ts INTEGER NOT NULL
);
CREATE INDEX guest_passes_lookup ON guest_passes(site_id, plate_text, status);
CREATE TABLE guest_pass_gates (
    pass_id TEXT REFERENCES guest_passes ON DELETE CASCADE,
    gate_id TEXT, PRIMARY KEY (pass_id, gate_id)
);
CREATE TABLE guest_pass_reservations (
    command_id TEXT PRIMARY KEY,
    pass_id TEXT NOT NULL REFERENCES guest_passes ON DELETE CASCADE,
    ts INTEGER NOT NULL
);
CREATE TABLE controller_events (
    gate_id TEXT NOT NULL, boot_id INTEGER NOT NULL, seq INTEGER NOT NULL,
    uptime_ms INTEGER NOT NULL, unix_ms INTEGER NOT NULL, kind INTEGER NOT NULL,
    source INTEGER NOT NULL, side INTEGER NOT NULL, result INTEGER NOT NULL,
    state_after INTEGER NOT NULL, request_id BLOB,
    PRIMARY KEY (gate_id, boot_id, seq)
);
CREATE TABLE open_audit (
    id INTEGER PRIMARY KEY AUTOINCREMENT, gate_id TEXT NOT NULL, request_id TEXT NOT NULL,
    side INTEGER NOT NULL, source INTEGER NOT NULL, actor TEXT NOT NULL DEFAULT '',
    decision_id TEXT NOT NULL DEFAULT '', queued_ms INTEGER NOT NULL DEFAULT 0,
    preempted_by TEXT NOT NULL DEFAULT '', outcome TEXT NOT NULL, ts INTEGER NOT NULL
);
)sql";

constexpr const char* kSchemaSql = R"sql(
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA synchronous  = NORMAL;

CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS allowlist (
    site_id     TEXT    NOT NULL,
    plate_text  TEXT    NOT NULL,
    owner_name  TEXT    NOT NULL DEFAULT '',
    owner_unit  TEXT    NOT NULL DEFAULT '',
    valid_from  INTEGER NOT NULL DEFAULT 0,
    valid_until INTEGER NOT NULL DEFAULT 0,
    notes       TEXT    NOT NULL DEFAULT '',
    added_by    TEXT    NOT NULL DEFAULT '',
    added_ts    INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (site_id, plate_text)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS allowlist_classes (
    site_id       TEXT    NOT NULL,
    plate_text    TEXT    NOT NULL,
    vehicle_class INTEGER NOT NULL,
    PRIMARY KEY (site_id, plate_text, vehicle_class),
    FOREIGN KEY (site_id, plate_text)
        REFERENCES allowlist(site_id, plate_text) ON DELETE CASCADE
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS allowlist_windows (
    site_id              TEXT    NOT NULL,
    plate_text           TEXT    NOT NULL,
    start_minute_of_day  INTEGER NOT NULL,
    end_minute_of_day    INTEGER NOT NULL,
    days_of_week_mask    INTEGER NOT NULL,
    FOREIGN KEY (site_id, plate_text)
        REFERENCES allowlist(site_id, plate_text) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS allowlist_windows_lookup
    ON allowlist_windows(site_id, plate_text);

CREATE TABLE IF NOT EXISTS blocklist (
    site_id    TEXT    NOT NULL,
    plate_text TEXT    NOT NULL,
    reason     TEXT    NOT NULL DEFAULT '',
    added_ts   INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (site_id, plate_text)
) WITHOUT ROWID;
)sql";

// Bind helpers — the sqlite C API uses 1-based indices.
void bind_text(sqlite3_stmt* stmt, int idx, std::string_view s) {
    sqlite3_bind_text(stmt, idx, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
}
void bind_int64(sqlite3_stmt* stmt, int idx, std::int64_t v) {
    sqlite3_bind_int64(stmt, idx, v);
}
void bind_int(sqlite3_stmt* stmt, int idx, int v) {
    sqlite3_bind_int(stmt, idx, v);
}

std::string column_text(sqlite3_stmt* stmt, int idx) {
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, idx));
    const auto n = sqlite3_column_bytes(stmt, idx);
    return p ? std::string{p, static_cast<std::size_t>(n)} : std::string{};
}

[[noreturn]] void throw_db(sqlite3* db, const std::string& what) {
    const std::string msg = what + ": " + (db ? sqlite3_errmsg(db) : "no handle");
    throw AuthDbException(msg);
}

// RAII wrapper around sqlite3_stmt — finalize() on scope exit.
struct Stmt {
    sqlite3_stmt* p = nullptr;
    Stmt() = default;
    explicit Stmt(sqlite3_stmt* s) : p(s) {}
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    Stmt(Stmt&& o) noexcept : p(std::exchange(o.p, nullptr)) {}
    Stmt& operator=(Stmt&& o) noexcept {
        if (this != &o) {
            if (p)
                sqlite3_finalize(p);
            p = std::exchange(o.p, nullptr);
        }
        return *this;
    }
    ~Stmt() {
        if (p)
            sqlite3_finalize(p);
    }
    sqlite3_stmt* get() const noexcept { return p; }
};

Stmt prepare(sqlite3* db, std::string_view sql) {
    sqlite3_stmt* s = nullptr;
    const auto rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &s, nullptr);
    if (rc != SQLITE_OK)
        throw_db(db, "prepare failed");
    return Stmt{s};
}

void exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    const auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        const std::string msg = err ? err : sqlite3_errmsg(db);
        sqlite3_free(err);
        throw AuthDbException("exec failed: " + msg);
    }
}

void run_migrations(sqlite3* db, const std::string& path) {
    exec(db, kSchemaSql);

    // Read the current version (zero rows on a fresh DB).
    int current = 0;
    {
        auto s = prepare(db, "SELECT version FROM schema_version LIMIT 1");
        if (sqlite3_step(s.get()) == SQLITE_ROW) {
            current = sqlite3_column_int(s.get(), 0);
        }
    }
    if (current == kSchemaVersion)
        return;
    if (current > kSchemaVersion) {
        throw AuthDbException("schema_version " + std::to_string(current) +
                              " is newer than this binary supports (" +
                              std::to_string(kSchemaVersion) + ")");
    }

    // Automatic backup of an existing file DB, before any change. A
    // backup left by an earlier failed attempt holds the same data.
    if (current > 0 && path != ":memory:") {
        const std::string backup = path + ".v" + std::to_string(current) + ".bak";
        if (!std::filesystem::exists(backup)) {
            auto v = prepare(db, "VACUUM INTO ?");
            bind_text(v.get(), 1, backup);
            if (sqlite3_step(v.get()) != SQLITE_DONE)
                throw_db(db, "backup before migration failed");
        }
    }

    // v0 -> v1 needs no DDL: IF NOT EXISTS already made the v1 schema.
    exec(db, "BEGIN IMMEDIATE");
    try {
        if (current < 2) {
            exec(db, kUpgradeTo2);
        }
        exec(db, kUpgradeTo3);
        exec(db, "DELETE FROM schema_version");
        auto s = prepare(db, "INSERT INTO schema_version(version) VALUES (?)");
        bind_int(s.get(), 1, kSchemaVersion);
        if (sqlite3_step(s.get()) != SQLITE_DONE)
            throw_db(db, "schema_version write failed");
        exec(db, "COMMIT");
    } catch (...) {
        exec(db, "ROLLBACK");
        throw;
    }
}

}  // namespace

// --- Free helpers -----------------------------------------------------------

std::string normalize_plate(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        const auto u = static_cast<unsigned char>(c);
        if (u == ' ' || u == '\t' || u == '-' || u == '_')
            continue;
        if (u >= 'a' && u <= 'z') {
            out.push_back(static_cast<char>(u - ('a' - 'A')));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

namespace {

// Half-open window check: `[start, end)` minutes-of-day, with day-of-week
// gate. ISO mask convention: bit0 = Mon … bit6 = Sun (per the .proto).
bool window_contains(const gate::v1::TimeWindow& w, int minute_of_day, int iso_dow_bit) {
    if (!(w.days_of_week_mask() & (1u << iso_dow_bit)))
        return false;
    const auto start = static_cast<int>(w.start_minute_of_day());
    const auto end = static_cast<int>(w.end_minute_of_day());
    // Wrap-around windows (e.g., 22:00–02:00) are encoded as end < start.
    if (start <= end)
        return minute_of_day >= start && minute_of_day < end;
    return minute_of_day >= start || minute_of_day < end;
}

bool class_allowed(const gate::v1::Allowlistentry& e, gate::v1::VehicleClass cls) {
    if (e.allowed_classes_size() == 0)
        return true;
    for (int i = 0; i < e.allowed_classes_size(); ++i) {
        if (e.allowed_classes(i) == cls)
            return true;
    }
    return false;
}

}  // namespace

bool is_allowed_now(const gate::v1::Allowlistentry& entry, gate::v1::VehicleClass vehicle_class,
                    std::time_t now_unix, const std::tm& now_local) {
    // 1. Validity-window dates.
    if (entry.has_valid_from() && entry.valid_from().seconds() > 0 &&
        now_unix < entry.valid_from().seconds()) {
        return false;
    }
    if (entry.has_valid_until() && entry.valid_until().seconds() > 0 &&
        now_unix >= entry.valid_until().seconds()) {
        return false;
    }
    // 2. Vehicle class restriction.
    if (!class_allowed(entry, vehicle_class))
        return false;
    // 3. Time-of-day windows.
    if (entry.time_windows_size() == 0)
        return true;
    const int minute_of_day = now_local.tm_hour * 60 + now_local.tm_min;
    // tm_wday: Sun=0..Sat=6. ISO bit: Mon=0..Sun=6.
    const int iso_bit = (now_local.tm_wday + 6) % 7;
    for (int i = 0; i < entry.time_windows_size(); ++i) {
        if (window_contains(entry.time_windows(i), minute_of_day, iso_bit)) {
            return true;
        }
    }
    return false;
}

// --- Impl -------------------------------------------------------------------

struct AllowlistStore::Impl {
    sqlite3* db = nullptr;
    mutable std::mutex mu;

    ~Impl() {
        if (db)
            sqlite3_close(db);
    }
};

AllowlistStore::AllowlistStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AllowlistStore::AllowlistStore(AllowlistStore&&) noexcept = default;
AllowlistStore& AllowlistStore::operator=(AllowlistStore&&) noexcept = default;
AllowlistStore::~AllowlistStore() = default;

AllowlistStore AllowlistStore::open(const std::filesystem::path& path) {
    auto impl = std::make_unique<Impl>();
    const auto path_str = path.string();
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path_str.c_str(), &impl->db, flags, nullptr) != SQLITE_OK) {
        const std::string msg = impl->db ? sqlite3_errmsg(impl->db) : "unknown";
        throw AuthDbException("AllowlistStore::open(" + path_str + "): " + msg);
    }
    run_migrations(impl->db, path_str);
    return AllowlistStore{std::move(impl)};
}

AllowlistStore::UpsertStats AllowlistStore::upsert(
    const std::string& site_id, std::span<const gate::v1::Allowlistentry> entries) {
    std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    UpsertStats stats;

    exec(db, "BEGIN IMMEDIATE");
    try {
        auto count = prepare(db, "SELECT 1 FROM allowlist WHERE site_id=? AND plate_text=?");
        auto upsert_main = prepare(db, R"sql(
            INSERT INTO allowlist (
                site_id, plate_text, owner_name, owner_unit,
                valid_from, valid_until, notes, added_by, added_ts
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(site_id, plate_text) DO UPDATE SET
                owner_name  = excluded.owner_name,
                owner_unit  = excluded.owner_unit,
                valid_from  = excluded.valid_from,
                valid_until = excluded.valid_until,
                notes       = excluded.notes,
                added_by    = excluded.added_by,
                added_ts    = excluded.added_ts
        )sql");
        auto del_classes =
            prepare(db, "DELETE FROM allowlist_classes WHERE site_id=? AND plate_text=?");
        auto ins_class = prepare(db,
                                 "INSERT OR IGNORE INTO allowlist_classes "
                                 "(site_id, plate_text, vehicle_class) VALUES (?, ?, ?)");
        auto del_windows =
            prepare(db, "DELETE FROM allowlist_windows WHERE site_id=? AND plate_text=?");
        auto ins_window = prepare(db, R"sql(
            INSERT INTO allowlist_windows (
                site_id, plate_text,
                start_minute_of_day, end_minute_of_day, days_of_week_mask
            ) VALUES (?, ?, ?, ?, ?)
        )sql");

        for (const auto& e : entries) {
            const std::string plate = normalize_plate(e.plate_text());
            if (plate.empty()) {
                throw AuthDbException("upsert: plate_text is empty after normalization");
            }

            // Existed before?
            sqlite3_reset(count.get());
            sqlite3_clear_bindings(count.get());
            bind_text(count.get(), 1, site_id);
            bind_text(count.get(), 2, plate);
            const bool existed = sqlite3_step(count.get()) == SQLITE_ROW;

            // Main row.
            sqlite3_reset(upsert_main.get());
            sqlite3_clear_bindings(upsert_main.get());
            bind_text(upsert_main.get(), 1, site_id);
            bind_text(upsert_main.get(), 2, plate);
            bind_text(upsert_main.get(), 3, e.owner_name());
            bind_text(upsert_main.get(), 4, e.owner_unit());
            bind_int64(upsert_main.get(), 5, e.has_valid_from() ? e.valid_from().seconds() : 0);
            bind_int64(upsert_main.get(), 6, e.has_valid_until() ? e.valid_until().seconds() : 0);
            bind_text(upsert_main.get(), 7, e.notes());
            bind_text(upsert_main.get(), 8, e.added_by());
            bind_int64(upsert_main.get(), 9, e.has_added_ts() ? e.added_ts().seconds() : 0);
            if (sqlite3_step(upsert_main.get()) != SQLITE_DONE)
                throw_db(db, "upsert main");

            // Classes — wipe and rewrite. Set semantics, not append.
            sqlite3_reset(del_classes.get());
            sqlite3_clear_bindings(del_classes.get());
            bind_text(del_classes.get(), 1, site_id);
            bind_text(del_classes.get(), 2, plate);
            if (sqlite3_step(del_classes.get()) != SQLITE_DONE)
                throw_db(db, "del classes");

            for (int i = 0; i < e.allowed_classes_size(); ++i) {
                sqlite3_reset(ins_class.get());
                sqlite3_clear_bindings(ins_class.get());
                bind_text(ins_class.get(), 1, site_id);
                bind_text(ins_class.get(), 2, plate);
                bind_int(ins_class.get(), 3, static_cast<int>(e.allowed_classes(i)));
                if (sqlite3_step(ins_class.get()) != SQLITE_DONE)
                    throw_db(db, "ins class");
            }

            // Windows — same wipe-and-rewrite policy.
            sqlite3_reset(del_windows.get());
            sqlite3_clear_bindings(del_windows.get());
            bind_text(del_windows.get(), 1, site_id);
            bind_text(del_windows.get(), 2, plate);
            if (sqlite3_step(del_windows.get()) != SQLITE_DONE)
                throw_db(db, "del windows");

            for (int i = 0; i < e.time_windows_size(); ++i) {
                const auto& w = e.time_windows(i);
                sqlite3_reset(ins_window.get());
                sqlite3_clear_bindings(ins_window.get());
                bind_text(ins_window.get(), 1, site_id);
                bind_text(ins_window.get(), 2, plate);
                bind_int(ins_window.get(), 3, static_cast<int>(w.start_minute_of_day()));
                bind_int(ins_window.get(), 4, static_cast<int>(w.end_minute_of_day()));
                bind_int64(ins_window.get(), 5, static_cast<std::int64_t>(w.days_of_week_mask()));
                if (sqlite3_step(ins_window.get()) != SQLITE_DONE)
                    throw_db(db, "ins window");
            }

            if (existed) {
                ++stats.updated;
            } else {
                ++stats.inserted;
            }
        }
        exec(db, "COMMIT");
    } catch (...) {
        exec(db, "ROLLBACK");
        throw;
    }
    return stats;
}

std::optional<gate::v1::Allowlistentry> AllowlistStore::lookup(const std::string& site_id,
                                                               std::string_view plate_text) const {
    std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    const std::string plate = normalize_plate(plate_text);

    auto main = prepare(db, R"sql(
        SELECT plate_text, owner_name, owner_unit, valid_from, valid_until,
               notes, added_by, added_ts
        FROM allowlist
        WHERE site_id = ? AND plate_text = ?
    )sql");
    bind_text(main.get(), 1, site_id);
    bind_text(main.get(), 2, plate);
    const auto rc = sqlite3_step(main.get());
    if (rc == SQLITE_DONE)
        return std::nullopt;
    if (rc != SQLITE_ROW)
        throw_db(db, "lookup");

    gate::v1::Allowlistentry e;
    e.set_plate_text(column_text(main.get(), 0));
    e.set_owner_name(column_text(main.get(), 1));
    e.set_owner_unit(column_text(main.get(), 2));
    if (const auto vf = sqlite3_column_int64(main.get(), 3); vf > 0) {
        e.mutable_valid_from()->set_seconds(vf);
    }
    if (const auto vu = sqlite3_column_int64(main.get(), 4); vu > 0) {
        e.mutable_valid_until()->set_seconds(vu);
    }
    e.set_notes(column_text(main.get(), 5));
    e.set_added_by(column_text(main.get(), 6));
    if (const auto at = sqlite3_column_int64(main.get(), 7); at > 0) {
        e.mutable_added_ts()->set_seconds(at);
    }

    auto classes = prepare(db,
                           "SELECT vehicle_class FROM allowlist_classes "
                           "WHERE site_id=? AND plate_text=? ORDER BY vehicle_class");
    bind_text(classes.get(), 1, site_id);
    bind_text(classes.get(), 2, plate);
    while (sqlite3_step(classes.get()) == SQLITE_ROW) {
        e.add_allowed_classes(
            static_cast<gate::v1::VehicleClass>(sqlite3_column_int(classes.get(), 0)));
    }

    auto windows = prepare(db, R"sql(
        SELECT start_minute_of_day, end_minute_of_day, days_of_week_mask
        FROM allowlist_windows
        WHERE site_id=? AND plate_text=?
        ORDER BY start_minute_of_day
    )sql");
    bind_text(windows.get(), 1, site_id);
    bind_text(windows.get(), 2, plate);
    while (sqlite3_step(windows.get()) == SQLITE_ROW) {
        auto* w = e.add_time_windows();
        w->set_start_minute_of_day(
            static_cast<std::uint32_t>(sqlite3_column_int(windows.get(), 0)));
        w->set_end_minute_of_day(static_cast<std::uint32_t>(sqlite3_column_int(windows.get(), 1)));
        w->set_days_of_week_mask(
            static_cast<std::uint32_t>(sqlite3_column_int64(windows.get(), 2)));
    }
    return e;
}

bool AllowlistStore::is_blocklisted(const std::string& site_id, std::string_view plate_text) const {
    std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    const std::string plate = normalize_plate(plate_text);
    auto s = prepare(db, "SELECT 1 FROM blocklist WHERE site_id=? AND plate_text=?");
    bind_text(s.get(), 1, site_id);
    bind_text(s.get(), 2, plate);
    return sqlite3_step(s.get()) == SQLITE_ROW;
}

std::uint32_t AllowlistStore::blocklist_upsert(const std::string& site_id,
                                               std::string_view plate_text, std::string_view reason,
                                               std::string_view added_by) {
    std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    const std::string plate = normalize_plate(plate_text);
    if (plate.empty()) {
        throw AuthDbException("blocklist_upsert: plate_text is empty after normalization");
    }
    auto s = prepare(db, R"sql(
        INSERT INTO blocklist (site_id, plate_text, reason, added_ts, added_by)
        VALUES (?, ?, ?, strftime('%s','now'), ?)
        ON CONFLICT(site_id, plate_text) DO UPDATE SET
            reason = excluded.reason,
            added_by = excluded.added_by,
            added_ts = excluded.added_ts
    )sql");
    bind_text(s.get(), 1, site_id);
    bind_text(s.get(), 2, plate);
    bind_text(s.get(), 3, reason);
    bind_text(s.get(), 4, added_by);
    if (sqlite3_step(s.get()) != SQLITE_DONE)
        throw_db(db, "blocklist_upsert");
    return 1;
}

AllowlistStore::BlockPage AllowlistStore::blocklist_list(const std::string& site_id,
                                                         std::uint32_t page_size,
                                                         std::string_view page_token) const {
    std::lock_guard lk{impl_->mu};
    const int limit = static_cast<int>(std::clamp<std::uint32_t>(page_size, 1, 1000));
    auto s = prepare(impl_->db, R"sql(
        SELECT plate_text, reason, added_by, added_ts FROM blocklist
        WHERE site_id=? AND plate_text>? ORDER BY plate_text LIMIT ?
    )sql");
    bind_text(s.get(), 1, site_id);
    bind_text(s.get(), 2, page_token);
    bind_int(s.get(), 3, limit + 1);
    BlockPage page;
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        if (static_cast<int>(page.entries.size()) == limit) {
            page.next_token = page.entries.back().plate_text();
            break;
        }
        auto& e = page.entries.emplace_back();
        e.set_plate_text(column_text(s.get(), 0));
        e.set_reason(column_text(s.get(), 1));
        e.set_added_by(column_text(s.get(), 2));
        e.mutable_added_ts()->set_seconds(sqlite3_column_int64(s.get(), 3));
    }
    return page;
}

namespace {

constexpr const char* kPassColumns =
    "pass_id, site_id, plate_text, guest_name, host, kind, valid_from, valid_until, "
    "entries_used, status, created_by";

gate::v1::GuestPass read_pass(sqlite3* db, sqlite3_stmt* s) {
    gate::v1::GuestPass p;
    p.set_pass_id(column_text(s, 0));
    p.set_site_id(column_text(s, 1));
    p.set_plate_text(column_text(s, 2));
    p.set_guest_name(column_text(s, 3));
    p.set_host(column_text(s, 4));
    p.set_kind(column_text(s, 5) == "single" ? gate::v1::GuestPass::SINGLE_VISIT
                                             : gate::v1::GuestPass::UNTIL);
    p.mutable_valid_from()->set_seconds(sqlite3_column_int64(s, 6));
    p.mutable_valid_until()->set_seconds(sqlite3_column_int64(s, 7));
    p.set_entries_used(static_cast<std::uint32_t>(sqlite3_column_int(s, 8)));
    p.set_status(column_text(s, 9));
    p.set_created_by(column_text(s, 10));
    auto g = prepare(db, "SELECT gate_id FROM guest_pass_gates WHERE pass_id=?");
    bind_text(g.get(), 1, p.pass_id());
    while (sqlite3_step(g.get()) == SQLITE_ROW) {
        p.add_gate_ids(column_text(g.get(), 0));
    }
    return p;
}

}  // namespace

gate::v1::GuestPass AllowlistStore::guest_pass_create(gate::v1::GuestPass pass,
                                                      std::time_t now_unix) {
    pass.set_plate_text(normalize_plate(pass.plate_text()));
    if (pass.plate_text().empty() || pass.pass_id().empty() || pass.site_id().empty()) {
        throw AuthDbException("guest pass needs pass_id, site_id and plate_text");
    }
    if (pass.kind() == gate::v1::GuestPass::UNSPECIFIED) {
        throw AuthDbException("guest pass needs a kind");
    }
    if (!pass.has_valid_from() || pass.valid_from().seconds() == 0) {
        pass.mutable_valid_from()->set_seconds(now_unix);
    }
    if (!pass.has_valid_until() || pass.valid_until().seconds() == 0) {
        if (pass.kind() == gate::v1::GuestPass::UNTIL) {
            throw AuthDbException("an until pass needs valid_until");
        }
        pass.mutable_valid_until()->set_seconds(pass.valid_from().seconds() + 86'400);
    }
    pass.set_status("active");
    std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    exec(db, "BEGIN IMMEDIATE");
    try {
        auto s = prepare(db, R"sql(
            INSERT INTO guest_passes (pass_id, site_id, plate_text, guest_name, host, kind,
                valid_from, valid_until, expected_make, expected_model, expected_colour,
                created_by, created_ts) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)
        )sql");
        bind_text(s.get(), 1, pass.pass_id());
        bind_text(s.get(), 2, pass.site_id());
        bind_text(s.get(), 3, pass.plate_text());
        bind_text(s.get(), 4, pass.guest_name());
        bind_text(s.get(), 5, pass.host());
        bind_text(s.get(), 6,
                  pass.kind() == gate::v1::GuestPass::SINGLE_VISIT ? "single" : "until");
        bind_int64(s.get(), 7, pass.valid_from().seconds());
        bind_int64(s.get(), 8, pass.valid_until().seconds());
        bind_text(s.get(), 9, pass.expected_make());
        bind_text(s.get(), 10, pass.expected_model());
        bind_text(s.get(), 11, pass.expected_colour());
        bind_text(s.get(), 12, pass.created_by());
        bind_int64(s.get(), 13, now_unix);
        if (sqlite3_step(s.get()) != SQLITE_DONE)
            throw_db(db, "guest_pass_create");
        auto g = prepare(db, "INSERT INTO guest_pass_gates (pass_id, gate_id) VALUES (?,?)");
        for (const auto& id : pass.gate_ids()) {
            sqlite3_reset(g.get());
            bind_text(g.get(), 1, pass.pass_id());
            bind_text(g.get(), 2, id);
            if (sqlite3_step(g.get()) != SQLITE_DONE)
                throw_db(db, "guest_pass_create gates");
        }
        exec(db, "COMMIT");
    } catch (...) {
        exec(db, "ROLLBACK");
        throw;
    }
    return pass;
}

std::vector<gate::v1::GuestPass> AllowlistStore::guest_pass_list(const std::string& site_id) const {
    std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, std::string{"SELECT "} + kPassColumns +
                                    " FROM guest_passes WHERE site_id=? ORDER BY created_ts DESC");
    bind_text(s.get(), 1, site_id);
    std::vector<gate::v1::GuestPass> out;
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        out.push_back(read_pass(impl_->db, s.get()));
    }
    return out;
}

std::optional<gate::v1::GuestPass> AllowlistStore::guest_pass_revoke(const std::string& pass_id) {
    std::lock_guard lk{impl_->mu};
    auto u = prepare(impl_->db, "UPDATE guest_passes SET status='revoked' WHERE pass_id=?");
    bind_text(u.get(), 1, pass_id);
    if (sqlite3_step(u.get()) != SQLITE_DONE)
        throw_db(impl_->db, "guest_pass_revoke");
    auto s = prepare(impl_->db,
                     std::string{"SELECT "} + kPassColumns + " FROM guest_passes WHERE pass_id=?");
    bind_text(s.get(), 1, pass_id);
    if (sqlite3_step(s.get()) != SQLITE_ROW)
        return std::nullopt;
    return read_pass(impl_->db, s.get());
}

std::optional<gate::v1::GuestPass> AllowlistStore::guest_pass_find(const std::string& site_id,
                                                                   std::string_view plate_text,
                                                                   const std::string& gate_id,
                                                                   gate::v1::GateSide side,
                                                                   std::time_t now_unix) const {
    std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, std::string{"SELECT "} + kPassColumns +
                                    " FROM guest_passes WHERE site_id=? AND plate_text=? "
                                    "AND status IN ('active','used') AND valid_from<=?");
    bind_text(s.get(), 1, site_id);
    bind_text(s.get(), 2, normalize_plate(plate_text));
    bind_int64(s.get(), 3, now_unix);
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        auto p = read_pass(impl_->db, s.get());
        const bool used = p.status() == "used";
        if (used && side != gate::v1::GATE_SIDE_EXIT) {
            continue;
        }
        const auto until = p.valid_until().seconds();
        if (now_unix >= until) {
            continue;
        }
        bool gate_ok = p.gate_ids_size() == 0;
        for (const auto& g : p.gate_ids()) {
            gate_ok = gate_ok || g == gate_id;
        }
        if (gate_ok) {
            return p;
        }
    }
    return std::nullopt;
}

bool AllowlistStore::guest_pass_reserve(const std::string& pass_id, const std::string& command_id,
                                        std::time_t now_unix) {
    std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, R"sql(
        INSERT INTO guest_pass_reservations (command_id, pass_id, ts)
        SELECT ?1, p.pass_id, ?3 FROM guest_passes p
        WHERE p.pass_id = ?2 AND p.status = 'active'
          AND p.valid_from <= ?3 AND p.valid_until > ?3
          AND (p.kind = 'until' OR NOT EXISTS (
                SELECT 1 FROM guest_pass_reservations r
                WHERE r.pass_id = p.pass_id AND r.ts > ?3 - ?4))
    )sql");
    bind_text(s.get(), 1, command_id);
    bind_text(s.get(), 2, pass_id);
    bind_int64(s.get(), 3, now_unix);
    bind_int64(s.get(), 4, kReservationTtlS);
    if (sqlite3_step(s.get()) != SQLITE_DONE)
        throw_db(impl_->db, "guest_pass_reserve");
    return sqlite3_changes(impl_->db) == 1;
}

void AllowlistStore::guest_pass_settle(const std::string& command_id, bool opened) {
    std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    exec(db, "BEGIN IMMEDIATE");
    try {
        if (opened) {
            auto u = prepare(db, R"sql(
                UPDATE guest_passes SET entries_used = entries_used + 1,
                    status = CASE WHEN kind = 'single' THEN 'used' ELSE status END
                WHERE status = 'active' AND pass_id =
                    (SELECT pass_id FROM guest_pass_reservations WHERE command_id = ?)
            )sql");
            bind_text(u.get(), 1, command_id);
            if (sqlite3_step(u.get()) != SQLITE_DONE)
                throw_db(db, "guest_pass_settle");
        }
        auto d = prepare(db, "DELETE FROM guest_pass_reservations WHERE command_id = ?");
        bind_text(d.get(), 1, command_id);
        if (sqlite3_step(d.get()) != SQLITE_DONE)
            throw_db(db, "guest_pass_settle");
        exec(db, "COMMIT");
    } catch (...) {
        exec(db, "ROLLBACK");
        throw;
    }
}

gate::v1::GateConfigRecord AllowlistStore::gate_config_save(const std::string& gate_id,
                                                            gate::v1::GateConfig cfg,
                                                            const std::string& updated_by,
                                                            std::time_t now_unix) {
    std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    auto v = prepare(db, "SELECT version, status, config FROM gate_configs WHERE gate_id=?");
    bind_text(v.get(), 1, gate_id);
    int version = 1;
    int status = gate::v1::CONFIG_VERIFIED;  // nothing to verify yet
    gate::v1::GateConfig prior;
    if (sqlite3_step(v.get()) == SQLITE_ROW) {
        version = sqlite3_column_int(v.get(), 0) + 1;
        status = sqlite3_column_int(v.get(), 1);
        prior.ParseFromArray(sqlite3_column_blob(v.get(), 2), sqlite3_column_bytes(v.get(), 2));
    }
    // Spec 13.1: only type, layout and lane need a fresh verify.
    if (cfg.type() != prior.type() || cfg.lock_layout() != prior.lock_layout() ||
        cfg.lane_mode() != prior.lane_mode()) {
        status = gate::v1::CONFIG_UNVERIFIED;
    }
    cfg.set_version(static_cast<std::uint32_t>(version));
    std::string blob;
    cfg.SerializeToString(&blob);
    auto s = prepare(db, R"sql(
        INSERT INTO gate_configs (gate_id, config, version, status, updated_by, updated_ts)
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(gate_id) DO UPDATE SET config=excluded.config, version=excluded.version,
            status=excluded.status, updated_by=excluded.updated_by, updated_ts=excluded.updated_ts
    )sql");
    bind_text(s.get(), 1, gate_id);
    sqlite3_bind_blob(s.get(), 2, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    bind_int(s.get(), 3, version);
    bind_int(s.get(), 4, status);
    bind_text(s.get(), 5, updated_by);
    bind_int64(s.get(), 6, now_unix);
    if (sqlite3_step(s.get()) != SQLITE_DONE)
        throw_db(db, "gate_config_save");
    gate::v1::GateConfigRecord rec;
    rec.set_gate_id(gate_id);
    *rec.mutable_config() = std::move(cfg);
    rec.set_status(static_cast<gate::v1::ConfigStatus>(status));
    rec.mutable_updated_ts()->set_seconds(now_unix);
    rec.set_updated_by(updated_by);
    return rec;
}

std::optional<gate::v1::GateConfigRecord> AllowlistStore::gate_config_get(
    const std::string& gate_id) const {
    std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db,
                     "SELECT config, status, updated_by, updated_ts FROM gate_configs "
                     "WHERE gate_id=?");
    bind_text(s.get(), 1, gate_id);
    if (sqlite3_step(s.get()) != SQLITE_ROW)
        return std::nullopt;
    gate::v1::GateConfigRecord rec;
    rec.set_gate_id(gate_id);
    rec.mutable_config()->ParseFromArray(sqlite3_column_blob(s.get(), 0),
                                         sqlite3_column_bytes(s.get(), 0));
    rec.set_status(static_cast<gate::v1::ConfigStatus>(sqlite3_column_int(s.get(), 1)));
    rec.set_updated_by(column_text(s.get(), 2));
    rec.mutable_updated_ts()->set_seconds(sqlite3_column_int64(s.get(), 3));
    return rec;
}

bool AllowlistStore::controller_event_insert(const std::string& gate_id,
                                             const gate::v1::ControllerEvent& e) {
    std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, R"sql(
        INSERT OR IGNORE INTO controller_events (gate_id, boot_id, seq, uptime_ms, unix_ms,
            kind, source, side, result, state_after, request_id)
        VALUES (?,?,?,?,?,?,?,?,?,?,?)
    )sql");
    bind_text(s.get(), 1, gate_id);
    bind_int64(s.get(), 2, e.boot_id());
    bind_int64(s.get(), 3, static_cast<std::int64_t>(e.seq()));
    bind_int64(s.get(), 4, static_cast<std::int64_t>(e.uptime_ms()));
    bind_int64(s.get(), 5, e.unix_ms());
    bind_int64(s.get(), 6, e.kind());
    bind_int(s.get(), 7, e.source());
    bind_int(s.get(), 8, e.side());
    bind_int64(s.get(), 9, e.result());
    bind_int(s.get(), 10, e.state_after());
    sqlite3_bind_blob(s.get(), 11, e.request_id().data(), static_cast<int>(e.request_id().size()),
                      SQLITE_TRANSIENT);
    if (sqlite3_step(s.get()) != SQLITE_DONE)
        throw_db(impl_->db, "controller_event_insert");
    return sqlite3_changes(impl_->db) == 1;
}

void AllowlistStore::open_audit_insert(const gate::v1::OpenRequestRecord& r, std::time_t now_unix) {
    std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, R"sql(
        INSERT INTO open_audit (gate_id, request_id, side, source, actor, decision_id,
            queued_ms, preempted_by, outcome, ts) VALUES (?,?,?,?,?,?,?,?,?,?)
    )sql");
    bind_text(s.get(), 1, r.request().gate_id());
    bind_text(s.get(), 2, r.request().request_id());
    bind_int(s.get(), 3, r.request().side());
    bind_int(s.get(), 4, r.request().source());
    bind_text(s.get(), 5, r.actor());
    bind_text(s.get(), 6, r.decision_id());
    bind_int64(s.get(), 7, r.queued_ms());
    bind_text(s.get(), 8, r.preempted_by());
    bind_text(s.get(), 9, r.outcome());
    bind_int64(s.get(), 10, now_unix);
    if (sqlite3_step(s.get()) != SQLITE_DONE)
        throw_db(impl_->db, "open_audit_insert");
}

std::size_t AllowlistStore::open_audit_count(const std::string& gate_id) const {
    std::lock_guard lk{impl_->mu};
    auto s = prepare(impl_->db, "SELECT COUNT(*) FROM open_audit WHERE gate_id=?");
    bind_text(s.get(), 1, gate_id);
    sqlite3_step(s.get());
    return static_cast<std::size_t>(sqlite3_column_int64(s.get(), 0));
}

std::size_t AllowlistStore::blocklist_remove(const std::string& site_id,
                                             std::span<const std::string> plate_texts) {
    std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    auto s = prepare(db, "DELETE FROM blocklist WHERE site_id=? AND plate_text=?");
    std::size_t removed = 0;
    for (const auto& p : plate_texts) {
        const std::string plate = normalize_plate(p);
        sqlite3_reset(s.get());
        sqlite3_clear_bindings(s.get());
        bind_text(s.get(), 1, site_id);
        bind_text(s.get(), 2, plate);
        if (sqlite3_step(s.get()) != SQLITE_DONE)
            throw_db(db, "blocklist_remove");
        removed += static_cast<std::size_t>(sqlite3_changes(db));
    }
    return removed;
}

std::size_t AllowlistStore::remove(const std::string& site_id,
                                   std::span<const std::string> plate_texts) {
    std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    auto s = prepare(db, "DELETE FROM allowlist WHERE site_id=? AND plate_text=?");
    std::size_t removed = 0;
    exec(db, "BEGIN IMMEDIATE");
    try {
        for (const auto& p : plate_texts) {
            const std::string plate = normalize_plate(p);
            sqlite3_reset(s.get());
            sqlite3_clear_bindings(s.get());
            bind_text(s.get(), 1, site_id);
            bind_text(s.get(), 2, plate);
            if (sqlite3_step(s.get()) != SQLITE_DONE)
                throw_db(db, "remove");
            removed += static_cast<std::size_t>(sqlite3_changes(db));
        }
        exec(db, "COMMIT");
    } catch (...) {
        exec(db, "ROLLBACK");
        throw;
    }
    return removed;
}

AllowlistStore::Page AllowlistStore::list(const std::string& site_id, std::uint32_t page_size,
                                          std::string_view page_token) const {
    std::lock_guard lk{impl_->mu};
    auto* db = impl_->db;
    const std::uint32_t limit = std::clamp<std::uint32_t>(page_size, 1, 1000);

    auto s = prepare(db, R"sql(
        SELECT plate_text, owner_name, owner_unit, valid_from, valid_until,
               notes, added_by, added_ts
        FROM allowlist
        WHERE site_id = ? AND plate_text > ?
        ORDER BY plate_text
        LIMIT ?
    )sql");
    bind_text(s.get(), 1, site_id);
    bind_text(s.get(), 2, page_token);
    bind_int(s.get(), 3, static_cast<int>(limit + 1));  // +1 to detect "more"

    Page page;
    page.entries.reserve(limit);
    std::vector<std::string> plates;
    plates.reserve(limit);
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        if (page.entries.size() == limit) {
            // The (limit+1)-th row exists — keep paging from the previous
            // plate. Break, don't return: this page's classes and windows
            // still have to be hydrated below.
            page.next_token = page.entries.back().plate_text();
            break;
        }
        gate::v1::Allowlistentry e;
        e.set_plate_text(column_text(s.get(), 0));
        e.set_owner_name(column_text(s.get(), 1));
        e.set_owner_unit(column_text(s.get(), 2));
        if (const auto vf = sqlite3_column_int64(s.get(), 3); vf > 0) {
            e.mutable_valid_from()->set_seconds(vf);
        }
        if (const auto vu = sqlite3_column_int64(s.get(), 4); vu > 0) {
            e.mutable_valid_until()->set_seconds(vu);
        }
        e.set_notes(column_text(s.get(), 5));
        e.set_added_by(column_text(s.get(), 6));
        if (const auto at = sqlite3_column_int64(s.get(), 7); at > 0) {
            e.mutable_added_ts()->set_seconds(at);
        }
        plates.push_back(e.plate_text());
        page.entries.push_back(std::move(e));
    }

    // Hydrate classes + windows for the page in two queries each rather
    // than (2 * limit) — important once `page_size` grows.
    if (page.entries.empty())
        return page;

    std::string in_clause = "(";
    for (std::size_t i = 0; i < plates.size(); ++i) {
        in_clause += i == 0 ? "?" : ",?";
    }
    in_clause += ")";

    {
        const std::string sql =
            "SELECT plate_text, vehicle_class FROM allowlist_classes "
            "WHERE site_id=? AND plate_text IN " +
            in_clause + " ORDER BY plate_text, vehicle_class";
        auto cs = prepare(db, sql);
        bind_text(cs.get(), 1, site_id);
        for (std::size_t i = 0; i < plates.size(); ++i) {
            bind_text(cs.get(), static_cast<int>(2 + i), plates[i]);
        }
        while (sqlite3_step(cs.get()) == SQLITE_ROW) {
            const auto plate = column_text(cs.get(), 0);
            const auto cls = sqlite3_column_int(cs.get(), 1);
            for (auto& e : page.entries) {
                if (e.plate_text() == plate) {
                    e.add_allowed_classes(static_cast<gate::v1::VehicleClass>(cls));
                    break;
                }
            }
        }
    }
    {
        const std::string sql =
            "SELECT plate_text, start_minute_of_day, end_minute_of_day, "
            "       days_of_week_mask "
            "FROM allowlist_windows "
            "WHERE site_id=? AND plate_text IN " +
            in_clause + " ORDER BY plate_text, start_minute_of_day";
        auto ws = prepare(db, sql);
        bind_text(ws.get(), 1, site_id);
        for (std::size_t i = 0; i < plates.size(); ++i) {
            bind_text(ws.get(), static_cast<int>(2 + i), plates[i]);
        }
        while (sqlite3_step(ws.get()) == SQLITE_ROW) {
            const auto plate = column_text(ws.get(), 0);
            for (auto& e : page.entries) {
                if (e.plate_text() == plate) {
                    auto* w = e.add_time_windows();
                    w->set_start_minute_of_day(
                        static_cast<std::uint32_t>(sqlite3_column_int(ws.get(), 1)));
                    w->set_end_minute_of_day(
                        static_cast<std::uint32_t>(sqlite3_column_int(ws.get(), 2)));
                    w->set_days_of_week_mask(
                        static_cast<std::uint32_t>(sqlite3_column_int64(ws.get(), 3)));
                    break;
                }
            }
        }
    }

    return page;
}

}  // namespace gate::auth
