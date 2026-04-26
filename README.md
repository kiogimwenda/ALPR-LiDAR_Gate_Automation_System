# ALPR + LiDAR Automated Vehicle Gate Control System

A production-grade automated vehicle gate controller that fuses **Automatic
License Plate Recognition (ALPR)** with **3D LiDAR** vehicle classification
to authorize or deny entry. ALPR runs YOLOv9 for plate detection and
PaddleOCR for character recognition, both accelerated via TensorRT on
NVIDIA GPUs. A 3D LiDAR pipeline classifies the vehicle independently. A
custom 4-layer ESP32-S3 field PCB drives the gate motor and senses
limit/safety inputs. A Drogon + SvelteKit dashboard provides real-time
monitoring, manual override, and audit logs.

**Repository:** [github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System](https://github.com/kiogimwenda/ALPR-LiDAR_Gate_Automation_System)
**License:** [GPL-3.0](LICENSE) — rationale in [ADR-000](docs/decisions/ADR-000-license.md)

---

## Why this project exists

Residential and small-commercial gate access in Kenya (and most emerging
markets) is currently solved by either a guard with a clipboard or a remote
clicker. Both fail at the same things: tailgating, lost remotes, unrecorded
entries, and zero auditability. This project is a self-hosted, on-prem
alternative that:

- **Recognizes plates from real cameras at the gate** (Hikvision 4 MP PoE),
  not curated benchmarks.
- **Cross-checks plate against vehicle class** via 3D LiDAR — a sedan with
  a truck's plate is denied even if OCR succeeds. This is the LiDAR-fusion
  story that makes the system tailgating- and clone-resistant.
- **Runs entirely on a single GPU server on the local LAN** (RTX 4060 8 GB)
  — no cloud round-trip, no monthly per-gate fees.
- **Is repairable in the field** — every component is sourced from
  Luthuli Avenue (Nairobi) or AliExpress, the BOM is published, and the
  PCB is hand-solderable with documented through-hole points for the
  high-current paths.

---

## High-level architecture

```
┌─────────────────────────── AT THE GATE ──────────────────────────────────┐
│                                                                          │
│   Hikvision 4MP camera ──┐                                              │
│   Unitree L1 LiDAR  ─────┤  PoE switch ──── Ethernet ──── GPU server   │
│   ESP32-S3 field PCB ────┤      │                          │             │
│   Limit switches × 2 ────┘      │                          │             │
│   Photoelectric beam            │                          │             │
│                                                            │             │
│        Relay outputs to gate motor (CENTURION D5/R5)       │             │
│                                                            │             │
└────────────────────────────────────────────────────────────┼─────────────┘
                                                             │
┌─────────────────────────── SERVER ROOM ─────────────────────┼────────────┐
│                                                            │             │
│   GPU server: ALPR (YOLOv9 + PaddleOCR, TensorRT) +       │             │
│   LiDAR pipeline + fusion engine + gRPC server +          │             │
│   Drogon dashboard backend + SvelteKit dashboard frontend │             │
│                                                            │             │
└────────────────────────────────────────────────────────────┴─────────────┘
```

Full subsystem flowcharts: [docs/diagrams/](docs/diagrams/) (7 Mermaid files).

---

## Development timeline

The project executes in five sequential phases. Each phase has a tagged
release on GitHub.

| Phase | Title | Status | Highlights |
|---|---|---|---|
| **0** | Environment bootstrap | ✅ Complete | WSL2 Debian + CUDA 13.1 + cuDNN 9.19 + TensorRT 10.15 + OpenCV 4.14 (CUDA source-build) verified by `smoke_test.cu`. |
| **1** | Clarifications & decisions | ✅ Complete | All 12 design questions answered; defaults accepted; gate type override applied. |
| **2** | Architecture | ✅ Complete | 7 Mermaid flowcharts, 11 ADRs (ADR-000…ADR-010), full directory skeleton, CI workflows. |
| **3** | Hardware research & build guides | ✅ Complete | 9 component guides + master build book + complete BOM + 48-page KiCad 9.0 PCB design guide (PDF). |
| **4** | Implementation | 🔵 **In progress** — see below | Modern C++20 server, ESP-IDF firmware, simulation, dashboard. |
| 5 | Delivery | ⏳ Pending | Runbook, commissioning checklist, demo script, public release. |

### Phase 4 sub-milestones (current)

| # | Milestone | Status |
|---|---|---|
| 4.1 | gRPC wire contract (`shared/proto`) | ✅ Complete |
| **4.2** | **Server inference (TensorRT engines for YOLOv9 + PaddleOCR)** | ✅ **Complete** |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.1 | TrtEngine RAII wrapper around TensorRT 10.x | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.2 | YOLOv9 plate detector | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.3 | PaddleOCR character recognizer | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.4 | ALPR pipeline orchestrator | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.5 | Python ONNX → TensorRT conversion tooling | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.2.6 | Catch2 inference unit tests | ✅ Complete |
| **4.3** | **Server RPC + fusion engine** | 🔵 **In progress** |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.1 | Allowlist + blocklist store (SQLite) | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.2 | Fusion engine (verdict ladder) | ✅ Complete |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.3 | Dashboard event broadcaster | ⏳ Pending |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.4 | gRPC server + Dashboard / Admin services | ⏳ Pending |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.5 | FieldControllerService + main.cpp | ⏳ Pending |
| &nbsp;&nbsp;&nbsp;&nbsp;4.3.6 | Integration tests + Phase 4.3 closure | ⏳ Pending |
| 4.4 | Firmware drivers (W5500, relays, sensors) | ⏳ Pending |
| 4.5 | Firmware app (state machine, gRPC client, OTA) | ⏳ Pending |
| 4.6 | Simulation harness | ⏳ Pending |
| 4.7 | Dashboard backend + frontend | ⏳ Pending |
| 4.8 | Deployment scripts (systemd, install) | ⏳ Pending |
| 4.9 | End-to-end integration tests | ⏳ Pending |

---

## Latest accomplishment — Phase 4.3.2: Fusion engine (verdict ladder)

> **Completed 2026-04-26.** Library in
> [`server/fusion/`](server/fusion/), tests in
> [`tests/fusion/fusion_engine_test.cpp`](tests/fusion/fusion_engine_test.cpp).

### What I built

The decision brain of the server: `gate_fusion` consumes an
`AuthorizeRequest` (a `DetectionFrame` from the ALPR + LiDAR pipelines
plus the site context) and returns an `AuthDecision` — the verdict the
firmware acts on and the dashboard logs. The verdict ladder follows
[`docs/diagrams/04-fusion-decision.mmd`](docs/diagrams/04-fusion-decision.mmd)
literally; nothing about the rules lives anywhere else in the codebase.

| Artifact | Purpose |
|---|---|
| `server/fusion/include/fusion/fusion_engine.hpp` | Public API: `FusionEngine{store, cfg}`, `decide(req)` (system-time path), `decide_at(req, now_unix, now_local)` (deterministic path), `set_override(gate_id, state)` for guard latches, plus a free `generate_uuidv4()` for decision IDs. |
| `server/fusion/src/fusion_engine.cpp` | Implementation: ten-step verdict ladder, multi-plate top-confidence picker, per-gate override map under a mutex, hand-rolled RFC 4122 UUIDv4 (no extra dep). |
| `server/fusion/CMakeLists.txt` | `gate_fusion` static library; public deps on `gate_proto` and `gate_auth`, no TensorRT/CUDA/OpenCV linkage. |
| `tests/fusion/fusion_engine_test.cpp` | 15 Catch2 cases / 148 assertions covering each branch of the ladder, the UUIDv4 format, and the multi-plate selection rule. |
| `tests/fusion/CMakeLists.txt` | `test_fusion_engine` binary, gated on `TARGET gate_fusion`. |
| `server/CMakeLists.txt`, `tests/CMakeLists.txt` (updated) | Same `if(TARGET …)` pattern as `auth/` — fusion is added when `gate_auth` exists, tests are added when `gate_fusion` exists. |

### Technical detail

#### Why a separate library, not part of `gate_auth`

The two are functionally orthogonal: `gate_auth` is a *data store*
(plates in / plates out, plus a pure validity helper), `gate_fusion` is
a *policy engine* (combines two probabilistic signals, applies overrides,
disambiguates failure modes). Splitting them means:

- The gRPC `AdminService` (4.3.4) can link only `gate_auth` — it has no
  business reading fusion config or override state.
- The `FusionEngine` can be unit-tested with an in-memory store and
  hand-built `AuthorizeRequest`s, without ever touching the dashboard
  service or the gRPC layer.
- A future replacement of the verdict ladder (e.g. a learned policy)
  swaps `gate_fusion` without disturbing the data layer or the API
  surface.

#### The verdict ladder, exactly

The ten steps run in this order; the first one to fire short-circuits.
The order matters — different orderings change semantics.

1. **Force-close override** → `DENIED (FORCE_CLOSE)`. A guard's
   lockdown beats every data-layer signal, including a guard's own
   later force-open if they conflict.
2. **No plate detected** → `DENIED (NO_PLATE_FOUND)`. Frame had a
   vehicle but no readable plate — surface as a distinct reason so
   the dashboard can show "ALPR retry needed."
3. **Pick best plate** by `detection_conf`. Multi-plate frames (rare
   on residential gates, common on parking-lot wide-angles) collapse
   to a single candidate here. Normalize via
   `gate::auth::normalize_plate()` and record on the decision.
4. **Pick best vehicle** by `class_conf`, default to
   `VEHICLE_CLASS_UNKNOWN` and the configured
   `lidar_missing_confidence` (default 0) when no vehicle was
   classified. Always record `matched_class`.
5. **Compute combined confidence** as `w1 * mean(detection, ocr) +
   w2 * lidar_class_conf`. The ALPR side is the *mean* of detection
   and OCR — a strong YOLO box around an unreadable plate is a poor
   match, and so is a clean OCR over a low-confidence detection.
6. **Force-open override** → `AUTHORIZED ("force-open override active")`.
   Runs *after* the plate is matched so the audit log records what was
   under the camera, not just "the guard latched the gate."
7. **Confidence floor** → `LOW_CONFIDENCE` with the more informative
   sub-reason: whichever side dragged the score down. A reason text
   includes the actual numbers (`combined=0.625, alpr=0.30, lidar=0.95,
   threshold=0.70`) so a guard reading the dashboard knows whether to
   reposition the camera or service the LiDAR.
8. **Blocklist** → `DENIED (BLOCKLISTED)`. Wins over the allowlist by
   construction; even a `override_allowlist=true` guard request cannot
   defeat a blocklist hit. (Force-open *can*, deliberately — see
   below.)
9. **Guard manual override (`override_allowlist`)** → `AUTHORIZED
   ("guard manual override")`. Bypasses the allowlist lookup but not
   the blocklist. Used when a visitor calls the intercom and the guard
   approves them through.
10. **Allowlist lookup**. If miss → `DENIED (NOT_ON_ALLOWLIST)`.
11. **Time-window + class restriction**. If
    `gate::auth::is_allowed_now()` denies, disambiguate the failure:
    - Wrong class → `MANUAL_REVIEW (CLASS_MISMATCH)`. Surfaces in the
      dashboard as "guard please decide" — a delivery van arriving at
      a sedan-only entry might still be legitimate.
    - Outside time window → `DENIED (OUTSIDE_WINDOW)`. Hard deny;
      time-windows are typically a hard contract (e.g. cleaning crew
      is *only* allowed Tuesday mornings).
12. Otherwise → `AUTHORIZED ("plate + class match")`.

Why force-open *can* defeat the blocklist (step 1 ahead of step 8) but
guard-`override_allowlist=true` *cannot* (step 9 after step 8):

- Force-open is a physical gate state ("gate stays up until I unlatch
  it") — a guard standing at the gate has already made the decision
  with full visual context. Pretending the data store still has a veto
  would be confusing operational behavior.
- `override_allowlist=true` is a *per-request* approval from the
  dashboard, often issued without seeing the vehicle. Letting a
  blocklisted plate through on a per-request approval is exactly the
  scenario blocklists exist to prevent.

#### Per-gate override state

Overrides are kept in an in-memory `std::unordered_map<gate_id,
OverrideState>` under a `std::mutex`. Three reasons not to persist them:

- They reflect **physical gate state**, which the firmware reports
  via `Telemetry`. The fusion engine's view should follow firmware,
  not lead it; persisting here would create a divergence risk.
- The dashboard is the source of truth for *requested* state. On
  server restart, the dashboard re-issues the latch.
- A locked-down gate after a multi-day server outage is almost never
  what operators want — re-confirmation by a human is better than a
  silent re-latch from a stale row.

The mutex is held for microseconds — the contention story is identical
to `AllowlistStore`'s. `set_override(gate_id, OverrideState::kNone)`
erases the entry rather than storing the kNone state, keeping the map
small.

#### Decision IDs — UUIDv4 by hand

Every `AuthDecision` carries a `decision_id` (per the proto), which the
dashboard uses to deduplicate replays and the firmware echoes on its
ack. The proto comment specifies UUIDv4. Rather than pull in a UUID
library, the engine generates them with `std::mt19937` seeded once per
thread from `std::random_device`, then sets the version (top nibble of
byte 6 = `0x4`) and variant (top two bits of byte 8 = `10`) bits per
RFC 4122. 122 bits of entropy per ID is plenty for a deployment that
issues at most a few decisions per second.

The implementation lives in the same TU as the engine; a unit test
matches 50 generated IDs against the canonical regex
`[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}`.

#### Two `decide` paths — production and deterministic

The public API exposes both `decide(req)` and `decide_at(req, now_unix,
now_local)`. The first delegates to the second after filling
`std::time(nullptr)` and `localtime_r(...)`. Tests always use the
deterministic path with hand-built `std::tm` for the day-of-week and
minute-of-day fields. This is the same pattern the inference layer
uses — pure code receives time as a parameter; the system-time
convenience wrapper lives at the API edge.

#### Verified run

```
$ ninja gate_fusion test_fusion_engine
[20/20] Linking CXX executable tests/fusion/test_fusion_engine

$ ./tests/fusion/test_fusion_engine --reporter compact
RNG seed: 2651663747
All tests passed (148 assertions in 15 test cases)
```

Combined with the prior milestones, the server-side test suite is now
38 cases / 244 assertions, all running under one second. Every prior
test still passes — the fusion library extends rather than replaces.

---

## Previous milestone — Phase 4.3.1: Allowlist + blocklist store (SQLite)

> **Completed 2026-04-25.** Library in
> [`server/auth/`](server/auth/), tests in
> [`tests/auth/allowlist_store_test.cpp`](tests/auth/allowlist_store_test.cpp).

### What I built

The first non-inference library on the server side: `gate_auth` — a thin,
thread-safe SQLite wrapper that owns the allowlist (which plates are
permitted) and the blocklist (which plates are explicitly denied), keyed
per-site for multi-tenant deployments. The fusion engine in 4.3.2 reads
through this store; the gRPC `AdminService` in 4.3.4 will write to it.

| Artifact | Purpose |
|---|---|
| `server/auth/include/auth/allowlist_store.hpp` | Public API: `AllowlistStore::open(path)`, `upsert`, `lookup`, `is_blocklisted`, `blocklist_upsert`/`remove`, `list` (paginated), `remove`. Also `normalize_plate()` and the `is_allowed_now(entry, class, now_unix, now_local)` policy helper. |
| `server/auth/src/allowlist_store.cpp` | sqlite3 C-API implementation with prepared-statement caching, WAL journaling, and a versioned schema migration on open. |
| `server/auth/CMakeLists.txt` | Builds `gate_auth` (static library) linked against `gate_proto` (public, for the entry types) and `SQLite::SQLite3` (private). |
| `tests/auth/allowlist_store_test.cpp` | 12 Catch2 v3 cases — 58 assertions — exercising normalization, CRUD round-trips, cascade-on-remove, pagination ordering, blocklist site-scoping, and every `is_allowed_now()` decision branch. |
| `tests/auth/CMakeLists.txt` | Test binary `test_auth_allowlist_store`, gated on `TARGET gate_auth`. |
| `server/CMakeLists.txt`, `tests/CMakeLists.txt` (updated) | Same `if(TARGET …)` gating model as 4.2 — auth is added to server/ when `gate_proto` exists, and tests are added when `gate_auth` exists. |

### Technical detail

#### Why SQLite, why now

The architecture (ADR-002 and the proto schema) treats the allowlist as
a hot-path lookup against camera frame rate (~25 fps per gate). Three
options were on the table:

1. **In-memory hash-map.** Fast, trivial. Loses everything on restart;
   needs a separate persistence path (and reconciliation when the
   dashboard writes mid-flight). Rejected — the allowlist *is* state.
2. **A real RDBMS (PostgreSQL).** Overkill for a single-server
   deployment with thousands of rows; adds a daemon to operate.
3. **SQLite, embedded.** Single-file, transactional, ACID, WAL for
   concurrent dashboard writes during inference reads, no daemon.
   Already in `vcpkg.json`.

SQLite (#3) wins on every axis that matters: the working set is small
enough that the entire database fits in OS page cache, point lookups
through the `(site_id, plate_text)` primary key are O(log n) on a
B-tree at minimum, and the schema is simple enough that operators can
inspect it with `sqlite3 alphabet.db ".schema"`. The write rate is
human-scale (admin UI, occasional bulk imports) so WAL contention isn't
a concern.

#### Schema layout

Three normalized tables with cascading deletes:

- **`allowlist`** — `(site_id, plate_text)` primary key, plus owner
  metadata (`owner_name`, `owner_unit`), validity window
  (`valid_from`, `valid_until` as unix epoch seconds; 0 = unbounded),
  free-form `notes`, and an audit trail (`added_by`, `added_ts`).
  `WITHOUT ROWID` because the natural primary key is already small
  text and we never need a stable rowid.
- **`allowlist_classes`** — many-to-many, `(site_id, plate_text,
  vehicle_class)` triple. An empty join is interpreted as "any
  class allowed," matching the proto's documented semantics.
- **`allowlist_windows`** — many-to-many, `(start_minute_of_day,
  end_minute_of_day, days_of_week_mask)` per row. No primary key
  because duplicate windows are merely redundant, not invalid; an
  index on `(site_id, plate_text)` covers the lookup path.
- **`blocklist`** — separate table, `(site_id, plate_text)` primary
  key, `reason` for audit. Always wins over the allowlist per the
  fusion-engine verdict ladder.
- **`schema_version`** — one row, monotonically increasing. The
  current schema is v1; future migrations append a per-version
  upgrade block rather than writing speculative down-migrations.

`PRAGMA journal_mode = WAL` and `PRAGMA synchronous = NORMAL` give us
concurrent reader/writer access and crash-consistent durability without
the per-transaction fsync cost of `synchronous=FULL`. These are the
standard production defaults for SQLite as a service-of-record.

#### Plate-text normalization

The proto contract states stored plates are *normalized uppercase*. To
keep that invariant honest, every public API path runs the input
through `normalize_plate()`:

- ASCII lowercase → uppercase (no locale dependency, no `std::toupper`
  surprises with `setlocale`).
- Strip ASCII spaces, tabs, dashes, and underscores — the typical OCR
  noise from PaddleOCR's CTC head and from human-entered admin lists.
- Pass non-ASCII bytes through unchanged so internationalized plates
  (e.g. with Greek or Cyrillic glyphs) round-trip safely.

This is a free function deliberately — the dashboard backend will need
the same normalization before it queries the store, and exposing it in
the public header lets that code share the canonical implementation
rather than reinvent it.

#### `is_allowed_now()` — the policy helper

Returns `true` when an entry is *currently* valid for a given vehicle
class. Three independent gates, evaluated in cheapness order:

1. **Validity-window dates.** `now_unix < valid_from` or
   `now_unix >= valid_until` ⇒ deny. The half-open `[from, until)`
   interpretation matches how digital ACLs always work — an entry
   that "expires Friday at midnight" is denied at the first second of
   Saturday, not seconds before.
2. **Vehicle class.** If `allowed_classes` is empty, any class is
   accepted; otherwise the LiDAR-reported class must be on the list.
3. **Time-of-day window.** If no windows are set, always accept.
   Otherwise the *current* local time (passed in as `std::tm` by the
   caller — the policy helper is pure and TZ-agnostic) must satisfy
   one of the entry's `TimeWindow`s. Each `TimeWindow` carries a
   `days_of_week_mask` (bit0 = Mon … bit6 = Sun, per the proto) and a
   half-open `[start, end)` minute range. Wrap-around windows
   (`end < start`, e.g. 22:00–02:00) are encoded by inversion.

Splitting this out as a free function — rather than a method on
`AllowlistStore` — means the fusion engine can call it on the entry
returned by `lookup()` without re-querying the database, and tests
can synthesize entries inline without touching a SQLite handle.

#### Upsert semantics

The proto's `UpsertAllowlistRequest` is *batch-with-conflict-replace*:
the dashboard sends a list of entries, each is added if new and
updated if its `(site_id, plate_text)` already exists, and the
response reports inserted vs updated counts. The store implements that
literally:

- A single `BEGIN IMMEDIATE` / `COMMIT` per batch — either every
  entry lands or none do, with `ROLLBACK` on any per-row failure.
  That matters for bulk imports (e.g. an HOA importing 200 plates
  from a CSV) where partial commits are worse than retrying.
- The main-row write is a SQL `INSERT ... ON CONFLICT DO UPDATE`
  (the SQLite-flavored equivalent of `MERGE`), which is one round-trip
  to the engine instead of `SELECT then INSERT/UPDATE`.
- Sub-rows (`allowlist_classes`, `allowlist_windows`) follow a
  *wipe-and-rewrite* policy: delete all sub-rows for the key, then
  insert the new ones. This gives the upsert "set semantics" — the
  caller's submitted list of classes/windows is now exactly what's
  stored, with no ghost rows from prior versions.
- Inserted-vs-updated counting is via a precursor `SELECT 1` keyed on
  the same primary key. SQLite's `last_insert_rowid()` doesn't
  distinguish insert-from-update for `WITHOUT ROWID` tables.

#### Pagination

`list()` orders by `plate_text` and uses the last plate of the page as
the next page token — a classic keyset cursor. Two reasons over
LIMIT/OFFSET:

- O(log n) per page regardless of how deep the cursor walks. OFFSET
  on a 100k-entry site reading page 50 of 50 would scan 49 × page_size
  rows just to discard them.
- Stable under concurrent writes: a row inserted "above" the cursor
  doesn't shift the page boundary.

The implementation requests `limit + 1` rows per page — if the extra
row arrives, there's another page and the token is set; otherwise the
response is the last page. Page-size is clamped to `[1, 1000]`.

To avoid the obvious 2×N round-trips for hydrating sub-rows on a page,
`list()` issues exactly two follow-up queries with `IN (?, ?, …)`
clauses sized to the page width, then stitches results back into the
returned `Allowlistentry` objects in memory.

#### Threading model

A `std::mutex` serializes every public method against the underlying
`sqlite3*` handle. SQLite's own thread safety is `SQLITE_OPEN_FULLMUTEX`
on open (chosen here for clarity over `SQLITE_OPEN_NOMUTEX` + an
external mutex), but the in-process mutex still matters for
multi-statement operations (e.g. `BEGIN`/per-row `INSERT`/`COMMIT`)
where ordering across threads must be preserved. Lock contention is a
non-issue at the rates we expect — the camera produces a frame every
40 ms; the lookup path holds the lock for microseconds.

#### Move-only with PIMPL

`AllowlistStore` is move-only with a `std::unique_ptr<Impl>` for the
sqlite3 handle. PIMPL was deliberate: it keeps `<sqlite3.h>` out of the
public header, so downstream targets (fusion engine, gRPC services,
tests) don't transitively pull in the SQLite C API. The static
factory `open()` is the only constructor — there is no default
constructor, so a half-initialized `AllowlistStore` is unrepresentable.

#### Verified run

```
$ ninja gate_auth test_auth_allowlist_store
[7/7] Linking CXX executable tests/auth/test_auth_allowlist_store

$ ./tests/auth/test_auth_allowlist_store --reporter compact
RNG seed: 3014959169
All tests passed (58 assertions in 12 test cases)
```

All 12 cases run against `:memory:` databases — no filesystem state
leaks, no per-test cleanup, sub-millisecond per case. The full suite
(11 inference + 12 auth = 23 cases, 96 assertions) completes in well
under a second.

---

## Previous milestone — Phase 4.2.6: Catch2 inference unit tests

> **Completed 2026-04-25.** Tests in
> [`tests/inference/cpu_algorithms_test.cpp`](tests/inference/cpu_algorithms_test.cpp).
> Algorithm extraction in
> [`server/inference/include/inference/detail/cpu_algorithms.hpp`](server/inference/include/inference/detail/cpu_algorithms.hpp)
> and [`server/inference/src/detail/cpu_algorithms.cpp`](server/inference/src/detail/cpu_algorithms.cpp).

### What I built

Eleven Catch2 v3 unit tests over the host-side inference algorithms — the
parts that don't need a GPU and therefore don't need TensorRT. They run in
under 0.3 s on a development workstation and exercise the same code path
the production server runs through, because the tests link against the
production `gate_inference` static library.

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/detail/cpu_algorithms.hpp` | New `gate::inference::detail` namespace exposing the four pure-CPU primitives — `letterbox`, `unmap_letterbox_box`, `ctc_greedy_decode`, `expand_box_with_margin` — as free functions over `cv::Mat` / `std::span<const float>`. |
| `server/inference/src/detail/cpu_algorithms.cpp` | Implementation of those four functions, lifted out of the existing class methods. |
| `server/inference/src/{yolo_plate_detector,paddle_ocr_recognizer,alpr_pipeline}.cpp` | Refactored to delegate to the new free functions, eliminating duplicate implementations. |
| `tests/inference/cpu_algorithms_test.cpp` | 11 Catch2 test cases covering letterbox geometry, CTC blank/repeat collapse, confidence-mean math, and crop-with-margin clamping. |
| `tests/inference/CMakeLists.txt` | Builds `test_inference_cpu_algorithms`; gated on `TARGET gate_inference` so a CPU-only or proto-only build skips the inference tests cleanly. |
| `tests/CMakeLists.txt` (updated) | Same `if(TARGET …)` gate now applied to `proto/` so a tests-only build with `BUILD_PROTO=OFF` doesn't try to compile against `gate_proto`. |
| Root `CMakeLists.txt` (updated) | Reordered to process `server/` before `tests/`, so the `if(TARGET gate_inference)` gate sees the freshly-declared library. |

### Technical detail

#### Why a refactor was the right move

The pre-test code carried three copies of essentially the same algorithms,
each baked into a private member function:

- `YoloPlateDetector::letterbox_` had its own letterbox math.
- `YoloPlateDetector::detect()` did box-unmapping inline at the call site.
- `AlprPipeline::expand_and_clamp_` had its own crop expansion + clamp.
- `PaddleOcrRecognizer::ctc_decode_` had its own CTC decoder.

Testing any of them required either a `friend` declaration (an
implementation-detail leak that lives in the public header), a synthetic
TensorRT engine (slow, fragile across TRT versions), or constructing a
full backend instance (impossible without an engine file). None of those
are good answers. Lifting the algorithms into a `detail` namespace is the
*least* invasive option that makes them testable: the public headers don't
change, every backend now calls a single canonical implementation, and the
tests exercise that implementation directly without booting CUDA.

The classes still own the orchestration — buffer sizing, device transfer,
TensorRT enqueue/sync, ownership lifetimes. Only the pure-CPU bits moved.

#### What the tests actually cover

Eleven cases across three algorithms:

**Letterbox (`[letterbox]` tag)** — 4 tests:

1. *Horizontal source pads top and bottom.* A 200×100 source into a
   320×320 canvas. Verifies the chosen scale is the smaller axis ratio
   (1.6), the resulting padding is zero on the X axis and 80 px on the Y
   axis, the padding pixel matches the requested fill (114, 114, 114),
   and the centered image content is intact.
2. *Vertical source pads left and right.* The mirror case — a 100×200
   source into the same canvas, verifying scale and pads swap roles.
3. *Center box round-trips through scale and pad.* Constructs a known
   `LetterboxParams`, picks a canvas-pixel box in the image band, and
   confirms `unmap_letterbox_box` returns the source-pixel rectangle
   that letterbox would map back to. This is the contract the YOLO
   detector depends on.
4. *Clamps to source extents and drops degenerate.* Two sub-cases —
   a box that overhangs the canvas's image band clamps to the source
   bottom; a box entirely inside the top padding strip is degenerate
   after clamping and the function returns a zero-area rect.

**CTC greedy decode (`[ctc]` tag)** — 4 tests, each with a synthetic
argmax-probability tensor built by a test helper:

1. *Skips blanks and collapses repeats.* The sequence
   `blank, A, A, blank, B, B, C, blank` with peak prob 0.9 decodes to
   `"ABC"` with mean confidence 0.9. This is the canonical CTC
   correctness check.
2. *All blanks.* Every step is the blank token. Output is empty text
   and confidence exactly 0 (not NaN — important because downstream
   policy code compares against thresholds).
3. *Confidence is mean, not sum.* Three unique chars with peak prob
   0.5 yields mean 0.5, not 1.5. Catches a regression where the
   accumulator gets returned without dividing.
4. *Out-of-range class indices.* If the argmax picks a class beyond
   the dictionary, the kept-count still increments but the text
   doesn't grow — confirms the bounds check on `dictionary_[best - 1]`.

**Crop with margin (`[crop]` tag)** — 3 tests:

1. *10 % margin grows symmetrically.* A 100×50 box at (100, 100) with
   margin 0.1 grows to (90, 95) origin and (120, 60) extent.
2. *Clamps to frame on every edge.* A box near (0, 0) with a 50 %
   margin can't go negative; both `x` and `y` clamp to 0 and the
   right/bottom stay inside the frame.
3. *Degenerate result reports empty.* A 2×2 box with `min_extent=4`
   returns a 0-area rect, signalling the pipeline to drop this
   detection rather than feed an empty crop to OCR.

#### CMake gating model

The Catch2 inference tests live in `tests/inference/`. They link against
`gate_inference`, which only exists when the build is configured with
`BUILD_SERVER=ON` and a working CUDA + TensorRT toolchain. Forcing the
test target to always build would make the CI matrix
(`-DENABLE_GPU=OFF`) fail on a missing target.

The fix is a per-target gate in `tests/CMakeLists.txt`:

```cmake
if(TARGET gate_inference)
    add_subdirectory(inference)
endif()
```

This needs root `CMakeLists.txt` to process `server/` *before* `tests/`,
so the target is declared by the time the gate is checked. Same gate now
applies to `proto/` so a test-only build with `BUILD_PROTO=OFF` doesn't
try to compile against `gate_proto`. CI happily skips both subdirs and
runs only what it has dependencies for; local development builds get the
full test suite without changing flags.

#### Verified run

```
Test project /tmp/gate-build-inference
   1: letterbox: vertical source pads left and right ........... Passed
   2: letterbox: horizontal source pads top and bottom ......... Passed
   3: expand_box_with_margin: 10% margin grows symmetrically ... Passed
   4: ctc_greedy_decode: all blanks → empty + zero conf ........ Passed
   5: expand_box_with_margin: clamps to frame on every edge .... Passed
   6: unmap_letterbox_box: clamps + drops degenerate ........... Passed
   7: ctc_greedy_decode: skips blanks and collapses repeats .... Passed
   8: unmap_letterbox_box: center box round-trips .............. Passed
   9: ctc_greedy_decode: confidence is mean of kept probs ...... Passed
  10: expand_box_with_margin: degenerate reported as empty ..... Passed
  11: ctc_greedy_decode: out-of-range class indices dropped .... Passed

100% tests passed, 0 tests failed out of 11
```

Total runtime under 0.3 s on the dev box. The tests are fast enough to be
part of every local rebuild, not just CI gating.

---

## Previous milestone — Phase 4.2.5: ONNX → TensorRT engine tooling

> **Completed 2026-04-25.** Scripts in
> [`scripts/export-models/`](scripts/export-models/) — `convert_onnx_to_trt.py`,
> `inspect_engine.py`, plus a [README](scripts/export-models/README.md)
> documenting the upstream YOLOv9 / PaddleOCR export commands.

### What I built

Phase 4.2.1–4.2.4 produced a C++ inference layer that consumes serialized
TensorRT engines (`.plan` files). 4.2.5 is the missing piece on the
**Python side**: how those `.plan` files are produced from upstream ONNX
exports, with the dynamic shape profiles, FP16 flags, and hardware
compatibility settings the C++ side expects to find at runtime.

| Artifact | Purpose |
|---|---|
| `scripts/export-models/convert_onnx_to_trt.py` | ONNX → `.plan` builder using TensorRT's Python `Builder` / `OnnxParser` / `BuilderConfig` API. FP16 default, optional INT8, `--min/--opt/--max-shape` profile knobs, `--hardware-compat ampere_plus` default for sm_80…sm_120 portability. Prints final I/O signatures after build. |
| `scripts/export-models/inspect_engine.py` | Deserializes a `.plan` and prints every I/O tensor's name, shape, dtype, and per-profile shape ranges. The fast way to verify the C++ Config tensor names still match what the engine actually exposes. |
| `scripts/export-models/README.md` | Documents the upstream export commands for YOLOv9 (`export.py --include onnx_end2end`) and PaddleOCR (`paddle2onnx`), plus the exact `convert_onnx_to_trt.py` invocations that match the C++ Config defaults. |

### Technical detail

#### Why a thin wrapper instead of `trtexec`

NVIDIA ships `trtexec` for one-off engine builds, and it's fine for that.
The trouble is that the engines this project produces aren't one-off:

- The PP-OCRv4 recognizer needs a **dynamic-shape optimization profile**
  with three width values (`min=32, opt=160, max=320`) chosen so the
  builder picks kernels tuned for typical Kenya plates (≈ 1:3.3 aspect)
  rather than for the worst-case 320 px. Encoding that profile in a
  shell-script `trtexec` invocation is doable but unreadable.
- Both engines need `HardwareCompatibilityLevel.AMPERE_PLUS` so the same
  `.plan` runs on the dev box (RTX 5060, sm_120) and the production
  server (RTX 4060, sm_89). `trtexec` exposes the flag but `--help` is
  ~400 lines and the right combination is not obvious.
- The C++ side binds tensors **by name**. Verifying that `softmax_2.tmp_0`
  is still what PaddleOCR exports needs an inspection step that's not a
  shell pipeline of `trtexec --dumpProfile` parsing.

A small, readable Python script that calls the same TRT API the C++ side
uses internally is easier to maintain than a shell wrapper around an
opaque tool.

#### `convert_onnx_to_trt.py` — what it actually does

Six steps:

1. **Logger.** A `trt.Logger` at `WARNING` (or `INFO` with `-v`) so the
   build output is small enough to read but loud enough to surface ONNX
   parse errors.
2. **Parse.** `Builder.create_network(0)` (explicit batch is now the only
   mode in TRT 10) → `OnnxParser`. On parse failure every parser error
   is dumped before exit so an upstream change to the ONNX surface
   doesn't fail silently.
3. **Builder config.** `set_memory_pool_limit(WORKSPACE, 4 GiB)` (CLI
   override available) — the workspace pool is what the builder uses to
   try alternative kernels, so a stingy budget produces measurably worse
   engines. 4 GiB fits both 8 GB GPUs with room to spare.
4. **Precision flags.** `BuilderFlag.FP16` by default. `INT8` is
   reachable via `--int8` but the project doesn't ship a calibration
   cache yet, so it'll fall back to FP16 for unquantized layers — the
   flag is there for Phase 5 hardening.
5. **Hardware compatibility.** `HardwareCompatibilityLevel.AMPERE_PLUS`
   trades 5–10 % inference speed vs. arch-specific kernels for a single
   `.plan` that runs on every machine in the fleet. Override with
   `--hardware-compat none` for benchmark-grade builds.
6. **Optimization profile.** If any of `--min/--opt/--max-shape` is
   given, all three are required and `--input-name` must point to the
   tensor. The shape parser accepts `NxCxHxW`-style strings so the CLI
   stays terse: `--max-shape 1x3x48x320`.

After `build_serialized_network()` returns the bytes, the script
re-deserializes the engine just to dump its I/O signatures — same code
path as `inspect_engine.py`. This confirms the build at the CLI without
needing a follow-up command.

#### `inspect_engine.py` — why a separate script

Engines outlive the build process. Six months from now, somebody
upgrades PaddleOCR, re-exports the recognizer, and the OCR result silently
drops one character because the output tensor was renamed from
`softmax_2.tmp_0` to `softmax_2`. The C++ side will throw a
`TrtException("tensor not found")` at startup, which is the right
behavior — but `inspect_engine.py` lets the operator see that mismatch
in seconds without booting the server, just by pointing it at the new
`.plan`.

It also prints the per-profile `(min, opt, max)` shape ranges for
dynamic inputs, which is the only way to confirm a converted engine's
profile actually matches what the C++ recognizer was sized for.

#### Why the upstream export commands are documented but not scripted

Both YOLOv9 and PaddleOCR ship their own canonical exporters as part of
their training repos. Mirroring those commands into a local Python
script would (a) immediately drift the moment upstream bumps a flag,
and (b) drag every dependency of `yolov9` and `paddleocr` into this
project's environment for a step that runs once. The README lists the
exact upstream commands instead, with the parameter values that produce
ONNX matching the C++ side's expectations (e.g. `--topk-all 100` so
EfficientNMS_TRT emits the `[1, 100, 4]` `det_boxes` shape that the
detector's `max_detections_` was sized for).

This is the same pattern the project uses elsewhere — never hand-write
canonical text; reference the canonical source.

#### What's *not* in this milestone

- Engine artifacts (`.plan` files). They're per-host, depend on the
  exact TRT version + GPU arch, and are gitignored under
  `server/models/`. Operator builds them at deploy time from the
  upstream ONNX.
- INT8 calibration cache. The flag is wired but the calibration data
  pipeline is a Phase 5 concern.
- Multi-batch optimization profiles. The C++ side runs one camera
  frame at a time; multi-image batching is a Phase 4.3.x feature and
  this script will gain a `--max-batch` flag when that lands.

---

## Previous milestone — Phase 4.2.4: ALPR pipeline orchestrator

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/alpr_pipeline.hpp`](server/inference/include/inference/alpr_pipeline.hpp)
> and [`server/inference/src/alpr_pipeline.cpp`](server/inference/src/alpr_pipeline.cpp).

### What I built

`AlprPipeline` is the single class the rest of the server (gate
controller, fusion engine, gRPC service) talks to for license-plate
reads. It owns both inference backends from the previous milestones,
chains them, and exposes one method:

```cpp
std::vector<PlateReading> AlprPipeline::process(const cv::Mat& frame_bgr);
```

`PlateReading` carries the detector geometry **and** the OCR result
together, so a downstream consumer never has to correlate two
parallel arrays:

```cpp
struct PlateReading {
    cv::Rect2f  box;             // detector box in original-frame pixels
    std::string text;            // CTC-decoded plate string
    float       detection_score; // YOLO confidence
    float       ocr_score;       // mean per-char CTC confidence
};
```

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/alpr_pipeline.hpp` | Public API: `PlateReading`, `AlprPipeline::Config` (bundles detector + recognizer config + crop policy), move-only pipeline class. |
| `server/inference/src/alpr_pipeline.cpp` | Implementation: top-K cap, margin-expanded crop with frame-edge clamp, batched OCR, joined output assembly. |
| `server/inference/CMakeLists.txt` (updated) | Adds `alpr_pipeline.cpp` to the `gate_inference` static library. |

### Technical detail

#### Why one class instead of free functions

Detection and recognition share three traits that argue for a single
owning object: (1) both hold non-trivial scratch buffers that should be
allocated once at startup; (2) both wrap a `TrtEngine` whose CUDA stream
should not outlive the engine; (3) the *policy* knobs (margin, top-K,
OCR floor) need to live somewhere that isn't either backend. A pipeline
class concentrates ownership in one place — when the gate controller
constructs an `AlprPipeline`, two TensorRT engines and their CUDA
streams come up as a unit, and the destruction order at shutdown is
guaranteed correct because both backends are members.

The class is move-only and noexcept-movable for the same reason as the
backends: the eventual multi-camera setup will hold a
`std::vector<AlprPipeline>` (one per camera lane), and reallocation must
not run an engine destructor by accident.

#### Top-K crop policy

`detector_.detect()` returns boxes sorted by descending confidence.
The pipeline takes the top
`Config::max_plates_per_frame` (default 8) and discards the rest before
OCR is invoked. The cap exists for two reasons:

- **Latency protection on noisy frames.** EfficientNMS still emits up
  to `max_detections` boxes (typically 100). On a clean residential gate
  frame that's almost always 1 plate, but on a wide-angle parking-lot
  shot the recognizer would otherwise be invoked dozens of times for
  low-confidence noise.
- **Predictable upper bound on OCR latency.** Phase 4.3 (fusion engine)
  will run on a fixed frame budget; pinning the OCR fan-out makes that
  budget computable.

#### Crop with margin and clamp

CRNN models are trained on plates with a small border of background
context. Cropping flush to the YOLO box trims the leftmost/rightmost
character — a known failure mode that drops one or two characters from
the OCR output. `expand_and_clamp_()` pads each detector box by
`Config::crop_margin` (default 8 %) of its width/height on every side,
then clamps the resulting ROI to the frame so a plate detected at the
edge of view doesn't index outside the image.

The expansion is done in float (`cv::Rect2f` arithmetic), then quantized
once at the end — `floor` for the origin and `ceil` for the size — so a
fractional 0.5 px never costs a column. Boxes that clamp to fewer than
4×4 pixels are dropped before OCR is called; the recognizer would
reject the empty crop anyway and this keeps the failure local.

#### Batched OCR

The `recognize_batch` overload added in 4.2.3 isn't yet truly batched at
the TensorRT level — it loops the single-image path. The pipeline still
calls it (instead of looping itself) because that's the API surface
that **will** become batched in Phase 4.3 without changing the
pipeline. When the recognizer's enqueue path grows real batch support,
the pipeline gets the speedup for free and `process()` stays unchanged.

#### Output policy

OCR results are returned **unfiltered** by `ocr_score` — the
`Config::ocr_confidence_floor` field stores the threshold but
`process()` does not apply it. This is deliberate: low-confidence reads
are useful telemetry (a guard reviewing the dashboard wants to see the
"almost-recognized" plates) and the gate-control policy is the
authoritative consumer of the threshold. Encoding the policy at the
pipeline boundary would force the dashboard to either re-derive it or
read filtered data.

The result list is in detection-score order (inherited from
`YoloPlateDetector::detect`), so callers can take `result[0]` as "the
most likely primary plate in this frame" without resorting.

#### Allocation discipline

`process()` itself allocates exactly two `std::vector`s per frame
(`crops` and `kept`), each pre-reserved to `n_keep`. The `cv::Mat`
crops use OpenCV's reference-counted pixel data — `frame_bgr(roi)` is
an O(1) view; the explicit `.clone()` produces an independent buffer
the recognizer can safely consume. No CUDA allocations happen here;
both engines' device buffers were sized at `load()` time and are reused.

#### Verified compile

```
[1/7] Scanning .../alpr_pipeline.cpp for CXX dependencies
[2/7] Generating CXX dyndep file
[3/4] Building CXX object .../alpr_pipeline.cpp.o
[4/4] Linking CXX static library libgate_inference.a
```

Built against TensorRT 10.16.1, CUDA 13.1.115, OpenCV 4.14.0, GCC
14.2.0 with `-std=c++20 -Wall -Wextra -Wpedantic -Werror` — no
diagnostics. clang-format pass applied to match the project's
`.clang-format` (Google base, `ColumnLimit 100`, `IndentWidth 4`,
`IncludeBlocks Regroup`) so the CI Lint job stays green.

---

## Previous milestone — Phase 4.2.3: PaddleOCR plate-text recognizer

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/paddle_ocr_recognizer.hpp`](server/inference/include/inference/paddle_ocr_recognizer.hpp)
> and [`server/inference/src/paddle_ocr_recognizer.cpp`](server/inference/src/paddle_ocr_recognizer.cpp).
> Dictionary asset at
> [`server/models/dict_kenya_plates.txt`](server/models/dict_kenya_plates.txt).

### What I built

A PaddleOCR PP-OCRv4-compatible plate-text recognizer that takes a
cropped plate image (the typical output of `YoloPlateDetector::detect`)
and returns a `RecognizedPlate { text, confidence }`. Like the
detector, it sits directly on top of the `TrtEngine` wrapper from
4.2.1 and uses the same async-enqueue / single-sync execution model so
the upcoming ALPR pipeline can chain detection → recognition without
ever reading the host CPU between models.

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/paddle_ocr_recognizer.hpp` | Public API: `RecognizedPlate` struct, `PaddleOcrRecognizer::Config`, move-only recognizer class. |
| `server/inference/src/paddle_ocr_recognizer.cpp` | Implementation: aspect-preserving resize + right-pad, per-channel mean/std normalization, async TRT inference, greedy CTC decode. |
| `server/models/dict_kenya_plates.txt` | 36-character dictionary (0–9, A–Z) covering every character that can appear on a Kenyan civilian or government plate. |
| `server/models/README.md` | Explains what model assets live in the tree (dictionaries) vs. what is built locally and gitignored (`.plan` engines, `.onnx` exports). |

### Technical detail

#### Preprocessing — what PaddleOCR actually expects

PP-OCRv4's plate recognizer is a CRNN-style network: a CNN backbone
that emits a sequence of feature columns, fed into a CTC head. The
input contract is unusual:

- **Fixed input height (48 px)** — required, because the CNN backbone's
  vertical stride collapses height to 1 in the feature map.
- **Variable input width up to a maximum (320 px)** — the recognizer
  reads left-to-right, so wider crops give more time steps but the
  trained max is 320.
- **Aspect-preserving resize** — squashing a wide plate into a square
  destroys character geometry; the model is trained on aspect-preserved
  inputs zero-padded on the right.

`preprocess_()` does exactly that: resize so height = 48 and width =
`round(48 × aspect)` clamped to `[1, 320]`, then `copyTo` into a
`(48 × 320, BGR, zero-padded)` canvas. The CTC head treats those
zero-padded columns as low-energy time steps and decodes them as blanks,
which the post-processor strips — so padding has no semantic effect on
the output text.

#### Normalization

PP-OCRv4 was trained with `(pixel/255 - mean) / std`, default
`mean = std = (0.5, 0.5, 0.5)`. With those symmetric values the
BGR-vs-RGB channel order is irrelevant, so the recognizer reads
OpenCV's native BGR directly and avoids a `cvtColor` round trip. The
arithmetic is per-channel (`cv::split` → subtract → divide), which keeps
us off `opencv_dnn::blobFromImage` and shaves a heavy module out of the
link line.

#### CTC greedy decode

The recognizer's output is `[1, T, C]` post-softmax probabilities. The
decoder is the standard CTC greedy:

```
for each time step t in [0, T):
    c = argmax_c output[t, c]
    if c == 0 (blank) or c == prev: skip            # CTC blank + repeat collapse
    text   += dictionary[c - 1]
    conf   += output[t, c]
    prev   = c
return (text, conf / kept_count)
```

Per-character confidence is the argmax probability at that time step;
overall plate confidence is the mean of those per-character values.
This is the right summary statistic for a downstream allow-list match —
a single low-confidence character in a 7-character plate drops the
score visibly, but a strong reading on the rest still indicates a high-
quality OCR.

A future enhancement (Phase 4.3 fusion engine) will use **per-character
confidence** rather than the mean to gate ambiguous chars (e.g.
`O` vs `0`) against the allow-list, but the mean is the right v1.

#### Dictionary contract

`dictionary_path` points to a UTF-8 text file with one character per
line. The model output's class 0 is the CTC blank token; class `i+1`
maps to dictionary line `i`. The recognizer enforces this on load —
if the model's class count doesn't equal `dictionary.size() + 1`, it
throws immediately with a descriptive `TrtException` so the failure
mode is "won't start" rather than "OCRs garbage".

The committed dictionary is tuned for **Kenya plates** specifically —
the format is `KXX 000X` (three letters + three digits + one letter),
and the 36-character vocabulary (0–9, A–Z) keeps the classifier head
small. Multi-region deployments swap the dictionary file without a
recompile; `server/models/README.md` documents the convention.

#### Allocation discipline

Identical to the detector: two host scratch buffers (`input_chw_`,
`output_logits_`), sized once at `load()` from the engine's resolved
shapes and reused for every recognition call. Per-frame inference
allocates only OpenCV's working memory for the resize + split.

#### Verified compile

```
[1/7] Scanning .../paddle_ocr_recognizer.cpp for CXX dependencies
[2/7] Generating CXX dyndep file
[3/5] Building CXX object .../paddle_ocr_recognizer.cpp.o
[4/5] Linking CXX static library libgate_inference.a
```

Built against the same toolchain as 4.2.1 and 4.2.2 (TensorRT 10.16.1,
CUDA 13.1.115, OpenCV 4.14.0, GCC 14.2.0,
`-std=c++20 -Wall -Wextra -Wpedantic -Werror`) — no diagnostics.

---

## Previous milestone — Phase 4.2.2: YOLOv9 plate detector

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/yolo_plate_detector.hpp`](server/inference/include/inference/yolo_plate_detector.hpp)
> and [`server/inference/src/yolo_plate_detector.cpp`](server/inference/src/yolo_plate_detector.cpp).

### What I built

A single-camera YOLOv9 license-plate detector built directly on the
`TrtEngine` wrapper from 4.2.1. It takes a BGR `cv::Mat` and returns a
`std::vector<PlateDetection>` whose boxes are already in the original
image's pixel coordinate frame — the call site does not need to know
anything about model input size, letterboxing, or NMS.

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/yolo_plate_detector.hpp` | Public API: `PlateDetection` struct, `YoloPlateDetector::Config`, move-only detector class. |
| `server/inference/src/yolo_plate_detector.cpp` | Implementation: letterbox, BGR→RGB normalization, CHW pack, async inference, EfficientNMS_TRT decode, coordinate unmap. |
| `server/inference/CMakeLists.txt` (updated) | Adds OpenCV (core + imgproc) to `gate_inference`'s public link line. |

### Technical detail

#### Why model-side NMS

The detector targets ONNX exports produced by the official `yolov9` repo
with the `--end2end` flag, which embeds the standard
`EfficientNMS_TRT` plugin in the model graph. The engine therefore emits
already-NMSed detections via four output tensors — `num_dets`,
`det_boxes`, `det_scores`, `det_classes` — and the C++ side never has to
implement anchor decoding or non-max suppression. This pushes
~2 ms of CPU work onto the GPU where it overlaps with the rest of the
forward pass, and keeps the call-site code under 200 lines.

The detector still applies a `confidence_floor` filter on the way back
out as defense-in-depth: the EfficientNMS thresholds are baked in at
export time, but the deployment may want a stricter floor without
re-exporting the engine.

#### Preprocessing pipeline

`YoloPlateDetector::detect(const cv::Mat& bgr)` does the standard
YOLO-family preprocessing in three OpenCV steps:

1. **Letterbox.** `letterbox_()` resizes the source frame to fit inside
   the model's input canvas (default 640×640) preserving aspect ratio,
   then pads the remainder with neutral gray `(114, 114, 114)` — the
   YOLOv9 / Ultralytics convention. The `(scale, pad_x, pad_y)` triple
   is captured so detection boxes can be unmapped exactly.
2. **Color + dtype.** `cv::cvtColor(... BGR2RGB)` then `convertTo(...,
   CV_32FC3, 1/255)`. Two function calls; OpenCV does the SIMD work.
3. **HWC → CHW.** `cv::split` writes the three planes directly into a
   contiguous `std::vector<float>` host buffer that the wrapper
   pre-allocated at `load()` time, so per-frame inference does no heap
   allocation in the hot path.

The host buffer is then handed to `TrtEngine::enqueue` as a
`std::span<const std::byte>` keyed by the input tensor name (`"images"`
by default).

#### Output decoding

`enqueue` is followed by a single `sync()` (we don't yet pipeline
detection with downstream OCR — that's 4.2.4 territory). The four
output buffers are then walked once:

```
for i in [0, num_dets[0]):
    score = scores[i]
    if score < confidence_floor: continue
    (x1, y1, x2, y2) = boxes[i*4 : i*4+4]      // letterboxed-input space
    x1 = max(0, (x1 - pad_x) / scale)          // → original-image space
    y1 = max(0, (y1 - pad_y) / scale)
    x2 = min(W, (x2 - pad_x) / scale)
    y2 = min(H, (y2 - pad_y) / scale)
    if x2 <= x1 or y2 <= y1: continue          // degenerate after clamp
    emit PlateDetection{box, score, class_id}
```

Detections are returned sorted by descending confidence so the ALPR
pipeline (4.2.4) can apply a top-K crop policy without resorting.

#### Allocation discipline

Five host scratch buffers (`input_chw_`, `num_dets_host_`, `boxes_host_`,
`scores_host_`, `classes_host_`) are sized once at `load()` from the
context-resolved output shapes and reused for every frame. The detector
makes zero allocations in the per-frame path beyond OpenCV's internal
working memory for the `cvtColor`/`convertTo`/`split` steps.

The `max_detections` constant comes from the engine itself —
`engine_->context()->getTensorShape("det_boxes")` returns
`[1, max_det, 4]` after the input shape is pinned at load time. This
keeps the C++ side automatically in sync with however the ONNX export
was configured.

#### CMake: surviving OpenCV-with-CUDA on a modern toolchain

OpenCV was source-built against CUDA 13.1, which means
`OpenCVConfig.cmake` unconditionally calls
`find_host_package(CUDA REQUIRED)` — the legacy `FindCUDA` module.
CMake 3.27+ defaulted policy `CMP0146` to `NEW`, which removes that
module, and the policy doesn't propagate through vcpkg's
`find_package` wrapper. Rather than fight scoping, the build pre-fills
the half-dozen `CUDA_*` variables that OpenCV's config actually reads —
sourced from the modern `CUDA::cudart` / `CUDA::cublas` /
`CUDA::cufft` / `CUDA::nppc` / `CUDA::nppial` / `CUDA::npps` imported
targets that `find_package(CUDAToolkit)` provides — and stubs out
`find_cuda_helper_libs` as a no-op since we've already populated the
libraries it would have found. OpenCV's `if(NOT CUDA_FOUND)` short-
circuits cleanly and the rest of its config proceeds normally.

This is documented inline in `server/inference/CMakeLists.txt` so the
next person who reads it doesn't have to re-derive the chain.

#### Verified compile

```
[1/6] Scanning .../trt_engine.cpp for CXX dependencies
[2/6] Scanning .../yolo_plate_detector.cpp for CXX dependencies
[3/6] Generating CXX dyndep file ...
[4/6] Building CXX object .../trt_engine.cpp.o
[5/6] Building CXX object .../yolo_plate_detector.cpp.o
[6/6] Linking CXX static library libgate_inference.a
```

Built against TensorRT 10.16.1, CUDA 13.1.115, OpenCV 4.14.0 (CUDA
source-build), GCC 14.2.0 with `-std=c++20 -Wall -Wextra -Wpedantic
-Werror` — no diagnostics.

---

## Previous milestone — Phase 4.2.1: TrtEngine RAII wrapper

> **Completed 2026-04-25.** Code in
> [`server/inference/include/inference/trt_engine.hpp`](server/inference/include/inference/trt_engine.hpp)
> and [`server/inference/src/trt_engine.cpp`](server/inference/src/trt_engine.cpp).

### What I built

A modern C++20 RAII wrapper around the TensorRT 10.x runtime API. It owns the
full inference state for one engine — the deserialized `ICudaEngine`, an
`IExecutionContext`, a CUDA stream, and per-binding device buffers — and
exposes a name-based, exception-throwing interface that the rest of the
inference layer (YOLOv9 detector, PaddleOCR recognizer, ALPR pipeline) is
built on top of.

| Artifact | Purpose |
|---|---|
| `server/inference/include/inference/trt_engine.hpp` | Public API: `TrtException`, `TrtLogger`, `TensorIo` descriptor, move-only `TrtEngine` class. |
| `server/inference/src/trt_engine.cpp` | Implementation: engine deserialization, binding introspection, dynamic-shape buffer sizing, async H↔D + `enqueueV3` execution path. |
| `server/inference/CMakeLists.txt` | Builds `gate_inference` static lib; locates TensorRT headers/libs via standard system paths or `-DTENSORRT_ROOT=…`. |
| `server/CMakeLists.txt` | Adds the `inference/` subtree under `BUILD_SERVER=ON`. |
| Root `CMakeLists.txt` | Conditionally pulls in `server/` only when CUDA + spdlog are resolved, mirroring the same graceful-skip pattern used for `shared/proto/` and `tests/`. |

### Technical detail

#### Why a hand-written wrapper

TensorRT's runtime API (`IRuntime`, `ICudaEngine`, `IExecutionContext`)
returns raw pointers, uses noexcept return-code error handling, and in TRT
10.x deletes via the C++ `delete` operator (the older `destroy()` virtual is
gone). Calling that surface directly from inference code would scatter
`if (!ok) return false;` checks through every detector and recognizer. The
wrapper:

- Concentrates all error handling at one boundary — every TRT failure
  becomes a `gate::inference::TrtException` with a descriptive message
  (failing call name + offending tensor + offending shape where relevant).
- Takes ownership of the runtime / engine / context / CUDA stream / device
  buffers via `unique_ptr` with custom deleters, so the destructor frees
  resources in the only correct order: device buffers → stream → context →
  engine → runtime.
- Exposes name-based binding (`engine->getIOTensorName(i)` /
  `setTensorAddress(name, ptr)`) so re-exporting an engine with renumbered
  bindings doesn't break call sites — they reference tensors by string.
- Is move-only and noexcept-movable, which lets it live inside containers
  (`std::vector<TrtEngine>`) for the multi-engine pipeline that Phases 4.2.2
  and 4.2.3 will assemble.

#### Engine load path

`TrtEngine::load(path, logger)`:

1. Reads the serialized `.plan` file into a `std::vector<std::byte>` in one
   shot. The plan is opaque bytes; we do not parse it — TensorRT does.
2. `nvinfer1::createInferRuntime(logger)` produces the runtime. The supplied
   logger is a TensorRT `ILogger` reference; the project's default
   implementation, `TrtLogger`, bridges TRT severities to spdlog levels
   (`kINTERNAL_ERROR → critical`, `kERROR → err`, `kWARNING → warn`, etc.)
   with a configurable threshold (default `kWARNING` to keep INFO chatter
   out of the production log).
3. `runtime_->deserializeCudaEngine(blob.data(), blob.size())` reconstructs
   the engine. A null return raises `TrtException` with the engine path so
   plan corruption surfaces immediately at startup, never at the first
   inference.
4. `engine_->createExecutionContext()` produces the per-thread context.
5. A dedicated CUDA stream is created via `cudaStreamCreate`. Every H↔D
   copy and every kernel launch is enqueued on this stream, so `sync()` is
   the single observation point for all in-flight work.

#### Binding introspection

After the context is created, the loader walks
`engine_->getNbIOTensors()` and builds a `TensorIo` descriptor for each
binding:

```cpp
struct TensorIo {
    std::string             name;       // canonical TRT tensor name
    nvinfer1::Dims          shape;      // -1 marks dynamic dims
    nvinfer1::DataType      dtype;
    bool                    is_input;
    std::size_t             elem_size;  // bytes per element
};
```

Two parallel containers index by binding position (`tensors_`,
`device_buffers_`, `device_buffer_bytes_`); a `name_to_index_` hash map
gives O(1) name lookup. All public methods take `std::string_view` and
resolve through that map so call sites don't carry binding numbers.

#### Dynamic-shape device buffer allocation

The hardest part of a generic TRT wrapper is sizing buffers when the engine
has dynamic dimensions (the `-1` dims YOLOv9 uses for `batch` and the OCR
recognizer uses for sequence length). The wrapper handles all three cases
in `allocate_buffers_()`:

1. **Static binding** — `volume(shape) * elem_size` is allocated directly.
2. **Dynamic input** — the wrapper queries
   `engine_->getProfileShape(name, 0, OptProfileSelector::kMAX)` and
   allocates for that maximum. Subsequent `set_input_shape()` calls with
   any in-profile shape reuse the same buffer.
3. **Dynamic output** — output shapes can also be `-1` (e.g. NMS-derived
   detection counts). The wrapper does a second pass: it primes every
   dynamic input with its kMAX shape via `setInputShape`, then asks the
   context to resolve each output via `getTensorShape(name)`, and allocates
   from there.

`buffer_bytes(name)` consults the context's *current* view of the shape, so
after `set_input_shape()` the reported size shrinks to match the runtime
shape — the device buffer is over-allocated (safe) but `enqueue()` only
copies the bytes the model actually consumes/produces.

#### Async execution path

`enqueue(host_in, host_out)` performs the entire ALPR-step lifecycle on the
internal stream:

1. **Re-bind every tensor address.** TRT 10's `enqueueV3` requires an
   address for every input and output to have been set since the last
   `setInputShape` call. The wrapper rebinds unconditionally so callers
   don't accidentally inherit a stale binding from a prior context use.
2. **Async H→D for every input** in the supplied map.
   `cudaMemcpyAsync(..., cudaMemcpyHostToDevice, stream_)`. Each input span
   is size-checked against `buffer_bytes(name)`; mismatches throw before
   any DMA is issued.
3. **Async forward pass.** `context_->enqueueV3(stream_)`. A `false` return
   raises `TrtException` with a hint about the most likely cause (unbound
   input or unset shape).
4. **Async D→H for every requested output**, with the same size-check
   discipline.
5. **`sync()`** is a separate, optional call. The split lets the caller
   overlap CPU work (post-processing, fusion, gRPC reply assembly) with
   GPU work, and gives the ALPR pipeline a place to insert a CUDA event
   for cross-stream barriers later.

Every CUDA call goes through a `check_cuda(status, "what")` helper that
converts the error code into `cudaGetErrorString(...)` text so failures in
production logs are immediately diagnosable.

#### Build integration

`server/inference/CMakeLists.txt` locates TensorRT through `find_path` /
`find_library` rather than `find_package` because TensorRT does not ship a
CMake config. The result is wrapped in an `IMPORTED` target,
`TensorRT::nvinfer`, with the header path attached as `SYSTEM` includes so
the project-wide `-Wall -Wextra -Wpedantic -Werror` doesn't flag TRT's
own headers. The library links `CUDA::cudart` from `CUDAToolkit` and
`spdlog::spdlog` from vcpkg.

The root `CMakeLists.txt` only descends into `server/` when `BUILD_SERVER`
is on **and** `find_package(spdlog CONFIG QUIET)` succeeds. CI's
non-toolchain Build job stays green because the inference module is simply
skipped with a clear status message; local development with the vcpkg
toolchain pulls the full dependency closure (gRPC, protobuf, drogon,
spdlog, sqlite3, fmt, nlohmann-json, Catch2, cli11) and builds normally.

#### Verified compile

Built clean against:

- TensorRT 10.16.1 (`libnvinfer.so` at `/usr/lib/x86_64-linux-gnu/`)
- CUDA Toolkit 13.1.115 (nvcc + cudart)
- GCC 14.2.0 with `-std=c++20 -Wall -Wextra -Wpedantic -Werror`
- spdlog 1.17.0 from vcpkg

Build output:

```
[1/4] Scanning .../trt_engine.cpp for CXX dependencies
[2/4] Generating CXX dyndep file ...
[3/4] Building CXX object .../trt_engine.cpp.o
[4/4] Linking CXX static library libgate_inference.a
```

No warnings, no diagnostics — strict warnings are kept on for first-party
code.

---

## Previous milestone — Phase 4.1: gRPC wire contract

> **Pulled this off on 2026-04-25.** Full code in
> [`shared/proto/`](shared/proto/) and [`tests/proto/`](tests/proto/).

### What I built

A single canonical `gate.v1` protobuf schema —
[`shared/proto/gate_service.proto`](shared/proto/gate_service.proto) — that
defines every byte that crosses a process boundary in the system. Firmware,
inference server, dashboard backend, and simulation all generate stubs from
this one file.

| Artifact | Purpose |
|---|---|
| `shared/proto/gate_service.proto` | 25 messages, 6 enums, 3 services, 10 RPC methods. |
| `shared/proto/CMakeLists.txt` | Generates `gate_proto` static lib (messages + gRPC stubs) for host C++ targets. Consumed by server, dashboard, and sim. |
| `shared/proto/README.md` | Developer reference for the wire contract. |
| `tests/proto/proto_contract_test.cpp` | Catch2 round-trip tests for telemetry, decisions, OTA, and the bidirectional `ControlEnvelope`. |
| `tests/proto/validate_descriptor.py` | protoc-only descriptor validator that runs in CI without the full C++ toolchain. |
| `.github/workflows/proto.yml` | CI job that compiles the descriptor and runs the validator on every change. |

### Technical detail

#### Service surface

Three services with deliberately chosen streaming patterns:

| Service / Method | Streaming kind | Why |
|---|---|---|
| `FieldControllerService.Control` | bidi (client+server stream) | One TCP connection per gate, kept alive for the lifetime of the field controller. Telemetry flows up at 1 Hz; commands flow down on demand; acks/faults multiplex over the same stream — half the connection count, no race between an incoming command and an outgoing ack. |
| `FieldControllerService.DeliverOta` | server stream | OTA images are 1.5–2 MB. Streaming 4 KB chunks (one ESP32 flash sector each) lets the firmware hash incrementally and abort early on signature mismatch without buffering the whole image in PSRAM. |
| `FieldControllerService.ReportOtaProgress` | client stream | Firmware emits a progress event for each phase (`DOWNLOADING → VERIFYING → INSTALLING → REBOOTING → COMPLETE`). One stream per OTA session. |
| `FieldControllerService.SubmitDetection` | unary | A single detection frame from the GPU pipeline returns one `AuthDecision`. Used inside the server and from the simulation harness. |
| `DashboardService.Subscribe` | server stream | The web UI gets a filtered, replayable feed of every `DashboardEvent` (decisions, telemetry, faults, OTA progress, commands, acks) via the `since_event_id` resume cursor. |
| `DashboardService.IssueCommand` / `Authorize` | unary | Manual override and synchronous test calls. |
| `AdminService.{Upsert,List,Delete}Allowlist` | unary | CRUD over the per-site allowlist. |

#### Key design choices

1. **`oneof ControlEnvelope.payload`** multiplexes telemetry, command,
   command-ack and fault events over the bidirectional stream. The contract
   test asserts the four payload tags and their stable field numbers
   (`kTelemetry`, `kAck`, `kFault`, `kCommand`).

2. **`google.protobuf.Timestamp` + `Duration` everywhere** — no integer
   "milliseconds since epoch" or hand-rolled time fields. This keeps every
   client (C++, Python sim, JS dashboard) using the same time semantics.

3. **`DashboardEvent.event_id` is monotonic** per server boot; the
   `DashboardSubscription.since_event_id` field lets the web UI resume after
   a refresh without losing events.

4. **`OtaChunk` size is fixed at 4096 bytes** to match an ESP32 flash sector
   (the contract test verifies a full sector round-trips). This eliminates
   the partial-sector edge case in firmware OTA.

5. **`Telemetry` is bandwidth-budgeted at 256 bytes**. The contract test
   `Telemetry stays under 256-byte firmware budget` enforces this at build
   time so we don't accidentally bloat the over-the-air protocol when
   adding fields.

6. **Stable field numbers and enum values** are documented in
   `validate_descriptor.py`. Any change that moves a number is a breaking
   change, requires a `gate.v2` package, and must be approved via ADR.

#### Build integration

`shared/proto/CMakeLists.txt` invokes `protoc` with the gRPC C++ plugin to
emit `gate_service.pb.{h,cc}` and `gate_service.grpc.pb.{h,cc}`, links them
into a single `gate_proto` static target, and re-exports the FileDescriptorSet
so other tools (gRPC reflection, dashboard codegen) can consume it. The
target is added by the root `CMakeLists.txt` whenever
`-DBUILD_PROTO=ON` (the default).

Generated code is compiled with `-Wno-unused-parameter -Wno-deprecated-declarations`
because `protoc` output is not under our control; first-party code remains
under the strict project-wide `-Wall -Wextra -Wpedantic -Werror`.

#### Validation strategy

Two layers, deliberately split:

- **`validate_descriptor.py`** runs in CI on every push that touches
  `shared/proto/`. It only needs `protoc` and Python's `google.protobuf` —
  no vcpkg toolchain bootstrap. It walks the descriptor and asserts:
    - All 25 expected messages exist.
    - All stable enum values have their documented numbers.
    - All 10 RPC methods exist with the correct client/server streaming kinds.
    - `ControlEnvelope` is a `oneof` named `payload` with the four
      documented variants.
- **`proto_contract_test.cpp`** is a Catch2 suite that round-trips every
  important message through `SerializeToString` / `ParseFromString`,
  verifies the `ControlEnvelope` oneof discriminator, and enforces the
  256-byte telemetry budget. It builds as part of the standard `tests/`
  target once the full vcpkg dependencies are in place.

#### Current validator output

```
Validating shared/proto/gate_service.proto
OK: package=gate.v1, syntax=proto3
OK: all 25 expected messages present
OK: enum VehicleClass has 9 stable values
OK: enum GateState has 7 stable values
OK: enum AuthVerdict has 5 stable values
OK: service FieldControllerService has 4 methods with correct streaming kinds
OK: service DashboardService has 3 methods with correct streaming kinds
OK: service AdminService has 3 methods with correct streaming kinds
OK: ControlEnvelope oneof has 4 expected payloads

All proto contract checks passed.
```

---

## Repository layout

```
gate-automation/
├── docs/                  # ADRs, diagrams, hardware build guides, env audit
│   ├── decisions/         # 11 ADRs (ADR-000 through ADR-010)
│   ├── diagrams/          # 7 Mermaid flowcharts
│   └── hardware/          # 9 component guides + master build book + KiCad PDF
├── hardware/
│   └── bom/               # Bill of materials with KES/USD pricing
├── shared/
│   ├── proto/             # ✅ Phase 4.1 — gRPC wire contract
│   └── include/           # Shared C++ headers
├── server/                # 🔵 Phase 4.2/4.3 — ALPR + LiDAR inference + fusion
│   ├── inference/         # ✅ Phase 4.2 — TensorRT engines + ALPR pipeline
│   ├── auth/              # ✅ Phase 4.3.1 — allowlist + blocklist store
│   └── fusion/            # ✅ Phase 4.3.2 — verdict-ladder decision engine
├── firmware/              # ⏳ Phase 4.4 — ESP-IDF field controller firmware
├── simulation/            # ⏳ Phase 4.6 — virtual gate harness
├── dashboard/             # ⏳ Phase 4.7 — Drogon backend + SvelteKit frontend
├── deployment/            # ⏳ Phase 4.8 — systemd units + install scripts
├── tests/                 # Catch2 unit + contract tests
│   ├── proto/             # ✅ Phase 4.1 — proto contract tests
│   ├── inference/         # ✅ Phase 4.2.6 — host-side algorithm tests
│   ├── auth/              # ✅ Phase 4.3.1 — allowlist store tests
│   └── fusion/            # ✅ Phase 4.3.2 — fusion engine tests
├── scripts/bootstrap/     # Reproducible WSL2 dev environment scripts
└── .github/workflows/     # CI: build, test, lint, codeql, commitlint, proto
```

---

## Hardware

Single-gate prototype BOM:

- **Gate-side electronics:** KES 58,300 (~USD 448)
- **GPU server (RTX 4060 + i5):** KES 85,200 (~USD 655)
- **System total without motor:** **KES 143,500 (~USD 1,103)**
- **With sliding gate motor (CENTURION D5):** KES 208,500 (~USD 1,603)

Full BOM with sources and prices: [`hardware/bom/prototype-bom.md`](hardware/bom/prototype-bom.md).

Build book (9 step-by-step component guides + master assembly): [`docs/hardware/BUILD_BOOK.md`](docs/hardware/BUILD_BOOK.md).

Production PCB: 4-layer 100×80 mm field controller designed in KiCad 9.0
— [48-page design guide PDF](docs/hardware/10-pcb-design-kicad9.pdf).

---

## Tech stack

| Layer | Tools |
|---|---|
| Compute | NVIDIA GPU (CUDA 13.1, cuDNN 9.19, TensorRT 10.15) |
| Inference | YOLOv9 (plate detection), PaddleOCR (character recognition) |
| Vision | OpenCV 4.14 (CUDA-enabled, source build) |
| Server (C++) | C++20, CMake 3.31, Ninja, mold, ccache, vcpkg manifest mode |
| RPC | gRPC + Protobuf (single canonical schema in `shared/proto/`) |
| Dashboard backend | Drogon (C++ web framework) |
| Dashboard frontend | SvelteKit |
| Field controller | ESP32-S3-WROOM-1-N16R8 + W5500 wired Ethernet |
| Firmware | ESP-IDF, modern C++ |
| OTA | Self-hosted, ed25519-signed, atomic swap with rollback |
| Tests | Catch2 v3, ASan/UBSan, clang-tidy, cppcheck, CodeQL |
| CI | GitHub Actions (matrix: GCC 13/14 × Clang 17/18 × Debug/Release) |

---

## Running it locally

### Bootstrap the dev environment (one-time, WSL2 Debian)

```bash
cd scripts/bootstrap
./01-toolchain.sh        # GCC, Clang, CMake, Ninja, mold, ccache
./02-cuda-stack.sh       # CUDA 13.1 + cuDNN 9.19 + TensorRT 10.15
./03-opencv-cuda.sh      # OpenCV 4.14 from source with CUDA
./04-vcpkg.sh            # vcpkg + manifest install
./05-esp-idf.sh          # ESP-IDF for firmware development
```

Verify with the GPU smoke test (see [`docs/env-audit.md`](docs/env-audit.md)).

### Validate the proto schema

```bash
python3 tests/proto/validate_descriptor.py
```

### Configure & build (Phase 4.1)

```bash
cmake --preset release
cmake --build --preset release --target gate_proto
ctest --preset release
```

---

## Documentation

- [Environment audit](docs/env-audit.md) — every tool, every version, every challenge solved during bootstrap
- [Architecture decisions (ADRs)](docs/decisions/) — 11 records covering license, RPC, web framework, MCU, LiDAR, camera, gate actuator, OTA, model licensing
- [System diagrams](docs/diagrams/) — 7 Mermaid flowcharts (system, ALPR, LiDAR, fusion, gate state machine, OTA, sim mode)
- [Hardware build book](docs/hardware/BUILD_BOOK.md) — bench prototype to working gate in ~3.5 h
- [PCB design guide (PDF)](docs/hardware/10-pcb-design-kicad9.pdf) — KiCad 9.0 schematic-to-Gerbers
- [Bill of materials](hardware/bom/prototype-bom.md) — every part with KES + USD pricing and Nairobi/AliExpress sources

---

## License

[GPL-3.0](LICENSE) — chosen for compatibility with YOLOv9's GPL-3.0 license
(see [ADR-000](docs/decisions/ADR-000-license.md) and
[ADR-010](docs/decisions/ADR-010-model-licensing.md)).

## Contributing

This is a personal portfolio project. External contributions are not yet
accepted, but feedback via GitHub issues is welcome.
