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

The server sends two messages to all clients:

**a) PythonEvent (opcode 0x06) — ObjectExplodingEvent**:
- Factory ID: 0x8129
- Event type: OBJECT_EXPLODING (0x0080004E)
- Contains: dying ship's object ID, killer's player ID, explosion lifetime (9.5 seconds)
- source = killer's ship object ID (for beam/torpedo kills, this is the attacking ship)
- dest = dying ship's object ID
- Sent reliably to the "NoMe" routing group

**b) Explosion (opcode 0x29)**:
- Contains: dying ship's object ID, impact position (compressed), damage amount, explosion radius
- Triggers client-side visual effects

Verified in a 33.5-minute stock trace with 55 weapon kills: every weapon kill produced
both messages. The ObjectExplodingEvent `source` field contains the **killer's ship
object ID** (not NULL), allowing the scoring system to identify who made the kill.

The server does **NOT** auto-respawn after combat kills. After the 9.5-second explosion
animation, the client returns to the ship selection screen and sends ObjCreateTeam (0x03)
when the player picks a new ship. All 38 ObjCreateTeam messages in the trace were
client-initiated relays — zero were server-originated.

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

After the 9.5-second explosion animation, the client returns to the ship selection screen.
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

### 5. Subsystem Health Burst on Death

The death sequence includes a **subsystem health burst** for self-destruct kills but
**not** for collision kills:

#### Self-Destruct Death

Immediately before the ObjectExplodingEvent, the server sends **3 StateUpdate packets**
(flag 0x20) containing zeroed health values for all subsystems. This burst provides clients
with a definitive final state — all subsystems are at 0% health.

Observed in Ambassador (species 2) and Warbird (species 8) self-destruct deaths:
- 3 StateUpdate packets with flag 0x20
- All subsystem health values zeroed
- Burst arrives before the ObjectExplodingEvent (factory 0x8129)

#### Collision Kill Death

Collision kills do **not** produce a health burst. The ship dies without a final subsystem
state broadcast — the last subsystem health values the client received were from the most
recent round-robin StateUpdate cycle before death.

This means clients may display stale subsystem health for collision-killed ships during the
9.5-second explosion animation. This is a behavioral divergence from self-destruct, but it
matches stock server behavior.

#### Implementation Note

Servers should send the 3-packet health burst for self-destruct deaths but may omit it for
combat kills (collision, weapon, explosion) to match stock behavior.

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
   a. Create an ObjectExplodingEvent with the killer's player ID and lifetime=9.5s
   b. Serialize as PythonEvent (opcode 0x06, factory 0x8129)
   c. Send reliably to all clients
   d. Send Explosion (opcode 0x29) with position, damage, and radius
   e. Process scoring (kill/death/score updates, SCORE_CHANGE broadcast)
   f. Clear ship state (mark as dead) — do NOT auto-respawn
   g. Wait for the client to send ObjCreateTeam (0x03) when the player picks a new ship

2. Do NOT send DestroyObject (0x14) for ship deaths — clients do not expect it
3. Do NOT auto-respawn — the client initiates its own respawn

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

**All death types**: No server-initiated respawn. After the 9.5-second explosion animation,
the client returns to the ship selection screen. The client picks a new ship and sends
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
