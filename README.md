# OpenBC -- Open Bridge Commander

> **This project is in early planning stages. No code has been written yet. Everything described below represents the design goals and intended architecture, not current functionality.**

Open-source reimplementation of the Star Trek: Bridge Commander (2002) engine. The goal is to replace the original NetImmerse 3.1 engine with modern open-source libraries while maintaining perfect API compatibility with the game's SWIG-generated Python scripting bindings. Requires a legitimate copy of Bridge Commander (available on GOG) for all game data files.

## Project Status: Planning

**Nothing is implemented.** The repository currently contains only planning documents and design specifications produced through reverse engineering analysis of the original game. There is no buildable code, no runnable server, and no playable client.

What exists today:
- Design documents in `docs/` describing the planned Phase 1 implementation
- AI agent configurations in `.claude/` used for analysis and planning
- This README

What does not exist yet:
- Any C source code
- Any Python compatibility shims
- A build system
- A working server or client
- Tests

## Planned Architecture

```
Original Python Scripts (user-supplied from BC install)
        |
        v
Reimplemented App/Appc SWIG API (5,711 functions, 816 constants)
        |
        v
Modern Engine (bgfx + SDL3 + flecs + miniaudio + GNS + RmlUi)
```

## Planned Tech Stack

All libraries listed below are intended dependencies. None are integrated yet.

| Subsystem | Planned Library | License |
|-----------|-----------------|---------|
| Rendering | bgfx | BSD-2 |
| Platform/Input | SDL3 | zlib |
| Game State (ECS) | flecs | MIT |
| Audio | miniaudio | MIT-0 |
| Networking (new) | GameNetworkingSockets | BSD-3 |
| Networking (legacy) | Raw UDP (custom) | -- |
| UI | RmlUi | MIT |
| Physics | JoltC or custom | MIT |
| Scripting | Python 3.x + 1.5.2 compat shim | PSF |
| Asset Loading | Custom NIF parser | -- |

## Planned Development Phases

None of these phases have started.

1. **Phase 1: Playable Dedicated Server** -- Headless server speaking the legacy BC multiplayer protocol. Vanilla clients connect, play matches (FFA/team deathmatch), chat, and experience full game lifecycle via message relay. ~595 API functions.
2. **Phase 2: Full Game Logic** -- Server-side scoring, AI systems, ship simulation, damage model. Enables coop missions and authoritative game state.
3. **Phase 3: Rendering Client** -- bgfx renderer, NIF asset loading, scene graph. First visual output.
4. **Phase 4: Full Client** -- UI (RmlUi), audio (miniaudio), input, single-player campaign support. Feature-complete reimplementation.

## Planned Project Layout

This directory structure is planned but does not exist yet.

```
src/
  engine/        # Core engine, game loop, ECS setup
  compat/        # SWIG API compatibility layer (App/Appc reimplementation)
  render/        # bgfx rendering pipeline
  network/       # Legacy BC protocol + GameNetworkingSockets
  audio/         # miniaudio integration
  physics/       # Ship dynamics, collision detection
  ui/            # RmlUi integration
  scripting/     # Python 3.x embedding + 1.5.2 compatibility shim
  assets/        # NIF file parser, asset pipeline
  platform/      # SDL3, OS-specific code
vendor/          # Single-header vendored libraries
tools/           # CLI tools (script migration, asset conversion)
tests/           # Test suite
docker/          # Server container files
docs/            # Planning and design documents (exists now)
```

## Design Documents

These planning documents describe the intended Phase 1 implementation:

- **[Phase 1 Requirements](docs/phase1-requirements.md)** -- Functional and non-functional requirements for the dedicated server
- **[Phase 1 Implementation Plan](docs/phase1-implementation-plan.md)** -- Work chunks, timeline, critical path, file manifest
- **[Phase 1 RE Gaps](docs/phase1-re-gaps.md)** -- Reverse engineering status: what's known, what's partially known, what still needs analysis
- **[Phase 1 API Surface](docs/phase1-api-surface.md)** -- Catalog of ~595 SWIG API functions needed, with priority tiers

## How It Will Work (Planned)

The original Bridge Commander exposes its entire game engine to Python scripts through a SWIG-generated API layer (`App` and `Appc` modules, totaling 5,711 functions and 816 constants). Every game script -- from ship hardpoints to multiplayer missions to the main menu -- calls into this API.

OpenBC's approach is to reimplement this API surface on top of modern libraries. The original game scripts (supplied by the user from their BC installation) run unmodified on OpenBC's reimplemented engine. A Python 1.5.2 compatibility shim handles the language version gap since the original scripts target Python 1.5.2 while OpenBC embeds Python 3.x.

Phase 1 specifically targets the dedicated server use case: a headless process that speaks the legacy BC multiplayer wire protocol. Vanilla BC clients (the original game from GOG) connect to it and play multiplayer matches. The server acts as a message relay -- it forwards game state between clients without simulating physics or combat. Each client runs the full game simulation locally.

## Related Repository

- **[STBC-Dedicated-Server](https://github.com/cadacious/STBC-Dedicated-Server)** -- Reverse engineering workspace. Contains decompiled reference code, protocol documentation, and analysis tools. This is where game analysis happens; OpenBC is the clean reimplementation.

## Legal Basis

This project aims to reimplement the SWIG API interface for interoperability with user-supplied game data. No copyrighted code or assets are or will be distributed. Legal precedent: Oracle v. Google (2021), Sega v. Accolade (1992), EU Software Directive Article 6. Users must supply their own legitimate copy of Star Trek: Bridge Commander.

## Contributing

This project is not yet ready for contributions. The design is still being finalized. Watch this repository for updates.

## License

TBD -- License will be selected before any code is written.
