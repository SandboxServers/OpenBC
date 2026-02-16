# OpenBC -- Open Bridge Commander

A clean-room, open-source reimplementation of Star Trek: Bridge Commander (2002).

Phase 1 server in active development. Stock BC 1.1 clients connect and play.

## What Is OpenBC?

OpenBC is a from-scratch reimplementation of Star Trek: Bridge Commander in portable C. The goal is a fully playable game -- server, client, and mod support -- built entirely from clean-room reverse engineering. No original source code is used.

This is not a wrapper, proxy, or compatibility layer around the original engine. OpenBC is a new implementation that speaks the same wire protocol. Stock BC 1.1 clients connect to an OpenBC server today, with no modifications needed. The future game client will load textures, sounds, and models from a legitimate BC install but run on a modern engine.

The original Bridge Commander embedded Python 1.5.2 with over 5,700 SWIG-generated API functions that controlled everything from menus to physics. OpenBC takes a different approach: all game functionality is reimplemented natively in C. There is no Python runtime and no script compatibility layer. Ship stats, weapons, maps, and game rules live in data files (TOML/JSON), moddable by default. Menus, UI, and game logic are reimplemented from scratch to match the original experience.

Zero copyrighted content is shipped. The game client will require a legitimate BC 1.1 installation for textures, sounds, and models. The dedicated server needs no game files at all -- it validates clients using precomputed hash manifests.

## Project Status

| Phase | Description | Status |
|-------|-------------|--------|
| **1. Dedicated Server** | | |
| A. Hash Manifest | Hash algorithms, manifest generator | Complete |
| B. Protocol Library | Wire cipher, codec, compressed types | Complete |
| C. Lobby Server | UDP transport, handshake, checksum, chat, GameSpy | Complete |
| D. Relay Server | Game events, combat relay, object lifecycle | Complete |
| E. Simulation Server | Server-authoritative physics, damage, game rules | Planned |
| **2. Game Client** | Rendering, audio, UI, input | Planning |

What works today:

- Stock BC 1.1 clients connect and play multiplayer matches
- Full handshake: GameSpy discovery, 4-round checksum validation, settings delivery
- Ship creation, weapons fire, damage, repairs, cloaking, warp -- all relayed
- Chat (global and team), lobby and in-game
- GameSpy LAN browser and internet master server registration
- Reliable delivery with retransmit and sequencing
- 7 test suites, including a 99-assertion integration test

## Quick Start

Prerequisites: `i686-w64-mingw32-gcc` cross-compiler, Make.

```
make all     # builds openbc-hash.exe and openbc-server.exe
make test    # runs all 7 test suites
```

Run the server:

```
./build/openbc-server.exe [options]

Options:
  -p <port>          Listen port (default: 22101)
  -n <name>          Server name (default: "OpenBC Server")
  -m <mode>          Game mode
  --system <n>       Star system index 1-9 (default: 1)
  --max <n>          Max players (default: 6)
  --time-limit <n>   Time limit in minutes
  --frag-limit <n>   Frag/kill limit
  --collision        Enable collision damage (default)
  --no-collision     Disable collision damage
  --friendly-fire    Enable friendly fire
  --no-friendly-fire Disable friendly fire (default)
  --manifest <path>  Hash manifest JSON (e.g. manifests/vanilla-1.1.json)
  --no-checksum      Accept all checksums (testing without game files)
  --master <h:p>     Master server address
  --log-level <lvl>  quiet|error|warn|info|debug|trace (default: info)
  --log-file <path>  Also write log to file
  -q / -v / -vv      Shorthand for quiet / debug / trace
```

Connect a stock BC 1.1 client by pointing it to the server IP via the LAN browser, or register with a master server using the `--master` flag.

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

UDP packets arrive, get deciphered (AlbyRules XOR), decoded (TGBufferStream wire format), and dispatched to the appropriate opcode handler. Game state is relayed to all connected peers. Hash manifests validate client file integrity without the server needing any game files.

```
src/
  checksum/    Hash algorithms, manifest validation
  network/     UDP transport, peer management, reliability, GameSpy
  protocol/    Wire codec, opcodes, handshake, game events
  json/        Lightweight JSON parser
  server/      Entry point, configuration, logging
tests/         7 test suites (unit + integration)
tools/         CLI tools (hash manifest generator, AHK test harness)
manifests/     Precomputed hash manifests (vanilla-1.1.json)
docs/          Design documents and protocol reference
```

## Design Principles

- **Protocol-first** -- Compatibility means speaking the wire protocol correctly. The stock client is the reference implementation; OpenBC matches its behavior exactly.
- **Zero original content** -- No copyrighted STBC material is shipped. The server validates clients using precomputed hash manifests, never needing the game files themselves.
- **Data-driven** -- Ship stats, weapons, maps, and game rules live in data files, not code. Change a ship's hull strength by editing a TOML file, not recompiling.
- **Mod-native** -- Every data layer is designed for extension. Mods are first-class data packs that overlay or replace base configuration.

## Tech Stack

**Server (current):**

| Component | Choice | Rationale |
|-----------|--------|-----------|
| Language | C11 | Performance, portability, no runtime dependencies |
| Build | Make | Simple, proven, cross-compiles from WSL2 to Win32 |
| Networking | Raw UDP (Winsock) | Wire-compatible with stock BC clients |
| Config | TOML (human-edited), JSON (machine-generated) | Readable config, structured manifests |
| Discovery | GameSpy protocol | LAN browser + 333networks-compatible master server |

**Client (planned):** The Phase 2 game client will use a modern rendering engine, ECS architecture, and cross-platform audio/input. Technology choices are under evaluation.

## Reverse Engineering

All critical protocol reverse engineering is complete, verified against the stock dedicated server with 30,000+ captured packets:

- Wire protocol fully reversed: AlbyRules XOR cipher, TGBufferStream codec, three-tier reliability
- 28 game opcodes identified and documented from the dispatch jump table
- Hash algorithms reimplemented: StringHash (4-lane Pearson) and FileHash (rotate-XOR)
- Full handshake sequence traced and reimplemented (GameSpy peek, checksum exchange, settings delivery)
- Damage pipeline traced: collision, weapon, and explosion paths through subsystem distribution
- All findings from Ghidra decompilation and packet analysis; no original source code was used

See the [verified protocol reference](docs/phase1-verified-protocol.md) for the complete wire format specification. The [STBC-Dedicated-Server](https://github.com/cadacious/STBC-Dedicated-Server) repository contains the RE workspace and a functional DDraw proxy server used for protocol verification.

## Game Data and Mods

- Ship definitions, maps, and game rules are stored in TOML data files
- The hash manifest system validates any mod combination without needing the actual files
- `openbc-hash` generates manifests from a BC install or mod pack directory
- Mods are designed as data packs that overlay base configuration
- Data registry and mod loader are planned for Phase E

## Documentation

- [Server Architecture RFC](docs/phase1-implementation-plan.md) -- Standalone server design: network, checksum, game state, physics, data registry, mod system
- [Requirements](docs/phase1-requirements.md) -- Functional and non-functional requirements
- [Verified Protocol](docs/phase1-verified-protocol.md) -- Complete wire protocol: opcodes, packet formats, handshake, reliable delivery, compressed types
- [Engine Architecture](docs/phase1-engine-architecture.md) -- Original BC engine internals (RE reference)
- [RE Gap Analysis](docs/phase1-re-gaps.md) -- Reverse engineering status (all critical items solved)
- [Data Registry](docs/phase1-api-surface.md) -- Ship, map, rules, and manifest data schemas

## Contributing

Contributions are welcome. The most useful things right now:

- **Testing**: Connect stock BC 1.1 clients, report connection issues or protocol mismatches
- **Reverse engineering**: Ghidra analysis, packet captures, behavioral documentation
- **Data files**: Ship stats, map definitions, game rule sets
- **Code review**: Protocol correctness, edge cases, platform compatibility

Open an issue to discuss before starting large changes.

## Related Projects

- [STBC-Dedicated-Server](https://github.com/cadacious/STBC-Dedicated-Server) -- Functional DDraw proxy dedicated server, RE workspace, decompiled reference code, and Ghidra annotation tools. RE findings feed directly into OpenBC.

## Legal Basis

OpenBC is a clean-room reimplementation of the Bridge Commander multiplayer protocol, created for interoperability. No copyrighted code or assets are distributed. The server ships with zero original STBC content.

Legal precedent for clean-room reimplementation: *Oracle America v. Google* (2021, U.S. Supreme Court), *Sega v. Accolade* (1992, 9th Circuit), EU Software Directive Article 6.

Star Trek, Bridge Commander, and related marks are trademarks of CBS Studios and Paramount Global. This project is not affiliated with or endorsed by the trademark holders.

## License

TBD -- License will be selected before the first tagged release.
