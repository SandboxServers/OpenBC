# OpenBC -- Open Bridge Commander

> **This project is in early planning stages. No code has been written yet. Everything described below represents the design goals and intended architecture, not current functionality.**

Open-source, standalone multiplayer server for Star Trek: Bridge Commander (2002). Clean-room reimplementation that speaks the BC 1.1 wire protocol -- stock clients connect and play without modification. Ships with zero copyrighted content: all game data is provided via hash manifests and data registries.

## Project Status: Planning

**Nothing is implemented.** The repository currently contains design documents and specifications produced through extensive reverse engineering of the original game.

What exists today:
- Design documents in `docs/` describing the server architecture and implementation plan
- Reverse engineering reference data (verified wire protocol, opcodes, data structures)
- AI agent configurations in `.claude/` used for analysis and planning
- This README

What does not exist yet:
- Any C source code
- Build system
- A working server or client
- Tests

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
```

## Design Principles

- **Protocol-first**: Compatibility means speaking the wire protocol correctly
- **Zero original content**: No copyrighted STBC material shipped. Game data referenced via hash manifests
- **Data-driven**: Ship stats, weapons, maps, and game rules live in TOML data files, not code
- **Mod-native**: Every layer exposes extension points. Mods are first-class data packs

## How It Works (Planned)

The original Bridge Commander uses a proprietary multiplayer protocol over raw UDP. OpenBC reimplements this protocol from scratch based on extensive reverse engineering of the original binary (30,000+ captured packets, full Ghidra decompilation).

Instead of running BC's original Python scripts, OpenBC takes a data-driven approach:
- **Ship definitions** (hull strength, weapons, subsystems, turn rates) come from `ships.toml`
- **Map definitions** (spawn points, system layout) come from `maps.toml`
- **Game rules** (frag limits, time limits, respawn) come from `rules.toml`
- **File verification** uses precomputed hash manifests (JSON) so the server never needs game files

Stock BC 1.1 clients connect to an OpenBC server just like they would to an original server. The server speaks the same wire protocol, validates the same checksums, and manages the same game lifecycle (lobby -> ship select -> play -> game over -> restart).

## Planned Tech Stack

| Component | Choice | Notes |
|-----------|--------|-------|
| Language | C (core) + Python 3 (tooling) | C for performance; Python for build/hash tools |
| Build | Make | Simple, cross-compiles from WSL2 |
| Config format | TOML (config), JSON (manifests) | Human-readable + machine-friendly |
| Physics | Custom (Euler + mesh-accurate collision) | Convex hulls from NIF geometry |
| Networking | Raw UDP (Winsock) | Wire-compatible with stock clients |
| Internet discovery | GameSpy protocol (333networks compatible) | LAN + master server support |

## Planned Development Phases

None of these phases have started.

- **Phase A: Hash Manifest Tool** -- Reimplement StringHash + FileHash algorithms, CLI tool to generate/verify hash manifests from a BC game installation
- **Phase B: Protocol Library** -- Clean-room AlbyRules cipher, TGBufferStream codec, compressed type encoders/decoders
- **Phase C: Lobby Server** -- UDP transport, peer management, checksum validation via manifests, settings delivery, chat relay, GameSpy discovery (LAN + master server)
- **Phase D: Relay Server** -- StateUpdate parsing and relay, weapon fire relay, object creation/destruction, ship data registry
- **Phase E: Simulation Server** -- Server-authoritative physics, mesh-accurate collision, damage system, subsystem tracking, game rules engine

## Planned Project Layout

This directory structure is planned but does not exist yet.

```
src/
├── network/       # UDP transport, peer management, reliability
│   └── legacy/    # AlbyRules cipher, wire format, GameSpy
├── protocol/      # Opcode handlers, message codec
├── checksum/      # Hash algorithms, manifest validation
├── game/          # Game state, object model, lifecycle FSM
├── physics/       # Movement, collision detection, damage
├── data/          # Registry loaders (ships, maps, rules)
├── mod/           # Mod pack loader, overlay system
└── server/        # Main entry point, config, console
tools/             # CLI tools (hash manifest generator, NIF collision extractor)
data/              # Default data files (ships.toml, maps.toml, rules.toml)
manifests/         # Precomputed hash manifests
tests/             # Test suite
docker/            # Server container files
docs/              # Design and reference documents (exists now)
```

## Design Documents

- **[Server Architecture RFC](docs/phase1-implementation-plan.md)** -- Complete standalone server design: network, checksum, game state, physics, data registry, mod system
- **[Requirements](docs/phase1-requirements.md)** -- Functional and non-functional requirements
- **[Verified Protocol](docs/phase1-verified-protocol.md)** -- Complete wire protocol reference: opcodes, packet formats, handshake, reliable delivery, compressed types
- **[Engine Architecture](docs/phase1-engine-architecture.md)** -- Original BC engine internals (reverse engineering reference)
- **[RE Gap Analysis](docs/phase1-re-gaps.md)** -- Reverse engineering status: all critical protocol items fully reversed
- **[Data Registry](docs/phase1-api-surface.md)** -- Ship, map, rules, and manifest data schemas

## Key Reverse Engineering Facts

All critical reverse engineering is complete (verified against the stock dedicated server with 30,000+ packet captures):

- **Wire protocol**: AlbyRules XOR cipher ("AlbyRules!" 10-byte key), raw UDP, three-tier send queues
- **Hash algorithms**: StringHash (4-lane Pearson) for filenames, FileHash (rotate-XOR, skips .pyc timestamps) for content
- **Game opcodes**: 28 active opcodes from jump table at 0x0069F534
- **Checksum exchange**: 4-round sequential verification (scripts/App.pyc, Autoexec.pyc, ships/*.pyc, mainmenu/*.pyc)
- **Handshake**: connect -> peek demux -> checksum -> Settings (0x00) -> GameInit (0x01) -> NewPlayerInGame (0x2A)
- **Damage system**: DoDamage pipeline fully traced (collision, weapon, explosion paths)

## Related Repository

- **[STBC-Dedicated-Server](https://github.com/cadacious/STBC-Dedicated-Server)** -- Functional DDraw proxy dedicated server and reverse engineering workspace. Contains a working headless server, decompiled reference code, protocol documentation, and Ghidra annotation tools. RE findings from this project feed directly into OpenBC's design.

## Legal Basis

This project is a clean-room reimplementation of the BC multiplayer protocol for interoperability. No copyrighted code or assets are distributed. The server ships with zero original STBC content. Legal precedent: Oracle v. Google (2021), Sega v. Accolade (1992), EU Software Directive Article 6.

## Contributing

This project is not yet ready for contributions. The design is still being finalized. Watch this repository for updates.

## License

TBD -- License will be selected before any code is written.
