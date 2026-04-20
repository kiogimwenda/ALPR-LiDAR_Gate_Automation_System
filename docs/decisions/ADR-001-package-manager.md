# ADR-001: Package Manager — vcpkg

## Status
Accepted

## Date
2026-04-19

## Context

The project needs a C++ package manager to handle third-party dependencies (gRPC, Catch2, a web framework, JSON library, etc.) across a CMake-based build. The two viable modern options are **vcpkg** and **Conan 2**.

### Options Evaluated

1. **vcpkg** (Microsoft, open source)
   - Integrates with CMake via toolchain file (`-DCMAKE_TOOLCHAIN_FILE`)
   - Manifest mode (`vcpkg.json`) pins dependencies per-project
   - Large registry (~2,500 ports), actively maintained
   - Binary caching out of the box
   - No Python dependency — single static binary
   - CI-friendly: GitHub Actions has first-party vcpkg caching

2. **Conan 2** (JFrog, open source)
   - Python-based (`pip install conan`), recipe-driven (`conanfile.py`)
   - More flexible for cross-compilation and custom build systems
   - Requires Python in CI and on dev machines
   - Profiles system is powerful but adds learning curve
   - Smaller registry for some niche packages

### Ranking

**Option 1 (vcpkg) is best** for this project because:
- Simpler CMake integration — one toolchain file, no generator step
- No Python dependency in the C++ build chain (Python is only for ML export scripts)
- CI integration is trivial on GitHub Actions
- The project's dependencies (gRPC, Catch2, Crow/Drogon, nlohmann-json) are all well-maintained vcpkg ports

**Option 2 (Conan 2) is the fallback** if we need cross-compilation profiles for the field PCB's MCU — but MCU firmware will likely use PlatformIO or ESP-IDF, not the same build system.

## Decision

Use **vcpkg** in manifest mode (`vcpkg.json` at project root) with CMake toolchain integration.

## Consequences

- All C++ dependencies declared in `vcpkg.json`
- CMake configured with `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`
- CI workflows use `vcpkg` with GitHub Actions caching
- Developers must have vcpkg installed (documented in bootstrap scripts)
