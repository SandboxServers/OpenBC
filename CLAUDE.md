# OpenBC — Open Bridge Commander

Open-source reimplementation of the Star Trek: Bridge Commander (2002) engine. Replaces NetImmerse 3.1 with modern open-source libraries while maintaining perfect API compatibility with the original SWIG-generated Python bindings. Requires a legitimate copy of Bridge Commander (available on GOG) for game data files.

## Architecture

```
Original Python Scripts (user-supplied from BC install)
        │
        ▼
Reimplemented App/Appc SWIG API (5,711 functions, 816 constants)
        │
        ▼
Modern Engine (bgfx + SDL3 + flecs + miniaudio + GNS + RmlUi)
```

## Tech Stack

| Subsystem | Library | License |
|-----------|---------|---------|
| Rendering | bgfx | BSD-2 |
| Platform/Input | SDL3 | zlib |
| Game State (ECS) | flecs | MIT |
| Audio | miniaudio | MIT-0 |
| Networking (new) | GameNetworkingSockets | BSD-3 |
| Networking (legacy) | Raw UDP (custom) | — |
| UI | RmlUi | MIT |
| Physics | JoltC or custom | MIT |
| Scripting | Python 3.x + 1.5.2 compat shim | PSF |
| Asset Loading | Custom NIF parser | — |

## Development Phases

1. **Standalone Server** — Headless dedicated server speaking legacy BC protocol (~595 API functions)
2. **Full Game Logic** — Ship/weapon/AI systems in flecs ECS (~1,142 more functions)
3. **Rendering Client** — bgfx renderer + NIF asset loading (~1,379 more functions)
4. **Full Client** — UI, audio, input, complete game (~2,893 more functions)

## Build

```bash
cmake -B build
cmake --build build

# Server only (no GPU dependencies):
cmake -B build -DOPENBC_SERVER_ONLY=ON
cmake --build build
```

## Project Layout

```
src/
├── engine/        # Core engine, game loop, ECS setup
├── compat/        # SWIG API compatibility layer (App/Appc reimplementation)
├── render/        # bgfx rendering pipeline
├── network/       # Legacy BC protocol + GameNetworkingSockets
├── audio/         # miniaudio integration
├── physics/       # Ship dynamics, collision detection
├── ui/            # RmlUi integration
├── scripting/     # Python 3.x embedding + 1.5.2 compatibility shim
├── assets/        # NIF file parser, asset pipeline
└── platform/      # SDL3, OS-specific code
vendor/            # Single-header vendored libraries
tools/             # CLI tools (script migration, asset conversion)
tests/             # Test suite
docker/            # Server container files
```

## Agent Roster (14 agents in .claude/agents/)

### Tier 1 — Core Architecture
- **swig-api-compat** — SWIG API compatibility (THE critical agent)
- **stbc-original-dev** — Original developer intent / design judgment
- **openbc-architect** — System architecture / integration
- **game-reverse-engineer** — Decompiled code analysis

### Tier 2 — Engine Subsystems
- **bgfx-renderer** — Rendering pipeline
- **flecs-ecs-architect** — ECS game state design
- **network-protocol** — Protocol engineering (legacy + new)
- **nif-asset-pipeline** — NIF file format / asset loading
- **rmlui-specialist** — UI reimplementation

### Tier 3 — Integration
- **python-migration** — Python 1.5.2→3.x compatibility
- **platform-integration** — SDL3, miniaudio, cross-platform
- **physics-sim** — Ship dynamics, collision, damage model

### Tier 4 — Quality & Community
- **mod-compat-tester** — Community mod compatibility testing
- **build-ci** — Build system, CI/CD, packaging

## Planning Documents

- **[Phase 1 Requirements](docs/phase1-requirements.md)** — Functional/non-functional requirements for the dedicated server
- **[Phase 1 Implementation Plan](docs/phase1-implementation-plan.md)** — Work chunks, timeline, critical path, file manifest
- **[Phase 1 Verified Protocol](docs/phase1-verified-protocol.md)** — Complete wire protocol: opcodes, packet formats, handshake, reliable delivery, data structures
- **[Phase 1 Engine Architecture](docs/phase1-engine-architecture.md)** — Original engine internals: NI 3.1 / TG Framework / Game Logic layers, message dispatchers, bootstrap
- **[Phase 1 RE Gaps](docs/phase1-re-gaps.md)** — Reverse engineering status (all critical items solved)
- **[Phase 1 API Surface](docs/phase1-api-surface.md)** — Catalog of ~595 SWIG API functions needed, with priority tiers

## Key RE Data (verified, from STBC-Dedi)

All critical reverse engineering is complete. Key verified facts:

- **Wire protocol**: AlbyRules XOR cipher ("AlbyRules!" 10-byte key), raw UDP, three-tier send queues (unreliable/reliable/priority)
- **Game opcodes**: 28 active opcodes verified from jump table at 0x0069F534 (see `phase1-verified-protocol.md` Section 5)
- **Python messages**: MAX_MESSAGE_TYPES = 0x2B; CHAT=0x2C, TEAM_CHAT=0x2D, MISSION_INIT=0x35, SCORE_CHANGE=0x36, SCORE=0x37, END_GAME=0x38, RESTART=0x39
- **Flag bytes**: 0x0097FA88=IsClient, 0x0097FA89=IsHost, 0x0097FA8A=IsMultiplayer
- **Ship creation**: ObjectCreateTeam (0x03), destruction: DestroyObject (0x14)
- **Handshake**: connect → GameSpy peek → 4 checksum rounds → Settings (0x00) → GameInit (0x01) → EnterSet (0x1F) → NewPlayerInGame (0x2A)

## Related Repository

- **[STBC-Dedicated-Server](../STBC-Dedicated-Server/)** — Functional DDraw proxy dedicated server and RE workspace. Contains a working headless server (collision damage, scoring, full game lifecycle), decompiled reference code, protocol docs, and Ghidra annotation tools. RE findings feed directly into OpenBC's planning docs.

## Legal Basis

This project reimplements the SWIG API interface for interoperability with user-supplied game data. No copyrighted code or assets are distributed. Legal precedent: Oracle v. Google (2021), Sega v. Accolade (1992), EU Software Directive Article 6. Users must supply their own legitimate copy of Star Trek: Bridge Commander.
