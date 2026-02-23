# Server Computation Model

What the stock Bridge Commander dedicated server computes locally versus merely relays from clients. Understanding this split is essential for a reimplementation — the server is NOT a thin relay.

**Clean room statement**: This document describes server behavior as observable through protocol analysis, packet captures, and the game's shipped Python scripting API. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Architecture: Distributed Simulation with Server Authority

Bridge Commander multiplayer uses a **distributed simulation** model:

- **All peers** (server and every client) run the full game simulation: power, repair, shields, weapons, physics
- The **server's copy is authoritative**: its StateUpdate messages (opcode 0x1C, flag 0x20) override client state for subsystem health
- **Clients simulate locally** for responsiveness, then get corrected by server updates at ~10Hz per ship
- **Weapon damage** is the notable exception: fully peer-computed with no server authority

This is NOT lockstep or deterministic. Floating-point differences, timing variations, and independent computation mean peer states WILL diverge. The StateUpdate system provides **eventual consistency**, correcting drift approximately 10 times per second per ship.

---

## Server COMPUTES (Active Simulation)

### 1. Collision Damage Processing

When a client detects a collision and sends CollisionEffect (opcode 0x15), the server does NOT blindly relay. It:

1. **Validates ownership**: the sender must be one of the two colliding ships
2. **Checks for duplicates**: if the other ship already reported this collision, drops the message
3. **Validates proximity**: computes bounding sphere distance between the two ships and rejects reports where the gap exceeds a threshold (~26 game units)
4. **Recomputes damage**: runs the full damage pipeline on the server's own ship objects — shield absorption, per-subsystem damage distribution, hull damage

The server does NOT trust the client's damage values. It recomputes damage from the collision geometry using its own simulation state.

**Authority**: CLIENT detects collisions. SERVER validates proximity and recomputes all damage.

### 2. Power System (1-second tick)

The server runs the full power simulation every 1.0 seconds:

- **Battery recharge**: Main and backup batteries recharge from reactor output (main conduit is health-scaled, backup is not)
- **Power distribution**: Available power allocated to all consumers (shields, weapons, engines, sensors, cloak, repair)
- **Graceful degradation**: When demand exceeds supply, each consumer receives a fraction (efficiency = received/wanted), no hard cutoff

The power system runs for all human-controlled ships on the server. Results are broadcast via StateUpdate (flag 0x20), which includes battery levels in the power subsystem's health data.

**Authority**: SERVER computes power. Clients also simulate for responsiveness but are corrected by server StateUpdates.

### 3. Repair System

The repair system ticks on all peers (server and clients). The server's role is authoritative because:

1. The server generates **repair completion events** broadcast as PythonEvent (opcode 0x06):
   - `ET_ADD_TO_REPAIR_LIST` — subsystem added to repair queue
   - `ET_REPAIR_COMPLETED` — subsystem fully repaired
   - `ET_REPAIR_CANNOT_BE_COMPLETED` — subsystem destroyed mid-repair
2. The server's subsystem health (via StateUpdate flag 0x20) overrides client state

These are the ONLY repair-related PythonEvents the server generates. Clients receive them and update their Engineering panel UI.

**Authority**: ALL PEERS compute repair locally. SERVER broadcasts authoritative health + repair events.

### 4. Shield Recharge

Shield recharge runs on ALL peers via the power system's energy allocation. Each peer independently recharges its copy of each ship's shields.

**Important limitation**: Per-facing shield HP is NOT synchronized between peers. Only the overall shield subsystem condition (one byte) is included in StateUpdate. The six individual shield facings (forward, aft, port, starboard, dorsal, ventral) can diverge between peers.

**Authority**: DUAL — server is authoritative for overall shield health; per-facing distribution is peer-local.

### 5. StateUpdate Generation

The server generates StateUpdate (opcode 0x1C) messages for every player's ship:

- **Round-robin**: One peer updated per tick, rotating through all connected players
- **Dirty flags**: Only changed fields are serialized (position, orientation, speed, subsystem health)
- **Subsystem budget**: 10-byte budget per tick for subsystem health serialization, round-robin through the subsystem list
- **Forced absolute position**: Full position write every 1.0 seconds regardless of whether position changed (prevents delta drift)

The server reads ship state from its own simulation objects — these reflect the cumulative effect of all damage, repair, power, and collision processing.

**Authority**: SERVER is authoritative for subsystem health (flag 0x20). Position data is relayed from clients (flag 0x02/0x04).

### 6. PythonEvent Generation (Opcode 0x06)

The server generates PythonEvent messages from exactly two sources:

| Source | Event Types | Trigger |
|--------|------------|---------|
| Repair event handler | ET_ADD_TO_REPAIR_LIST, ET_REPAIR_COMPLETED, ET_REPAIR_CANNOT_BE_COMPLETED | Repair simulation ticks |
| Object exploding handler | ET_OBJECT_EXPLODING | Ship health reaches zero |

These events are serialized as opcode 0x06 and sent to all clients. They are freshly constructed messages from the server's own simulation, NOT relays of client messages.

**Authority**: SERVER generates these events. They are authoritative.

### 7. Explosion Damage (Opcode 0x29)

When a ship dies, the server generates an Explosion message sent to all clients. This carries area-of-effect damage parameters (damage amount, blast radius, position) that clients apply to nearby ships.

**Authority**: SERVER generates and broadcasts. Clients apply the damage locally.

---

## Server RELAYS (Passive Forwarding)

### 1. Weapon Combat

All weapon-related messages are relayed opaquely:

| Opcode | Name | Server Action |
|--------|------|---------------|
| 0x07 | StartFiring | Relay to all + apply locally |
| 0x08 | StopFiring | Relay to all + apply locally |
| 0x09 | StopFiringAtTarget | Relay to all + apply locally |
| 0x19 | TorpedoFire | Relay to all + create torpedo locally |
| 0x1A | BeamFire | Relay to all + apply beam locally |
| 0x1B | TorpTypeChange | Relay to all + apply locally |

The server relays these messages AND applies them to its own simulation (creating torpedoes, activating beams). However, **weapon damage is computed independently by each peer** — there is no server-side weapon damage validation or computation.

Each peer that receives a weapon event:
1. Creates the weapon effect (beam trace, torpedo projectile)
2. Independently detects hits against local copies of target ships
3. Independently computes damage through the full damage pipeline

This means weapon damage can diverge between peers. This is a known characteristic of the stock game.

### 2. Ship Physics (Position/Velocity)

Each client computes its own ship's physics locally and broadcasts position via StateUpdate. The server:
1. Receives the client's position StateUpdate
2. Applies the position to its local copy of the client's ship
3. Includes that position in StateUpdates sent to other clients

The server performs **no physics simulation** and **no position validation**. It trusts client-reported positions.

### 3. Player Actions (Generic Event Forward Group)

Opcodes 0x07 through 0x12 and 0x1B are in the "event forward" group. For each:
1. Server receives the message from the sender
2. Removes the sender from the relay group temporarily
3. Relays to all other peers in the group
4. Re-adds the sender
5. Also deserializes and applies the event locally on the server

These include: StartFiring, StopFiring, SubsysStatus (shield toggle), AddToRepairList, StartCloak, StopCloak, StartWarp, RepairListPriority, SetPhaserLevel, TorpTypeChange.

### 4. PythonEvent2 (Opcode 0x0D) — LOCAL ONLY

PythonEvent2 (opcode 0x0D) is processed **locally on the server only**. It is NOT relayed to other clients. The server deserializes the event and posts it to its local event system, but does not forward it.

This is distinct from PythonEvent (0x06), which the server generates fresh copies of from its own simulation. See [tgmessage-routing.md](../protocol/tgmessage-routing.md) for details.

---

## Summary Table

| System | Server Computes? | Server Relays? | Authority |
|--------|-----------------|----------------|-----------|
| **Collision damage** | YES — validates + recomputes full damage pipeline | Implicit via local application | SERVER |
| **Weapon damage** | NO — each peer computes independently | YES — fire/stop events relayed | EACH PEER |
| **StateUpdate** | YES — reads live state from server's ship objects | YES — host broadcasts to all | SERVER (health), CLIENT (position) |
| **Power system** | YES — battery recharge, power distribution | Via StateUpdate | SERVER |
| **Repair system** | YES — all peers repair; server generates events | Via StateUpdate + PythonEvent 0x06 | SERVER (health + events) |
| **Shield recharge** | YES — all peers recharge | Via StateUpdate (overall only) | DUAL (overall=server, per-facing=local) |
| **PythonEvent generation** | YES — repair events + death events | YES — opcode 0x06 to all | SERVER |
| **Ship physics** | NO — trusts client position data | YES — position relayed | CLIENT (own ship) |
| **Explosion damage** | YES — generates opcode 0x29 | YES — broadcast to all | SERVER |

---

## Implications for Reimplementation

### Must Implement (Server-Side)

1. **Full subsystem simulation**: Power (1s tick), repair (continuous), shield recharge (continuous)
2. **Collision damage validation**: Proximity check + full damage pipeline
3. **StateUpdate generation**: Round-robin subsystem health, position relay, dirty flags, 10-byte subsystem budget
4. **Repair event broadcasting**: Three repair event types as PythonEvent (0x06)
5. **Death detection**: When ship health reaches zero, generate ObjectExploding PythonEvent (0x06)
6. **Explosion broadcasting**: Generate opcode 0x29 with damage parameters

### Can Trust Clients

1. **Ship position/velocity**: Accept StateUpdates without validation
2. **Weapon fire commands**: Relay without validation
3. **Collision detection**: Accept collision reports, validate proximity only

### Design Principle

The stock server's approach is pragmatic: simulate everything locally for consistency, use the network for synchronization rather than authority. The server's StateUpdate flag 0x20 is the correction mechanism — it pushes authoritative subsystem health at ~10Hz, overriding any local drift on clients.

This means a reimplemented server cannot be "just a relay" — it must run the full game simulation to produce correct subsystem health values for StateUpdate broadcasting.

---

## Related Documents

- **[server-authority.md](server-authority.md)** — Authority model analysis with anti-cheat tiers
- **[../game-systems/collision-detection-system.md](../game-systems/collision-detection-system.md)** — How collision detection feeds into damage
- **[../game-systems/power-system.md](../game-systems/power-system.md)** — Power system details
- **[../game-systems/repair-system.md](../game-systems/repair-system.md)** — Repair system details
- **[../wire-formats/stateupdate-wire-format.md](../wire-formats/stateupdate-wire-format.md)** — StateUpdate field format
- **[../protocol/tgmessage-routing.md](../protocol/tgmessage-routing.md)** — Message routing and relay behavior
