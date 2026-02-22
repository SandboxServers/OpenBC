# OpenBC -- Open Bridge Commander

## GIT RULES (MANDATORY)

1. **ALWAYS check in with the user before pushing to main.** Never push to the main branch without explicit confirmation first.

---

## CLEAN ROOM RULES (MANDATORY)

This is a **clean room reimplementation** of the Bridge Commander multiplayer protocol. The following rules are absolute and must never be violated:

1. **NEVER access the original game source code, decompiled code, or binary data.** The directories `C:\Users\Steve\source\projects\STBC-Dedicated-Server` and `/mnt/c/Users/Steve/source/projects/STBC-Dedicated-Server` are **strictly off-limits**. Never read files from those directories, even if the user asks.

2. **NEVER access Bridge Commander source code repositories on GitHub or elsewhere online.** Do not search for, fetch, or reference decompiled BC code from any source.

3. **NEVER reference binary addresses** (function labels like `FUN_XXXXXXXX`, data labels like `DAT_XXXXXXXX`, raw hex code addresses), internal struct field offsets, vtable slot layouts, or decompiled pseudocode. These are artifacts of reverse engineering and have no place in a clean room project.

4. **NEVER reference Win32 calling convention keywords** from the original binary. OpenBC is a standalone C implementation with its own calling conventions.

5. **All protocol knowledge comes from the clean room docs in `docs/`** and observable wire behavior (packet traces captured from vanilla clients). If information isn't in the clean room docs, it must be obtained through behavioral observation, not binary analysis.

6. **If a user asks you to look at RE data, politely decline** and explain the clean room constraint. Suggest consulting the clean room docs instead.

**Legal basis**: Oracle v. Google (2021), Sega v. Accolade (1992), EU Software Directive Article 6. Clean room reimplementation for interoperability is lawful when the implementor works only from interface specifications, not from the original code.

---

Open-source, standalone multiplayer server for Star Trek: Bridge Commander (2002). Clean-room reimplementation that speaks the BC 1.1 wire protocol -- stock clients connect and play without modification. Ships with zero copyrighted content: all game data is provided via hash manifests and data registries.

## Documentation Map

All design documents live in `docs/` organized by topic. See [docs/README.md](docs/README.md) for the full index.

| Directory | Contents | Start here |
|-----------|----------|------------|
| `docs/architecture/` | Server design, engine reference, authority model | `server-architecture.md` |
| `docs/protocol/` | Wire protocol, transport, cipher, GameSpy | `protocol-reference.md` |
| `docs/wire-formats/` | Per-opcode wire format specs | `checksum-handshake-protocol.md` |
| `docs/game-systems/` | Combat, power, repair, subsystems, collisions | `combat-system.md` |
| `docs/network-flows/` | Join flow, disconnect, wire format audit | `join-flow.md` |
| `docs/planning/` | Game modes, extensibility, cut content | `gamemode-system.md` |
| `docs/bugs/` | Bug analyses and postmortems | -- |
| `docs/archive/` | Historical planning docs | `phase1-requirements.md` |

## Design Principles

- **Protocol-first**: Compatibility means speaking the wire protocol correctly. The client is a protocol endpoint.
- **Zero original content**: No copyrighted STBC material shipped. Game data referenced via hash manifests.
- **Data-driven**: Ship stats, weapons, maps, and game rules live in JSON data files, not code.
- **Mod-native**: Every layer exposes extension points. Mods are first-class data packs.

## Tech Stack

| Component | Choice | Rationale |
|-----------|--------|-----------|
| Language | C (core) + Python 3 (tooling) | Performance-critical server; Python for build tools |
| Build | Make | Simple, proven; native on Linux/macOS, cross-compiles to Win32 via MinGW |
| Data | JSON (machine-generated) | Ship/projectile registry, hash manifests |
| Networking | Raw UDP (Winsock / BSD sockets) | Wire-compatible with stock BC clients |

## Development Phases

- **Phase A: Hash Manifest Tool** -- Extract lookup tables, reimplement StringHash + FileHash, CLI manifest generator/verifier ✓
- **Phase B: Protocol Library** -- AlbyRules cipher, TGBufferStream codec, compressed type encoders ✓
- **Phase C: Lobby Server** -- UDP socket, peer management, checksum validation via manifests, settings delivery, chat relay, GameSpy LAN + master server ✓
- **Phase D: Relay Server** -- StateUpdate parsing/relay, weapon fire relay, object creation/destruction, ship data registry ✓
- **Phase E: Simulation Server** -- Ship data registry, movement, combat simulation (cloaking, tractor, repair), dynamic AI battle, hierarchical subsystem health, power simulation ✓

## Project Layout

```
src/
├── checksum/      # Hash algorithms, manifest validation
├── game/          # Ship data registry, ship state, movement, combat simulation
├── json/          # Lightweight JSON parser
├── network/       # UDP transport, peer management, reliability, GameSpy
├── protocol/      # Wire codec, opcodes, handshake, game events
└── server/        # Main entry point, config, logging
tools/             # CLI tools (hash manifest, data scraper, diagnostics)
data/              # Ship and projectile data (vanilla-1.1/)
manifests/         # Precomputed hash manifests (vanilla-1.1.json)
tests/             # 11 test suites (unit + integration)
docs/              # Design documents and protocol reference
```

## Agent Roster (14 agents in .claude/agents/)

### Tier 1 -- Core Architecture
- **openbc-architect** -- System architecture, cross-cutting design decisions
- **protocol-analyst** -- Behavioral protocol analysis from packet captures and observable wire behavior
- **design-intent** -- Design judgment calls based on observable game behavior and readable Python scripts
- **network-protocol** -- Protocol engineering (wire format, opcodes, reliability)

### Tier 2 -- Engine Subsystems
- **physics-sim** -- Ship dynamics, collision detection, damage model
- **nif-asset-pipeline** -- NIF file format, collision mesh extraction
- **build-ci** -- Build system, CI/CD, packaging, Docker

### Tier 3 -- Future Phases (Client)
- **bgfx-renderer** -- Rendering pipeline (Phase D+)
- **rmlui-specialist** -- UI reimplementation (Phase D+)
- **platform-integration** -- SDL3, miniaudio, cross-platform (Phase D+)

### Tier 4 -- Legacy / Reference
- **swig-api-compat** -- SWIG API reference (historical, informs data registry design)
- **flecs-ecs-architect** -- ECS design reference (historical)
- **python-migration** -- Python compatibility reference (historical)
- **mod-compat-tester** -- Community mod compatibility testing

## Implementation Gotchas

Hard-won bugs — do not repeat:

- **GCC -O2 dead-store elimination**: `i686-w64-mingw32-gcc -O2` silently drops `memcpy`/field-stores into structs after `memset()`. In `bc_peers_add()` this wiped the peer address. Fix: use a `volatile u8 *dst` byte-copy loop. Adding new fields to `bc_peer_t` can re-trigger this.
- **Win32 stack probing crash**: Large local arrays in functions that call into functions with large struct locals (e.g. `bc_checksum_resp_t` ~10KB) can skip the 4KB guard page → silent crash (exit code 5, no output). MinGW lacks `__chkstk`. Fix: avoid large stack arrays; write directly into output structs.
- **Type 0x00 keepalive**: The keepalive handler must only `continue` during player-name extraction, not for all type 0x00 messages — other 0x00 subtypes still need processing.
- **Never delete build/ log files**: The user stores server session logs in `build/` (e.g. `build/openbc-*.log`). Do NOT run `make clean` or `rm -rf build/` without explicit confirmation.

## Key Source Files

- Server entry: `src/server/main.c`
- Protocol codec/cipher: `src/protocol/{cipher,buffer,opcodes,handshake}.c`
- Game events & builders: `src/protocol/{game_events,game_builders}.c`
- Network: `src/network/{net,peer,transport,reliable,gamespy,master}.c`
- Client transport (test harness side): `src/network/client_transport.c`
- Checksum: `src/checksum/{string_hash,file_hash,hash_tables,manifest}.c`
- Game systems: `src/game/{ship_data,ship_state,movement,combat}.c`
- Logging: `src/server/log.c`
- Test harness helpers: `tests/test_util.h` (unit), `tests/test_harness.h` (integration)
- Ship/projectile data: `data/vanilla-1.1/`
- Data scraper: `tools/scrape_bc.py`

## Legal Basis

This project is a clean-room reimplementation of the BC multiplayer protocol for interoperability. No copyrighted code or assets are distributed. The server ships with zero original STBC content. Legal precedent: Oracle v. Google (2021), Sega v. Accolade (1992), EU Software Directive Article 6.
