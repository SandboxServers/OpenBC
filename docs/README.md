# OpenBC Documentation

Navigation index for all design documents, protocol references, and system specifications.

## Directory Structure

| Directory | Description | Start here |
|-----------|-------------|------------|
| [architecture/](architecture/) | High-level server design, engine reference, authority model | [server-architecture.md](architecture/server-architecture.md) |
| [protocol/](protocol/) | Wire protocol reference, transport layer, cipher, GameSpy | [protocol-reference.md](protocol/protocol-reference.md) |
| [wire-formats/](wire-formats/) | Per-opcode wire format specifications | [checksum-handshake-protocol.md](wire-formats/checksum-handshake-protocol.md) |
| [game-systems/](game-systems/) | Game simulation: combat, power, repair, subsystems, collisions | [combat-system.md](game-systems/combat-system.md) |
| [network-flows/](network-flows/) | Connection lifecycle: join, disconnect, wire format audit | [join-flow.md](network-flows/join-flow.md) |
| [planning/](planning/) | Future work: game modes, extensibility, cut content analysis | [gamemode-system.md](planning/gamemode-system.md) |
| [bugs/](bugs/) | Bug analyses, postmortems, and fix documentation | -- |
| [archive/](archive/) | Historical planning documents kept for reference | [phase1-requirements.md](archive/phase1-requirements.md) |

## Quick Reference

**Building a new opcode handler?** Start with [protocol-reference.md](protocol/protocol-reference.md) for the opcode table, then check [wire-formats/](wire-formats/) for the specific opcode's wire format.

**Working on combat or damage?** Load [game-systems/](game-systems/) -- combat, power, repair, subsystems, and collision docs are all there.

**Debugging a connection issue?** See [network-flows/join-flow.md](network-flows/join-flow.md) for the full handshake sequence, or [network-flows/disconnect-flow.md](network-flows/disconnect-flow.md) for cleanup.

**Understanding the original engine?** See [architecture/engine-reference.md](architecture/engine-reference.md) for the BC engine's behavioral architecture.
