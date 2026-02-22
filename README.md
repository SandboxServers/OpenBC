# OpenBC -- Open Bridge Commander

A clean-room, open-source reimplementation of Star Trek: Bridge Commander (2002).

Phase 1 server complete. Stock BC 1.1 clients connect and play.

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

### Validated in stock BC 1.1 client sessions

Validation in progress -- features get promoted here as they're confirmed working in real multiplayer sessions.

### Implemented and tested in server harness

All of these features work in our test harness and generate correct wire-format packets. End-to-end validation with stock clients is ongoing.

- Full handshake: GameSpy discovery, 4-round checksum validation, settings delivery
- Ship creation, weapons fire, damage, repairs, cloaking, warp -- all relayed
- Server-authoritative damage: collision, beam, torpedo, and explosion pipelines
- Collision damage with dual scaling paths, dead zone, ownership validation, and deduplication
- Power system: reactor/battery/conduit simulation, 10Hz subsystem health broadcast, sign-bit encoding
- PythonEvent generation: subsystem damage/repair events, ship explosion events, score tracking
- Respawn system with configurable timer and frag/time limit win conditions
- Chat (global and team), lobby and in-game
- GameSpy LAN browser and internet master server registration
- Reliable delivery with retransmit, sequencing, and fragment reassembly
- Dynamic AI battles with seeded RNG
- Manifest auto-detection, session summary at shutdown, graceful shutdown handling

### Project facts

- Ship data registry: 16 ships, 15 projectile types loaded from JSON
- Cross-platform: builds natively on Linux and macOS, cross-compiles to Win32 via MinGW
- 11 test suites, 226 tests, 1,184 assertions

## Quick Start

**Linux / macOS (native):**

```
make all     # builds openbc-hash and openbc-server
make test    # runs all 11 test suites
./build/openbc-server [options]
```

**Windows (cross-compile from WSL2 or Linux):**

```
make all PLATFORM=Windows   # builds openbc-hash.exe and openbc-server.exe
make test PLATFORM=Windows  # runs all 11 test suites (WSL2 runs .exe natively)
```

Prerequisites: a C11 compiler (`cc` on Linux/macOS, `i686-w64-mingw32-gcc` for Windows cross-compile), and Make.

Run the server:

```
./build/openbc-server [options]

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

UDP packets arrive, get deciphered (AlbyRules stream cipher), decoded (TGBufferStream wire format), and dispatched to the appropriate opcode handler. Game state is relayed to all connected peers. Hash manifests validate client file integrity without the server needing any game files.

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
data/          Ship and projectile data (vanilla-1.1/)
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
| Build | Make | Simple, proven; native on Linux/macOS, cross-compiles to Win32 via MinGW |
| Networking | Raw UDP (Winsock / BSD sockets) | Wire-compatible with stock BC clients; builds natively on all platforms |
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

See the [verified protocol reference](docs/protocol/protocol-reference.md) for the complete wire format specification. The [STBC-Dedicated-Server](https://github.com/cadacious/STBC-Dedicated-Server) repository contains a functional DDraw proxy server used for protocol verification and packet capture.

## Game Data and Mods

The original Bridge Commander had a thriving mod community that pushed the game far beyond its original scope -- new ships, new UI panels, entirely new game modes. Those mods succeeded despite the engine, not because of it, fighting their way in through undocumented Python hooks and fragile monkey-patching. OpenBC is designed so that none of that is necessary. Every layer exposes clean extensibility points:

- **Data packs** -- Ship stats, weapons, projectiles, maps, and game rules are JSON. Add a ship by adding a file, not by hacking a script.
- **Custom opcodes** -- The network layer supports registering new message types. Mods that need custom client-server communication get a proper channel for it.
- **UI and menus** *(Phase 2)* -- The planned client UI will be built on a modular document system. Mods will be able to add new panels, replace existing screens, or reskin the entire interface.
- **Game logic hooks** *(Phase 2)* -- Planned extensibility points for game rules, damage formulas, victory conditions, and event handling.
- **Hash manifests** -- The manifest system validates any mod combination without the server needing the actual files. `openbc-hash` generates manifests from a BC install or mod pack directory.

The base game data -- 16 ships and 15 projectile types -- ships in `data/vanilla-1.1/`, extracted from BC's readable scripts by `tools/scrape_bc.py`. The goal is that any mod that existed for Bridge Commander can reimplement itself in OpenBC through supported extension points, without having to pick up a crowbar.

## Documentation

**Design & Architecture:**
- [Server Architecture RFC](docs/architecture/server-architecture.md) -- Standalone server design: network, checksum, game state, physics, data registry, mod system
- [Engine Architecture](docs/architecture/engine-reference.md) -- Original BC engine architecture (behavioral reference)
- [Data Registry](docs/game-systems/data-registry.md) -- Ship, map, rules, and manifest data schemas
- [Server Authority](docs/architecture/server-authority.md) -- Authority model: what the server computes vs. relays

**Protocol & Wire Format:**
- [Verified Protocol](docs/protocol/protocol-reference.md) -- Complete wire protocol: opcodes, packet formats, handshake, reliable delivery, compressed types
- [Transport Layer](docs/protocol/transport-layer.md) -- UDP transport: packet framing, message types, reliability, cipher integration
- [Transport Cipher](docs/protocol/transport-cipher.md) -- AlbyRules PRNG cipher: key schedule, cross-multiplication, plaintext feedback
- [GameSpy Protocol](docs/protocol/gamespy-protocol.md) -- LAN discovery, master server heartbeat, challenge-response
- [Join Flow](docs/network-flows/join-flow.md) -- Connection lifecycle: connect, checksums, lobby, gameplay
- [Checksum Handshake](docs/wire-formats/checksum-handshake-protocol.md) -- Hash algorithms, 5-round checksum exchange
- [Disconnect Flow](docs/network-flows/disconnect-flow.md) -- Player disconnect detection and cleanup
- [Wire Format Audit](docs/network-flows/wire-format-audit.md) -- Audit of wire format implementation vs spec
- Wire format specs: [ObjCreate](docs/wire-formats/objcreate-wire-format.md), [StateUpdate](docs/wire-formats/stateupdate-wire-format.md), [CollisionEffect](docs/wire-formats/collision-effect-wire-format.md), [Explosion](docs/wire-formats/explosion-wire-format.md)
- [ObjCreate Unknown Species](docs/bugs/objcreate-unknown-species.md) -- Behavior when species index exceeds client ship table

**Game Systems:**
- [Combat System](docs/game-systems/combat-system.md) -- Damage pipeline, shields, cloaking, tractor beams, repair
- [Power & Reactor System](docs/game-systems/power-system.md) -- Reactor, batteries, conduits, consumer draw, power wire format
- [Repair System](docs/game-systems/repair-system.md) -- Repair queue, priority toggle, PythonEvent wire format
- [Ship Subsystems](docs/game-systems/ship-subsystems.md) -- Fixed subsystem index table, HP values, StateUpdate serialization
- [Collision Detection](docs/game-systems/collision-detection-system.md) -- Collision damage scaling, dual damage paths
- [Collision Shield Interaction](docs/game-systems/collision-shield-interaction.md) -- Shield absorption during collisions
- [PythonEvent Wire Format](docs/wire-formats/pythonevent-wire-format.md) -- Factory IDs, event types, subsystem and explosion events

**Testing & Tools:**
- [Test Suite](tests/README.md) -- 11 test suites, test frameworks, adding new tests
- [Tools](tools/README.md) -- CLI tools, data scraper, diagnostic utilities

## Contributing

Contributions are welcome. The most useful things right now:

- **Testing**: Connect stock BC 1.1 clients, report connection issues or protocol mismatches
- **Cross-platform testing**: Build and test on macOS, different Linux distributions
- **Protocol analysis**: Packet captures, behavioral observation, documentation
- **Data files**: Ship stats, map definitions, game rule sets
- **Code review**: Protocol correctness, edge cases, platform compatibility

**Clean room requirement:** This is a clean-room reimplementation. All protocol knowledge must come from observable wire behavior (packet captures, behavioral observation) and the docs in `docs/` -- never from decompiled or original source code. Do not reference disassembly output, binary addresses, or decompiled pseudocode in issues, PRs, or comments. See `CLAUDE.md` for the full clean room rules and legal basis.

**Code style:** Builds must pass with zero warnings under `-Wall -Wextra -Wpedantic`. Run `make test` before submitting.

Open an issue to discuss before starting large changes.

## Related Projects

- [STBC-Dedicated-Server](https://github.com/cadacious/STBC-Dedicated-Server) -- Functional DDraw proxy dedicated server used for packet capture and protocol verification. Behavioral observations feed directly into OpenBC's clean-room documentation.

## Legal Basis

OpenBC is a clean-room reimplementation of the Bridge Commander multiplayer protocol, created for interoperability. No copyrighted code or assets are distributed. The server ships with zero original STBC content.

Legal precedent for clean-room reimplementation: *Oracle America v. Google* (2021, U.S. Supreme Court), *Sega v. Accolade* (1992, 9th Circuit), EU Software Directive Article 6.

Star Trek, Bridge Commander, and related marks are trademarks of CBS Studios and Paramount Global. This project is not affiliated with or endorsed by the trademark holders.

## License

TBD -- License will be selected before the first tagged release.
