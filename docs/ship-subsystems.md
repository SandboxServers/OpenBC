# Ship Subsystem Specification

This document defines how ship subsystems are organized, serialized in the wire protocol, and how health data maps to byte positions in StateUpdate flag 0x20 messages.

**Clean room statement**: This specification is derived from observable network behavior (packet captures), the game's shipped Python scripting API, and hardpoint script analysis. No binary addresses, memory offsets, or decompiled code are referenced.

---

## 1. Subsystem Architecture: Hierarchical, Not Flat

Ship subsystems use a **two-level hierarchy**: top-level "system" containers hold individual "child" subsystems. The wire protocol serializes this hierarchy recursively, not as a flat array.

### Top-Level Systems (in the serialization list)

These are the subsystem categories that appear in the ship's serialization list. The **order** they appear is determined by the ship's hardpoint script (the order `AddToSet()` is called in `LoadPropertySet()`).

| Type Constant | Subsystem | WriteState Format | Notes |
|--------------|-----------|-------------------|-------|
| `CT_HULL_SUBSYSTEM` | Hull | Base | One or more per ship (hull + bridge) |
| `CT_SHIELD_SUBSYSTEM` | Shield Generator | Base | Shield HP sent separately via flag 0x40 |
| `CT_SENSOR_SUBSYSTEM` | Sensor Array | Powered | |
| `CT_POWER_SUBSYSTEM` | Warp Core / Reactor | Power | Writes 2 extra power-level bytes |
| `CT_IMPULSE_ENGINE_SUBSYSTEM` | Impulse Engine System | Powered | Children: individual engines |
| `CT_WARP_ENGINE_SUBSYSTEM` | Warp Engine System | Powered | Children: individual engines |
| `CT_TORPEDO_SYSTEM` | Torpedo System | Powered | Children: torpedo tubes |
| `CT_PHASER_SYSTEM` | Phaser System | Powered | Children: phaser banks |
| `CT_TRACTOR_BEAM_SYSTEM` | Tractor Beam System | Powered | Children: tractor projectors |
| `CT_CLOAKING_SUBSYSTEM` | Cloaking Device | Powered | Only on ships with cloak |
| `CT_REPAIR_SUBSYSTEM` | Repair System | Powered | |

### Child Subsystems (serialized recursively within parents)

These subsystems are **not** directly in the serialization list. They are children of their parent system and are written recursively when their parent's `WriteState` runs.

| Type Constant | Subsystem | Parent System |
|--------------|-----------|---------------|
| `CT_PHASER_BANK` | Phaser Bank | Phaser System |
| `CT_TORPEDO_TUBE` | Torpedo Tube | Torpedo System |
| `CT_TRACTOR_BEAM_PROJECTOR` | Tractor Beam Projector | Tractor Beam System |
| `CT_PULSE_WEAPON` | Pulse Weapon | Pulse Weapon System |
| `CT_ENGINE_PROPERTY` | Individual Engine | Impulse Engine or Warp Engine System |

### How Reparenting Works

When a ship is created:
1. The hardpoint script adds ALL subsystems (both systems and individual components) to the serialization list via `AddToSet()`
2. A linking pass runs that identifies child subsystems and **removes them** from the serialization list
3. Removed children are attached to their parent system's child array
4. After linking, only top-level systems remain in the serialization list

This means the serialization list has far fewer entries than the total subsystem count (e.g., Sovereign has 11 top-level entries, not 33).

---

## 2. Wire Protocol Index Mapping

### There Is No Fixed Index Table

**Critical correction**: The wire protocol does NOT use a fixed index table (0=Reactor, 1=Repair, etc.). The `start_index` byte in flag 0x20 is a **position in the ship's serialization list**, and that list's contents and order are determined entirely by the hardpoint script.

Two different ship classes may have completely different subsystems at the same index position. A Sovereign has Hull at index 0; a Bird of Prey might have a different subsystem at index 0 depending on its hardpoint script.

### How Sender and Receiver Stay In Sync

Both the server and client:
1. Execute the **same** hardpoint Python script for each ship class (scripts are checksum-verified)
2. Run the **same** subsystem setup and linking code
3. End up with **identical** serialization lists in the same order

The `start_index` byte tells the receiver which list position the round-robin starts from. The receiver skips to that position in its own (identical) list and reads the data sequentially.

### Determining a Ship's Subsystem Order

To know the wire order for any ship class, examine its hardpoint script's `LoadPropertySet()` function. The order of `AddToSet("Scene Root", prop)` calls determines the initial list order. After the linking pass removes children, the remaining top-level systems are in their original relative order.

---

## 3. WriteState Serialization Formats

Each top-level subsystem writes its own data using one of three formats. The format is determined by the subsystem's type (not configurable).

### Format 1: Base (ShipSubsystem)

Used by: Hull, Shield Generator, individual weapons/engines (as children)

```
[condition: u8]           // Current HP as fraction of max: truncate(currentHP / maxHP * 255)
                          //   0xFF = 100% health, 0x00 = destroyed
[child_0 WriteState]      // Recursive: each child writes its own block
[child_1 WriteState]      // ...in child-array order (order added to parent)
...
```

The condition byte is always present. Children are written recursively — each child uses its own WriteState format (always Format 1 for individual weapons/engines).

### Format 2: Powered Subsystem

Used by: Sensor Array, Impulse Engine System, Warp Engine System, Torpedo System, Phaser System, Tractor Beam System, Cloaking Device, Repair System

```
[base WriteState]              // Condition byte + recursive children (Format 1)
[has_power_data: bit]          // Bit-packed boolean (0x21=yes, 0x20=no)
if has_power_data:
    [power_pct_wanted: u8]     // Requested power allocation: truncate(powerPercentage * 100)
                               //   Range 0-100, where 100 = full power requested
```

The `has_power_data` flag is **false for the local player's own ship** (optimization: the owner already has local power allocation state). For **remote ships**, it is always true and the power percentage byte follows.

### Format 3: Power Subsystem

Used by: Warp Core / Reactor only

```
[base WriteState]              // Condition byte + recursive children (Format 1)
[main_battery_pct: u8]        // Main battery level: truncate(mainPower / mainLimit * 255)
                               //   0xFF = fully charged, 0x00 = empty
[backup_battery_pct: u8]      // Backup battery level: truncate(backupPower / backupLimit * 255)
```

**Always** writes both battery bytes regardless of whether this is the local player's ship or a remote ship.

### Encoding Summary

| Value | Formula | Range | Scale |
|-------|---------|-------|-------|
| Condition (HP) | truncate(current / max * 255) | 0-255 | 0xFF = full |
| Power allocation | truncate(percentage * 100) | 0-100 | 100 = full power |
| Battery level | truncate(current / limit * 255) | 0-255 | 0xFF = full |

### Bit Packing

The `has_power_data` bits from consecutive Powered subsystems are packed using the standard bit-packing format (`[count:3][values:5]`, up to 5 bits per byte). WriteByte calls for condition and power bytes interleave independently with the bit stream.

### Byte Budget

Each subsystem's WriteState output is **variable-length**. A system with 8 phaser bank children writes more bytes than a hull with no children. The round-robin serializer writes subsystems until **10 bytes** of total stream space are consumed (including the `start_index` byte), then stops. The next tick picks up where it left off.

---

## 4. Round-Robin Serialization Algorithm

The server sends subsystem health data in a cycling window, not all at once:

```
state:
    cursor       // Current position in the subsystem list (persists across ticks)
    index        // Integer index of cursor position

algorithm:
    if cursor is uninitialized:
        cursor = first subsystem in list
        index = 0

    initial_cursor = cursor
    write_byte(index)           // start_index tells receiver where we begin

    loop:
        subsystem = cursor.data
        advance cursor to next
        increment index

        subsystem.WriteState(stream, isOwnShip)

        if cursor reached end of list:
            cursor = first subsystem in list
            index = 0

        if cursor == initial_cursor:
            break                   // Full cycle complete

        if total_bytes_written >= 10:
            break                   // Budget exhausted (10 bytes including start_index)

    // cursor and index persist for next tick
```

The budget check uses **total stream bytes written** (including the `start_index` byte), not just subsystem data bytes. This means 9 bytes are available for actual subsystem data.

Over multiple ticks, every subsystem gets its health synchronized. For a ship with 11 top-level systems, a full cycle takes ~4-6 ticks depending on how many children each system has and whether this is a remote or own ship.

---

## 5. Receiver Algorithm

```
start_index = read_byte(stream)

node = first subsystem in list
skip forward (start_index) nodes

loop while stream has data:
    if node is null: break

    subsystem = node.data
    advance node to next

    subsystem.ReadState(stream)     // Reads exactly what WriteState wrote

    if node reached end of list:
        node = first subsystem      // Wrap to beginning
```

The receiver's `ReadState` is the exact inverse of `WriteState` — it reads the condition byte, recursively reads children, and reads any type-specific extras. Because both sides have identical subsystem lists from the same hardpoint script, the formats always match.

---

## 6. Sovereign-Class Example

Based on the Sovereign's hardpoint script `LoadPropertySet()` call order, after the linking pass removes children:

| List Index | Subsystem | Format | Children | Bytes (remote ship) |
|-----------|-----------|--------|----------|---------------------|
| 0 | Hull | Base | 0 | 1 (condition) |
| 1 | Shield Generator | Base | 0 | 1 (condition; shield facing HP via flag 0x40) |
| 2 | Sensor Array | Powered | 0 | 3 (cond + bit + powerPct) |
| 3 | Warp Core (Reactor) | Power | 0 | 3 (cond + 2 battery bytes) |
| 4 | Impulse Engines | Powered | 2 (Port + Starboard) | 5 (cond + 2 children + bit + powerPct) |
| 5 | Torpedoes | Powered | 6 tubes (Fwd 1-4, Aft 1-2) | 9 (cond + 6 children + bit + powerPct) |
| 6 | Repair | Powered | 0 | 3 (cond + bit + powerPct) |
| 7 | Bridge (Hull) | Base | 0 | 1 (condition) |
| 8 | Phasers | Powered | 8 banks | 11 (cond + 8 children + bit + powerPct) |
| 9 | Tractors | Powered | 4 projectors | 7 (cond + 4 children + bit + powerPct) |
| 10 | Warp Engines | Powered | 2 (Port + Starboard) | 5 (cond + 2 children + bit + powerPct) |

**Total top-level entries: 11** (not 33 — the "33" count includes all individual weapons and engines)

For the **local player's own ship**, Powered subsystems omit the power percentage byte (bit=0), saving ~1 byte each. The Power subsystem (Reactor) always writes both battery bytes regardless.

### Example: Full Cycle Timing

With a 10-byte budget per tick at ~10 Hz (remote ship):
- Tick 1 (index 0): Hull(1) + Shield(1) + Sensor(3) + Reactor(3) = 8 bytes, then Impulse starts (5 bytes would exceed budget — serializer stops after budget check)
- Tick 2 (index 4): Impulse(5) + Torpedoes starts (9 bytes would exceed)
- Tick 3 (index 5): Torpedoes(9)
- Tick 4 (index 6): Repair(3) + Bridge(1) + Phasers starts (11 bytes would exceed)
- Tick 5 (index 8): Phasers(11) — over budget but completes since it started within budget
- Tick 6 (index 9): Tractors(7) + Warp Engines starts
- Full cycle: ~5-6 ticks = ~500-600ms

Actual timing varies because the serializer checks the budget **before** each subsystem's WriteState, not after. A subsystem that starts within budget will complete even if it exceeds the limit.

---

## 7. Subsystem HP Values (Sovereign Class)

Values from the Sovereign hardpoint script:

| Subsystem | Count | Max HP (Condition) | Repair Complexity |
|-----------|-------|--------------------|-------------------|
| Warp Core (reactor) | 1 | 7,000 | 2.0 |
| Repair System | 1 | 8,000 | 1.0 |
| Shield Generator | 1 | 10,000 | -- |
| Torpedo System | 1 | 6,000 | -- |
| Forward Torpedo Tubes | 4 | 2,200 each | -- |
| Aft Torpedo Tubes | 2 | 2,200 each | -- |
| Phaser Emitters | 8 | 1,000 each | -- |
| Impulse Engines (system) | 1 | 3,000 | 3.0 |
| Port/Starboard Impulse | 2 | 3,000 each | -- |
| Warp Engines (system) | 1 | 8,000 | -- |
| Port/Starboard Warp | 2 | 4,500 each | -- |
| Phaser Controller (system) | 1 | 8,000 | -- |
| Sensor Array | 1 | 8,000 | 1.0 |
| Tractor System | 1 | 3,000 | 7.0 |
| Tractor Beams | 4 | 1,500 each | 7.0 |
| Bridge | 1 | 10,000 | 4.0 |
| Hull | 1 | 12,000 | 3.0 |

### Shield HP (per facing)

| Facing | Max HP | Recharge Rate (HP/sec) |
|--------|--------|----------------------|
| Front | 11,000 | 12.0 |
| Rear | 5,500 | 12.0 |
| Top | 11,000 | 12.0 |
| Bottom | 11,000 | 12.0 |
| Left | 5,500 | 12.0 |
| Right | 5,500 | 12.0 |

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

## 8. Subsystem Type Constants

These constants identify subsystem types in the scripting API and are used during ship setup:

### System-Level Types (remain in serialization list)

| Constant | Name |
|----------|------|
| `CT_SHIP_SUBSYSTEM` | Base subsystem type |
| `CT_POWERED_SUBSYSTEM` | Base powered subsystem |
| `CT_WEAPON_SYSTEM` | Generic weapon system container |
| `CT_TORPEDO_SYSTEM` | Torpedo system container |
| `CT_PHASER_SYSTEM` | Phaser system container |
| `CT_PULSE_WEAPON_SYSTEM` | Pulse weapon system container |
| `CT_TRACTOR_BEAM_SYSTEM` | Tractor beam system container |
| `CT_POWER_SUBSYSTEM` | Reactor / warp core |
| `CT_SENSOR_SUBSYSTEM` | Sensor array |
| `CT_CLOAKING_SUBSYSTEM` | Cloaking device |
| `CT_WARP_ENGINE_SUBSYSTEM` | Warp engine system |
| `CT_IMPULSE_ENGINE_SUBSYSTEM` | Impulse engine system |
| `CT_HULL_SUBSYSTEM` | Hull section |
| `CT_SHIELD_SUBSYSTEM` | Shield generator |
| `CT_REPAIR_SUBSYSTEM` | Repair system |

### Child Types (removed from list, attached to parents)

| Constant | Name | Parent |
|----------|------|--------|
| `CT_PHASER_BANK` | Individual phaser bank | Phaser System |
| `CT_TORPEDO_TUBE` | Individual torpedo tube | Torpedo System |
| `CT_TRACTOR_BEAM_PROJECTOR` | Individual tractor projector | Tractor Beam System |
| `CT_PULSE_WEAPON` | Individual pulse weapon | Pulse Weapon System |
| `CT_ENGINE_PROPERTY` | Individual engine | Impulse or Warp Engine System |

---

## 9. Key Behavioral Guarantees

1. **Order is script-determined**: The serialization list order comes from the hardpoint script's `AddToSet()` call order, not from any fixed table.

2. **Both sides agree**: Server and client always have identical serialization lists because they run the same checksum-verified hardpoint script and the same setup/linking logic.

3. **Missing subsystems are absent, not padded**: A ship without a cloaking device simply doesn't have a cloak entry in its list. There is no 0xFF padding or skipped index.

4. **Children are recursive**: When a system writes its state, it recursively writes all children. The receiver reads them in the same order.

5. **Round-robin is position-based**: The `start_index` byte is a list position (0 through list_length-1), not a subsystem type ID.

6. **Budget is approximate**: The 10-byte budget (including `start_index`) is checked **before** each subsystem's WriteState call. A subsystem that starts within budget will finish its complete WriteState output even if it exceeds the limit.

7. **Full cycle time depends on ship class**: Ships with more subsystems (especially many weapons) take more ticks to complete a full health cycle.

---

## Related Documents

- **[stateupdate-wire-format.md](stateupdate-wire-format.md)** -- StateUpdate message format, dirty flags, compressed types
- **[combat-system.md](combat-system.md)** -- How damage interacts with subsystems
- **[objcreate-wire-format.md](objcreate-wire-format.md)** -- Ship creation message
