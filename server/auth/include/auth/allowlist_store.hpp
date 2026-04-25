// allowlist_store.hpp — SQLite-backed allowlist + blocklist.
//
// The store is the source of truth for "which plate is permitted, when, and
// for what vehicle class." Field-controller and dashboard code never touch
// the database directly; both go through this class.
//
// Threading: the store owns a single sqlite3 handle and serializes access
// behind a mutex. Concurrent readers are not parallelized — at the volumes
// we expect (a few thousand entries per site, lookups gated by camera
// frame rate) the contention is negligible and a multi-handle pool would
// be premature.
//
// Plate-text normalization: stored plates are uppercase ASCII with all
// whitespace and dashes stripped. Callers should normalize before lookup;
// `normalize_plate()` does this canonically.

#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "gate_service.pb.h"

namespace gate::auth {

class AuthDbException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Canonical plate-text normalization. Uppercases ASCII letters, strips
// spaces, tabs, and ASCII dashes. Non-ASCII bytes pass through unchanged
// so internationalized plates round-trip safely.
std::string normalize_plate(std::string_view raw);

// True if `entry` is currently active for `vehicle_class`. Composes:
//   - valid_from/valid_until window in unix-epoch seconds
//   - allowed_classes (empty = any class)
//   - time_windows (empty = always; else any window matching local time)
// `now_unix` is wall-clock; `now_local` supplies minute-of-day and weekday
// for the time-window check. A null/zeroed `now_local` is interpreted as
// "no time restriction enforced" — used by tests that only care about
// validity windows.
bool is_allowed_now(const gate::v1::Allowlistentry& entry,
                    gate::v1::VehicleClass vehicle_class, std::time_t now_unix,
                    const std::tm& now_local);

class AllowlistStore {
public:
    struct UpsertStats {
        std::uint32_t inserted = 0;
        std::uint32_t updated = 0;
    };

    struct Page {
        std::vector<gate::v1::Allowlistentry> entries;
        // Empty when the page is the last. Opaque token; current
        // implementation is the last plate_text on the page.
        std::string next_token;
    };

    // Open or create the database at `path`. Use ":memory:" for tests.
    // Runs schema migrations on open; throws AuthDbException on failure.
    static AllowlistStore open(const std::filesystem::path& path);

    // Idempotent upsert keyed on (site_id, plate_text). plate_text is
    // canonicalized via normalize_plate() before storage. Returns the
    // count of rows that were inserted vs updated.
    UpsertStats upsert(const std::string& site_id,
                       std::span<const gate::v1::Allowlistentry> entries);

    // Returns the entry with all sub-rows (classes, windows) populated, or
    // nullopt when the plate is not on the site's allowlist.
    std::optional<gate::v1::Allowlistentry> lookup(const std::string& site_id,
                                                   std::string_view plate_text) const;

    // True iff the site has the (normalized) plate in its blocklist.
    bool is_blocklisted(const std::string& site_id, std::string_view plate_text) const;

    // Add or replace blocklist entries. `reason` is free-form text shown
    // in DENIED audit logs.
    std::uint32_t blocklist_upsert(const std::string& site_id, std::string_view plate_text,
                                   std::string_view reason);

    // Remove blocklist entries; returns the number actually removed.
    std::size_t blocklist_remove(const std::string& site_id,
                                 std::span<const std::string> plate_texts);

    // Paginated listing, ordered by plate_text. `page_size` clamped to
    // [1, 1000]. `page_token` empty for first page.
    Page list(const std::string& site_id, std::uint32_t page_size,
              std::string_view page_token) const;

    // Remove allowlist entries; returns the number actually removed.
    std::size_t remove(const std::string& site_id, std::span<const std::string> plate_texts);

    AllowlistStore(AllowlistStore&&) noexcept;
    AllowlistStore& operator=(AllowlistStore&&) noexcept;
    ~AllowlistStore();
    AllowlistStore(const AllowlistStore&) = delete;
    AllowlistStore& operator=(const AllowlistStore&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit AllowlistStore(std::unique_ptr<Impl> impl);
};

}  // namespace gate::auth
