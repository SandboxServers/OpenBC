# OpenBC -- Agent Instructions

This file is for OpenAI Codex and other AI coding agents. The canonical project instructions live in **CLAUDE.md** at the repository root. Read that file in full before doing any work -- it contains the architecture, protocol reference, build system, verified facts, and implementation gotchas that apply to all contributors (human or AI).

Everything below supplements CLAUDE.md with operational notes for agents that don't auto-load it.

## Critical: Clean Room Rules

This is a **clean room reimplementation**. These rules are absolute:

- **NEVER** access, search for, or reference original Bridge Commander source code, decompiled code, binary data, or reverse-engineered repositories (GitHub or elsewhere).
- **NEVER** reference binary addresses (`FUN_XXXXXXXX`, `DAT_XXXXXXXX`), struct field offsets, vtable layouts, or decompiled pseudocode.
- All protocol knowledge comes from `docs/` and observable wire behavior (packet traces). If it's not in the clean room docs, it must come from behavioral observation, not binary analysis.

**Legal basis**: Oracle v. Google (2021), Sega v. Accolade (1992). See CLAUDE.md for full details.

## Build & Test

```bash
# Build (cross-compiles from Linux/WSL2 to Win32 via MinGW)
make all

# Run all tests
make test

# Clean (but NEVER delete build/*.log files -- they contain session logs)
make clean
```

Toolchain: `i686-w64-mingw32-gcc` with `-Wall -Wextra -Wpedantic`. Zero warnings policy.

## Project Structure

- `src/` -- C source (checksum, game, json, network, protocol, server)
- `tests/` -- 11 test suites (unit via `test_util.h`, integration via `test_harness.h`)
- `docs/` -- Clean room protocol documentation (the single source of truth)
- `data/` -- Ship/projectile registry (JSON, machine-generated)
- `manifests/` -- Hash manifests for file integrity verification
- `tools/` -- Python 3 CLI tools (data scraper, trace comparison, diagnostics)

## Key Conventions

- C (not C++). No external dependencies beyond MinGW's libc and Winsock2.
- Raw UDP networking, wire-compatible with stock BC 1.1 clients.
- JSON data files drive all game configuration (ships, weapons, maps, rules).
- Test fixtures in `tests/fixtures/` include `.pyc` stub files that must remain tracked.
- Read `docs/` before implementing any protocol or game logic -- do not guess at wire formats.
