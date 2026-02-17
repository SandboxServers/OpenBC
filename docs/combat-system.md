# Combat System Specification

This document describes the combat mechanics implemented in Bridge Commander multiplayer: damage processing, shields, weapons, cloaking, tractor beams, and the repair system.

**Clean room statement**: This document describes combat mechanics as implemented in `src/game/combat.c` and observable in-game behavior. No binary addresses, memory offsets, or decompiled code are referenced.

---

## 1. Damage Pipeline

All damage flows through a common pipeline with three entry points:

```
Collision Impact ──┐
                   │
Weapon Hit ────────┼──→ Apply Damage ──→ Shield Absorption ──→ Hull Damage ──→ Subsystem Damage
                   │
Explosion (0x29) ──┘
```

### Gate Conditions

Damage is silently dropped if either of these conditions is not met:
- The target ship must have a valid scene graph (visual representation loaded)
- The target ship must have a valid damage target (subsystem list populated)

Both conditions are always true for properly initialized ships. They protect against damage applied to ships that are still loading or being destroyed.

### Damage Authority

- **Collision damage**: Host-authoritative. Computed on the host, distributed to all clients via CollisionEffect (0x15) and Explosion (0x29)
- **Weapon damage**: Each client computes weapon hits independently (receiver-local hit detection)
- **Subsystem health**: Server-authoritative. Sent in StateUpdate flag 0x20

---

## 2. Shield System

### Facings

Ships have 6 shield facings, each with independent hit points:

| Index | Facing | Ship-Local Axis |
|-------|--------|-----------------|
| 0 | Front | +Y direction |
| 1 | Rear | -Y direction |
| 2 | Top | +Z direction |
| 3 | Bottom | -Z direction |
| 4 | Left | +X direction |
| 5 | Right | -X direction |

### Facing Determination

The shield facing that absorbs damage is determined by projecting the impact direction vector onto the ship's local coordinate frame:

1. Compute ship-local axes: `forward = +Y`, `up = +Z`, `right = cross(forward, up) = +X`
2. Project impact direction onto each axis via dot product
3. Find the dominant axis (largest absolute component)
4. Select facing based on sign of the dominant component

### Shield Absorption

When damage is applied:
1. Determine which shield facing the impact hits
2. If the shield has HP remaining:
   - If damage <= shield HP: shield absorbs all damage, **no hull damage**
   - If damage > shield HP: shield drops to 0, overflow passes to hull
3. If shield HP is already 0: all damage goes directly to hull

### Shield Recharge

- Shields recharge continuously when the ship is fully **DECLOAKED**
- Recharge rate is per-facing, defined in ship class data (HP per second)
- Recharge is capped at the facing's maximum HP
- **Recharge stops entirely while cloaked** (any cloak state other than DECLOAKED)

---

## 3. Hull and Subsystem Damage

### Hull Damage

Overflow damage from shields is applied directly to hull HP:
- `hull_hp -= overflow_damage`
- If `hull_hp <= 0`: ship is destroyed (see Section 7)

### Subsystem Damage

After hull damage is applied, 50% of the overflow damage is applied to the nearest subsystem:

```
subsystem_damage = overflow * 0.5
```

**Nearest subsystem** is determined by:
1. Transform impact direction to ship-local coordinates
2. For each subsystem with a defined radius > 0:
   - Compute Euclidean distance from impact point to subsystem position
   - If distance <= subsystem radius: candidate
3. Select the candidate with the smallest distance
4. If no subsystem is in range: no subsystem damage

### Subsystem Disabled Threshold

A subsystem becomes disabled when its HP falls below:
```
disabled_threshold = max_condition * (1.0 - disabled_pct)
```

Where `disabled_pct` is defined per subsystem type in ship class data (typically 0.5-0.8). A disabled subsystem cannot function (shields don't absorb, weapons can't fire, etc.) but is not destroyed.

---

## 4. Weapon Systems

### Phaser / Pulse Weapons

**Firing gates** (all must be true):
- Ship is alive
- Ship is fully DECLOAKED (cloak_state == DECLOAKED)
- Bank index is valid (within the ship's phaser bank count)
- Subsystem is alive (HP > 0)
- Subsystem is not disabled (HP >= disabled threshold)
- Charge >= minimum firing charge

**On fire**:
- Charge resets to 0.0
- Builds a BeamFire (0x1A) packet with shooter ID, direction, and optional target

**Charge recharge**:
- Rate: `recharge_rate * power_level * dt` (power_level is 0.0-1.0)
- Capped at `max_charge`
- Only recharges when DECLOAKED and subsystem is alive

### Torpedo Tubes

**Firing gates**:
- Ship is alive
- Ship is fully DECLOAKED
- Not currently switching torpedo types
- Tube index is valid
- Tube subsystem is alive (HP > 0)
- Cooldown is 0 (tube is ready)

**On fire**:
- Cooldown set to subsystem's `reload_delay`
- Builds a TorpedoFire (0x19) packet

**Cooldown**:
- Each tube has an independent cooldown timer
- Decrements by `dt` each tick
- Ready to fire when cooldown reaches 0

### Torpedo Type Switching

When the player switches torpedo type:
- All tubes enter a reload delay equal to the **maximum** `reload_delay` across all tubes
- Cannot fire any torpedoes during the switch transition
- Switch completes when the timer reaches 0

---

## 5. Cloaking Device

### State Machine

```
DECLOAKED ──(start cloak)──→ CLOAKING ──(timer)──→ CLOAKED
    ▲                                                  │
    │                                          (stop cloak)
    │                                                  │
    └───────(timer)──── DECLOAKING ←───────────────────┘
```

| State | Value | Description |
|-------|-------|-------------|
| DECLOAKED | 0 | Fully visible, shields and weapons operational |
| CLOAKING | 1 | Transitioning to cloaked (shields down, weapons disabled) |
| CLOAKED | 2 | Fully cloaked (invisible, shields down, weapons disabled) |
| DECLOAKING | 3 | Transitioning to visible (shields still down, weapons disabled) |

### Transition Time

All transitions take **3.0 seconds**.

### Cloaking Effects

**On cloak start** (DECLOAKED → CLOAKING):
- All shield facings immediately drop to 0 HP
- Weapons cannot fire (fire gates check cloak_state == DECLOAKED)
- Shield recharge stops

**While CLOAKED**:
- Ship is invisible to other players
- No shield recharge
- Cannot fire weapons

**On decloak** (CLOAKED/CLOAKING → DECLOAKING):
- 3-second vulnerability window (visible but shields/weapons still offline)
- Shields begin recharging from 0 only after reaching DECLOAKED state

### Cloaking Prerequisites

- Ship must have a cloaking device subsystem (`type = "cloak"`)
- Ship class must have `can_cloak = true`
- Cloaking device subsystem must be alive (HP > 0)
- Ship must be in DECLOAKED state (can't re-cloak during transition)

### Wire Protocol

- **StartCloak (0x0E)**: Client → Server → All (reliable)
- **StopCloak (0x0F)**: Client → Server → All (reliable)
- **StateUpdate flag 0x40**: Cloak state as bit-packed boolean (0x20=OFF, 0x21=ON)

---

## 6. Tractor Beams

### Engagement

**Prerequisites**:
- Ship is alive and DECLOAKED
- Ship class has tractor capability (`has_tractor = true`)
- No existing tractor lock (one lock at a time per ship)
- Tractor beam subsystem is alive (HP > 0)

### Effects (per tick)

**Speed drag**:
```
drag = max_damage * dt * 0.1
target.speed = max(0, target.speed - drag)
```

**Tractor damage** (low continuous damage):
```
damage = max_damage * dt * 0.02    (2% of max_damage per second)
```

Damage is applied via the standard damage pipeline (shield absorption → hull → subsystem).

### Auto-Release

The tractor beam automatically releases when:
- Target exceeds `max_damage_distance` from the tractor ship
- Tractor subsystem is destroyed (HP = 0)
- Either ship is destroyed

---

## 7. Ship Death

A ship is destroyed when `hull_hp <= 0`:

1. `alive` flag set to false
2. Server sends **DestroyObject (0x14)** to all clients: `[0x14][object_id:i32]`
3. Server sends **Explosion (0x29)** to all clients: `[0x29][object_id:i32][impact:cv4][damage:cf16][radius:cf16]`
4. All tractor locks on this ship auto-release

### Respawn

There is no dedicated respawn mechanism. To respawn a ship:
1. Destroy the old object (0x14 + 0x29)
2. Create a new object (0x03 ObjCreateTeam with fresh HP)

---

## 8. Repair System

### Queue

Each ship maintains a repair queue of up to 8 subsystem indices, ordered by priority (index 0 = highest priority).

### Repair Rate

```
heal_per_second = max_repair_points * num_repair_teams
heal_per_tick = heal_per_second * dt
```

Only the **top-priority subsystem** (queue index 0) is repaired each tick.

### Auto-Queue

Subsystems below their disabled threshold are automatically added to the repair queue:
```
if subsystem_hp < max_condition * (1.0 - disabled_pct):
    add to queue (if not already queued)
```

### Auto-Remove

When a subsystem is fully repaired (HP reaches `max_condition`), it is automatically removed from the queue.

### Constraints

- Maximum 8 subsystems in queue simultaneously
- Duplicate entries are rejected
- Queue can be manually reordered by the player (RepairListPriority, opcode 0x11)
- Subsystems at 0 HP are NOT automatically queued (only those between 0 and the disabled threshold)

---

## Related Documents

- **[ship-subsystems.md](ship-subsystems.md)** -- Subsystem index table and HP values
- **[phase1-verified-protocol.md](phase1-verified-protocol.md)** -- Wire formats for all combat opcodes
- **[server-authority.md](server-authority.md)** -- Authority model (who computes what)
