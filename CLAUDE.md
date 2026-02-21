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

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    OpenBC Server                         │
│                                                          │
│  ┌─────────┐  ┌──────────┐  ┌─────────┐  ┌───────────┐  │
│  │ Network  │  │ Checksum │  │  Game   │  │   Mod     │  │
│  │  Layer   │──│ Validator│──│  State  │──│  Loader   │  │
│  │ (UDP)    │  │(Manifest)│  │ Engine  │  │           │  │
│  └────┬─────┘  └──────────┘  └────┬────┘  └─────┬─────┘  │
│       │                          │              │         │
│  ┌────┴─────┐  ┌──────────┐  ┌───┴────┐  ┌─────┴─────┐  │
│  │ Protocol │  │  Opcode  │  │Physics │  │   Data    │  │
│  │  Codec   │  │ Handlers │  │  Sim   │  │ Registry  │  │
│  │(wire fmt)│  │(28 actv) │  │        │  │(ships,etc)│  │
│  └──────────┘  └──────────┘  └────────┘  └───────────┘  │
└──────────────────────────────────────────────────────────┘
        ▲                                        ▲
        │ UDP packets                            │ JSON
        ▼                                        │
  ┌───────────┐                          ┌───────────────┐
  │ Stock BC  │                          │  Mod Packs    │
  │  Client   │                          │ (data + hash  │
  │  (1.1)    │                          │  manifests)   │
  └───────────┘                          └───────────────┘
```

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
data/              # Ship and projectile data (vanilla-1.1.json)
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

## Planning Documents

- **[Server Architecture RFC](docs/phase1-implementation-plan.md)** -- Standalone server design: network, checksum, game state, physics, data registry, mod system
- **[Requirements](docs/phase1-requirements.md)** -- Functional/non-functional requirements
- **[Verified Protocol](docs/phase1-verified-protocol.md)** -- Complete wire protocol: opcodes, packet formats, handshake, reliable delivery
- **[Engine Architecture](docs/phase1-engine-architecture.md)** -- Protocol architecture that OpenBC must replicate (behavioral reference)
- **[Data Registry](docs/phase1-api-surface.md)** -- Ship, map, rules, and manifest data schemas

## Protocol & System Documentation

- **[Transport Cipher](docs/transport-cipher.md)** -- AlbyRules PRNG cipher: key schedule, cross-multiplication, plaintext feedback
- **[GameSpy Protocol](docs/gamespy-protocol.md)** -- LAN discovery, master server heartbeat, challenge-response crypto (gsmsalg)
- **[Join Flow](docs/join-flow.md)** -- Connection lifecycle: connect → checksums → lobby → gameplay
- **[Combat System](docs/combat-system.md)** -- Damage pipeline, shields, cloaking, tractor beams, repair system
- **[Ship Subsystems](docs/ship-subsystems.md)** -- Fixed subsystem index table, HP values, StateUpdate serialization
- **[Checksum Handshake](docs/checksum-handshake-protocol.md)** -- Hash algorithms, 5-round checksum exchange
- **[Disconnect Flow](docs/disconnect-flow.md)** -- Player disconnect detection and cleanup
- **[Server Authority](docs/server-authority.md)** -- Authority model (who computes what)
- **[Wire Format Audit](docs/wire-format-audit.md)** -- Audit of wire format implementation vs spec

## Verified Protocol Facts

All critical protocol knowledge is documented in the clean room docs. Key verified facts:

- **Wire protocol**: AlbyRules PRNG cipher ("AlbyRules!" 10-byte key), raw UDP, three-tier send queues (unreliable/reliable/priority)
- **Hash algorithms**: StringHash (4-lane Pearson) for filenames, FileHash (rotate-XOR) for file contents
- **Game opcodes**: 28 active opcodes (see `phase1-verified-protocol.md` Section 5)
- **Script messages**: MAX_MESSAGE_TYPES = 0x2B; CHAT=0x2C, TEAM_CHAT=0x2D, MISSION_INIT=0x35, SCORE_CHANGE=0x36, SCORE=0x37, END_GAME=0x38, RESTART=0x39
- **Ship creation**: ObjectCreateTeam (0x03), destruction: DestroyObject (0x14)
- **Handshake**: connect -> GameSpy peek -> 4 checksum rounds -> Settings (0x00) -> GameInit (0x01) -> EnterSet (0x1F) -> NewPlayerInGame (0x2A)
- **AlbyRules cipher**: Stream cipher with PRNG-derived keystream + plaintext feedback. Byte 0 (direction flag) is NEVER encrypted. Per-packet reset. PRNG: LCG cross-multiplication (mult=0x4E35, add=0x15A), 5 rounds per key schedule. API: `alby_cipher_encrypt()` / `alby_cipher_decrypt()`
- **CF16 codec**: encode uses `(value - lo) / (hi - lo) * 4096`; decode uses `lo + (mantissa / 4095.0f) * (hi - lo)`. The 4096/4095 asymmetry is intentional.
- **Connect handshake**: Server responds to Connect(0x03) with Connect(0x03), NOT ConnectAck(0x05). Format: `[0x03][0x06][0xC0][0x00][0x00][slot]`. ConnectAck(0x05) means disconnect/shutdown only.
- **Player slots**: BC_MAX_PLAYERS=7 (slot 0=Dedicated Server, slots 1-6=humans). Wire slot = array index + 1. Direction byte = wire slot. All loops skip slot 0. GameSpy numplayers = count - 1.
- **NewPlayerInGame (0x2A)**: 2 bytes `[0x2A][0x20]` (trailing space, not a length byte)
- **Transport type 0x32**: Dual-use -- reliable (flags & 0x80, 5-byte header) AND unreliable (flags==0x00, 3-byte header). Parser must bifurcate; only ACK reliable messages.
- **Checksum file tree format**: `[file_count:u16][files…][subdir_count:u8][all_name_hashes…][all_trees…]`. subdir_count is u8 (NOT u16), names-first-then-trees (NOT interleaved). Always present even when zero. dir_hash uses leaf directory name only (no trailing separator).
- **GameSpy**: gamename="bcommander", secret key="Nm3aZ9", LAN query port=6500, master heartbeat port=27900. Server listens on two sockets (game port + query port).
- **Reliable sequencing**: internal counter increments by 1; wire format `[seqHi=counter][seqLo=0]` → wire value increments by 256. ACK byte = seqHi.
- **UDP batching**: One UDP packet can contain multiple transport messages. Consumers must iterate all (see `cached_pkt` pattern in `tests/test_harness.h`).

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
- Ship/projectile data: `data/vanilla-1.1.json`
- Data scraper: `tools/scrape_bc.py`

## Legal Basis

This project is a clean-room reimplementation of the BC multiplayer protocol for interoperability. No copyrighted code or assets are distributed. The server ships with zero original STBC content. Legal precedent: Oracle v. Google (2021), Sega v. Accolade (1992), EU Software Directive Article 6.
