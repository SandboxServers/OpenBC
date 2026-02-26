# 3. WriteState Serialization Formats


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

