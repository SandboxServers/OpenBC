# Shield System — Clean Room Specification

Behavioral specification of the Bridge Commander shield system. Describes the 6-facing directional shield model, recharge mechanics, power interaction, and network serialization.

**Clean room statement**: This specification is derived from observable behavior, network packet captures, the game's shipped Python scripting API, and hardpoint script analysis. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

Each ship has a shield subsystem with **6 independent facings** arranged as a virtual ellipsoid around the hull. Each facing has its own HP pool and recharges independently from the ship's power grid. Damage is applied directionally — the facing closest to the impact absorbs damage first, with overflow passing through to hull and subsystems.

See also:
- [collision-shield-interaction.md](collision-shield-interaction.md) — how the three damage paths (collision, weapon, explosion) interact with shields
- [power-system/](power-system/) — power budget system that feeds shield recharge
- [ship-subsystems/](ship-subsystems/) — subsystem hierarchy and StateUpdate serialization

---

## Shield Facings

Six facings, arranged along the ship's local coordinate axes:

| Index | Name | Axis Direction |
|-------|------|----------------|
| 0 | Front | +Y (forward) |
| 1 | Rear | -Y (aft) |
| 2 | Top | +Z (up) |
| 3 | Bottom | -Z (down) |
| 4 | Left | -X (port) |
| 5 | Right | +X (starboard) |

**Opposite pairs**: Front(0) ↔ Rear(1), Top(2) ↔ Bottom(3), Left(4) ↔ Right(5).

The special value `-1` represents "no shield" (used when a hit misses all shield geometry).

---

## Hardpoint Configuration

Each shield subsystem is configured per-ship via hardpoint scripts with two arrays of 6 values:

| Parameter | Type | Per-Facing | Description |
|-----------|------|------------|-------------|
| MaxShields | float[6] | Yes | Maximum HP per facing (via `SetMaxShields`) |
| ChargePerSecond | float[6] | Yes | Recharge rate per facing (via `SetShieldChargePerSecond`) |

**Default**: If not explicitly set, all 6 facings default to 1000.0 max HP.

### Typical Ship Values

| Ship | MaxShields (per facing) | ChargePerSecond (per facing) |
|------|------------------------|------------------------------|
| Sovereign | 6000 | 15 |
| Galaxy | 5600 | 12 |
| Akira | 3600 | 11 |
| Warbird | 4000 | 8 |
| Vor'cha | Front: 24000, Others: varies | Front: 28, Others: 2-9 |

Note: The Vor'cha has asymmetric shield values — heavily forward-biased. Most Federation ships use uniform values across all 6 facings.

---

## Shield Facing Determination

When damage arrives at a specific point, the shield system must determine which of the 6 facings should absorb it. Two methods are used depending on the damage type.

### Method 1: Maximum Component Projection (Fast Path)

Used for collision damage and cases where the impact normal is already known in ship-local space.

**Algorithm**: Given a 3D normal vector in ship-local space, find the cardinal axis most closely aligned with it:

1. Rearrange components to `{Y, Z, X}` (forward, up, right)
2. Find the maximum positive component among indices 0-2
3. Find the maximum negated component among indices 3-5 (most-negative of Y, Z, X)
4. The overall maximum determines the dominant direction:

| Dominant Index | Component | Shield Facing |
|----------------|-----------|---------------|
| 0 | +Y (forward) | Front (0) |
| 1 | +Z (up) | Top (2) |
| 2 | +X (right) | Right (5) |
| 3 | -Y (aft) | Rear (1) |
| 4 | -Z (down) | Bottom (3) |
| 5 | -X (left) | Left (4) |

This is an **axis-aligned maximum component test** — equivalent to finding the dominant face of a cube enclosing the unit normal. No trigonometry or dot products required.

### Method 2: Ray-Ellipsoid Intersection (Weapon Path)

Used when a weapon fires at a ship and the hit point must be projected onto the shield ellipsoid.

1. Transform ray endpoints from world space to the shield ellipsoid's local space
2. Normalize by the ellipsoid semi-axes (making it a unit sphere problem)
3. Perform ray-sphere intersection test against the unit sphere
4. Compute the outward normal at the intersection point
5. Un-normalize back to ship-local space
6. Pass the normal to the maximum component projection (Method 1) to get the facing

The ellipsoid semi-axes are stored in the ship's scene graph node and define the shape of the shield "bubble." Ships with elongated profiles (like the Galaxy class) have appropriately shaped ellipsoids.

---

## Shield Absorption

### Three Damage Paths

Bridge Commander has three distinct ways shields absorb damage, depending on the source. See [collision-shield-interaction.md](collision-shield-interaction.md) for the complete cross-path comparison.

#### Area-Effect Damage (Explosions)

Distributes damage **equally across all 6 facings**:

```
totalAbsorbed = 0
damagePerFacing = totalDamage * (1/6)

for each facing in 0..5:
    absorption = min(damagePerFacing, currentHP[facing])
    currentHP[facing] -= absorption
    totalAbsorbed += absorption

overflowDamage = totalDamage - totalAbsorbed
```

**Key behavior**: This is NOT all-or-nothing. Each facing independently absorbs up to its current HP for that facing's 1/6 share. A ship with 5 full facings and 1 empty facing still loses 1/6 of incoming damage to hull.

#### Directed Damage (Weapons)

Weapon hits use a **binary shield gate**: the hit is either fully absorbed by the shield facing, or passes through entirely.

1. Ray-ellipsoid intersection determines which shield facing was hit
2. If the facing has HP > 0: hit is **fully absorbed** (shield visual effect, no hull damage)
3. If the facing has HP == 0 (breached): full damage passes to hull and subsystems

The shield facing's HP is decremented when it absorbs a hit. The HP reduction equals the damage amount, clamped to `[0, maxHP]`.

#### Collision/Subsystem Damage (Per-Handler)

Collision damage goes through the per-subsystem spatial damage handler pipeline. Shield facings participate as spatial regions — each shield zone tests for intersection with the collision's damage volume. Shield zones that intersect absorb damage from their HP pool; overflow passes to hull.

### HP Clamping

When shield HP changes (from damage or recharge), it is always clamped:

```
newHP = clamp(newHP, 0.0, maxShields[facing])
```

Both floor (0.0) and ceiling (maxShields) are enforced.

---

## Shield Recharge

### Power-Driven Recharge Formula

Shield recharge is powered by the ship's power grid (see [power-system/](power-system/)). The shield subsystem receives a power budget each tick, which is converted to shield HP.

**Per-facing recharge formula**:

```
normalizedPower = subsystemPowerLevel * (1/6)    # divide total power across 6 facings

if normalizedPower <= 0:
    return powerBudget                            # no power = no recharge

hpGain = (chargePerSecond[facing] * powerBudget) / normalizedPower
newHP = currentHP[facing] + hpGain

if newHP > maxShields[facing]:
    # Shield facing is full — return excess power for redistribution
    ratio = chargePerSecond[facing] / normalizedPower
    excess = (newHP - maxShields[facing]) / max(ratio, epsilon)
    currentHP[facing] = maxShields[facing]
    return excess
else:
    currentHP[facing] = newHP
    return 0.0                                    # all power consumed
```

**Key details**:
- `powerBudget` is an energy allocation from the power system, NOT frame time
- `chargePerSecond` is the conversion factor from energy to shield HP
- The `1/6` factor distributes the subsystem's total power equally across 6 facings
- **Overflow power is returned** to the caller for redistribution to other facings
- A facing that is already full consumes no power — its budget is available to other facings

### Recharge Scheduling

Shield recharge does NOT run every frame. Instead, it runs through the event/timer system:

1. At initialization, a random phase offset is computed: `offset = random() * 0.33 * 3.05e-05`
2. This staggers shield recharge events across ticks to prevent all subsystems from updating simultaneously
3. The recharge tick fires periodically; each tick calls the per-facing recharge formula
4. Power redistribution across facings happens within a single tick — facings that are full give their excess budget to facings that need it

### Recharge Gate Conditions

Shield recharge requires all of the following:
1. **Shield subsystem exists** on the ship
2. **Shield subsystem is enabled** (not disabled by cloaking or other effects)
3. **Shield subsystem is not destroyed** (HP > 0)
4. **Power is available** (subsystem power level > 0)

If any condition fails, shields do not recharge.

---

## Shield Status Queries

### IsShieldBreached(facing)

Returns `true` if the specified facing's current HP is 0 (fully depleted). Does NOT return true for partial damage — only when the facing is completely gone.

### IsAnyShieldBreached()

Returns `true` if ANY of the 6 facings has 0 HP. Useful for AI decision-making.

### GetShieldPercentage()

Returns the minimum of `currentHP[facing] / maxHP[facing]` across all 6 facings. This represents the weakest facing, not an average.

---

## Cloaking Interaction

Shield behavior during cloak transitions:

### Cloaking (shields go down)

1. When cloaking begins, a **delayed disable** is scheduled
2. The delay equals the CloakingSubsystem's ShieldDelay parameter (default: 1.0 second)
3. During this delay window, shields are still active (the ship can take shield-absorbed hits while cloaking)
4. After the delay, the shield subsystem is disabled: no absorption, no recharge, hidden visually
5. **Shield HP is preserved** — the values are NOT zeroed, the subsystem is simply turned off

### Decloaking (shields come back)

1. When decloaking completes (ship fully visible), a **delayed re-enable** is scheduled
2. The re-enable delay also equals ShieldDelay
3. After the delay, the shield subsystem re-enables and begins recharging normally
4. If any shield facing was at 0 HP, it is reset to 1.0 HP (minimum operational level)

### Summary

- During cloak: shields are functionally disabled (no absorption, no recharge)
- Shield HP values are preserved through the cloak/decloak cycle
- There is a brief window (ShieldDelay seconds) at both ends of the transition where the cloaking/decloaking state doesn't match the shield state

See [cloaking-system.md](cloaking-system.md) for the full cloaking state machine.

---

## Network Serialization

### StateUpdate Flag 0x40 (Cloak State)

The shield subsystem's enabled/disabled state is transmitted as part of the cloaking data in StateUpdate flag 0x40:

```
if flag 0x40 is set:
    WriteBit(shield_enabled)    # 1 = shields on, 0 = shields off (cloaked)
```

The client reads this bit and triggers its own local StartCloaking/StopCloaking transition, which handles the shield visual effects independently.

**Important**: The network serializes the on/off boolean, NOT the full shield state machine value. Clients run their own timers and transitions locally.

### StateUpdate Flag 0x20 (Subsystem Health)

Shield subsystem health is serialized as part of the subsystem list in flag 0x20 using the Base WriteState format (see [ship-subsystems/](ship-subsystems/)). This transmits the overall shield subsystem condition, not per-facing HP.

### Per-Facing HP

Per-facing shield HP values are NOT individually serialized in the StateUpdate. Clients track per-facing HP locally from:
1. Initial values from the hardpoint configuration
2. Damage events (explosion, weapon hit, collision) that arrive as separate opcodes
3. Local recharge simulation

This means per-facing HP can drift between host and client if events are lost or misordered. The game relies on the relatively high frequency of damage events to keep clients approximately synchronized.

---

## Implementation Requirements

An OpenBC server implementation SHALL:

1. **Track 6 independent shield facings** per ship, each with current HP and max HP
2. **Determine shield facing** using the maximum component projection algorithm for collision/normal-based damage
3. **Determine shield facing** using ray-ellipsoid intersection for weapon hits
4. **Apply area-effect damage** by dividing equally across all 6 facings with per-facing clamped absorption
5. **Apply directed damage** using the binary shield gate (absorbed or pass-through, not partial)
6. **Recharge shields** using the power-driven formula with per-facing budgeting and overflow redistribution
7. **Disable shields during cloaking** with the configurable ShieldDelay parameter
8. **Preserve shield HP** through cloak/decloak cycles
9. **Reset facing HP to 1.0** after decloaking if any facing was at 0
10. **Serialize cloak state** (on/off) in StateUpdate flag 0x40
11. **Serialize subsystem health** in StateUpdate flag 0x20 via the standard subsystem WriteState format

---

## Constants

| Name | Value | Description |
|------|-------|-------------|
| NUM_SHIELD_FACINGS | 6 | Always 6 (front, rear, top, bottom, left, right) |
| DEFAULT_MAX_SHIELDS | 1000.0 | Default max HP per facing if not set by hardpoint |
| AREA_DAMAGE_DIVISOR | 1/6 (0.16667) | Per-facing share of area-effect damage |
| CLOAK_SHIELD_DELAY | 1.0 | Default shield disable/re-enable delay (seconds, configurable) |
| MIN_SHIELD_HP_ON_DECLOAK | 1.0 | Minimum HP restored per facing after decloaking |
