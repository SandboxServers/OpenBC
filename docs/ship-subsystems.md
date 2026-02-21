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
| `CT_ENGINE_PROPERTY` | Individual Engine | Impulse or Warp Engine System (determined by engine type tag — see below) |

### How Reparenting Works

When a ship is created:
1. The hardpoint script adds ALL subsystems (both systems and individual components) to the serialization list via `AddToSet()`
2. A linking pass runs that identifies child subsystems and **removes them** from the serialization list
3. Removed children are attached to their parent system's child array
4. After linking, only top-level systems remain in the serialization list

This means the serialization list has far fewer entries than the total subsystem count (e.g., Sovereign has 11 top-level entries, not 33).

### Engine Type Tag

Individual engines (`CT_ENGINE_PROPERTY`) are the **only** child subsystem type that can belong to either of two different parent systems. All other child types have unambiguous parents — phasers always go to the phaser system, torpedoes always go to the torpedo system, etc. But an individual engine could be an impulse engine or a warp engine.

The game resolves this with an **explicit tag** set in the hardpoint script via `SetEngineType()`:

| Enum Value | Constant | Meaning |
|-----------|----------|---------|
| 0 | `EP_IMPULSE` | Attach to the Impulse Engine System |
| 1 | `EP_WARP` | Attach to the Warp Engine System |

**Default**: `EP_IMPULSE` — if `SetEngineType()` is never called, the engine is treated as an impulse engine.

**Hardpoint script example** (Sovereign class):
```python
PortImpulse = App.EngineProperty_Create("Port Impulse")
PortImpulse.SetEngineType(PortImpulse.EP_IMPULSE)

PortWarp = App.EngineProperty_Create("Port Warp")
PortWarp.SetEngineType(PortWarp.EP_WARP)
```

During the linking pass, the engine type tag is read to determine which parent system receives each individual engine child.

**Implementation notes**:
- The scraper must parse `SetEngineType()` calls from hardpoint scripts and store the engine type in `serialization.json` children entries (e.g., `"engine_type": "impulse"` or `"engine_type": "warp"`)
- The server's linking pass must read the engine type to assign each individual engine to the correct parent system container
- All 16 stock ships explicitly call `SetEngineType()` on every individual engine — no stock ship relies on the default. However, mods may omit the call, so honoring the default (EP_IMPULSE) is important for compatibility.

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

## 6. Cross-Reference: All Stock Flyable Ships

Summary of all 16 stock multiplayer ships showing top-level subsystem count and weapon/engine children. Ships are grouped by faction.

| Ship | Faction | Top-Level | Beams | Torps | Impulse | Warp | Tractors | Cloak | Repair | Bridge |
|------|---------|-----------|-------|-------|---------|------|----------|-------|--------|--------|
| Sovereign | Federation | 11 | 8 | 6 | 2 | 2 | 4 | -- | Yes | Yes |
| Galaxy | Federation | 11 | 8 | 6 | 3 | 2 | 4 | -- | Yes | Yes |
| Nebula | Federation | 11 | 8 | 6 | 2 | 2 | 2 | -- | Yes | Yes |
| Akira | Federation | 11 | 8 | 6 | 2 | 2 | 2 | -- | Yes | Yes |
| Ambassador | Federation | 11 | 8 | 4 | 2 | 2 | 2 | -- | Yes | Yes |
| Shuttle | Federation | 9 | 1 | 0 | 2 | 2 | 1 | -- | Yes | -- |
| Vor'cha | Klingon | 12 | 1 | 3 | 2 | 2 | 2 | Yes | Yes | -- |
| Bird of Prey | Klingon | 10 | 0 | 1 | 1 | 2 | 0 | Yes | Yes | -- |
| Warbird | Romulan | 13 | 1 | 2 | 2 | 2 | 2 | Yes | Yes | Yes |
| Marauder | Ferengi | 10 | 1 | 0 | 2 | 2 | 2 | -- | Yes | -- |
| Galor | Cardassian | 9 | 4 | 1 | 2 | 1 | 0 | -- | Yes | -- |
| Keldon | Cardassian | 10 | 4 | 2 | 4 | 1 | 2 | -- | Yes | -- |
| Card. Hybrid | Cardassian | 11 | 7 | 3 | 2 | 3 | 2 | -- | Yes | -- |
| Kessok Heavy | Kessok | 10 | 8 | 2 | 2 | 2 | 0 | Yes | Yes | -- |
| Kessok Light | Kessok | 10 | 8 | 1 | 2 | 2 | 0 | Yes | Yes | -- |
| Card. Freighter | Civilian | 8 | 0 | 0 | 2 | 1 | 1 | -- | Yes | -- |

**Column notes**:
- **Top-Level**: Number of entries in the serialization list (determines round-robin cycle length)
- **Beams/Torps/Impulse/Warp/Tractors**: Number of *children* recursively serialized within their parent system
- **Cloak**: Whether a Cloaking Device entry exists in the serialization list
- **Repair**: All ships have a repair system (named "Repair", "Repair System", or "Engineering")
- **Bridge**: Whether a Bridge hull subsystem exists (separate from the main Hull entry)

**Weapon system note**: The Vor'cha, Warbird, Bird of Prey, Marauder, and Card. Hybrid have **two weapon system entries** — one for beam/phaser weapons (`WST_PHASER`) and one for pulse/disruptor cannons (`WST_PULSE`). These appear as separate top-level entries in the serialization list. All other ships have a single phaser/beam system.

---

## 7. Per-Ship Serialization Lists

For each ship, the table shows the serialization list after the linking pass removes children. The **List Index** column is the `start_index` value used in the wire protocol. The **Bytes** column shows bytes written per WriteState call for a **remote ship** (Powered subsystems include the power data bit + byte).

### Federation

#### Sovereign (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Power | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 5 | Torpedoes | Powered | 6 (Fwd 1-4, Aft 1-2) | 9 |
| 6 | Repair | Powered | 0 | 3 |
| 7 | Bridge | Base | 0 | 1 |
| 8 | Phasers | Powered | 8 (Ventral 1-4, Dorsal 1-4) | 11 |
| 9 | Tractors | Powered | 4 (Aft 1-2, Fwd 1-2) | 7 |
| 10 | Warp Engines | Powered | 2 (Port, Star) | 5 |

Full cycle (remote): ~49 bytes over ~5-6 ticks (~500-600ms)

#### Galaxy (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Power | 0 | 3 |
| 2 | Shield Generator | Base | 0 | 1 |
| 3 | Sensor Array | Powered | 0 | 3 |
| 4 | Torpedoes | Powered | 6 (Fwd 1-4, Aft 1-2) | 9 |
| 5 | Phasers | Powered | 8 (Ventral 1-4, Dorsal 1-4) | 11 |
| 6 | Impulse Engines | Powered | 3 (Port, Star, Center) | 6 |
| 7 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 8 | Tractors | Powered | 4 (Aft 1-2, Fwd 1-2) | 7 |
| 9 | Bridge | Base | 0 | 1 |
| 10 | Engineering | Powered | 0 | 3 |

#### Nebula (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Power | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 5 | Phasers | Powered | 8 (Ventral 1-4, Dorsal 1-4) | 11 |
| 6 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Torpedoes | Powered | 6 (Fwd 1-4, Aft 1-2) | 9 |
| 8 | Repair | Powered | 0 | 3 |
| 9 | Tractors | Powered | 2 (Aft, Fwd) | 5 |
| 10 | Bridge | Base | 0 | 1 |

#### Akira (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Power | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 5 | Phasers | Powered | 8 (Ventral 1-4, Dorsal 1-4) | 11 |
| 6 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Torpedoes | Powered | 6 (Fwd 1-4, Aft 1-2) | 9 |
| 8 | Engineering | Powered | 0 | 3 |
| 9 | Tractors | Powered | 2 (Fwd, Aft) | 5 |
| 10 | Bridge | Base | 0 | 1 |

#### Ambassador (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Power | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 5 | Phasers | Powered | 8 (Ventral 1-3, Dorsal 1-3, Aft 1-2) | 11 |
| 6 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Torpedoes | Powered | 4 (Fwd 1-2, Aft 1-2) | 7 |
| 8 | Engineering | Powered | 0 | 3 |
| 9 | Bridge | Base | 0 | 1 |
| 10 | Tractors | Powered | 2 (Fwd, Aft) | 5 |

#### Shuttle (9 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Sensor Array | Powered | 0 | 3 |
| 4 | Shield Generator | Base | 0 | 1 |
| 5 | Phasers | Powered | 1 (Phaser) | 4 |
| 6 | Repair | Powered | 0 | 3 |
| 7 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 8 | Tractors | Powered | 1 (Fwd Tractor) | 4 |

Full cycle: ~29 bytes over ~3-4 ticks

### Klingon

#### Vor'cha (12 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Disruptor Beams | Powered | 1 (Disruptor) | 4 |
| 4 | Disruptor Cannons | Powered | 2 (Port, Star Cannon) | 5 |
| 5 | Torpedoes | Powered | 3 (Fwd 1-2, Aft) | 6 |
| 6 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 8 | Cloaking Device | Powered | 0 | 3 |
| 9 | Sensor Array | Powered | 0 | 3 |
| 10 | Repair System | Powered | 0 | 3 |
| 11 | Tractors | Powered | 2 (Aft, Fwd) | 5 |

Note: Two weapon systems — Disruptor Beams (WST_PHASER, idx 3) and Disruptor Cannons (WST_PULSE, idx 4).

#### Bird of Prey (10 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Disruptor Cannons | Powered | 2 (Port, Star Cannon) | 5 |
| 4 | Torpedoes | Powered | 1 (Fwd Torpedo) | 4 |
| 5 | Impulse Engines | Powered | 1 (Impulse Engine) | 4 |
| 6 | Warp Engines | Powered | 2 (Port, Star Warp) | 5 |
| 7 | Cloaking Device | Powered | 0 | 3 |
| 8 | Sensor Array | Powered | 0 | 3 |
| 9 | Engineering | Powered | 0 | 3 |

Note: No beam weapon system (only pulse cannons). No tractors, no bridge.

### Romulan

#### Warbird (13 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Power Plant | Power | 0 | 3 |
| 3 | Disruptor Beam | Powered | 1 (Disruptor) | 4 |
| 4 | Disruptor Cannons | Powered | 4 (Port 1-2, Star 1-2) | 7 |
| 5 | Torpedoes | Powered | 2 (Fwd, Aft) | 5 |
| 6 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 8 | Cloaking Device | Powered | 0 | 3 |
| 9 | Sensor Array | Powered | 0 | 3 |
| 10 | Engineering | Powered | 0 | 3 |
| 11 | Bridge | Base | 0 | 1 |
| 12 | Tractors | Powered | 2 (Aft, Fwd) | 5 |

Most complex ship (13 top-level entries = longest full cycle). Two weapon systems — Disruptor Beam (WST_PHASER) and Disruptor Cannons (WST_PULSE). "Power Plant" is the Romulan name for the reactor.

### Ferengi

#### Marauder (10 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Phasers | Powered | 1 (Ventral Phaser) | 4 |
| 4 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 5 | Warp Engines | Powered | 2 (Star, Port) | 5 |
| 6 | Tractors | Powered | 2 (Fwd, Aft) | 5 |
| 7 | Sensor Array | Powered | 0 | 3 |
| 8 | Repair Subsystem | Powered | 0 | 3 |
| 9 | Plasma Emitters | Powered | 2 (Port, Star Emitter) | 5 |

Two weapon systems — Phasers (WST_PHASER, idx 3) and Plasma Emitters (WST_PULSE, idx 9). No torpedoes.

### Cardassian

#### Galor (9 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Compressors | Powered | 4 (Fwd, Port, Star, Aft Beam) | 7 |
| 4 | Torpedoes | Powered | 1 (Fwd Torpedo) | 4 |
| 5 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 6 | Warp Engine | Powered | 1 (Warp Engine 1) | 4 |
| 7 | Repair Subsystem | Powered | 0 | 3 |
| 8 | Sensor Array | Powered | 0 | 3 |

Note: Single warp engine system with 1 child. No tractors. "Compressors" is the Cardassian phaser system name.

#### Keldon (10 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Compressors | Powered | 4 (Fwd, Port, Star, Aft Beam) | 7 |
| 4 | Torpedoes | Powered | 2 (Fwd, Aft) | 5 |
| 5 | Impulse Engines | Powered | 4 (Engine 1-4) | 7 |
| 6 | Warp Engine | Powered | 1 (Warp Engine 1) | 4 |
| 7 | Sensor Array | Powered | 0 | 3 |
| 8 | Repair Subsystem | Powered | 0 | 3 |
| 9 | Tractors | Powered | 2 (Ventral, Dorsal) | 5 |

Note: 4 impulse engines (unique among stock ships). Single warp engine system with 1 child.

#### Cardassian Hybrid (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Power | 0 | 3 |
| 2 | Torpedoes | Powered | 3 (Torpedo 1-2, Aft) | 6 |
| 3 | Repair System | Powered | 0 | 3 |
| 4 | Shield Generator | Base | 0 | 1 |
| 5 | Sensor Array | Powered | 0 | 3 |
| 6 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Warp Engines | Powered | 3 (Port, Star, Center) | 6 |
| 8 | Beams | Powered | 7 (Fwd Compressor, Fwd 1-2, Ventral 1-2, Dorsal 1-2) | 10 |
| 9 | Disruptor Cannons | Powered | 1 (Cannon) | 4 |
| 10 | Tractors | Powered | 2 (Fwd, Aft) | 5 |

Two weapon systems — Beams (WST_PHASER, idx 8) and Disruptor Cannons (WST_PULSE, idx 9). 3 warp engine children (unique: has Center Warp).

### Kessok

#### Kessok Heavy (10 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Power | 0 | 3 |
| 2 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 3 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 4 | Positron Beams | Powered | 8 (Fwd 1-4, Ventral 1-2, Dorsal 1-2) | 11 |
| 5 | Torpedoes | Powered | 2 (Tube 1-2) | 5 |
| 6 | Repair System | Powered | 0 | 3 |
| 7 | Shield Generator | Base | 0 | 1 |
| 8 | Sensor Array | Powered | 0 | 3 |
| 9 | Cloaking Device | Powered | 0 | 3 |

"Positron Beams" is the Kessok phaser system name (WST_PHASER type, 8 children).

#### Kessok Light (10 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Power | 0 | 3 |
| 2 | Torpedoes | Powered | 1 (Torpedo) | 4 |
| 3 | Repair System | Powered | 0 | 3 |
| 4 | Shield Generator | Base | 0 | 1 |
| 5 | Sensor Array | Powered | 0 | 3 |
| 6 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 8 | Beams | Powered | 8 (Fwd 1-2, Port 1-2, Star 1-2, Aft 1-2) | 11 |
| 9 | Cloaking Device | Powered | 0 | 3 |

### Civilian

#### Cardassian Freighter (8 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 2 | Warp Engines | Powered | 1 (Warp) | 4 |
| 3 | Engineering | Powered | 0 | 3 |
| 4 | Tractors | Powered | 1 (Tractor Beam) | 4 |
| 5 | Sensor Array | Powered | 0 | 3 |
| 6 | Shield Generator | Base | 0 | 1 |
| 7 | Warp Core | Power | 0 | 3 |

Simplest ship: no weapons, no bridge. Full cycle: ~24 bytes over ~3 ticks.

---

## 8. Sovereign-Class Detailed Example

For the **local player's own ship**, Powered subsystems omit the power percentage byte (bit=0), saving ~1 byte each. The Power subsystem (Reactor) always writes both battery bytes regardless.

### Full Cycle Timing (remote ship)

With a 10-byte budget per tick at ~10 Hz:
- Tick 1 (index 0): Hull(1) + Shield(1) + Sensor(3) + Reactor(3) = 8 bytes, then Impulse starts (5 bytes would exceed budget — serializer stops after budget check)
- Tick 2 (index 4): Impulse(5) + Torpedoes starts (9 bytes would exceed)
- Tick 3 (index 5): Torpedoes(9)
- Tick 4 (index 6): Repair(3) + Bridge(1) + Phasers starts (11 bytes would exceed)
- Tick 5 (index 8): Phasers(11) — over budget but completes since it started within budget
- Tick 6 (index 9): Tractors(7) + Warp Engines starts
- Full cycle: ~5-6 ticks = ~500-600ms

Actual timing varies because the serializer checks the budget **before** each subsystem's WriteState, not after. A subsystem that starts within budget will complete even if it exceeds the limit.

---

## 9. Subsystem HP Values (Sovereign Class)

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

## 10. Subsystem Type Constants

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

## 11. Key Behavioral Guarantees

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
