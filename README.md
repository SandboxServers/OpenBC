# OpenBC -- Open Bridge Commander

A clean-room, open-source reimplementation of Star Trek: Bridge Commander (2002).

Phase 1 server in active development. Stock BC 1.1 clients connect and play.

## What Is OpenBC?

OpenBC is a from-scratch reimplementation of Star Trek: Bridge Commander in portable C. The goal is a fully playable game -- server, client, and mod support -- built entirely from clean-room reverse engineering. No original source code is used.

This is not a wrapper, proxy, or compatibility layer around the original engine. OpenBC is a new implementation that speaks the same wire protocol. Stock BC 1.1 clients connect to an OpenBC server today, with no modifications needed. The future game client will load textures, sounds, and models from a legitimate BC install but run on a modern engine.

The original Bridge Commander embedded Python 1.5.2 with over 5,700 SWIG-generated API functions that controlled everything from menus to physics. OpenBC takes a different approach: all game functionality is reimplemented natively in C. There is no Python runtime and no script compatibility layer. Ship stats, weapons, maps, and game rules live in JSON data files, moddable by default. Menus, UI, and game logic are reimplemented from scratch to match the original experience.

Zero copyrighted content is shipped. The game client will require a legitimate BC 1.1 installation for textures, sounds, and models. The dedicated server needs no game files at all -- it validates clients using precomputed hash manifests.

## Project Status

| Phase | Description | Status |
|-------|-------------|--------|
| **1. Dedicated Server** | | |
| A. Hash Manifest | Hash algorithms, manifest generator | Complete |
| B. Protocol Library | Wire cipher, codec, compressed types | Complete |
| C. Lobby Server | UDP transport, handshake, checksum, chat, GameSpy | Complete |
| D. Relay Server | Game events, combat relay, object lifecycle | Complete |
| E. Simulation Server | Ship data registry, movement, combat simulation | Complete |
| **2. Game Client** | Rendering, audio, UI, input | Planning |

What works today:

- Stock BC 1.1 clients connect and play multiplayer matches
- Full handshake: GameSpy discovery, 4-round checksum validation, settings delivery
- Ship creation, weapons fire, damage, repairs, cloaking, warp -- all relayed
- Server-authoritative damage: collision, weapon, and explosion pipelines
- Chat (global and team), lobby and in-game
- GameSpy LAN browser and internet master server registration
- Reliable delivery with retransmit and sequencing
- Ship data registry: 16 ships, 15 projectile types loaded from JSON
- Server-side movement and combat simulation (cloaking, tractor beams, repair)
- Dynamic AI battles with seeded RNG
- Manifest auto-detection, session summary at shutdown, graceful shutdown handling
- 11 test suites, 226 tests, 1,184 assertions (including networked battle integration)

**Important caveat:** "Complete" means the server-side logic is implemented and tested against our own test harness. It does not mean every feature has been validated end-to-end with a stock BC client. The wire protocol for connection, checksums, lobby, chat, relay, and basic gameplay is proven against real clients. Server-authoritative features like damage simulation generate correct wire-format packets, but whether the stock client accepts and renders all of them is still being verified.

## Quick Start

Prerequisites: `i686-w64-mingw32-gcc` cross-compiler, Make.

```
make all     # builds openbc-hash.exe and openbc-server.exe
make test    # runs all 11 test suites
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
  --master <h:p>     Master server address (repeatable; replaces defaults)
  --no-master        Disable all master server heartbeating
  --log-level <lvl>  quiet|error|warn|info|debug|trace (default: info)
  --log-file <path>  Also write log to file
  -q / -v / -vv      Shorthand for quiet / debug / trace
```

Connect a stock BC 1.1 client by pointing it to the server IP via the LAN browser, or register with a master server using the `--master` flag (repeatable to specify multiple masters; `--no-master` disables heartbeating entirely).

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

UDP packets arrive, get deciphered (AlbyRules XOR), decoded (TGBufferStream wire format), and dispatched to the appropriate opcode handler. Game state is relayed to all connected peers. Hash manifests validate client file integrity without the server needing any game files.

```
src/
  checksum/    Hash algorithms, manifest validation
  game/        Ship data registry, ship state, movement, combat simulation
  json/        Lightweight JSON parser
  network/     UDP transport, peer management, reliability, GameSpy
  protocol/    Wire codec, opcodes, handshake, game events
  server/      Entry point, configuration, logging
tests/         11 test suites (unit + integration)
tools/         CLI tools (hash manifest generator, data scraper, diagnostics)
data/          Ship and projectile data (vanilla-1.1.json)
manifests/     Precomputed hash manifests (vanilla-1.1.json)
docs/          Design documents and protocol reference
```

## Design Principles

- **Protocol-first** -- Compatibility means speaking the wire protocol correctly. The stock client is the reference implementation; OpenBC matches its behavior exactly.
- **Zero original content** -- No copyrighted STBC material is shipped. The server validates clients using precomputed hash manifests, never needing the game files themselves.
- **Data-driven** -- Ship stats, weapons, maps, and game rules live in JSON data files, not code. Change a ship's hull strength by editing a data file, not recompiling.
- **Mod-native** -- Every data layer is designed for extension. Mods are first-class data packs that overlay or replace base configuration.

## Tech Stack

**Server (current):**

| Component | Choice | Rationale |
|-----------|--------|-----------|
| Language | C11 | Performance, portability, no runtime dependencies |
| Build | Make | Simple, proven, cross-compiles from WSL2 to Win32 |
| Networking | Raw UDP (Winsock) | Wire-compatible with stock BC clients |
| Data | JSON (machine-generated) | Ship/projectile registry, hash manifests |
| Discovery | GameSpy protocol | LAN browser + 333networks-compatible master server |

**Client (planned):** The Phase 2 game client will use a modern rendering engine, ECS architecture, and cross-platform audio/input. Technology choices are under evaluation.

## Protocol Analysis

All critical protocol analysis is complete, verified against the stock dedicated server with 30,000+ captured packets:

- Wire protocol fully documented: AlbyRules stream cipher, TGBufferStream codec, three-tier reliability
- 28 game opcodes identified and documented
- Hash algorithms reimplemented: StringHash (4-lane Pearson) and FileHash (rotate-XOR)
- Full handshake sequence traced and reimplemented (GameSpy peek, checksum exchange, settings delivery)
- Damage pipeline documented: collision, weapon, and explosion paths through subsystem distribution
- All findings from packet captures, behavioral observation, and readable game scripts; no original source code was used

See the [verified protocol reference](docs/phase1-verified-protocol.md) for the complete wire format specification. The [STBC-Dedicated-Server](https://github.com/cadacious/STBC-Dedicated-Server) repository contains a functional DDraw proxy server used for protocol verification and packet capture.

## Game Data and Mods

- Ship definitions (16 ships) and projectile data (15 types) are stored in `data/vanilla-1.1.json`, extracted from BC reference scripts by `tools/scrape_bc.py`
- The ship data registry loads this JSON at server startup, providing hull strength, shield capacity, subsystem counts, weapon hardpoints, and projectile properties
- The hash manifest system validates any mod combination without needing the actual files
- `openbc-hash` generates manifests from a BC install or mod pack directory
- Mods are designed as data packs that overlay base configuration

## Documentation

**Design & Architecture:**
- [Server Architecture RFC](docs/phase1-implementation-plan.md) -- Standalone server design: network, checksum, game state, physics, data registry, mod system
- [Requirements](docs/phase1-requirements.md) -- Functional and non-functional requirements
- [Engine Architecture](docs/phase1-engine-architecture.md) -- Original BC engine architecture (behavioral reference)
- [Data Registry](docs/phase1-api-surface.md) -- Ship, map, rules, and manifest data schemas
- [Server Authority](docs/server-authority.md) -- Authority model: what the server computes vs. relays

**Protocol & Wire Format:**
- [Verified Protocol](docs/phase1-verified-protocol.md) -- Complete wire protocol: opcodes, packet formats, handshake, reliable delivery, compressed types
- [Transport Cipher](docs/transport-cipher.md) -- AlbyRules PRNG cipher: key schedule, cross-multiplication, plaintext feedback
- [GameSpy Protocol](docs/gamespy-protocol.md) -- LAN discovery, master server heartbeat, challenge-response
- [Join Flow](docs/join-flow.md) -- Connection lifecycle: connect, checksums, lobby, gameplay
- [Checksum Handshake](docs/checksum-handshake-protocol.md) -- Hash algorithms, 5-round checksum exchange
- [Disconnect Flow](docs/disconnect-flow.md) -- Player disconnect detection and cleanup
- [Wire Format Audit](docs/wire-format-audit.md) -- Audit of wire format implementation vs spec
- Wire format specs: [ObjCreate](docs/objcreate-wire-format.md), [StateUpdate](docs/stateupdate-wire-format.md), [CollisionEffect](docs/collision-effect-wire-format.md)

**Game Systems:**
- [Combat System](docs/combat-system.md) -- Damage pipeline, shields, cloaking, tractor beams, repair
- [Ship Subsystems](docs/ship-subsystems.md) -- Fixed subsystem index table, HP values, StateUpdate serialization

**Testing & Tools:**
- [Test Suite](tests/README.md) -- 11 test suites, test frameworks, adding new tests
- [Tools](tools/README.md) -- CLI tools, data scraper, diagnostic utilities

## Contributing

Contributions are welcome. The most useful things right now:

- **Testing**: Connect stock BC 1.1 clients, report connection issues or protocol mismatches
- **Protocol analysis**: Packet captures, behavioral observation, documentation
- **Data files**: Ship stats, map definitions, game rule sets
- **Code review**: Protocol correctness, edge cases, platform compatibility

Open an issue to discuss before starting large changes.

## Related Projects

- [STBC-Dedicated-Server](https://github.com/cadacious/STBC-Dedicated-Server) -- Functional DDraw proxy dedicated server used for packet capture and protocol verification. Behavioral observations feed directly into OpenBC's clean-room documentation.

## Legal Basis

OpenBC is a clean-room reimplementation of the Bridge Commander multiplayer protocol, created for interoperability. No copyrighted code or assets are distributed. The server ships with zero original STBC content.

Legal precedent for clean-room reimplementation: *Oracle America v. Google* (2021, U.S. Supreme Court), *Sega v. Accolade* (1992, 9th Circuit), EU Software Directive Article 6.

Star Trek, Bridge Commander, and related marks are trademarks of CBS Studios and Paramount Global. This project is not affiliated with or endorsed by the trademark holders.

## License

TBD -- License will be selected before the first tagged release.
