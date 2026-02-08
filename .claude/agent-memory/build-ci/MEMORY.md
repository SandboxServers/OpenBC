# Build/CI Agent Memory

## Phase 1 Plan Status
- Comprehensive build plan produced (2026-02-07). Research only, no code written yet.
- See `phase1-build-plan.md` for full details.

## Dependency Versions (Pinned)
- **flecs**: v4.1.4 (Dec 2024) - vendored as single-header in `vendor/flecs/`
- **ENet**: v1.3.18 (Apr 2024) - optional FetchContent, OFF by default for Phase 1
- **Python**: >= 3.8 via FindPython3 Development.Embed component
- **CMake minimum**: 3.20

## Key Architecture Decisions
- C11 standard (not C99, not C17)
- `OPENBC_SERVER_ONLY=ON` default for Phase 1 (no bgfx/SDL3/miniaudio/RmlUi)
- Raw UDP networking (NOT ENet) for BC protocol compatibility
- BC default port: 22101 (0x5655)
- flecs vendored (not FetchContent) - offline builds, ~400KB total
- Python found via system packages, not built from source
- ccache on Linux/macOS, sccache on Windows for CI

## Platform Notes
- **Windows**: ws2_32.lib + iphlpapi.lib for networking, `_WIN32_WINNT=0x0601`
- **Linux**: python3-dev package, pthread linked via flecs
- **macOS**: `Python3_FIND_FRAMEWORK=LAST` to prefer Homebrew over system Python

## Docker
- Multi-stage: debian:bookworm-slim builder + runtime
- Runtime needs: python3 + libpython3.11 (no -dev)
- Final image ~150-170 MB
- Port: 22101/udp
- Mount game data at /data/bc

## CI Matrix
- ubuntu-24.04 (GCC 13 + Clang 18), windows-latest (MSVC), macos-14 (AppleClang)
- Debug + Release for each
- Docker build on main/tags only

## Test Strategy
- CTest + custom assertion macros (no third-party test framework)
- Suites: protocol, checksum, compat, ecs, scripting, server_startup
- Unity (ThrowTheSwitch) as upgrade path if needed later

## Files Index
- [phase1-build-plan.md](phase1-build-plan.md) - detailed plan with all CMake content
