# Repair System — Behavioral Specification

How Bridge Commander's repair system works: queue management, repair rate formula, priority toggling, wire formats, and Engineering panel behavior. This document describes observable behavior and game script values only.

**Clean room statement**: This document describes repair system mechanics as observable in-game behavior, readable game scripts, and wire protocol analysis. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

Each ship has a single repair subsystem that manages a queue of damaged subsystems. The repair system continuously repairs subsystems from the front of the queue, with multiple repair teams working simultaneously. Players interact via the Engineering panel to prioritize which subsystems are repaired first.

---

## 1. Repair Queue

### Data Structure

The repair queue is a **doubly-linked list** with no fixed maximum size. It grows dynamically as subsystems are damaged.

### Queue Operations

| Operation | Behavior |
|-----------|----------|
| **Add** | New subsystems are inserted at the **tail** of the queue |
| **Remove** | Subsystems are removed when fully repaired, destroyed, or manually removed |
| **Duplicate check** | The queue is walked before adding; duplicates are silently rejected |
| **Destroyed exclusion** | Subsystems with 0 HP are NOT added to the queue; instead, the UI is notified to display them as "destroyed" |

### Logical Partitions

The queue has two logical zones determined by `num_repair_teams`:

| Zone | Positions | Status |
|------|-----------|--------|
| **Active** | First `num_repair_teams` items from head | Being actively repaired |
| **Waiting** | Remaining items after active zone | Queued but not yet receiving repair |

---

## 2. Repair Rate Formula

```
raw_repair = max_repair_points * repair_system_health_pct * dt

divisor = min(queue_count, num_repair_teams)

per_subsystem = raw_repair / divisor

condition_gain = per_subsystem / subsystem_repair_complexity
```

### Key Characteristics

1. **Repair system health scales output**: A damaged repair bay repairs slower. At 50% health, repair output is halved.
2. **Multiple subsystems repaired simultaneously**: Up to `num_repair_teams` subsystems are repaired each tick.
3. **Equal division**: The raw repair amount is split equally among all active repair slots (`min(queue_count, num_repair_teams)`).
4. **RepairComplexity divisor**: Each subsystem has a complexity value that further reduces repair speed. Higher complexity = slower repair.
5. **Destroyed subsystems are skipped**: If a queued subsystem reaches 0 HP, it is not repaired. Instead, a "cannot be completed" notification is sent. It does NOT consume a repair team slot.

### Example (Sovereign class, healthy repair system, 2 items queued)

```
raw_repair = 50.0 * 1.0 * 0.033 = 1.65 per tick (at 30fps)
divisor = min(2, 3) = 2
per_subsystem = 1.65 / 2 = 0.825

For a sensor (complexity=1.0): condition_gain = 0.825 / 1.0 = 0.825 HP/tick
For a phaser (complexity=3.0): condition_gain = 0.825 / 3.0 = 0.275 HP/tick
For a tractor (complexity=7.0): condition_gain = 0.825 / 7.0 = 0.118 HP/tick
```

---

## 3. Priority Toggle Algorithm

When a player clicks a subsystem in the Engineering repair panel, a priority toggle occurs:

| Current Position | Action |
|-----------------|--------|
| **Active zone** (being repaired) | **Demote**: Remove from current position, re-insert at **tail** |
| **Waiting zone** (not being repaired) | **Promote**: Remove from current position, re-insert at **head** |

This is a **binary toggle** — there is no "move up one position" or gradual reordering. Clicking always jumps to the front or the back.

### "Being Repaired" Check

A subsystem is considered "being repaired" if it appears within the first `num_repair_teams` positions from the queue head. The check walks forward from head, counting up to `num_repair_teams` nodes.

---

## 4. Queue Auto-Management

### Auto-Add on Damage

When a subsystem takes damage (its condition drops below max_condition), the repair system automatically adds it to the queue:

1. Damage event fires (subsystem_hit)
2. Repair subsystem catches the event
3. Calls add-to-queue with duplicate rejection
4. If added AND this is the host in multiplayer: broadcasts notification to all clients

### Auto-Remove on Full Repair

During the repair tick, when a subsystem's condition reaches max_condition:

1. A "repair completed" event is posted
2. The handler removes the subsystem from the queue
3. UI is refreshed

### Destroyed Subsystem Handling

If a subsystem reaches 0 HP while in the repair queue:

1. During the repair tick, the destroyed subsystem is detected (condition <= 0)
2. A "repair cannot be completed" event is posted
3. The handler removes the subsystem from the queue
4. UI moves the item to the "destroyed" display area (vs just removing it)

### Rebuilt Subsystem Re-Queue

When a destroyed subsystem is rebuilt (e.g. via script command):

1. A "subsystem rebuilt" event fires
2. The handler checks if condition < max_condition
3. If so, re-queues the subsystem for continued repair

---

## 5. Engineering Panel UI

### Three Display Areas

| Area | Content | Click Action |
|------|---------|-------------|
| **Repair Area** | Subsystems being actively repaired (first `num_repair_teams`) | Click → demote to tail |
| **Waiting Area** | Queued but waiting for a free repair team | Click → promote to head |
| **Destroyed Area** | Subsystems at 0 HP | No action |

### Player Ship Tracking

The Engineering panel tracks whichever ship the local player controls. When the player's ship changes (e.g. spectator mode, game start), the panel:

1. Clears all displayed items
2. Points to the new ship's repair subsystem
3. Sets the number of active repair slots from the new ship's `num_repair_teams`

---

## 6. Wire Protocol

### Three Network Paths

Repair events use three distinct opcodes depending on direction and purpose:

#### Path 1: PythonEvent (opcode 0x06) — Host Auto-Notifications

**Direction**: Host → All Clients (reliable)

The host generates these automatically during the repair tick. They use the `SubsystemEvent` serialization (factory type `0x0101`).

| Event | Meaning |
|-------|---------|
| ADD_TO_REPAIR_LIST | Subsystem was damaged and added to repair queue |
| REPAIR_COMPLETED | Subsystem reached max HP, removed from queue |
| REPAIR_CANNOT_BE_COMPLETED | Subsystem destroyed while in queue |

**Wire format** (17 bytes fixed):
```
Offset  Size  Type    Field            Notes
------  ----  ----    -----            -----
0       1     u8      opcode           0x06
1       4     i32     factory_id       0x00000101
5       4     i32     event_type       Event constant (see table above)
9       4     i32     source_obj_id    Damaged subsystem's network object ID
13      4     i32     dest_obj_id      Repair subsystem's network object ID
```

Both source and dest contain **subsystem-level object IDs** (globally unique, auto-assigned at construction), NOT ship IDs. These are resolved on the receiving end via the global object hash table.

#### Path 2: AddToRepairList (opcode 0x0B) — Client Manual Repair Request

**Direction**: Client → Host → All (relayed via GenericEventForward)
**Event type**: preserved (no override)

Sent when a player manually requests repair of a subsystem. Uses `CharEvent` serialization (factory type `0x0105`).

**Wire format** (18 bytes fixed):
```
Offset  Size  Type    Field            Notes
------  ----  ----    -----            -----
0       1     u8      opcode           0x0B
1       4     i32     factory_id       0x00000105
5       4     i32     event_type       ADD_TO_REPAIR_LIST
9       4     i32     source_obj_id    Source object
13      4     i32     dest_obj_id      Related object
17      1     u8      char_value       Extra data
```

#### Path 3: RepairListPriority (opcode 0x11) — Client Priority Toggle

**Direction**: Client → Host → All (relayed via GenericEventForward)
**Event type**: REPAIR_INCREASE_PRIORITY (preserved, no override)

Sent when a player clicks a subsystem in the repair queue to change its priority.

**Wire format** (18 bytes fixed):
```
Offset  Size  Type    Field            Notes
------  ----  ----    -----            -----
0       1     u8      opcode           0x11
1       4     i32     factory_id       0x00000105
5       4     i32     event_type       REPAIR_INCREASE_PRIORITY
9       4     i32     source_obj_id    Source object
13      4     i32     dest_obj_id      Related object
17      1     u8      char_value       Subsystem ID
```

### GenericEventForward Relay Pattern

Opcodes 0x0B and 0x11 use the same relay mechanism as other GenericEventForward opcodes (0x07-0x12, 0x1B):

1. Client sends to host
2. Host removes sender from "Forward" group temporarily
3. Host forwards message to remaining group members
4. Host re-adds sender
5. Host deserializes and dispatches locally

Both opcodes have event type override = 0 (preserve original), meaning the event type from the wire is used directly on the receiving end.

### Singleplayer Gate

The local `HandleAddToRepairList` handler has a multiplayer gate — it **only runs in singleplayer**. In multiplayer, the opcode 0x0B network path handles repair list additions instead. This prevents double-processing.

---

## 7. Event Types

| Event | Direction | Trigger |
|-------|-----------|---------|
| ADD_TO_REPAIR_LIST | Host → All (opcode 0x06) | Subsystem damaged and auto-queued |
| REPAIR_COMPLETED | Host → All (opcode 0x06) | Subsystem reached max HP |
| REPAIR_CANNOT_BE_COMPLETED | Host → All (opcode 0x06) | Subsystem destroyed while queued |
| REPAIR_INCREASE_PRIORITY | Client → Host (opcode 0x11) | Player clicked priority toggle |
| SUBSYSTEM_HIT | Internal only | Damage triggers auto-add to queue |
| SUBSYSTEM_DAMAGED | Internal only | General damage tracking |
| SET_PLAYER | Internal only | Player's ship changed, reconfigure UI |

---

## 8. Collision → Repair Chain

When two ships collide, the following chain generates approximately **14 repair queue events**:

1. Collision detected → collision damage applied to both ships
2. Each damaged subsystem's condition drops below max → SUBSYSTEM_HIT event fires
3. Repair subsystem catches SUBSYSTEM_HIT → adds subsystem to queue (rejects duplicates)
4. If add succeeds on host: posts ADD_TO_REPAIR_LIST event
5. HostEventHandler serializes as opcode 0x06 → sends reliably to all clients
6. Each client deserializes, resolves object IDs via hash table, adds to local queue

**Per collision**: ~7 subsystems per ship x 2 ships = ~14 PythonEvent messages. Exact count varies with collision geometry and duplicate rejection.

---

## 9. Server Implementation Notes

### Host-Only Repair Processing

The repair tick (Update function) is **gated on host or standalone**:
- Standalone (not multiplayer): always processes repairs
- Multiplayer host: processes repairs
- Multiplayer client: does NOT process repairs (relies on host notifications)

### Event Broadcasting

The host broadcasts three event types via HostEventHandler (opcode 0x06):
- ADD_TO_REPAIR_LIST — so clients update their local queues
- REPAIR_COMPLETED — so clients remove completed items
- REPAIR_CANNOT_BE_COMPLETED — so clients show destroyed indicators

All three are sent reliably (ACK required).

### Object ID Resolution

Repair events reference subsystems by their globally unique network object IDs. These are NOT derived from ship IDs — they are sequential values assigned at construction time from a global auto-increment counter. The receiving end resolves IDs via a global hash table lookup.

A server implementation must:
1. Assign unique object IDs to every subsystem at ship creation time
2. Maintain a lookup table mapping ID → subsystem object
3. Use these IDs in all repair-related events

---

## 10. Ship Reference Values

### Sovereign-Class Repair Properties

| Property | Value |
|----------|-------|
| MaxRepairPoints | 50.0 |
| NumRepairTeams | 3 |
| Repair MaxCondition | 8,000 |
| Repair RepairComplexity | 1.0 |

### Subsystem RepairComplexity Values

| Subsystem | RepairComplexity | Effect |
|-----------|-----------------|--------|
| Sensor Array | 1.0 | Fastest repair (no divisor penalty) |
| Repair System | 1.0 | Fastest |
| Warp Core | 2.0 | 2x slower than complexity 1.0 |
| Hull | 3.0 | 3x slower |
| Impulse Engines | 3.0 | 3x slower |
| Bridge | 4.0 | 4x slower |
| Tractor System | 7.0 | Slowest (7x penalty) |
| Tractor Projector | 7.0 | Slowest |

Higher RepairComplexity = proportionally slower repair for that subsystem. The complexity value acts as a direct divisor on the repair points received.

---

## Related Documents

- [combat-system.md](combat-system.md) — Consolidated combat system spec (shields, weapons, cloak, tractor, repair summary)
- [collision-damage-event-chain.md](collision-damage-event-chain.md) — Collision → damage → repair event chain
- [collision-shield-interaction.md](collision-shield-interaction.md) — Shield absorption during collisions
- [pythonevent-wire-format.md](pythonevent-wire-format.md) — PythonEvent (opcode 0x06) polymorphic transport
- [set-phaser-level-wire-format.md](set-phaser-level-wire-format.md) — GenericEventForward pattern reference
- [ship-subsystems.md](ship-subsystems.md) — Full subsystem catalog with HP and properties
