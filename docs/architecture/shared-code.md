# Shared Code Architecture

OpenBC's server and client share a single repository with common code in `src/shared/`. This document describes how code is organized to support both executables from one codebase.

## Directory Structure

```
src/
  shared/     Game logic, protocol, data types (compiled into both EXEs)
  server/     Server main, headless systems, server-only dispatch
  client/     Client main, rendering, audio, input, UI
```

### `src/shared/` -- Common Code

Code in `shared/` is compiled into both the server and client binaries. It contains:

- **Protocol codec** -- Wire format encode/decode, opcode definitions, buffer streams
- **Game data types** -- Ship class definitions, weapon data, subsystem types
- **Ship state** -- Ship state struct, subsystem HP tracking, position
- **Combat logic** -- Damage calculation, shield absorption, collision math
- **Power simulation** -- Reactor, battery, conduit, efficiency computation
- **Repair system** -- Repair queue, rate formula
- **Movement** -- Position tracking, velocity, heading
- **Checksum algorithms** -- StringHash, FileHash
- **JSON parser** -- Lightweight JSON parser for data registry
- **Cipher** -- AlbyRules stream cipher
- **Types** -- Common type definitions, math types, constants

### `src/server/` -- Server-Only Code

Code that only exists in the dedicated server binary:

- **Server main** -- Entry point, CLI argument parsing, TOML config loading
- **Server dispatch** -- Incoming packet routing, opcode handler registration
- **Module loader** -- DLL loading, `obc_module_load` lookup, lifecycle
- **Event bus** -- Event subscription, dispatch, priority ordering
- **Peer management** -- Connection tracking, slot allocation, timeout detection
- **GameSpy** -- LAN discovery responses, master server heartbeat
- **Reliability** -- Retransmit, sequencing, fragment reassembly (server-side state)
- **Server stats** -- Tick timing, queue depths, player counts

### `src/client/` -- Client-Only Code

Code that only exists in the game client binary:

- **Client main** -- Entry point, window creation, main loop
- **Rendering** -- bgfx backend, shader management, draw calls
- **Scene graph** -- Node tree, transform hierarchy, culling
- **NIF parser** -- V3.1 NIF file loader, mesh extraction, texture binding
- **Asset resolver** -- Mod override chain, game directory search, fallback paths
- **Audio** -- miniaudio backend, 3D positional audio, music state machine
- **Input** -- SDL3 keyboard/mouse input, keybindings
- **UI** -- LCARS UI system, ship select, HUD, menus
- **Visual effects** -- Phaser beams, torpedoes, shield hits, explosions, engine glow
- **Client networking** -- Client-side transport, state interpolation, prediction
- **Dev tools** -- Dear ImGui integration for debugging

## Build System

Both EXEs are built from the same Makefile:

```
make server     # Compiles src/shared/ + src/server/ -> openbc-server
make client     # Compiles src/shared/ + src/client/ -> openbc-client
make all        # Builds both
make test       # Tests use shared/ code directly
```

### bgfx Noop Backend

The server binary links against bgfx's Noop backend. This means shared code that references rendering types (e.g., scene graph node structs used by both movement and rendering) compiles for both targets without `#ifdef` guards. The Noop backend provides valid type definitions and no-op function stubs.

## Shared vs. Split Decisions

| Component | Shared? | Rationale |
|-----------|---------|-----------|
| Protocol codec | Yes | Both sides encode/decode the same wire format |
| Combat math | Yes | Server computes authoritatively; client predicts locally |
| Power sim | Yes | Server computes; client displays |
| Ship state struct | Yes | Identical state representation on both sides |
| Game data types | Yes | Both load from the same JSON registry |
| Cipher | Yes | Both sides encrypt/decrypt |
| Checksum algos | Yes | Server validates; client computes |
| Peer management | No | Server tracks N peers; client has 1 connection |
| Module loader | No | Server-only; client uses different extension model |
| Rendering | No | Server has no GPU |
| Audio | No | Server has no audio device |
| Input | No | Server has no input devices |
| UI | No | Server has no display |
| NIF parser | No | Server doesn't load models (uses data registry) |

## Module System and Shared Code

The module DLL system is server-only. Client game logic uses the same underlying combat/power/repair code from `shared/` but invokes it through the client's own event loop, not through the module loader.

This is intentional: the server needs modularity for mod extensibility. The client needs rendering integration and frame-level control that doesn't map to the DLL handler model.

## Migration Path

The current Phase 1 codebase has all code in flat directories:

```
src/checksum/   -> src/shared/checksum/
src/game/       -> src/shared/game/
src/json/       -> src/shared/json/
src/network/    -> split: shared/protocol/ + server/network/
src/protocol/   -> src/shared/protocol/
src/server/     -> stays in src/server/
```

The migration is a file-move refactor. No logic changes. `#include` paths update and the Makefile gains a second target.

## See Also

- [Plugin System](plugin-system.md) -- Server-side module architecture
- [Server Architecture](server-architecture.md) -- Current server design
