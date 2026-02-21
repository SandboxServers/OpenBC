# PythonEvent Wire Format (Opcode 0x06)

Wire format specification for Star Trek: Bridge Commander's primary event forwarding
message, documented from network packet captures and the game's shipped Python
scripting API.

**Clean room statement**: This document describes observable multiplayer behavior and
network traffic patterns. No binary addresses, memory offsets, or decompiled code are
referenced.

---

## Overview

Opcode 0x06 (PythonEvent) is a **polymorphic serialized-event transport**. The host
broadcasts game events to all clients using this opcode. The payload after the opcode
byte is a serialized event object. The **first 4 bytes** of the payload are a factory
type ID that determines which event class follows — different event types carry
different fields.

This is the primary mechanism for synchronizing repair-list changes, explosion
notifications, and forwarded script events in multiplayer.

**Direction**: Host → All Clients (via "NoMe" routing group)
**Reliability**: Sent reliably (ACK required)
**Frequency**: ~251 per 15-minute 3-player combat session (~3,432 total in a 34-minute
session — the most frequently sent game opcode)

### Opcode 0x0D (PythonEvent2)

Opcode 0x0D shares the same receiver logic and has identical wire format. Both opcodes
are decoded identically; 0x0D provides an alternate event path.

---

## Message Structure

```
Offset  Size  Type    Field          Notes
------  ----  ----    -----          -----
0       1     u8      opcode         Always 0x06
1       4     i32     factory_id     Event class type (determines remaining layout)
5       4     i32     event_type     Event type constant (0x008000xx)
9       4     i32     source_obj_id  Source object reference
13      4     i32     dest_obj_id    Destination/related object reference
[class-specific extension follows]
```

The first 17 bytes are common to all event classes (base event fields). The payload
after byte 16 depends on `factory_id`.

All multi-byte values are **little-endian**.

---

## Object Reference Encoding

Object reference fields use a three-way encoding:

| Wire Value | Meaning |
|-----------|---------|
| `0x00000000` | NULL (no object) |
| `0xFFFFFFFF` | Sentinel (-1, "self" reference) |
| Any other value | Network object ID |

### Ship Object IDs

Ship object IDs follow a player-based allocation: Player N base = `0x3FFFFFFF + (N * 0x40000)`.

### Subsystem Object IDs

Subsystems do **not** derive their IDs from the ship's base ID. Each subsystem receives
a globally unique ID from a sequential auto-increment counter at construction time. The
assignment order depends on the ship's subsystem creation sequence, which varies by ship
class.

This means:
- Subsystem IDs are **not predictable** from the ship's ID alone
- The only way to resolve a subsystem ID is via hash table lookup on the receiving end
- All subsystem IDs are assigned before the ship enters the game, so they are stable
  for the lifetime of that ship instance
- When a ship is destroyed and respawned, its subsystems receive new IDs from the counter

A server implementation must maintain a mapping of subsystem IDs to subsystem objects,
populated when the ship's subsystem list is created during InitObject deserialization.

---

## Three Event Classes

### 1. SubsystemEvent (factory_id = 0x00000101)

The most common event in collision traffic. No extension fields beyond the base event —
factory_id + event_type + two object references is the complete payload.

```
Offset  Size  Type    Field            Notes
------  ----  ----    -----            -----
0       1     u8      opcode           0x06
1       4     i32     factory_id       0x00000101
5       4     i32     event_type       See table below
9       4     i32     source_obj_id    Damaged subsystem (subsystem's own object ID)
13      4     i32     dest_obj_id      Repair subsystem that queued it (repair sub's object ID)
```

**Total**: 17 bytes (fixed).

Both object references are **subsystem-level IDs**, not ship IDs. See "Subsystem Object
IDs" above for how these are allocated.

**Event types**:

| Event Type | Name | Meaning |
|-----------|------|---------|
| `0x008000DF` | ADD_TO_REPAIR_LIST | Subsystem damaged, added to repair queue |
| `0x00800074` | REPAIR_COMPLETED | Subsystem condition reached max (repair finished) |
| `0x00800075` | REPAIR_CANNOT_BE_COMPLETED | Subsystem destroyed while in repair queue (condition reached 0.0) |

### 2. CharEvent (factory_id = 0x00000105)

Extends SubsystemEvent with a single byte payload. This class is primarily used by
opcodes 0x07-0x12 and 0x1B (weapon, cloak, and warp events) rather than opcode 0x06.
Documented here because the polymorphic deserializer handles any registered factory
type.

```
Offset  Size  Type    Field            Notes
------  ----  ----    -----            -----
0       1     u8      opcode           0x06
1       4     i32     factory_id       0x00000105
5       4     i32     event_type       Depends on specific event
9       4     i32     source_obj_id    Source object
13      4     i32     dest_obj_id      Related object
17      1     u8      char_value       Single-byte payload
```

**Total**: 18 bytes (fixed).

See [set-phaser-level-wire-format.md](set-phaser-level-wire-format.md) for detailed
analysis of CharEvent usage.

### Class Hierarchy

```
Event (base, factory 0x02)
  └── SubsystemEvent (factory 0x101)
        └── CharEvent (factory 0x105)
```

The `IsA` check reports true for all IDs in the ancestry chain.

### 3. ObjectExplodingEvent (factory_id = 0x00008129)

Carries ship destruction notifications. Extends the base event with a firing player
ID (who killed the ship) and an explosion lifetime (visual effect duration).

```
Offset  Size  Type    Field              Notes
------  ----  ----    -----              -----
0       1     u8      opcode             0x06
1       4     i32     factory_id         0x00008129
5       4     i32     event_type         Always 0x0080004E (OBJECT_EXPLODING)
9       4     i32     source_obj_id      Object that is exploding
13      4     i32     dest_obj_id        Target (typically NULL or sentinel)
17      4     i32     firing_player_id   Connection ID of the killer
21      4     f32     lifetime           Explosion effect duration (seconds)
```

**Total**: 25 bytes (fixed).

---

## Three Producers

Not all PythonEvent messages come from the same source. The game has three distinct
event-to-network pathways:

### 1. Host Event Handler

**Registered for**: ADD_TO_REPAIR_LIST, REPAIR_COMPLETED, REPAIR_CANNOT_BE_COMPLETED

This is the collision damage pathway. When a subsystem is damaged and added to the
repair queue, the handler serializes the event and sends it reliably to the "NoMe"
routing group (all peers except the host itself).

**Gate condition**: Only registered when multiplayer is active.

**Event generation chain**:
1. Subsystem condition decreases below maximum → posts SUBSYSTEM_HIT (internal only)
2. Repair subsystem catches SUBSYSTEM_HIT, adds to repair queue (rejects duplicates)
3. If add succeeded AND host AND multiplayer → posts ADD_TO_REPAIR_LIST
4. Host Event Handler catches ADD_TO_REPAIR_LIST → serializes as opcode 0x06

### 2. Object Exploding Handler

**Registered for**: OBJECT_EXPLODING (0x0080004E)

When a ship is destroyed, this handler serializes the explosion event and sends it
reliably to the "NoMe" group. In single-player, it directly triggers the explosion
visual effect instead.

**Gate condition**: Always registered, but internally checks multiplayer flag. In
multiplayer: serialize + send. In single-player: apply locally.

### 3. Generic Event Forward (NOT opcode 0x06)

Used by many other handlers (StartFiring, StopFiring, SubsystemStatus, StartCloak,
etc.) that forward events over the network. This pathway writes the **specific opcode**
for each event type (0x07, 0x08, 0x0A, etc.), **NOT** 0x06. Included here only to
clarify that it is NOT a source of PythonEvent messages despite sharing the same
serialization mechanism.

---

## Receiver Behavior

### Opcode 0x06 / 0x0D Receiver

When a client receives a PythonEvent:

1. **Skip** the opcode byte (0x06)
2. **Read** the factory type ID (first 4 bytes of payload)
3. **Construct** the correct event class using the factory registry
4. **Deserialize** remaining fields via the class-specific reader
5. **Resolve** object references (network IDs → local objects)
6. **Dispatch** the event through the local event system

Local event handlers then process the event — for example, updating the repair queue
UI, playing explosion sound effects, or triggering visual feedback.

**No relay**: The opcode 0x06 receiver does NOT relay the message. It only deserializes
and dispatches locally. PythonEvents originate on the host and are sent directly to
clients — no further forwarding is needed.

### Client-to-Host Path (rare)

If a client sends an opcode 0x06 message to the host (script-initiated events), the
host will:
1. Relay the message to all other connected clients (excluding sender)
2. Also process the event locally

This relay path ensures all peers see script events regardless of origin.

### Opcodes 0x07-0x12, 0x1B (Generic Event Forward)

These opcodes use a **different receiver** that performs both relay and dispatch. The
receiver applies an event type override after deserialization — implementing the
sender/receiver event code pairing system.

#### Event Type Override Table

| Opcode | Name | Sender Code | Receiver Override |
|--------|------|-------------|-------------------|
| 0x07 | StartFiring | 0x008000D8 | 0x008000D7 |
| 0x08 | StopFiring | 0x008000DA | 0x008000D9 |
| 0x09 | StopFiringAtTarget | 0x008000DC | 0x008000DB |
| 0x0A | SubsystemStatusChanged | 0x0080006C | 0x0080006C (no change) |
| 0x0B | AddToRepairList | 0x008000DF | Preserve original |
| 0x0C | ClientEvent | (varies) | Preserve original |
| 0x0E | StartCloaking | 0x008000E2 | 0x008000E3 |
| 0x0F | StopCloaking | 0x008000E4 | 0x008000E5 |
| 0x10 | StartWarp | 0x008000EC | 0x008000ED |
| 0x11 | RepairListPriority | 0x00800076 | Preserve original |
| 0x12 | SetPhaserLevel | 0x008000E0 | Preserve original |
| 0x1B | TorpedoTypeChange | 0x008000FE | 0x008000FD |

"Preserve original" means the event arrives with its wire event code unchanged. Opcodes
with an override replace the event code after deserialization — this implements
sender/receiver asymmetry (e.g., a sender locally posts "start firing notify" but
receivers post "start firing command").

---

## Collision Damage → PythonEvent Chain

When two ships collide in multiplayer, the host generates approximately **14 PythonEvent
(opcode 0x06) messages**:

```
Collision Detection
    │
    ▼
Host Validates Collision (ownership, proximity, dedup)
    │
    ▼
Per-Contact Damage Application
    │
    ▼
Per-Subsystem Condition Update (SetCondition)
    │
    ├──→ Posts SUBSYSTEM_HIT event (internal)
    │        │
    │        ▼
    │    Repair Subsystem catches SUBSYSTEM_HIT
    │        │
    │        ▼
    │    Adds subsystem to repair queue (rejects duplicates)
    │        │
    │        ▼
    │    Posts ADD_TO_REPAIR_LIST [host + MP only]
    │        │
    │        ▼
    │    Host Event Handler serializes as PythonEvent (0x06)
    │        │
    │        ▼
    │    Sent reliably to "NoMe" group
    │
    ▼
(next subsystem...)
```

### Why ~14 Messages

- Two ships collide → each takes damage
- Each ship has ~7 top-level subsystems in the damage volume (shields, hull sections,
  weapons, etc.)
- Each damaged subsystem → one ADD_TO_REPAIR_LIST → one PythonEvent
- 7 subsystems × 2 ships = ~14 PythonEvent messages

The exact count varies with collision geometry and whether subsystems are already in the
repair queue (duplicates are rejected).

### Key Behavioral Invariant

ADD_TO_REPAIR_LIST is ONLY posted when **all three conditions** are true:
- The subsystem was successfully added to the queue (not a duplicate)
- The game is running as host
- Multiplayer is active

This prevents clients from generating spurious repair events and ensures only the host's
authoritative damage decisions produce network traffic.

---

## Decoded Packet Examples

### Example 1: ADD_TO_REPAIR_LIST (17 bytes)

```
06                    opcode = 0x06 (PythonEvent)
01 01 00 00           factory_id = 0x00000101 (SubsystemEvent)
DF 00 80 00           event_type = 0x008000DF (ADD_TO_REPAIR_LIST)
2A 00 00 00           source_obj = 0x0000002A (damaged subsystem's object ID)
1E 00 00 00           dest_obj = 0x0000001E (repair subsystem's object ID)
```

Note: subsystem object IDs are small sequential integers from the global counter, not
player-base IDs like ship objects.

### Example 2: OBJECT_EXPLODING (25 bytes)

```
06                    opcode = 0x06 (PythonEvent)
29 81 00 00           factory_id = 0x00008129 (ObjectExplodingEvent)
4E 00 80 00           event_type = 0x0080004E (OBJECT_EXPLODING)
FF FF FF 3F           source_obj = 0x3FFFFFFF (Player 0's ship, exploding)
FF FF FF FF           dest_obj = sentinel (-1)
02 00 00 00           firing_player_id = 2 (killed by player 2)
00 00 80 3F           lifetime = 1.0f (1 second explosion)
```

### Example 3: REPAIR_COMPLETED (17 bytes)

```
06                    opcode = 0x06 (PythonEvent)
01 01 00 00           factory_id = 0x00000101 (SubsystemEvent)
74 00 80 00           event_type = 0x00800074 (REPAIR_COMPLETED)
2A 00 00 00           source_obj = 0x0000002A (repaired subsystem's object ID)
1E 00 00 00           dest_obj = 0x0000001E (repair subsystem's object ID)
```

---

## Traffic Statistics (15-minute 3-player session)

| Direction | Count | Notes |
|-----------|-------|-------|
| PythonEvent S→C | ~251 | Repair list + explosions + script events |
| PythonEvent C→S | 0 | Clients never send 0x06 in the collision path |
| CollisionEffect C→S | ~84 | Client collision reports (opcode 0x15) |

All collision-path PythonEvents are **host-generated, server-to-client only**.

---

## Required Event Registrations

For the collision → PythonEvent chain to function, three registration levels must be active:

### 1. Repair Subsystem Per-Instance Registration

Each ship's repair subsystem must register handlers during its initialization:
- SUBSYSTEM_HIT → adds damaged subsystem to repair queue
- REPAIR_COMPLETED → cleanup
- SUBSYSTEM_DAMAGED → tracks damage state
- REPAIR_CANCELLED → cleanup

Registered per ship instance (not globally). **Not gated on multiplayer** — always
registered.

### 2. Multiplayer Game Host Event Handler Registration

The multiplayer game object registers during construction:
- ADD_TO_REPAIR_LIST → serializes as opcode 0x06
- REPAIR_COMPLETED → serializes as opcode 0x06
- REPAIR_CANCELLED → serializes as opcode 0x06
- OBJECT_EXPLODING → serializes as opcode 0x06

**Gated on multiplayer mode** — only registered when a multiplayer game is active.

### 3. Ship Class Static Registration

Global (class-level) registration for collision processing:
- COLLISION_EFFECT → collision validation + damage application
- HOST_COLLISION_EFFECT → same, for client-reported collisions

Registered during class initialization (not per-instance).

If any registration level is missing, the chain breaks silently — damage may still
apply, but no PythonEvent messages are generated, and clients see no repair queue
updates.

---

## Server Implementation Notes

### Minimal Server (hull damage only)

A server that only tracks hull HP (no repair queue) can skip PythonEvent generation
entirely. Collision damage still applies through the damage pipeline; PythonEvent
messages only carry repair-queue notifications and explosion effects.

### Full Server (subsystem repair + explosions)

For a full implementation:

1. **Repair events**: When applying collision damage to a subsystem:
   a. Check if condition decreased below maximum
   b. If so, add to the ship's repair queue (reject duplicates)
   c. If the add succeeds and this is the host in multiplayer:
      - Serialize a SubsystemEvent (factory 0x101) with event type ADD_TO_REPAIR_LIST
      - Send reliably to all other peers via the "NoMe" routing group

2. **Explosion events**: When a ship is destroyed:
   a. Serialize an ObjectExplodingEvent (factory 0x8129) with the killer's player ID
      and explosion duration
   b. Send reliably to all other peers via "NoMe"

3. **Repair completion/cancellation**: When repair finishes or is cancelled:
   - Same pattern as repair events with the appropriate event type

### Serialization Pattern (all producers)

All three producers use the same message construction:
1. Write opcode byte `0x06`
2. Serialize the event object (factory_id + event_type + object refs + class extensions)
3. Wrap in a reliable message
4. Send to "NoMe" routing group

### Client Relay

If a client sends an opcode 0x06 to the host (script events), the host should:
1. Forward to all other peers (excluding sender)
2. Process locally

---

## Related Documents

- **[collision-damage-event-chain.md](collision-damage-event-chain.md)** — Complete collision → PythonEvent chain
- **[collision-effect-wire-format.md](collision-effect-wire-format.md)** — Opcode 0x15 (client collision reports)
- **[collision-detection-system.md](collision-detection-system.md)** — 3-tier collision detection pipeline
- **[set-phaser-level-wire-format.md](set-phaser-level-wire-format.md)** — CharEvent (0x105) detailed analysis
- **[explosion-wire-format.md](explosion-wire-format.md)** — Opcode 0x29 (Explosion damage)
- **[combat-system.md](combat-system.md)** — Full damage pipeline, shields, subsystem distribution
- **[ship-subsystems.md](ship-subsystems.md)** — Subsystem index table and HP values
- **[server-authority.md](server-authority.md)** — Authority model (collision damage is host-authoritative)
- **[stateupdate-wire-format.md](stateupdate-wire-format.md)** — Subsystem health in StateUpdate flag 0x20
