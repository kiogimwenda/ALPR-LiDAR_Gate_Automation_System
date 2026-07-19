# Contributing

Thanks for your interest. This project is a production-grade system
with a documented history — the fastest way to be effective in it is
to read the [README](README.md)'s chronological log for the subsystem
you're touching, and the relevant [ADRs](docs/decisions/) before
proposing a change that crosses one.

## Ground rules

- **License:** GPL-3.0 ([why](docs/decisions/ADR-000-license.md) —
  YOLOv9's weights propagate it). Contributions are accepted under the
  same license.
- **Conduct:** see [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
- **Branching:** work lands on `develop`; `main` tracks tagged
  releases. Branch from `develop`, PR back into it.
- **Architecture changes need an ADR.** Anything that alters a
  decision recorded in `docs/decisions/` supersedes it with a new
  numbered ADR (see ADR-011/ADR-012 for the pattern) rather than
  editing history.

## Building

Host stack (server, dashboard, simulation, tests) — vcpkg manifest,
CMake presets:

```bash
cmake --preset debug-cpu && cmake --build --preset debug-cpu   # no GPU needed
cmake --preset debug                                           # CUDA + TensorRT build
```

Firmware — ESP-IDF (see [firmware/README.md](firmware/README.md)):

```bash
cd firmware && idf.py set-target esp32s3 && idf.py build
```

## The bar for merging

CI enforces three gates on every push; run them locally first:

```bash
# 1. Tests — the full suite, including the multi-process E2E passes.
ctest --test-dir build/debug-cpu --output-on-failure

# 2. Format — clang-format over the CI lint scope.
find server firmware shared dashboard/backend simulation \
     \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cc' \) \
     -not -path 'firmware/components/gate_rpc/src/pb/*' \
     | xargs clang-format --dry-run -Werror

# 3. Static analysis.
cppcheck --enable=warning,performance,portability --error-exitcode=1 \
     --inline-suppr --suppress=missingInclude \
     --suppress=normalCheckLevelMaxBranches \
     server/src firmware/src shared/include dashboard/backend/src simulation/src
```

Beyond the mechanical gates:

- **New behavior ships with tests.** Pure logic gets unit tests
  (Catch2); cross-process behavior extends the E2E harness
  (`tests/e2e/e2e_local_stack.py`). Look at a neighboring test
  directory for the house pattern before inventing one.
- **Security posture is non-negotiable:** asked-for security never
  silently degrades (an unreadable secret refuses to start; a missing
  firmware cert fails the build), and security-off is legal but loud.
  A PR that weakens either property will be declined regardless of
  its other merits. Read [docs/security.md](docs/security.md) first.
- **Comments explain why, not what** — constraints, invariants, and
  traps the code can't express. Match the density of the surrounding
  file.
- **Firmware purity split:** logic that can be pure (state machine,
  framing, OTA rules) stays pure and host-tested; only the thin
  integration layer touches ESP-IDF. Keep it that way — the bench
  plan's debugging model depends on it.

## Commit messages

Conventional-commits style as used throughout the history
(`feat(vision): …`, `fix(rpc): …`, `docs(adr): …`), imperative mood,
body explaining what changed and why when it isn't obvious.

## Secrets

Never commit: TLS/PKI material (`/etc/gate/tls`,
`firmware/main/certs/*.pem`), JWT secrets, admin password hashes with
real passwords behind them, or OTA signing keys (the signing key never
leaves the offline release machine — [ADR-009](docs/decisions/ADR-009-ota-strategy.md)).
The relevant `.gitignore` entries exist; don't work around them.
