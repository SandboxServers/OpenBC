# Ship Subsystem Specification

This document defines the fixed subsystem index table used in the wire protocol, the per-class subsystem catalog, and how subsystem health is serialized in StateUpdate messages.

**Clean room statement**: Subsystem indices are defined in `include/openbc/opcodes.h`. HP values are from the ship data registry (`data/vanilla-1.1.json`), extracted from readable game scripts. No binary addresses, memory offsets, or decompiled code are referenced.

---

## 1. Fixed Subsystem Index Table

Every ship uses a fixed set of subsystem indices for wire protocol serialization. The indices are invariant across ship classes (a Sovereign and a Bird of Prey use the same index for "Reactor", even if the BoP doesn't have all subsystems).

| Index | Constant | Subsystem Type | Notes |
|-------|----------|---------------|-------|
| 0x00 | `BC_SUBSYS_REACTOR` | Reactor (Primary) | Main power generation |
| 0x01 | `BC_SUBSYS_REPAIR` | Repair System | Crew repair capability |
| 0x02 | `BC_SUBSYS_CLOAK` | Cloaking Device | Not present on all ships |
| 0x03 | `BC_SUBSYS_POWERED` | Powered Systems | General power distribution |
| 0x04 | `BC_SUBSYS_LIFE_SUPPORT` | Life Support | Crew life support |
| 0x05 | `BC_SUBSYS_SHIELDS` | Shield Generator | Powers all 6 shield facings |
| 0x06 | `BC_SUBSYS_TORPEDO_1` | Torpedo Tube 1 | Forward |
| 0x07 | `BC_SUBSYS_TORPEDO_2` | Torpedo Tube 2 | Forward |
| 0x08 | `BC_SUBSYS_TORPEDO_3` | Torpedo Tube 3 | Forward |
| 0x09 | `BC_SUBSYS_TORPEDO_4` | Torpedo Tube 4 | Forward |
| 0x0A | `BC_SUBSYS_TORPEDO_5` | Torpedo Tube 5 | Aft |
| 0x0B | `BC_SUBSYS_TORPEDO_6` | Torpedo Tube 6 | Aft |
| 0x0C | `BC_SUBSYS_PHASER_1` | Phaser Emitter 1 | |
| 0x0D | `BC_SUBSYS_PHASER_2` | Phaser Emitter 2 | |
| 0x0E | `BC_SUBSYS_PHASER_3` | Phaser Emitter 3 | |
| 0x0F | `BC_SUBSYS_PHASER_4` | Phaser Emitter 4 | |
| 0x10 | `BC_SUBSYS_PHASER_5` | Phaser Emitter 5 | |
| 0x11 | `BC_SUBSYS_PHASER_6` | Phaser Emitter 6 | |
| 0x12 | `BC_SUBSYS_PHASER_7` | Phaser Emitter 7 | |
| 0x13 | `BC_SUBSYS_PHASER_8` | Phaser Emitter 8 | |
| 0x14 | `BC_SUBSYS_IMPULSE_1` | Impulse Engine 1 | |
| 0x15 | `BC_SUBSYS_IMPULSE_2` | Impulse Engine 2 | |
| 0x16 | `BC_SUBSYS_IMPULSE_3` | Impulse Engine 3 | |
| 0x17 | `BC_SUBSYS_IMPULSE_4` | Impulse Engine 4 | |
| 0x18 | `BC_SUBSYS_WARP_DRIVE` | Warp Drive | |
| 0x19 | `BC_SUBSYS_PHASER_CTRL` | Phaser Controller | Controls all phaser banks |
| 0x1A | `BC_SUBSYS_PULSE_WEAPON` | Pulse Weapon | Not present on all ships |
| 0x1B | `BC_SUBSYS_SENSORS` | Sensor Array | |
| 0x1C | `BC_SUBSYS_REACTOR_2` | Reactor (Secondary) | Backup power |
| 0x1D | `BC_SUBSYS_TRACTOR_1` | Tractor Beam 1 | |
| 0x1E | `BC_SUBSYS_TRACTOR_2` | Tractor Beam 2 | |
| 0x1F | `BC_SUBSYS_TRACTOR_3` | Tractor Beam 3 | |
| 0x20 | `BC_SUBSYS_TRACTOR_4` | Tractor Beam 4 | |

**Maximum index**: `BC_SUBSYS_MAX = 0x21` (33 subsystems).

The actual count varies by ship class. A ship's subsystem list includes only the subsystems defined in its hardpoint script. Empty/unused indices are either skipped or transmitted as 0xFF (full health) in wire messages.

---

## 2. Subsystem Categories

### Core Systems (indices 0x00 - 0x05)

Always present on all ships (6 subsystems):
- **Reactor**: Main power source. If destroyed, ship loses power.
- **Repair**: Enables crew repair capability. Repair rate defined per ship class.
- **Cloak**: Cloaking device. Only functional if `can_cloak = true` in ship class.
- **Powered**: General power distribution bus.
- **Life Support**: Crew survival. Not directly gameplay-affecting in multiplayer.
- **Shields**: Shield generator powering all 6 facings.

### Weapons (indices 0x06 - 0x13)

Up to 6 torpedo tubes and 8 phaser emitters:
- **Torpedo Tubes**: Each tube has independent reload cooldown, HP, and ammo type.
- **Phaser Emitters**: Each bank has independent charge, arc constraints, and HP.

### Propulsion (indices 0x14 - 0x18)

Up to 4 impulse engines and 1 warp drive:
- **Impulse Engines**: Affect sublight speed and acceleration.
- **Warp Drive**: Enables warp travel (not directly used in combat).

### Special Systems (indices 0x19 - 0x20)

- **Phaser Controller** (0x19): Master controller for all phaser banks. If disabled, all phasers stop.
- **Pulse Weapon** (0x1A): Alternate weapon type (some ship classes only).
- **Sensors** (0x1B): Sensor array.
- **Reactor 2** (0x1C): Secondary reactor (backup power).
- **Tractor Beams** (0x1D - 0x20): Up to 4 tractor beam emitters.

---

## 3. Example: Sovereign Class

The Sovereign class has 33 subsystems plus a hull reference. Values from `sovereign.py` hardpoint script:

| Subsystem | Count | Max HP (condition) | Repair Complexity |
|-----------|-------|--------------------|-------------------|
| Warp Core (reactor) | 1 | 7,000 | 2.0 |
| Repair System | 1 | 8,000 | 1.0 |
| Shield Generator | 1 | 10,000 | — |
| Torpedo System | 1 | 6,000 | — |
| Forward Torpedo Tubes | 4 | 2,200 each | — |
| Aft Torpedo Tubes | 2 | 2,200 each | — |
| Phaser Emitters | 8 | 1,000 each | — |
| Impulse Engines (system) | 1 | 3,000 | 3.0 |
| Port/Starboard Impulse | 2 | 3,000 each | — |
| Warp Engines (system) | 1 | 8,000 | — |
| Port/Starboard Warp | 2 | 4,500 each | — |
| Phaser Controller | 1 | 8,000 | — |
| Sensor Array | 1 | 8,000 | 1.0 |
| Tractor System | 1 | 3,000 | 7.0 |
| Tractor Beams | 4 | 1,500 each | 7.0 |
| Bridge | 1 | 10,000 | 4.0 |
| **Total** | **33** | | |

Note: Not all ships have a cloaking device or all subsystem types. The Sovereign hardpoint does not define separate Reactor, Life Support, Cloaking Device, or Powered Systems subsystems — those subsystem indices exist in the protocol but the Sovereign maps its power source to the Warp Core.

### Shield HP (per facing)

| Facing | Max HP | Recharge Rate (HP/sec) |
|--------|--------|----------------------|
| Front | 11,000 | 12.0 |
| Rear | 5,500 | 12.0 |
| Top | 11,000 | 12.0 |
| Bottom | 11,000 | 12.0 |
| Left | 5,500 | 12.0 |
| Right | 5,500 | 12.0 |

Shield HP varies significantly between facings. Forward and dorsal facings have double the HP of aft and lateral facings.

### Hull HP

- **Sovereign**: 12,000 total hull HP (repair complexity: 3.0)

### Repair Configuration

- Max Repair Points: 50.0
- Num Repair Teams: 3
- Repair System HP: 8,000 (repair complexity: 1.0)

### Weapon Configuration

**Phasers** (8 emitters):
- Max Charge: 5.0
- Min Firing Charge: 3.0
- Recharge Rate: 0.08
- Normal Discharge Rate: 1.0
- Max Damage: 300.0
- Max Damage Distance: 70.0

**Torpedoes** (6 tubes):
- Reload Delay: 40.0 seconds
- Max Condition: 2,200 per tube

**Tractor Beams** (4 projectors):
- Max Damage (force): 80.0
- Max Damage Distance: 114.0
- Recharge Rate: 0.3 (aft) / 0.5 (forward)

---

## 4. StateUpdate Subsystem Serialization

Subsystem health is sent in StateUpdate (opcode 0x1C) using the **flag 0x20** (SUBSYSTEMS) bit. This is server-authoritative data sent from server to all clients.

### Round-Robin Encoding

Rather than sending all subsystem health values every tick (which would be ~33 bytes), StateUpdate uses round-robin encoding:

```
[startIndex:u8][health_0:u8][health_1:u8]...[health_N:u8]
```

- `startIndex`: The subsystem index where this update begins
- Each health byte is a scaled HP value: `0xFF = 100%`, `0x00 = 0%`
- The number of health bytes varies (~6-10 per update, budget of ~10 bytes per tick)
- Over multiple updates, all subsystems are covered in a cycling window

### Health Byte Encoding

```
health_byte = (u8)(current_hp / max_condition * 255.0)
```

Special values:
- `0xFF` = 100% health (full)
- `0x64` = ~39% health
- `0x00` = 0% health (destroyed)

### Direction Split

Subsystem health data flows in only one direction:
- **Server → Clients**: flag 0x20 (SUBSYSTEMS) -- server is authoritative for damage
- **Clients → Server**: flag 0x80 (WEAPONS) -- clients report weapon charge/cooldown

A StateUpdate from the ship owner (client) never includes flag 0x20. A StateUpdate from the server never includes flag 0x80.

---

## 5. Ship Creation Pipeline

When a player selects a ship, the creation process follows this pipeline:

1. **Load NIF model**: Read the ship's NIF (NetImmerse) geometry file from disk
2. **Add properties**: Attach subsystem definitions from ship class data
3. **Create subsystems**: Initialize each subsystem with full HP
4. **Serialize**: Build the ObjCreateTeam (0x03) ship blob for wire transmission

### Timing (observed)

| Operation | Duration |
|-----------|----------|
| First load (cold) | ~200ms |
| Cached load (same class) | ~40ms |
| Different class switch | ~300ms |

### Ship Blob Format

The ObjCreateTeam ship blob includes a subsystem health array at the end:

```
[...position, orientation, velocity, name, class...]
[subsystem_health: one byte per subsystem, round-robin format]
```

At creation time, all health bytes are `0xFF` (100%).

---

## 6. Subsystem Properties

Each subsystem in the ship class data includes:

| Property | Type | Description |
|----------|------|-------------|
| name | string | Human-readable name |
| type | string | Category: `"hull"`, `"phaser"`, `"torpedo_tube"`, `"shield"`, etc. |
| position | vec3 | Location in ship-local coordinates |
| radius | f32 | Damage hit radius (defines AABB for overlap testing) |
| max_condition | f32 | Maximum HP |
| disabled_pct | f32 | HP fraction below which subsystem is disabled (0.0-1.0) |
| is_critical | bool | Whether destruction affects ship survival |
| is_targetable | bool | Whether players can target this subsystem |
| repair_complexity | f32 | Affects repair time |

Weapon-specific properties:
| Property | Type | Description |
|----------|------|-------------|
| max_damage | f32 | Damage per hit |
| max_charge | f32 | Maximum phaser charge |
| min_firing_charge | f32 | Minimum charge to fire |
| recharge_rate | f32 | Charge per second |
| reload_delay | f32 | Torpedo tube reload time |
| max_damage_distance | f32 | Maximum effective range |

---

## Related Documents

- **[combat-system.md](combat-system.md)** -- How damage interacts with subsystems
- **[phase1-verified-protocol.md](phase1-verified-protocol.md)** -- StateUpdate wire format (Section 8)
- **[phase1-api-surface.md](phase1-api-surface.md)** -- Ship data registry schema
