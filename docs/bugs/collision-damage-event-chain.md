# Collision Damage Event Chain

How collision damage generates network messages in Bridge Commander multiplayer, documented from observable behavior, network packet captures, and the game's shipped Python scripting API.

**Clean room statement**: This document describes the collision damage event chain as observable multiplayer behavior and network traffic patterns. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

When two ships collide in multiplayer, the host applies damage to subsystems and then broadcasts PythonEvent (opcode 0x06) messages to all clients. These messages carry repair-list additions — one per damaged subsystem. Clients use these to update their repair queue UI and trigger visual/audio feedback.

The event count depends on the collision type:
- **Two ships collide**: ~14 events (7 subsystems × 2 ships = ~14)
- **Ship collides with environment**: ~3-5 events (fewer subsystems in damage volume)
- **Collision-induced death**: ~13 events at death (all damaged subsystems get repair events)

The chain is entirely C++ engine-driven. Python scripting is not involved in generating these messages.

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
    ├──→ Posts SUBSYSTEM_HIT event
    │        │
    │        ▼
    │    Repair Subsystem catches SUBSYSTEM_HIT
    │        │
    │        ▼
    │    Adds subsystem to repair queue
    │        │
    │        ▼
    │    Posts ADD_TO_REPAIR_LIST event [host + MP only]
    │        │
    │        ▼
    │    HostEventHandler serializes as PythonEvent (0x06)
    │        │
    │        ▼
    │    Sent reliably to "NoMe" group (all peers except host)
    │
    ▼
(next subsystem...)
```

---

## Step-by-Step Chain

### 1. Collision Detection

The engine's proximity manager detects overlapping bounding spheres and runs narrow-phase collision tests. When a collision is confirmed, it posts a **COLLISION_EFFECT** event to the internal event system.

### 2. Ship Collision Handler (host path)

On the host, the ship's collision handler runs with full authority:

1. **Validates** the collision (ownership check, proximity check — see [collision-effect-wire-format.md](../wire-formats/collision-effect-wire-format.md))
2. **Forwards** the collision to all clients via opcode 0x15 (CollisionEffect) using the "NoMe" routing group
3. **Applies damage** locally by iterating over contact points

For collisions reported by clients (via opcode 0x15 arriving at the host), a separate handler validates the report and then feeds it into the same damage application path.

### 3. Per-Contact Damage Application

For each contact point in the collision:

1. The contact position is decompressed to a world-space Vec3
2. Subsystems whose bounding boxes overlap the damage volume are identified
3. Each overlapping subsystem receives damage proportional to `collision_force / contact_count`
4. The subsystem's **condition** (health) is reduced accordingly

### 4. Subsystem Condition Update

When a subsystem's condition changes (health decreases), the engine:

1. Stores the new condition value
2. Computes the condition ratio (current / maximum)
3. If the new condition is below maximum AND the ship is still alive:
   - Posts a **SUBSYSTEM_HIT** event to the internal event system
   - The event identifies both the ship and the specific damaged subsystem

This is the critical event that starts the network message chain.

### 5. Repair Subsystem Catches SUBSYSTEM_HIT

Each ship's repair subsystem registers a per-instance event handler for SUBSYSTEM_HIT during its initialization. When the event arrives:

1. The damaged subsystem is looked up from the event data
2. The subsystem is added to the repair queue (duplicates are rejected)
3. If the add succeeds **AND** the game is running as host **AND** multiplayer is active:
   - A new **ADD_TO_REPAIR_LIST** event is posted to the event system

The host+multiplayer gate is critical: in single-player or on clients, the repair queue is still updated locally, but no network event is generated.

### 6. HostEventHandler Serializes as PythonEvent (0x06)

The multiplayer game object registers a handler for ADD_TO_REPAIR_LIST events (among others). This handler:

1. Writes opcode byte `0x06` as the first byte
2. Serializes the event data (event type, source subsystem, target subsystem) into a buffer
3. Creates a reliable TGMessage containing the serialized data
4. Sends it to the **"NoMe"** routing group (all peers except the host itself)

The HostEventHandler also handles two other event types using the same pattern:
- **REPAIR_COMPLETE** — when a subsystem's condition reaches maximum (repair finished)
- **REPAIR_CANNOT_BE_COMPLETED** — when a subsystem is destroyed while in the repair queue (condition reaches 0.0)

All three produce opcode 0x06 messages with identical framing; only the serialized event type differs.

### 7. Client Receives PythonEvent (0x06)

On the receiving client, the PythonEvent handler (opcode 0x06):

1. Deserializes the event from the message payload
2. Posts it to the local event system
3. Local handlers update the repair queue UI, play sound effects, etc.

---

## Why ~14 PythonEvents Per Collision

Verified from stock 3-player combat packet captures (33.5-minute session, 84 collisions):

- Two ships collide
- Each ship has ~7 top-level subsystems in the damage volume (shields, hull sections, weapons, etc.)
- Each damaged subsystem generates one SUBSYSTEM_HIT → one ADD_TO_REPAIR_LIST → one PythonEvent
- **7 subsystems x 2 ships = ~14 PythonEvent messages**
- Exact per-collision counts of **12-14 confirmed** from trace analysis

The exact count varies with collision geometry (which subsystems overlap the damage volume) and whether subsystems are already in the repair queue (duplicates are rejected).

---

## Event Types in the Chain

| Event | Role | Where Generated |
|-------|------|-----------------|
| COLLISION_EFFECT | Starts the chain | Proximity manager |
| HOST_COLLISION_EFFECT | Variant for client-reported collisions | CollisionEffect (0x15) receiver |
| COLLISION_DAMAGE | Auto-repair trigger (internal) | Damage application |
| SUBSYSTEM_HIT | Per-subsystem damage notification | Condition update |
| ADD_TO_REPAIR_LIST | Triggers network broadcast | Repair subsystem (host+MP only) |
| OBJECT_EXPLODING | Ship destruction notification | Death handler (also produces 0x06) |

---

## Three Sources of Opcode 0x06

Not all PythonEvent messages come from the collision chain. The game has three distinct producers:

### 1. HostEventHandler

Registered for: ADD_TO_REPAIR_LIST, REPAIR_COMPLETE, REPAIR_CANCELLED

This is the collision damage path described above. Serializes the event and sends it reliably to the "NoMe" group.

### 2. ObjectExplodingHandler

Registered for: OBJECT_EXPLODING

When a ship is destroyed, this handler serializes the explosion event as opcode 0x06 and sends it to the "NoMe" group. Gated on multiplayer mode.

### 3. GenericEventForward (NOT opcode 0x06)

A shared function used by many handlers (StartFiring, StopFiring, SubsystemStatus, StartCloak, etc.) that forwards events to the "NoMe" group. However, this function writes the **specific opcode** for each event type (0x07, 0x08, 0x0A, 0x0E, 0x0F, 0x15, etc.), NOT 0x06. It shares the same send pattern but produces different opcodes.

---

## PythonEvent Relay Logic (opcode 0x06 receiver)

When the host receives a PythonEvent from a client (for client-originated events like script actions):

1. Looks up the "Forward" routing group
2. Temporarily removes the sender from the group
3. Forwards the message to all remaining group members
4. Re-adds the sender to the group
5. If the sender is not the host itself: also posts the event locally

Collision-damage PythonEvents do NOT use this relay path — they originate on the host and are sent directly to the "NoMe" group.

---

## Traffic Statistics (from 15-minute 3-player session)

| Direction | Count | Notes |
|-----------|-------|-------|
| PythonEvent S→C | ~251 | Repair list + explosions + script events |
| PythonEvent C→S | 0 | Clients never send 0x06 in the collision path |
| CollisionEffect C→S | ~84 | Client collision reports |

All collision-path PythonEvents are **host-generated, server-to-client only**.

---

## Required Event Registrations

For the collision → PythonEvent chain to function, three registrations must be active:

### 1. Repair Subsystem Per-Instance Registration

Each ship's repair subsystem must register handlers during its initialization:
- SUBSYSTEM_HIT → adds damaged subsystem to repair queue
- REPAIR_COMPLETE → cleanup
- SUBSYSTEM_DAMAGED → tracks damage state
- REPAIR_CANCELLED → cleanup

These are registered per ship instance (not globally) when the repair subsystem is created and attached to a ship. **Not gated on multiplayer** — always registered.

### 2. MultiplayerGame HostEventHandler Registration

The multiplayer game object registers during its construction:
- ADD_TO_REPAIR_LIST → serializes as opcode 0x06
- REPAIR_COMPLETE → serializes as opcode 0x06
- REPAIR_CANCELLED → serializes as opcode 0x06

**Gated on multiplayer mode** — only registered when a multiplayer game is active.

### 3. Ship Class Static Registration

Global (class-level) registration for collision processing:
- COLLISION_EFFECT → collision validation + damage application
- HOST_COLLISION_EFFECT → same, for client-reported collisions

Registered during class initialization (not per-instance).

If any of these registrations are missing, the chain breaks silently — damage may still apply, but no PythonEvent messages are generated, and clients see no repair queue updates.

---

## Subsystem Object ID Allocation Requirement

Subsystem TGObject IDs **must** be allocated from the owning player's object ID range for
clients to resolve damage events. Each player's ID range starts at
`base = 0x3FFFFFFF + N × 0x40000` (262,143 IDs per player).

When a TGSubsystemEvent carries a `source_obj_id`, the receiving client calls
`ReadObjectRef` to look up the subsystem in the TGObject hash table. If the subsystem's ID
is outside the player's range (e.g., a small sequential integer from a global counter),
the lookup fails silently and the damage event is dropped.

**Correct**: Subsystem IDs like `0x40000002`, `0x40000018` (in player 1's range)
**Incorrect**: Subsystem IDs like `0x00000010`, `0x0000001E` (global counter, client can't resolve)

This applies to all subsystem objects: the damaged subsystem's ID (source) and the
RepairSubsystem's ID (dest) must both be in-range.

---

## Implementation Notes

### Minimal Server (hull damage only)

A server that only tracks hull HP (no repair queue) can skip the PythonEvent chain entirely. Collision damage still applies through the damage pipeline; the PythonEvent messages only carry repair-queue notifications.

### Full Server (subsystem repair)

For a full implementation with repair queue synchronization:

1. When applying collision damage to a subsystem, check if its condition decreased
2. If so, add it to the ship's repair queue (reject duplicates)
3. If the add succeeds and this is the host in multiplayer:
   - Serialize an ADD_TO_REPAIR_LIST event as a PythonEvent (0x06)
   - Send reliably to all other peers

### Key Behavioral Invariant

The ADD_TO_REPAIR_LIST event is ONLY posted when all three conditions are true:
- The subsystem was successfully added to the queue (not a duplicate)
- The game is running as host (`IsHost == true`)
- Multiplayer is active (`IsMultiplayer == true`)

This prevents clients from generating spurious repair events and ensures only the host's authoritative damage decisions produce network traffic.

---

## Per-Ship Subsystem Event Verification (2026-02-22)

A 4-minute collision test session with one player cycling through 4 of the 16 flyable ships
revealed significant variation in subsystem event generation per ship species.

### Test Results

| Species | Ship | Death Type | TGSubsystemEvents | Health Burst | F5 Repair UI |
|---------|------|------------|-------------------|--------------|--------------|
| 5 | Sovereign | Collision kill | 1 | No | Not checked |
| 3 | Galaxy | Collision kill | 9 | No | Not checked |
| 2 | Ambassador | Self-destruct | 5 | Yes (3 pkts) | Not checked |
| 8 | Warbird | Self-destruct | 6 | Yes (3 pkts) | **Yes** |

### Key Findings

**1. First-Ship Subsystem Event Deficit**

The Sovereign was the first ship created in the session (obj=0x3FFFFFFF). It generated only
1 TGSubsystemEvent at collision death, compared to 5-9 for subsequent ships. This suggests
a registration or initialization timing issue specific to the first ship object created after
game start. See issue #61 for tracking.

**2. Subsystem IDs Now In Correct Player Range**

All TGSubsystemEvent source IDs were observed in the owning player's object ID range
(0x3FFFFFFF-0x400FFFFF for player 1). This confirms the subsystem ID allocation fix (#58)
is working — clients can now resolve damage events via ReadObjectRef.

**3. Health Burst Asymmetry (Self-Destruct vs. Collision Kill)**

Self-destruct deaths produce a "health burst" — 3 StateUpdate packets (flag 0x20) with all
subsystem health values zeroed. This burst appears immediately before the ObjectExplodingEvent.

Collision kills do **not** produce a health burst. The ship dies without a final subsystem
state broadcast. This is a behavioral divergence from self-destruct that may affect client-side
repair UI updates.

**4. End-to-End Chain Verified (Warbird)**

The Warbird (species 8, 4th ship) provided the first end-to-end verification of the full
chain: collision damage → subsystem condition update → repair queue addition → PythonEvent
broadcast → client F5 engineering screen showing damaged subsystems. This confirms the
entire pipeline from collision detection through client UI is functional.

### Per-Ship Detail

**Sovereign (species 5, obj=0x3FFFFFFF, 1st ship):**
- StateUpdate 0x20: Yes, round-robin indices 0,5,7,9
- TGSubsystemEvents at death: 1 (subsys=0x40000000, repair=0x4000000E)
- Health burst: No (collision kill)
- **Status: BROKEN** — first-ship deficit, 1 vs expected ~13

**Galaxy (species 3, obj=0x40000021, 2nd ship):**
- StateUpdate 0x20: Yes, round-robin indices 0,5,7,9,4,6+; values 0x0F, 0x40, 0xCC, 0x60
- TGSubsystemEvents at non-lethal collision: 2 (IDs 0x4000002E, 0x40000042)
- TGSubsystemEvents at death: 9 (IDs 0x2F-0x35, 0x40-0x41)
- Health burst: No (collision kill)
- **Status: BEST RESULT** — 9/~13 parity

**Ambassador (species 2, obj=0x40000044, 3rd ship):**
- StateUpdate 0x20: Yes; values 0xE5, 0x20, 0x66
- TGSubsystemEvents at self-destruct: 5 (IDs 0x45, 0x46, 0x48, 0x4D, 0x5D)
- Health burst: Yes (3 StateUpdate packets, all subsystems zeroed)
- **Status: PARTIAL** — 5/~13 parity

**Warbird (species 8, obj=0x40000062, 4th ship):**
- StateUpdate 0x20: Yes; values 0xBF, 0x40, 0x60, 0x6D, 0x1F
- TGSubsystemEvents at self-destruct: 6 (IDs 0x64, 0x65, 0x67, 0x69, 0x6E, 0x78)
- Health burst: Yes (3 StateUpdate packets, all subsystems zeroed)
- F5 Repair UI: **Confirmed working** — engineering screen showed damaged subsystems
- **Status: WORKING** — 6/~13 parity, full end-to-end chain verified

### Remaining Ships (12 Untested)

The following species have not yet been tested for subsystem event parity:
Akira (1), Nebula (4), Bird of Prey (6), Vorcha (7), Marauder (9), Galor (10),
Keldon (11), CardHybrid (12), KessokHeavy (13), KessokLight (14), Shuttle (15),
CardFreighter (16).

Per-ship tracking comments are maintained on issue #63.

---

## Related Documents

- **[collision-effect-wire-format.md](../wire-formats/collision-effect-wire-format.md)** — Opcode 0x15 wire format and host validation checks
- **[combat-system.md](../game-systems/combat-system.md)** — Full damage pipeline, shield absorption, subsystem damage distribution
- **[ship-subsystems/](../game-systems/ship-subsystems/)** — Subsystem index table and HP values
- **[server-authority.md](../architecture/server-authority.md)** — Authority model (collision damage is host-authoritative)
- **[stateupdate-wire-format/](../wire-formats/stateupdate-wire-format/)** — How subsystem health is sent in StateUpdate flag 0x20
- **[explosion-wire-format.md](../wire-formats/explosion-wire-format.md)** — Opcode 0x29 wire format
