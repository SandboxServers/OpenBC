# Ship Death Lifecycle — Clean-Room Behavioral Spec

Behavioral specification for how ships are destroyed and respawned in Bridge Commander multiplayer. Documented from observable network traffic patterns. Suitable for clean-room reimplementation.

**Clean room statement**: This document describes observable multiplayer behavior and network traffic patterns. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

When a ship is destroyed in multiplayer, the server broadcasts an explosion event to all clients and then immediately creates a new ship object for the same player (respawn). The protocol includes a "destroy object" message type, but it is **not used** for multiplayer ship death in stock behavior.

---

## Death Sequence

### 1. Ship Hull Reaches Zero

The damage pipeline (collision, weapon, or explosion) reduces the ship's hull condition to zero. The engine posts an OBJECT_EXPLODING event internally.

### 2. Explosion Broadcast

The server sends two messages to all clients:

**a) PythonEvent (opcode 0x06) — ObjectExplodingEvent**:
- Factory ID: 0x8129
- Event type: OBJECT_EXPLODING (0x0080004E)
- Contains: dying ship's object ID, killer's player ID, explosion lifetime (duration in seconds)
- Sent reliably to the "NoMe" routing group

**b) Explosion (opcode 0x29)**:
- Contains: dying ship's object ID, impact position (compressed), damage amount, explosion radius
- Triggers client-side visual effects

### 3. Respawn

The server creates a new ship object for the same player and broadcasts it:

**ObjCreateTeam (opcode 0x03)**:
- Contains: owner player slot, team assignment, full serialized ship data
- The new ship has fresh subsystems at full health
- The player's identity and team assignment are preserved

### 4. No DestroyObject Message

The protocol defines a DestroyObject message (opcode 0x14), but stock multiplayer does **not** use it for ship deaths. Zero DestroyObject messages were observed across 59 ship deaths in a 33.5-minute combat session. The old ship object is implicitly replaced by the new one.

DestroyObject may be used for:
- Non-ship object cleanup (projectiles, torpedoes)
- Player disconnect cleanup
- Single-player scenarios

---

## Scoring Interaction

When a ship is destroyed, the server should:

1. Determine the killer (from the OBJECT_EXPLODING event's firing player ID)
2. Update kill/death counts
3. Compute scores for all damage contributors
4. Broadcast SCORE_CHANGE (0x36) to all clients

> **Known anomaly**: Stock dedicated servers correctly send SCORE_CHANGE for collision
> kills but may not send it for weapon kills. Implementations should ensure SCORE_CHANGE
> is broadcast for ALL kill types regardless of the damage source.

See [gamemode-system.md](../planning/gamemode-system.md) for full scoring specification.

---

## Implementation Notes

### Server

1. When hull condition reaches zero:
   a. Create an ObjectExplodingEvent with the killer's player ID and explosion duration
   b. Serialize as PythonEvent (opcode 0x06, factory 0x8129)
   c. Send reliably to all clients
   d. Send Explosion (opcode 0x29) with position, damage, and radius
   e. Process scoring (kill/death/score updates, SCORE_CHANGE broadcast)
   f. Create a new ship for the same player (ObjCreateTeam, opcode 0x03)

2. Do NOT send DestroyObject (0x14) for ship deaths — clients do not expect it

### Client

1. On receiving ObjectExplodingEvent: trigger explosion visual/audio at the ship's position
2. On receiving Explosion (0x29): apply explosion visual effects
3. On receiving ObjCreateTeam (0x03): replace the dead ship object with the new one
4. The old ship object is cleaned up when the new one arrives — no explicit destroy needed

### Respawn Timing

The server sends the respawn (0x03) immediately after the explosion broadcast. There is no configurable respawn delay in stock behavior — the ship reappears at full health as soon as the server processes the death event.

---

## Related Documents

- **[explosion-wire-format.md](../wire-formats/explosion-wire-format.md)** — Opcode 0x29 wire format
- **[pythonevent-wire-format.md](../wire-formats/pythonevent-wire-format.md)** — ObjectExplodingEvent (factory 0x8129)
- **[objcreate-wire-format.md](../wire-formats/objcreate-wire-format.md)** — ObjCreateTeam (opcode 0x03) for respawn
- **[gamemode-system.md](../planning/gamemode-system.md)** — Scoring, frag limits, end game conditions
- **[collision-damage-event-chain.md](../bugs/collision-damage-event-chain.md)** — Collision → PythonEvent chain
- **[disconnect-flow.md](../network-flows/disconnect-flow.md)** — Player disconnect (separate from ship death)
