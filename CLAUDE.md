# OpenBC -- Open Bridge Commander

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
        │ UDP packets                            │ TOML/JSON
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
- **Data-driven**: Ship stats, weapons, maps, and game rules live in TOML data files, not code.
- **Mod-native**: Every layer exposes extension points. Mods are first-class data packs.

## Tech Stack

| Component | Choice | Rationale |
|-----------|--------|-----------|
| Language | C (core) + Python 3 (tooling) | Performance-critical server; Python for build tools |
| Build | Make | Simple, proven, cross-compiles from WSL2 |
| Config | TOML (human-edited), JSON (machine-generated) | Readable config, structured manifests |
| Physics | Custom (Euler + mesh-accurate collision) | Matches original BC fidelity |
| Networking | Raw UDP (Winsock) | Wire-compatible with stock BC clients |
| Collision meshes | NIF parser (build-time tool) | Convex hull extraction from ship models |

## Development Phases

- **Phase A: Hash Manifest Tool** -- Extract lookup tables, reimplement StringHash + FileHash, CLI manifest generator/verifier
- **Phase B: Protocol Library** -- AlbyRules cipher, TGBufferStream codec, compressed type encoders
- **Phase C: Lobby Server** -- UDP socket, peer management, checksum validation via manifests, settings delivery, chat relay, GameSpy LAN + master server
- **Phase D: Relay Server** -- StateUpdate parsing/relay, weapon fire relay, object creation/destruction, ship data registry
- **Phase E: Simulation Server** -- Server-authoritative physics, damage system, subsystem tracking, game rules engine

## Project Layout

```
src/
├── network/       # UDP transport, peer management, reliability
│   └── legacy/    # AlbyRules cipher, wire format, GameSpy
├── protocol/      # Opcode handlers, message codec, TGBufferStream
├── checksum/      # Hash algorithms, manifest validation
├── game/          # Game state, object model, lifecycle FSM
├── physics/       # Movement, collision detection, damage
├── data/          # Registry loaders (ships, maps, rules)
├── mod/           # Mod pack loader, overlay system
└── server/        # Main entry point, config, console
tools/             # CLI tools (hash manifest generator, NIF collision extractor)
data/              # Default data files (ships.toml, maps.toml, rules.toml)
manifests/         # Precomputed hash manifests (vanilla-1.1.json, etc.)
tests/             # Test suite
docker/            # Server container files
docs/              # Design and reference documents
```

## Agent Roster (14 agents in .claude/agents/)

### Tier 1 -- Core Architecture
- **openbc-architect** -- System architecture, cross-cutting design decisions
- **game-reverse-engineer** -- Decompiled code analysis, RE findings
- **stbc-original-dev** -- Original developer intent, design judgment calls
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
- **[Engine Architecture](docs/phase1-engine-architecture.md)** -- Original BC engine internals (reference for RE data)
- **[RE Gap Analysis](docs/phase1-re-gaps.md)** -- Reverse engineering status (all critical items solved)
- **[Data Registry](docs/phase1-api-surface.md)** -- Ship, map, rules, and manifest data schemas

## Key RE Data (verified, from STBC-Dedi)

All critical reverse engineering is complete. Key verified facts:

- **Wire protocol**: AlbyRules XOR cipher ("AlbyRules!" 10-byte key), raw UDP, three-tier send queues (unreliable/reliable/priority)
- **Hash algorithms**: StringHash (4-lane Pearson, FUN_007202e0) for filenames, FileHash (rotate-XOR, FUN_006a62f0) for file contents
- **Game opcodes**: 28 active opcodes verified from jump table at 0x0069F534 (see `phase1-verified-protocol.md` Section 5)
- **Script messages**: MAX_MESSAGE_TYPES = 0x2B; CHAT=0x2C, TEAM_CHAT=0x2D, MISSION_INIT=0x35, SCORE_CHANGE=0x36, SCORE=0x37, END_GAME=0x38, RESTART=0x39
- **Flag bytes**: 0x0097FA88=IsClient, 0x0097FA89=IsHost, 0x0097FA8A=IsMultiplayer
- **Ship creation**: ObjectCreateTeam (0x03), destruction: DestroyObject (0x14)
- **Handshake**: connect -> GameSpy peek -> 4 checksum rounds -> Settings (0x00) -> GameInit (0x01) -> EnterSet (0x1F) -> NewPlayerInGame (0x2A)
- **Collision**: DoDamage (0x00594020), subsystem distribution via handler array at ship+0x128

## Related Repository

- **[STBC-Dedicated-Server](../STBC-Dedicated-Server/)** -- Functional DDraw proxy dedicated server and RE workspace. Contains a working headless server (collision damage, scoring, full game lifecycle), decompiled reference code, protocol docs, and Ghidra annotation tools. RE findings feed directly into OpenBC's design.

## Legal Basis

This project is a clean-room reimplementation of the BC multiplayer protocol for interoperability. No copyrighted code or assets are distributed. The server ships with zero original STBC content. Legal precedent: Oracle v. Google (2021), Sega v. Accolade (1992), EU Software Directive Article 6.
