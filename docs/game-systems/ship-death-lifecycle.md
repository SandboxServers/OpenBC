# Ship Death Lifecycle — Clean-Room Behavioral Spec

Behavioral specification for how ships are destroyed and respawned in Bridge Commander multiplayer. Documented from observable network traffic patterns. Suitable for clean-room reimplementation.

**Clean room statement**: This document describes observable multiplayer behavior and network traffic patterns. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

When a ship is destroyed in multiplayer, the server broadcasts an explosion event to all
clients. The server does **not** auto-respawn the player — instead, the client returns to
the ship selection screen and sends ObjCreateTeam (0x03) when the player picks a new ship.
The protocol includes a "destroy object" message type, but it is **not used** for
multiplayer ship death in stock behavior.

---

## Death Sequence

### 1. Ship Hull Reaches Zero

The damage pipeline (collision, weapon, or explosion) reduces the ship's hull condition to zero. The engine posts an OBJECT_EXPLODING event internally.

### 2. Explosion Broadcast

The messages sent depend on whether this is a **combat kill** or a **self-destruct**:

#### Combat Kill (weapon, collision, or explosion damage)

The server sends these messages to all clients:

**a) PythonEvent (opcode 0x06) — ObjectExplodingEvent**:
- Factory ID: 0x8129
- Event type: OBJECT_EXPLODING (0x0080004E)
- Contains: dying ship's object ID, killer's player ID, explosion lifetime
- source = killer's ship object ID (or NULL for environment kills)
- dest = dying ship's object ID
- Sent reliably to the "NoMe" routing group

**b) ~13 TGSubsystemEvent (factory 0x0101) — ADD_TO_REPAIR_LIST**:
- One per damaged subsystem at the moment of death
- Event code: 0x800000DF
- source = damaged subsystem's TGObject ID (must be in the player's object ID range)
- dest = ship's RepairSubsystem TGObject ID
- Count varies by collision geometry (12-14 confirmed from traces)

**c) SCORE_CHANGE (0x36)**:
- Kill/death credit as appropriate

**d) Explosion (opcode 0x29)** — for **weapon kills only**:
- Contains: dying ship's object ID, impact position (compressed), damage amount, explosion radius
- Triggers client-side visual effects
- **NOT sent for collision-induced kills** — only ObjectExplodingEvent triggers the animation

The server does **NOT** auto-respawn after combat kills. After the explosion animation,
the client returns to the ship selection screen and sends ObjCreateTeam (0x03) when the
player picks a new ship.

> **Note**: Whether Explosion (0x29) is sent depends on the kill source. Stock traces
> confirm: collision kills = no Explosion; weapon kills = Explosion sent. Self-destruct =
> no Explosion. This distinction needs further weapon kill trace verification.

#### Self-Destruct (opcode 0x13)

The server sends **only** the ObjectExplodingEvent:

**a) PythonEvent (opcode 0x06) — ObjectExplodingEvent**:
- Factory ID: 0x8129
- source = NULL (0x00000000) — no attacker for self-destruct
- dest = dying ship's object ID
- lifetime = 9.5 seconds
- Sent reliably

**b) SCORE_CHANGE (0x36)**:
- Death counted for the self-destructing player
- No kill credit awarded (attacker is NULL)

**NOT sent for self-destruct:**
- Explosion (0x29) — not used; only ObjectExplodingEvent triggers the animation
- ObjCreateTeam (0x03) — no auto-respawn; the client picks a new ship

### 3. Respawn

Respawn behavior is the same for all death types (combat kill, self-destruct, collision):

After the explosion animation, the client returns to the ship selection screen.
The **client** sends ObjCreateTeam (0x03) when the player picks a new ship. The server
relays this to all other clients via star topology.

The server does **NOT** auto-respawn for any death type. All ObjCreateTeam messages after
death are client-initiated.

### 4. No DestroyObject Message

The protocol defines a DestroyObject message (opcode 0x14), but stock multiplayer does **not** use it for ship deaths — neither combat kills nor self-destruct. Zero DestroyObject messages were observed across 59 combat kills and self-destruct tests. The old ship object is implicitly replaced by the new one (combat) or exists as wreckage during the explosion animation (self-destruct).

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

### Server — Combat Kill

1. When hull condition reaches zero from combat damage:
   a. Create an ObjectExplodingEvent with the killer's player ID and lifetime
   b. Serialize as PythonEvent (opcode 0x06, factory 0x8129)
   c. Send reliably to all clients
   d. Send TGSubsystemEvent (factory 0x0101, code 0x800000DF) for each damaged subsystem
   e. For **weapon kills only**: send Explosion (opcode 0x29) with position, damage, radius
   f. Process scoring (kill/death/score updates, SCORE_CHANGE broadcast)
   g. Clear ship state (mark as dead) — do NOT auto-respawn
   h. Wait for the client to send ObjCreateTeam (0x03) when the player picks a new ship

2. Do NOT send DestroyObject (0x14) for ship deaths — clients do not expect it
3. Do NOT auto-respawn — the client initiates its own respawn
4. Do NOT send Explosion (0x29) for collision-induced kills — only for weapon kills

### Server — Self-Destruct

1. When hull condition reaches zero from self-destruct (opcode 0x13):
   a. Create an ObjectExplodingEvent with source=NULL, dest=dying ship, lifetime=9.5s
   b. Serialize as PythonEvent (opcode 0x06, factory 0x8129)
   c. Send reliably to all clients
   d. Process scoring (death counted, no kill credit, SCORE_CHANGE broadcast)
   e. Do NOT send Explosion (0x29) — only ObjectExplodingEvent triggers the animation
   f. Do NOT auto-respawn — wait for the client to send ObjCreateTeam
   g. Continue sending/receiving StateUpdates during the 9.5s explosion period

2. Do NOT send DestroyObject (0x14) — the ship exists as wreckage during explosion

### Client

1. On receiving ObjectExplodingEvent: trigger explosion visual/audio at the ship's position, play for `lifetime` seconds (9.5s)
2. On receiving Explosion (0x29): apply explosion visual effects (combat kills only)
3. On receiving ObjCreateTeam (0x03): replace the dead ship object with the new one
4. For self-destruct: after explosion timer expires, return to ship selection screen; player picks a new ship, client sends ObjCreateTeam

### Respawn Timing

**All death types**: No server-initiated respawn. After the explosion animation, the
client returns to the ship selection screen. The client picks a new ship and sends
ObjCreateTeam (0x03) with the **client's player slot** and team assignment (not the host's
slot). The server relays this to all other clients.

---

## Related Documents

- **[explosion-wire-format.md](../wire-formats/explosion-wire-format.md)** — Opcode 0x29 wire format
- **[pythonevent-wire-format.md](../wire-formats/pythonevent-wire-format.md)** — ObjectExplodingEvent (factory 0x8129)
- **[objcreate-wire-format.md](../wire-formats/objcreate-wire-format.md)** — ObjCreateTeam (opcode 0x03) for respawn
- **[gamemode-system.md](../planning/gamemode-system.md)** — Scoring, frag limits, end game conditions
- **[collision-damage-event-chain.md](../bugs/collision-damage-event-chain.md)** — Collision → PythonEvent chain
- **[disconnect-flow.md](../network-flows/disconnect-flow.md)** — Player disconnect (separate from ship death)
