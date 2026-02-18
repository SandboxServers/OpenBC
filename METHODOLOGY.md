# Clean Room Development Methodology

This document describes the development methodology used by the OpenBC project. Its purpose is to establish and record the separation procedures that ensure no copyrighted code from Star Trek: Bridge Commander is present in this repository. This methodology has been in effect since the project's inception in February 2026.

## Background

Star Trek: Bridge Commander (2002) is a space combat game whose multiplayer protocol is undocumented. The game's dedicated server has been unavailable for years, and the community has no way to host multiplayer games without running the full game client. OpenBC exists to provide a standalone, open-source dedicated server that speaks the same wire protocol, allowing unmodified BC clients to connect and play.

This is a clean room reimplementation for interoperability purposes, consistent with established legal precedent including *Oracle v. Google* (US Supreme Court, 2021), *Sega v. Accolade* (9th Circuit, 1992), and EU Software Directive Article 6. The server distributes zero copyrighted content; operators supply their own copy of Bridge Commander.

## Two-Session Architecture

Development is split across two completely isolated AI sessions, each with a distinct role and strict access boundaries.

### Reverse Engineering Session

A separate Claude AI session operates in a dedicated reverse engineering workspace. This session has access to Ghidra decompilation output, binary analysis tools, function addresses, struct layouts, and other RE artifacts derived from the original game executable. Its sole output is **specification documents** -- written descriptions of protocol behavior, wire formats, and system interactions.

These specification documents describe *what* the protocol does and *how* data appears on the wire. They do not contain decompiled code, function addresses, struct offsets, vtable layouts, or any other artifact that could constitute a derivative work of the original binary. The RE session distills implementation-specific knowledge into behavioral descriptions suitable for independent reimplementation.

### Clean Room Implementation Session

This session (the one that produces all code in this repository) operates under explicit constraints defined in the project's `CLAUDE.md` file, which is checked into version control and auditable. These constraints include:

- **No access to the original game binary, source code, or decompiled output.** The RE workspace directory is explicitly forbidden and named in the system prompt.
- **No access to RE repositories on GitHub or elsewhere online.** The session will not search for, fetch, or reference decompiled BC code from any source.
- **No reference to binary addresses, function labels, data labels, struct field offsets, vtable slot indices, or decompiled pseudocode.** These are artifacts of reverse engineering and are excluded from the clean room.
- **All protocol knowledge comes from specification documents in `docs/`** and from observable wire behavior (packet captures from unmodified vanilla clients).
- **If asked to access RE data, the session will decline** and redirect to the specification documents.

These constraints are enforced at the system prompt level and are active for every interaction in the implementation workspace.

## Information Flow

```
 ┌─────────────────────┐         ┌──────────────────────┐
 │   RE Session        │         │  Clean Room Session   │
 │                     │         │                       │
 │  Ghidra output      │         │  Protocol specs only  │
 │  Binary analysis    │  docs/  │  Packet captures      │
 │  Decompiled code    │ ──────> │  Behavioral docs      │
 │  Struct layouts     │         │  Wire format refs     │
 │  Function addresses │         │                       │
 │                     │         │  Produces: all code   │
 └─────────────────────┘         └──────────────────────┘
         │                                  │
         │  NO direct connection            │
         │  NO shared memory                │
         │  NO cross-session access         │
         └──────────────────────────────────┘
```

The only artifact that crosses the boundary is the `docs/` directory, which contains behavioral specifications written in plain English with wire format diagrams. These documents are reviewed before being placed in the clean room workspace to ensure they contain no RE artifacts.

### Specification Documents

The following documents form the complete specification set consumed by the clean room session:

| Document | Subject |
|----------|---------|
| `transport-cipher.md` | Stream cipher algorithm and key schedule |
| `gamespy-protocol.md` | LAN discovery and master server handshake |
| `join-flow.md` | Connection lifecycle from connect to gameplay |
| `checksum-handshake-protocol.md` | File integrity hash exchange |
| `disconnect-flow.md` | Player disconnect detection and cleanup |
| `combat-system.md` | Damage model, shields, cloaking, tractor beams, repair |
| `ship-subsystems.md` | Subsystem index table and health serialization |
| `server-authority.md` | Client vs. server authority boundaries |
| `stateupdate-wire-format.md` | Position and state synchronization format |
| `objcreate-wire-format.md` | Ship creation message format |
| `collision-effect-wire-format.md` | Collision damage report format |
| `phase1-verified-protocol.md` | Complete protocol reference (opcodes, formats, handshake) |

Each document describes observable behavior and wire-level data formats. None contain decompiled code or binary-specific implementation details.

## Human Director Role

The project lead (Cadacious) directs both sessions but functions strictly as a project manager -- setting goals, prioritizing work, providing test feedback, and correcting course based on observable outcomes (e.g., "the client disconnected," "damage wasn't applied," "the server browser shows the wrong data"). The project lead has no background in C programming or reverse engineering and does not interpret decompiled output. They are not a technical contamination vector between sessions; they relay goals and results, not implementation details.

## AI Context Isolation

Claude AI sessions provide a stronger isolation guarantee than traditional human clean room methodology:

- **No shared memory.** Each session starts with a blank context. There is no mechanism for one session to access another session's conversation history, tool outputs, or generated artifacts.
- **No persistent state.** Sessions do not retain information between conversations beyond what is explicitly written to files in their respective workspaces.
- **No cross-session access.** The clean room session cannot read files from the RE workspace. This is enforced by explicit directory restrictions in the system prompt.
- **No subconscious contamination.** Unlike human developers, who might unconsciously recall code structure or variable names from previously reviewed material, AI sessions have no latent memory of prior sessions. Each conversation begins from the system prompt and workspace files only.

This eliminates the primary risk in traditional clean room development: that an engineer who has seen the original source code might inadvertently reproduce its structure, naming conventions, or implementation choices.

## Verification

The separation methodology can be verified through several means:

1. **`CLAUDE.md` is version-controlled.** The clean room constraints are visible in the repository history from the project's first commit.
2. **Specification documents are version-controlled.** The `docs/` directory contents can be reviewed to confirm they contain behavioral descriptions, not RE artifacts.
3. **No binary addresses appear in any source file.** A repository-wide search for patterns like `FUN_`, `DAT_`, `0x00[4-6]` (typical Ghidra address ranges), or decompiled pseudocode will return no results.
4. **Code structure diverges from the original.** OpenBC is written in C with its own architecture (ECS-influenced, data-driven, JSON-configured). It does not mirror the original game's C++ class hierarchy, naming conventions, or code organization.

## Established

This methodology was established in February 2026 and has been in continuous effect for all development work in this repository.
