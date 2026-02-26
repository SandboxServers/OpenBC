# OpenBC Documentation

Navigation index for all design documents, protocol references, and system specifications.

## Directory Structure

| Directory | Description | Start here |
|-----------|-------------|------------|
| [architecture/](architecture/) | High-level server design, engine reference, authority model | [server-architecture.md](architecture/server-architecture.md) |
| [protocol/](protocol/) | Wire protocol reference, transport layer, cipher, GameSpy | [protocol-reference.md](protocol/protocol-reference.md) |
| [wire-formats/](wire-formats/) | Per-opcode wire format specifications | [README.md](wire-formats/README.md) |
| [game-systems/](game-systems/) | Game simulation: combat, power, repair, subsystems, collisions, AI, camera | [README.md](game-systems/README.md) |
| [network-flows/](network-flows/) | Connection lifecycle: join, disconnect, wire format audit | [README.md](network-flows/README.md) |
| [modding/](modding/) | Modding guides: getting started, DLL, Lua, TOML, total conversion | [getting-started.md](modding/getting-started.md) |
| [planning/](planning/) | Future work: game modes, extensibility, cut content analysis | [gamemode-system.md](planning/gamemode-system.md) |
| [bugs/](bugs/) | Bug analyses, postmortems, and fix documentation | -- |
| [guides/](guides/) | Coding patterns, cross-platform portability, tech stack gotchas, PR review process | [coding-patterns.md](guides/coding-patterns.md), [pr-review-process.md](guides/pr-review-process.md) |
| [archive/](archive/) | Historical planning documents kept for reference | [phase1-requirements.md](archive/phase1-requirements.md) |

## Recent Additions

### Game Systems (`game-systems/`)
- [shield-system.md](game-systems/shield-system.md) — 6-facing ellipsoid shields, recharge formula, cloak delay
- [weapon-system.md](game-systems/weapon-system.md) — Phaser charge/discharge, torpedo reload, CanFire gates
- [cloaking-system.md](game-systems/cloaking-system.md) — 4-state cloak machine, shield interaction, energy failure
- [tractor-beam-system.md](game-systems/tractor-beam-system.md) — 6 modes, force computation, speed drag

### Wire Formats (`wire-formats/`)
- [tgobjptrevent-wire-format.md](wire-formats/tgobjptrevent-wire-format.md) — TGObjPtrEvent (factory 0x010C), 45% of combat PythonEvents
- [objnotfound-requestobj-wire-format.md](wire-formats/objnotfound-requestobj-wire-format.md) — Object recovery protocol (opcodes 0x1D/0x1E)
- [enter-set-wire-format.md](wire-formats/enter-set-wire-format.md) — EnterSet (opcode 0x1F), multi-zone missions

### Protocol (`protocol/`)
- [stream-primitives.md](protocol/stream-primitives.md) — CF16 encoding, bit packing, CompressedVector3/4

### Bugs (`bugs/bug-reports/`)
- [20260226-stateupdate-authority-and-cadence-gap.md](bugs/bug-reports/20260226-stateupdate-authority-and-cadence-gap.md) — Clean-room wire-behavior report on shield flicker/subsystem drift/hull disagreement and replication fix targets

## Quick Reference

**Building a new opcode handler?** Start with [protocol-reference.md](protocol/protocol-reference.md) for the opcode table, then check [wire-formats/](wire-formats/) for the specific opcode's wire format.

**Working on combat or damage?** Load [game-systems/](game-systems/) -- combat, power, repair, subsystems, collision, and AI docs are all there.

**Debugging a connection issue?** See [network-flows/join-flow.md](network-flows/join-flow.md) for the full handshake sequence, or [network-flows/disconnect-flow.md](network-flows/disconnect-flow.md) for cleanup.

**Understanding the original engine?** See [architecture/engine-reference.md](architecture/engine-reference.md) for the BC engine's behavioral architecture.

**Writing a mod?** Start with [modding/getting-started.md](modding/getting-started.md) for the overview, then dive into the specific guide for your tier: [TOML config](modding/toml-reference.md), [Lua scripting](modding/lua-scripting-guide.md), or [C DLLs](modding/dll-module-guide.md).

**Understanding the plugin system?** See [architecture/plugin-system.md](architecture/plugin-system.md) for the module lifecycle and [architecture/event-system.md](architecture/event-system.md) for the event bus.
