# Combat System Specification

This document describes the combat mechanics implemented in Bridge Commander multiplayer: damage processing, shields, weapons, cloaking, tractor beams, and the repair system.

**Clean room statement**: This document describes combat mechanics as observable in-game behavior and readable game scripts. No binary addresses, memory offsets, or decompiled code are referenced.

---

## 1. Damage Pipeline

All damage flows through a common pipeline with three entry points:

```
Collision Impact ──┐
                   │
Weapon Hit ────────┼──→ Apply Damage ──→ Shield Check ──→ Hull Damage ──→ Subsystem Damage
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

### Collision Damage Formulas

Two distinct per-contact scaling paths exist, used in different contexts:

**Path 1 — Direct collision (multi-contact)**:
```
raw = (collision_energy / ship_mass) / contact_count
scaled = raw * 0.1 + 0.1
damage = min(scaled, 0.5)    // hard cap at 0.5 per contact
radius = 6000.0              // fixed
```
Output range: 0.1 to 0.5 (fractional of radius). Feeds into `DoDamage` which distributes across hull and subsystem arrays.

**Path 2 — Collision effect handler (per-contact, shield-first)**:
```
raw = (collision_energy / ship_mass) / contact_count
if (raw > 0.01):                          // dead zone — very gentle collisions ignored
    scaled = raw * 900.0 + 500.0          // absolute HP damage
    shield_absorption(ship, direction, &scaled, shield_scale=1.5)
```
Output range: 500.0+ (absolute HP). Feeds into the shield absorption distributor first, then per-subsystem damage. Each subsystem receives the full per-contact damage; overflow (overkill) is accumulated and returned as remainder.

Path 2 produces much larger absolute values (verified: avg ~6000, max ~13000 per subsystem hit in live traces). The `900× + 500` amplifier converts small energy ratios into significant subsystem damage.

### Weapon Damage Scaling

When a weapon hit passes through the damage pipeline:
- Damage is doubled (×2.0)
- Hit radius is halved (×0.5)
- Only phaser (type 0) and torpedo (type 1) hits are processed

### Resistance Scaling

Each ship has two multipliers that scale incoming damage:
- **Damage radius multiplier** (1.0 = normal, 0.0 = immune)
- **Damage falloff multiplier** (1.0 = normal)

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
| 4 | Left (Port) | -X direction |
| 5 | Right (Starboard) | +X direction |

### Facing Determination

The shield facing that absorbs damage is determined by a **maximum component test** on the impact direction vector in the ship's local coordinate frame:

1. Rearrange the impact normal components to {Y, Z, X} (forward, up, right)
2. Find the largest absolute component across all 6 signed directions (+Y, +Z, +X, -Y, -Z, -X)
3. The dominant direction determines the shield facing

For directed weapon hits, a more precise test is used: the weapon ray is intersected against the ship's **shield ellipsoid** (axis-aligned to the ship), and the outward normal at the intersection point is passed through the same facing determination.

### Shield Absorption

Shield absorption works differently for area-effect vs directed damage:

#### Area-Effect Damage (explosions, environmental)

Damage is distributed equally across all 6 facings:
```
per_facing_damage = total_damage / 6
For each facing:
    absorbed = min(per_facing_damage, current_shield_hp[facing])
    current_shield_hp[facing] -= absorbed
    total_absorbed += absorbed
overflow_to_hull = total_damage - total_absorbed
```

This is NOT all-or-nothing. Each facing independently absorbs its share. A ship with 5 full shield facings and 1 depleted facing takes 1/6 of the total damage to hull.

#### Directed Damage (weapons, collisions)

A geometry intersection test determines which shield facing is hit. The specific facing absorbs damage up to its current HP; overflow passes to hull.

### Shield Recharge

Shield recharge is driven by the ship's power system, NOT by simple time-based rate:

```
hp_gain = (charge_per_second[facing] * power_budget) / (total_power / 6)
```

- `charge_per_second` is defined per-facing in ship class data
- `power_budget` is an energy allocation from the ship's power subsystem
- The `1/6` factor distributes power equally across facings
- Overflow power from a fully-charged facing is redistributed to others
- Recharge runs through the engine's periodic event system, not a direct per-tick call

**Recharge stops while cloaked** (shield subsystem is disabled during cloak).

### Example: Sovereign Shield HP (from hardpoint scripts)

| Facing | Max HP | Charge Rate (HP/sec) |
|--------|--------|---------------------|
| Front | 11,000 | 12.0 |
| Rear | 5,500 | 12.0 |
| Top | 11,000 | 12.0 |
| Bottom | 11,000 | 12.0 |
| Left | 5,500 | 12.0 |
| Right | 5,500 | 12.0 |

---

## 3. Hull and Subsystem Damage

### Hull Damage

Overflow damage from shields is applied directly to hull HP:
- `hull_hp -= overflow_damage`
- If `hull_hp <= 0`: ship is destroyed (see Section 7)

### Subsystem Damage Distribution

After shield absorption, remaining damage is tested against subsystems using **axis-aligned bounding box (AABB) overlap tests**. Each subsystem has a defined position and radius in ship-local coordinates. The damage volume (built from the hit position and damage radius) is tested against each subsystem's bounding box.

Subsystems whose bounding box overlaps the damage volume take damage. This is a spatial overlap test, not a distance calculation.

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
- Ship is not cloaked (cloak disables weapon subsystems)
- Subsystem is alive (HP > 0)
- Subsystem is not disabled (HP >= disabled threshold)
- Charge >= minimum firing charge
- Weapon can-fire flag is set

**Charge recharge**:
```
charge += recharge_rate * power_level * dt * power_multiplier
```
- `power_level` is 0.0-1.0 (power allocation to weapons)
- `power_multiplier` comes from the weapon system's power budget
- Non-owner ships (in multiplayer) recharge at a reduced rate
- Capped at `max_charge`
- Only recharges when not cloaked and subsystem is alive

**Phaser intensity modes**: Phasers have three intensity settings (LOW, MED, HIGH) that affect both discharge rate during firing and damage output per tick. Higher intensity drains charge faster but deals more damage.

**On fire**:
- Charge begins discharging at the intensity-dependent rate
- When charge reaches 0, firing stops automatically
- Builds a BeamFire (0x1A) packet with shooter ID, direction, and optional target

### Torpedo Tubes

**Firing gates**:
- Ship is alive
- Ship is not cloaked
- Tube subsystem is alive (HP > 0)
- Subsystem is not disabled
- At least one torpedo loaded (num_ready > 0)
- Ammo available for current type
- Minimum fire interval elapsed (prevents rapid double-fire)

**On fire**:
- Torpedo count decremented
- Cooldown timer starts for the fired slot
- Builds a TorpedoFire (0x19) packet

**Cooldown**:
- Each tube maintains an array of timer slots (one per max_ready)
- Timers track time since fire; tube reloads when timer expires (reload_delay reached)
- Each tube reloads independently

### Torpedo Type Switching

When the player switches torpedo type:
- All tubes are **unloaded** (ready count set to 0)
- All cooldown timers are **cleared**
- In multiplayer: tubes are NOT immediately reloaded — they must go through normal reload cycle
- Effective lockout = longest reload_delay across all tubes (since all restart simultaneously)
- In single-player: tubes are immediately reloaded with the new type (no lockout)

---

## 5. Cloaking Device

### State Machine

The cloaking device has 4 operational states:

```
DECLOAKED ──(start cloak)──→ CLOAKING ──(timer)──→ CLOAKED
    ▲                                                  │
    │                                          (stop cloak)
    │                                                  │
    └───────(timer)──── DECLOAKING ←───────────────────┘
```

| State | Description |
|-------|-------------|
| DECLOAKED | Fully visible, shields and weapons operational |
| CLOAKING | Transitioning to cloaked (shields disabling, weapons disabled) |
| CLOAKED | Fully cloaked (invisible, shields down, weapons disabled) |
| DECLOAKING | Transitioning to visible (shields still down, weapons disabled) |

### Transition Time

Transitions use a configurable timer (settable via `SetCloakTime()`). The timer counts up during CLOAKING and down during DECLOAKING. Both directions use the same duration.

### Cloaking Effects

**On cloak start** (DECLOAKED → CLOAKING):
- Shield subsystem is **functionally disabled** (stops absorbing and recharging)
- Shield HP is **preserved** (not dropped to zero)
- Shield visuals fade out over a configurable delay (ShieldDelay, default ~1 second)
- Weapon subsystems are disabled via the power subsystem mechanism
- Visual transparency begins ramping

**While CLOAKED**:
- Ship is invisible to other players
- No shield recharge
- Cannot fire weapons
- If power drops below threshold, ship auto-decloaks (energy failure)

**On decloak** (CLOAKED/CLOAKING → DECLOAKING):
- Visual transparency ramps back
- After reaching DECLOAKED state, shield re-enable is delayed by ShieldDelay
- Shields begin recharging from their preserved HP only after re-enabling
- If shield HP was 0 during cloak, it is reset to 1.0 HP on decloak

### Cloaking Prerequisites

- Ship must have a cloaking device subsystem
- Cloaking device subsystem must be alive (HP > 0)
- Ship must be in DECLOAKED state (can't re-cloak during transition)
- Sufficient power available (recursive energy check)

### Wire Protocol

- **StartCloak (0x0E)**: Client → Server → All (reliable, event-forwarded)
- **StopCloak (0x0F)**: Client → Server → All (reliable, event-forwarded)
- **StateUpdate flag 0x40**: Cloak state as boolean (on/off). Client runs its own local state machine with visual effects.

---

## 6. Tractor Beams

### Modes

Tractor beams support 6 modes:

| Mode | Name | Behavior |
|------|------|----------|
| 0 | HOLD | Zero target velocity |
| 1 | TOW | Move target toward source (default) |
| 2 | PULL | Pull target closer |
| 3 | PUSH | Push target away |
| 4 | DOCK_STAGE_1 | Docking approach phase |
| 5 | DOCK_STAGE_2 | Final docking alignment |

### Engagement Prerequisites

- Ship is alive and not cloaked
- Ship class has tractor capability
- Tractor beam subsystem is alive (HP > 0)

### Tractor Force

Per-projector force each tick:
```
distance_ratio = min(1.0, max_damage_distance / beam_distance)
force = max_damage * system_condition_pct * projector_condition_pct * distance_ratio * dt
```

Key characteristics:
- **Distance falloff**: Linear beyond max_damage_distance
- **Health scaling**: Both tractor system and individual projector health affect force output
- A damaged tractor system produces less force

### Speed Drag

Tractor beams reduce the target's speed **multiplicatively**:
```
tractor_ratio = force_used / total_max_damage
effective_speed *= (1.0 - tractor_ratio)
```

- At full tractor output, target speed drops to zero
- At half output, speed is halved
- The same ratio is applied to acceleration, angular velocity, and angular acceleration
- The impulse engine subsystem reads the tractor ratio each tick

### Tractor Beams Do NOT Apply Direct Damage

The tractor beam modes only manipulate target velocity and angular velocity. No damage is applied to the target ship through the tractor beam itself.

### Friendly Fire Protection

The game tracks cumulative tractor time against friendly ships, with a warning threshold and a maximum before forced release.

### Auto-Release

The tractor beam releases when:
- Beam intersection test fails (target out of range or line-of-sight lost)
- Tractor subsystem is disabled or destroyed
- Either ship is destroyed

---

## 7. Ship Death

A ship is destroyed when `hull_hp <= 0`:

1. Ship marked as dead
2. Server sends **DestroyObject (0x14)** to all clients: `[0x14][object_id:i32]`
3. Server sends **Explosion (0x29)** to all clients: `[0x29][object_id:i32][impact:cv4][damage:cf16][radius:cf16]`
4. All tractor locks on this ship release

### Respawn

There is no dedicated respawn mechanism. To respawn a ship:
1. Destroy the old object (0x14 + 0x29)
2. Create a new object (0x03 ObjCreateTeam with fresh HP)

---

## 8. Repair System

### Queue

Each ship maintains a repair queue as a **dynamically-sized linked list** (no fixed maximum). Subsystems are added at the tail and repaired from the head.

### Repair Rate

```
raw_repair = max_repair_points * repair_system_health_pct * dt
per_subsystem = raw_repair / min(queue_count, num_repair_teams)
condition_gain = per_subsystem / subsystem_repair_complexity
```

Key characteristics:
- **Multiple subsystems repaired simultaneously** (up to `num_repair_teams`)
- Repair amount is divided equally among active repairs
- The repair subsystem's own health scales output (damaged repair bay = slower)
- `repair_complexity` per subsystem acts as a final divisor (higher = slower)

### Queue Rules

- **Destroyed subsystems (0 HP) are skipped** — they remain in queue but receive no repair and generate a "cannot be completed" notification
- **Destroyed subsystems are NOT added** — only subsystems with HP > 0 can be queued
- **Duplicates rejected** — queue is checked before adding
- **Auto-remove on full repair** — removed when HP reaches max_condition
- **Repair only runs on host/standalone** — gated on host or non-multiplayer

### Priority Toggle

Clicking a subsystem in the Engineering panel triggers a binary toggle:
- **Active repair slot** (being repaired) → demote to **tail** of queue
- **Waiting area** (not being repaired) → promote to **head** of queue

No "move up one position" — always jumps to front or back.

### Wire Protocol

- **PythonEvent (0x06)**: Host → All (auto-notifications: add/complete/cannot-complete)
- **AddToRepairList (0x0B)**: Client → Host → All (manual repair request, GenericEventForward relay)
- **RepairListPriority (0x11)**: Client → Host → All (priority toggle, GenericEventForward relay)

See [repair-system.md](repair-system.md) for complete behavioral spec including wire formats, priority toggle algorithm, collision→repair chain, and Engineering panel UI areas.

---

## Related Documents

- **[ship-subsystems.md](ship-subsystems.md)** -- Subsystem index table and HP values
- **[phase1-verified-protocol.md](phase1-verified-protocol.md)** -- Wire formats for all combat opcodes
- **[server-authority.md](../architecture/server-authority.md)** -- Authority model (who computes what)
