# Tractor Beam System — Clean Room Specification

Behavioral specification of the Bridge Commander tractor beam mechanics: 6 operational modes, force computation, speed drag, distance falloff, and friendly-fire penalty system.

**Clean room statement**: This specification is derived from observable behavior, network packet captures, the game's shipped Python scripting API, and hardpoint script analysis. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

The tractor beam is a weapon system that applies force to a target ship instead of dealing damage. It can hold, tow, pull, push, or dock targets. Using the tractor beam creates a **speed drag** on the source ship — the more force applied, the slower the source ship can move.

See also:
- [weapon-system.md](weapon-system.md) — weapon charge/recharge mechanics (shared base class)
- [combat-system.md](combat-system.md) — damage pipeline (tractor does NOT deal damage)
- [power-system.md](power-system.md) — power budget for tractor subsystem

---

## Class Hierarchy

```
WeaponSystem
  └── TractorBeamSystem (container, manages all projectors)

Weapon → EnergyWeapon
  └── TractorBeamProjector (individual beam emitter)
```

A ship may have multiple TractorBeamProjectors (e.g., forward and aft) managed by a single TractorBeamSystem.

---

## Hardpoint Configuration

### TractorBeamSystem (Container)

| Parameter | Accessor | Typical (Galaxy) | Description |
|-----------|----------|-------------------|-------------|
| MaxCondition | `SetMaxCondition()` | 3000.0 | System HP |
| WeaponSystemType | `SetWeaponSystemType()` | WST_TRACTOR | Must be set to tractor type |
| SingleFire | `SetSingleFire()` | 1 | Only one projector fires at a time |
| AimedWeapon | `SetAimedWeapon()` | 0 | Not an aimed weapon |
| NormalPowerPerSecond | `SetNormalPowerPerSecond()` | 600.0 | Power draw rate |

### TractorBeamProjector (Per-Emitter)

| Parameter | Accessor | Typical (Galaxy Fwd) | Typical (Galaxy Aft) | Description |
|-----------|----------|---------------------|---------------------|-------------|
| MaxDamage | `SetMaxDamage()` | 50.0 | 80.0 | Maximum tractor force (NOT damage) |
| MaxDamageDistance | `SetMaxDamageDistance()` | 118.0 | 118.0 | Full-force range |
| MaxCharge | `SetMaxCharge()` | 5.0 | 5.0 | Charge capacity |
| MinFiringCharge | `SetMinFiringCharge()` | 3.0 | 3.0 | Minimum charge to activate |
| NormalDischargeRate | `SetNormalDischargeRate()` | 1.0 | 1.0 | Charge drain during operation |
| RechargeRate | `SetRechargeRate()` | 0.3 | 0.5 | Charge recovery rate |

**Note**: Despite the name "MaxDamage", tractor beams do NOT deal damage. The MaxDamage value represents the maximum tractor force output.

---

## Tractor Beam Modes

The tractor system has 6 operational modes, selectable via scripting API:

| Value | Mode | Description |
|-------|------|-------------|
| 0 | HOLD | Zero target's velocity — hold in place |
| 1 | TOW | Move target toward source ship (default mode) |
| 2 | PULL | Pull target closer to source |
| 3 | PUSH | Push target away from source |
| 4 | DOCK_STAGE_1 | Docking approach — tow + transition to DOCK_STAGE_2 |
| 5 | DOCK_STAGE_2 | Docking final alignment |

### Mode Behavior Details

#### HOLD (Mode 0)
- Computes the force needed to stop the target: `neededForce = targetMass * targetSpeed`
- If tractor force ≥ needed force: target velocity set to zero, excess force returned
- If tractor force < needed force: target velocity is scaled down by `(1.0 - force/neededForce)`
- Target maintains position but cannot accelerate away

#### TOW (Mode 1) and DOCK_STAGE_1 (Mode 4)
- First stops the target (same as HOLD)
- Remaining force moves target toward the source ship
- Movement distance per tick is capped by a class-level constant × dt
- In DOCK_STAGE_1: automatically transitions to DOCK_STAGE_2 when target is close enough
- TOW applies impulse toward source and sets target angular velocity (turn toward source)

#### PULL (Mode 2)
- Applies force vector pulling the target directly toward the source ship
- Does not first stop the target (target can still drift)

#### PUSH (Mode 3)
- Applies force vector pushing the target away from the source ship

#### DOCK_STAGE_2 (Mode 5)
- Final docking alignment
- Precisely positions and orients the target relative to the source
- Posts ET_TRACTOR_TARGET_DOCKED event on completion

---

## Force Computation

### Per-Projector Tractor Force

```
function ComputeTractorForce(projector, dt, beamDistance):
    // Distance falloff: full force within MaxDamageDistance, linear falloff beyond
    distanceRatio = maxDamageDistance / beamDistance
    distanceRatio = min(distanceRatio, 1.0)

    // Health scaling: both system AND projector health affect output
    systemHealthPct = parentSystem.conditionPercentage
    projectorHealthPct = projector.conditionPercentage

    // Base force from MaxDamage
    force = maxDamage * (systemHealthPct * projectorHealthPct) * distanceRatio

    // Optional: if system has a tracked target, scale by target's condition
    if parentSystem.targetTracker != NULL:
        force *= targetTracker.averagedCondition

    return force * dt
```

**Key characteristics**:
- Force is **doubly health-scaled**: both the system's overall health AND the projector's individual health reduce output
- **Distance falloff is linear**: at 2× MaxDamageDistance, force is halved
- Distance falloff is **clamped at 1.0**: within MaxDamageDistance, full force (no bonus for being close)

### Force Accumulation

Each frame:
1. TractorBeamSystem::Update resets `forceUsed = 0` and recalculates `totalMaxDamage` (sum of all projector MaxDamage values)
2. Each active projector calls ComputeTractorForce and applies the mode-specific behavior
3. The difference between computed force and remaining force is accumulated into `forceUsed`

---

## Speed Drag

The tractor beam creates a **multiplicative speed drag** on the source ship. This is computed in the ImpulseEngine subsystem's update:

```
tractorRatio = forceUsed / totalMaxDamage
effectiveMaxSpeed *= (1.0 - tractorRatio)
effectiveMaxAccel *= (1.0 - tractorRatio)
effectiveMaxAngularVelocity *= (1.0 - tractorRatio)
effectiveMaxAngularAccel *= (1.0 - tractorRatio)
```

Where:
- `forceUsed` = accumulated force from all active projectors this frame
- `totalMaxDamage` = sum of MaxDamage across all projectors (maximum possible force)
- The ratio represents what fraction of the tractor system's capacity is being used

**Key characteristics**:
- The drag is **multiplicative**, not additive
- At full tractor output (ratio = 1.0): ship speed drops to zero
- At half output (ratio = 0.5): ship speed is halved
- The drag affects ALL FOUR engine statistics: max speed, max acceleration, max angular velocity, and max angular acceleration
- A ship tractoring a target is significantly slowed, creating a tactical tradeoff

### ImpulseEngine Connection

The ImpulseEngine subsystem stores a reference to its TractorBeamSystem (set via `SetTractorBeamSystem()` in hardpoint scripts). This creates the coupling between tractor force output and engine performance.

---

## Tractor Beam Does NOT Deal Damage

After comprehensive analysis of all six mode handler functions, **none of them call any damage function on the target ship**. The tractor beam only manipulates the target's velocity and angular velocity.

Claims of tractor damage (e.g., "max_damage * dt * 0.02") are **not supported** by the code.

---

## Beam Lifecycle

### Activation
1. Player orders tractor beam fire (via targeting + fire command)
2. CanFire gates checked (same as other weapons — see [weapon-system.md](weapon-system.md))
3. Beam intersection test: ray from source to target
4. If intersection: ET_TRACTOR_BEAM_STARTED_HITTING event posted

### Per-Tick
1. Beam intersection test re-evaluated (target may have moved)
2. If still hitting: compute force, apply mode behavior, accumulate forceUsed
3. If target lost: ET_TRACTOR_BEAM_STOPPED_HITTING event posted

### Release Conditions
- Target moves out of beam range (intersection test fails)
- Tractor subsystem is destroyed or disabled
- Either ship is destroyed
- Player orders cease fire
- Charge depletes (same as phaser discharge)

---

## Friendly Fire Tractor Penalty

The game tracks how long a player tractors friendly ships:

| Parameter | Description |
|-----------|-------------|
| FriendlyTractorTime | Accumulated time spent tractoring friendlies |
| FriendlyTractorWarning | Time threshold for warning the player |
| MaxFriendlyTractorTime | Time threshold for forced release |

This implements a progressive penalty: tractoring allied ships accumulates time. A warning fires at the warning threshold. If the player continues past the maximum, the tractor is forcibly released.

---

## Network Events

| Event | Name | Direction |
|-------|------|-----------|
| ET_TRACTOR_BEAM_STARTED_FIRING | Tractor activated | Client → Server → All (via PythonEvent) |
| ET_TRACTOR_BEAM_STARTED_HITTING | Beam is hitting target | Local notification |
| ET_TRACTOR_BEAM_STOPPED_HITTING | Beam lost target | Local notification |
| ET_WEAPON_FIRED | Generic weapon fire (dual-fired with above) | Client → Server → All |
| ET_STOP_FIRING_AT_TARGET_NOTIFY | Stop firing at target (host-only) | Host → All (opcode 0x09) |
| ET_TRACTOR_TARGET_DOCKED | Docking complete | Local notification |

---

## Implementation Requirements

An OpenBC server implementation SHALL:

1. **Implement 6 tractor modes** (HOLD, TOW, PULL, PUSH, DOCK_STAGE_1, DOCK_STAGE_2) with mode-specific force application
2. **Compute per-projector force** using the health-scaled, distance-falloff formula
3. **Accumulate force** across all active projectors per tick
4. **Apply multiplicative speed drag** to the source ship's engine: all 4 stats (speed, accel, angVel, angAccel)
5. **NOT deal damage** via tractor beam (force only, no HP reduction)
6. **Support automatic DOCK_STAGE_1 → DOCK_STAGE_2 transition** when target is close enough
7. **Support friendly tractor time tracking** with warning and forced-release thresholds
8. **Use charge/recharge mechanics** from the weapon base class (charge, discharge, CanFire gates)
9. **Post dual events** on fire (ET_TRACTOR_BEAM_STARTED_FIRING + ET_WEAPON_FIRED)
10. **Clamp distance ratio** to 1.0 (no bonus for close range)

---

## Constants

| Name | Description |
|------|-------------|
| TBS_HOLD | Mode 0: Hold target in place |
| TBS_TOW | Mode 1: Tow target toward source (default) |
| TBS_PULL | Mode 2: Pull target closer |
| TBS_PUSH | Mode 3: Push target away |
| TBS_DOCK_STAGE_1 | Mode 4: Docking approach |
| TBS_DOCK_STAGE_2 | Mode 5: Docking final alignment |
| WST_TRACTOR | WeaponSystemType constant for tractor systems |
