# Collision Damage Event Chain

How collision damage generates network messages in Bridge Commander multiplayer, documented from observable behavior, network packet captures, and the game's shipped Python scripting API.

**Clean room statement**: This document describes the collision damage event chain as observable multiplayer behavior and network traffic patterns. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

When two ships collide in multiplayer, the host applies damage to subsystems and then broadcasts approximately **14 PythonEvent (opcode 0x06) messages** to all clients. These messages carry repair-list additions — one per damaged subsystem. Clients use these to update their repair queue UI and trigger visual/audio feedback.

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

1. **Validates** the collision (ownership check, proximity check — see [collision-effect-wire-format.md](collision-effect-wire-format.md))
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
- **REPAIR_COMPLETE** — when a subsystem finishes repairing
- **REPAIR_CANCELLED** — when a repair is cancelled

All three produce opcode 0x06 messages with identical framing; only the serialized event type differs.

### 7. Client Receives PythonEvent (0x06)

On the receiving client, the PythonEvent handler (opcode 0x06):

1. Deserializes the event from the message payload
2. Posts it to the local event system
3. Local handlers update the repair queue UI, play sound effects, etc.

---

## Why ~14 PythonEvents Per Collision

Observed in stock 3-player combat sessions:

- Two ships collide
- Each ship has ~7 top-level subsystems in the damage volume (shields, hull sections, weapons, etc.)
- Each damaged subsystem generates one SUBSYSTEM_HIT → one ADD_TO_REPAIR_LIST → one PythonEvent
- **7 subsystems x 2 ships = ~14 PythonEvent messages**

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

## Related Documents

- **[collision-effect-wire-format.md](collision-effect-wire-format.md)** — Opcode 0x15 wire format and host validation checks
- **[combat-system.md](combat-system.md)** — Full damage pipeline, shield absorption, subsystem damage distribution
- **[ship-subsystems.md](ship-subsystems.md)** — Subsystem index table and HP values
- **[server-authority.md](server-authority.md)** — Authority model (collision damage is host-authoritative)
- **[stateupdate-wire-format.md](stateupdate-wire-format.md)** — How subsystem health is sent in StateUpdate flag 0x20
- **[explosion-wire-format.md](explosion-wire-format.md)** — Opcode 0x29 wire format
