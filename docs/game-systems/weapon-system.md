# Weapon System — Clean Room Specification

Behavioral specification of the Bridge Commander weapon firing mechanics: phaser charge/discharge, torpedo reload/cooldown, CanFire gate conditions, intensity modes, and the weapon update loop.

**Clean room statement**: This specification is derived from observable behavior, network packet captures, the game's shipped Python scripting API, and hardpoint script analysis. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

Bridge Commander has two primary weapon types: **phasers** (continuous beam weapons) and **torpedoes** (projectile weapons). Both are managed by a **WeaponSystem** container that handles targeting, firing chains, and per-tick updates.

See also:
- [combat-system.md](combat-system.md) — damage pipeline and damage scaling
- Wire format specs: [beam-fire-wire-format.md](../wire-formats/beam-fire-wire-format.md), [torpedo-fire-wire-format.md](../wire-formats/torpedo-fire-wire-format.md)
- [event-forward-wire-format.md](../wire-formats/event-forward-wire-format.md) — generic event forwarding (opcodes 0x07-0x12) for weapon control commands

---

## Class Hierarchy

```
Weapon (base)
  ├── EnergyWeapon
  │     ├── PhaserBank
  │     └── TractorBeamProjector (see tractor-beam-system.md)
  └── TorpedoTube

WeaponSystem (container, holds N weapons)
  ├── PhaserSystem (inherits WeaponSystem)
  ├── TorpedoSystem (inherits WeaponSystem)
  └── TractorBeamSystem (see tractor-beam-system.md)
```

TractorBeamProjector inherits from EnergyWeapon, sharing its charge/recharge fields (MaxCharge, MinFiringCharge, NormalDischargeRate, RechargeRate). See [tractor-beam-system.md](tractor-beam-system.md) for tractor-specific behavior.

---

## Part 1: Phaser System

### Hardpoint Configuration (EnergyWeaponProperty)

| Parameter | Accessor | Typical (Sovereign) | Description |
|-----------|----------|---------------------|-------------|
| MaxCharge | `GetMaxCharge()` | 5.0 | Maximum charge level |
| RechargeRate | `GetRechargeRate()` | 0.08 | Charge units gained per second per unit power |
| NormalDischargeRate | `GetNormalDischargeRate()` | 1.0 | Base discharge rate during firing |
| MinFiringCharge | `GetMinFiringCharge()` | 3.0 | Minimum charge required to fire |
| MaxDamage | `GetMaxDamage()` | 300.0 | Maximum damage output per tick |
| MaxDamageDistance | `GetMaxDamageDistance()` | 70.0 | Maximum effective range |
| MaxCondition | `SetMaxCondition()` | 1000.0 | Subsystem HP |

### Phaser Charge Recharge Formula

When the phaser is **not firing**, charge accumulates each tick:

```
delta_charge = rechargeRate * powerLevel * dt * powerMultiplier

if ship is NOT the owner's ship (AI or remote player):
    delta_charge *= AI_RECHARGE_MULTIPLIER

chargeLevel += delta_charge
chargeLevel = min(chargeLevel, maxCharge)
```

Where:
- `rechargeRate` = hardpoint property (e.g., 0.08)
- `powerLevel` = power allocation to this weapon (0.0-1.0, default 1.0)
- `dt` = frame time in seconds
- `powerMultiplier` = caller-provided scale factor
- `AI_RECHARGE_MULTIPLIER` = constant penalty for non-owner ships (slower AI recharge)

### Phaser Intensity Modes

Phasers have three intensity settings that affect both damage output and charge consumption:

| Mode | Name | Discharge Rate | Damage Scale |
|------|------|----------------|--------------|
| 0 | LOW | Slowest drain | Lowest damage |
| 1 | MEDIUM | Medium drain | Medium damage |
| 2 | HIGH | Fastest drain | Highest damage |

Each mode has two constants: a **discharge rate** (charge consumed per second while firing) and a **damage scale** (damage output multiplier per tick).

A fourth mode value (3) represents STOPPED/DEPLETED with zero discharge rate.

### Phaser Discharge (While Firing)

When the phaser is firing at intensity HIGH (2) or STOPPED (3):

```
chargeLevel -= dischargeRate[intensityMode] * dt

if chargeLevel <= 0:
    chargeLevel = 0
    StopFiring()    # beam visual ends
```

**Note**: LOW and MEDIUM intensity modes do NOT discharge in the analyzed code path. Only HIGH and the STOPPED state consume charge.

### Phaser Damage Per Tick

```
distance_ratio = min(maxDamageDistance / beamDistance, 1.0)
damage = maxDamage * (powerLevel * parentSystemPower) * distance_ratio * intensityScale[mode] * dt
```

Where:
- `beamDistance` = actual distance from the phaser emitter to the beam hit point
- `maxDamageDistance` = hardpoint property (e.g., 70.0 for Sovereign phasers)
- `parentSystemPower` is the overall PhaserSystem's power percentage
- Within `maxDamageDistance`: full damage (ratio clamped to 1.0). Beyond: linear falloff.

### Phaser Intensity Network Opcode

The phaser intensity setting is synchronized via opcode 0x12 (SetPhaserLevel) through the generic event forward handler (see [set-phaser-level-wire-format.md](../wire-formats/set-phaser-level-wire-format.md)).

---

## Part 2: Torpedo System

### Hardpoint Configuration (TorpedoTubeProperty)

| Parameter | Accessor | Typical (Sovereign) | Description |
|-----------|----------|---------------------|-------------|
| ReloadDelay | `GetReloadDelay()` | 40.0 | Seconds to reload one torpedo |
| MaxReady | `GetMaxReady()` | 1 | Maximum loaded torpedoes per tube |
| ImmediateDelay | (getter) | 0.25 | Minimum seconds between consecutive fires |
| MaxCondition | `SetMaxCondition()` | 1000.0 | Subsystem HP |

### TorpedoSystem Fields

The TorpedoSystem container tracks:
- **Per-type ammo counts**: How many torpedoes of each type remain
- **Current ammo type**: Currently selected torpedo type index
- **Total ammo consumed**: Running counter across all fires
- **Last system fire time**: Game time when any tube in this system last fired

### Torpedo Reload Logic

Each torpedo tube maintains an array of reload timer slots (one per `MaxReady`):

**Timer states**:
- `-1.0`: Slot is loaded/ready (torpedo available)
- `0.0`: Slot cooldown just started
- `> 0.0`: Slot is cooling down (elapsed time since fire)

**Reload process** (`ReloadTorpedo`):
1. Check `numReady < maxReady` AND ammo available for current type
2. If both pass: increment `numReady`
3. Find the slot with the longest timer value, reset it to `-1.0` (mark as loaded)
4. Post `ET_RELOAD_TORPEDO` event

**Unload process** (`UnloadTorpedo`):
1. Decrement `numReady`
2. Find the first slot with timer ≤ 0.0, reset it to 0.0 (mark as empty)

Reload is event-driven: the game posts a reload event when `gameTime - lastFireTime >= ReloadDelay`, triggering `ReloadTorpedo`.

### Torpedo Fire Logic

When a torpedo fires:
1. Call `CanFire()` — if false, abort
2. Create torpedo projectile object (scene graph node + collision shape)
3. Record fire time (`lastFireTime = gameTime`)
4. Decrement `numReady` and system available/total counts
5. Find a tube slot with completed cooldown, mark as "cooldown started" (0.0)
6. Set up torpedo with launch parameters (direction, speed, target)
7. Post `ET_WEAPON_FIRED` event
8. Record system-level fire time
9. If host: send TorpedoFire network packet (opcode 0x19)

### Torpedo Type Switch (SetAmmoType)

When the player selects a different torpedo type:

**Multiplayer path** (`immediate=1`):
1. **Unload all tubes** — decrement every tube's `numReady` to 0
2. **Clear all cooldown timers** — reset all timer slots to 0.0
3. **Do NOT immediately reload** — tubes must go through their normal reload cycle
4. Post `ET_AMMO_TYPE_CHANGED` and `ET_AMMO_SWITCH_STARTED` events
5. If host and type actually changed: send network event

**Offline/local path** (`immediate=0`):
1. Unload all tubes (same as above)
2. Clear all timers (same)
3. **Immediately reload** all tubes with new type (no lockout)

**Type switch "lockout"**: In multiplayer, the effective lockout duration equals the longest `ReloadDelay` across all tubes in the system. This is NOT an explicit timer — it's a side effect of clearing all tubes and requiring normal reload. For Sovereign torpedoes (ReloadDelay=40.0), the lockout is 40 seconds.

---

## Part 3: CanFire Gate Conditions

### Common Gates (All Weapon Types)

All weapons share these base conditions:

1. **Ship is alive** — the parent ship must exist and have its alive flag set
2. **Subsystem is alive (HP > 0)** — the weapon subsystem must have positive condition
3. **Subsystem is not disabled** — condition percentage must be above the disabled threshold (typically 0.75 = 75% condition to remain functional)
4. **WeaponSystem "can fire" flag** — a configuration byte that can globally disable the system

### Phaser-Specific Gates

In addition to common gates:

5. **Charge ≥ MinFiringCharge** — `chargeLevel >= property.MinFiringCharge` (e.g., 3.0 out of 5.0 max)
6. **Ship is fully decloaked** — enforced at the event/system level (cloaking disables weapon subsystems via subsystem disable events), not in individual CanFire

### Torpedo-Specific Gates

In addition to common gates:

5. **numReady > 0** — at least one torpedo is loaded
6. **Ammo available** — `ammoRemaining > totalConsumed` for the current type
7. **Cooldown expired** — `gameTime - lastFireTime >= ImmediateDelay` (prevents rapid double-fires; typically 0.25 seconds)

### Subsystem Alive Check (Recursive)

The "subsystem alive" check is recursive: it verifies the weapon itself AND all child subsystems in its tree are functional. If a weapon has child components, all must be alive for the weapon to fire.

---

## Part 4: Weapon Update Loop

### WeaponSystem::UpdateWeapons (Per Frame)

The main weapon tick runs every frame for each active weapon system:

1. **Gate**: If ship is dead, abort
2. **Clean up dead targets** from the target list
3. **Get firing chain** — determines which weapons fire in what order (round-robin groups)
4. **Build candidate list** — weapons matching the current firing chain group
5. **Try firing each candidate**:
   - Update random fire delay timer
   - Check if delay has expired (introduces slight randomization in fire timing)
   - Call `CanFire()` — if false, call `StopFiring()` and move on
   - Call `Fire(dt, flag)` — attempt to fire at targets from the target list
   - If no target list: try "direct fire" (no target, for area suppression)
6. **Track last fired weapon** for round-robin ordering
7. **If no weapons fired**: try next group in firing chain, cycle through all groups

### Random Fire Delay

Weapon fire is NOT perfectly periodic. A small random delay timer introduces slight variation in fire timing:
- Each weapon has a random delay timer that counts up by `dt` each frame
- When the timer exceeds a threshold, the weapon can attempt to fire
- After a fire attempt, the timer is re-randomized
- AI-assisted fire (aim assist enabled) bypasses the delay timer

This creates naturalistic firing patterns rather than perfectly metronomic fire.

### Firing Chains and Groups

The WeaponSystem supports "firing chains" — named sequences that control which weapons fire in what order. Each chain consists of groups; within each group, weapons fire in round-robin order.

- **Single fire mode**: Only one weapon fires per tick (advances to next weapon each tick)
- **Volley mode**: All weapons in the current group fire simultaneously
- Chains cycle through groups automatically when the current group can't fire

---

## Part 5: Network Authority

### Host-Authoritative Events

- **TorpedoFire (0x19)**: Only sent by the host. Clients receive and replay torpedo launches.
- **StopFiringAtTarget (0x09)**: Only generated on the host (ET_STOP_FIRING_AT_TARGET_NOTIFY).

### Client-Originated Events (Forwarded by Server)

- **StartFiring (0x07)**: Client sends to server, server relays to all
- **StopFiring (0x08)**: Same relay pattern
- **SetPhaserLevel (0x12)**: Same relay pattern
- **TorpTypeChange (0x1B)**: Same relay pattern

### Confirmed Wire Details (Feb 2026)

From a stock dedicated server trace with 2 players:

| Opcode | Factory | Event Code | Wire Size | Observed |
|--------|---------|------------|-----------|----------|
| 0x07 StartFiring | 0x8128 | 0x008000D8 | 25 bytes | 346 (always duplicate pair) |
| 0x08 StopFiring | 0x0101 | 0x008000DA | 17 bytes | 339 (single message) |
| 0x12 SetPhaserLevel | 0x0105 | 0x008000E0 | 18 bytes | 10 (char values: 0x00=LOW, 0x02=HIGH) |
| 0x1B TorpTypeChange | 0x0105 | 0x008000FE | 18 bytes | 2 (char value = new torpedo type index) |
| 0x19 TorpedoFire | (own handler) | — | — | 206 |

SetPhaserLevel and TorpedoTypeChange both use TGCharEvent (factory 0x0105) with a single
extra byte beyond the 17-byte base event. The char value encodes the new setting.

### Per-Client Damage Computation

Weapon damage is **not server-authoritative**. Each client independently:
1. Receives weapon fire events (beam position, torpedo trajectory)
2. Performs its own hit detection (ray-geometry intersection for beams, collision for torpedoes)
3. Applies damage locally

This means weapon damage can differ slightly between clients. The host's subsystem health values (StateUpdate flag 0x20) serve as the authoritative ground truth, periodically correcting client drift.

---

## Constants

| Name | Value | Description |
|------|-------|-------------|
| INTENSITY_LOW | 0 | Lowest phaser output |
| INTENSITY_MEDIUM | 1 | Medium phaser output |
| INTENSITY_HIGH | 2 | Highest phaser output |
| INTENSITY_STOPPED | 3 | Charge depleted / not firing |
| TORPEDO_TIMER_LOADED | -1.0 | Timer slot value meaning "loaded/ready" |
| TORPEDO_TIMER_EMPTY | 0.0 | Timer slot value meaning "cooldown started" |
| DISTANCE_RATIO_CAP | 1.0 | Maximum distance ratio for damage calculation |

---

## Event Types

| Event | Name | Context |
|-------|------|---------|
| ET_WEAPON_FIRED | 0x7C offset | Posted for every weapon fire (phaser, torpedo, tractor). Carried via TGObjPtrEvent with the target ID in the object pointer field. |
| ET_PHASER_STARTED_FIRING | 0x81 offset | Posted when a phaser begins firing. Dual-fired with ET_WEAPON_FIRED. |
| ET_PHASER_STOPPED_FIRING | 0x83 offset | Posted when a phaser stops firing. |
| ET_TORPEDO_FIRED | 0x66 offset | Torpedo-specific fire event (separate from ET_WEAPON_FIRED). |
| ET_RELOAD_TORPEDO | 0x65 offset | Posted when a torpedo tube reloads. |
| ET_AMMO_TYPE_CHANGED | 0x67 offset | Posted when torpedo type changes. |
| ET_AMMO_SWITCH_STARTED | 0x68 offset | Posted when immediate type switch begins. |
| ET_TORP_TYPE_CHANGE | 0xFE offset | Network event for MP torpedo type sync. |

The "offset" values are relative to `ET_TEMP_TYPE` (the base event constant). The actual hex value is `ET_TEMP_TYPE + offset`.
