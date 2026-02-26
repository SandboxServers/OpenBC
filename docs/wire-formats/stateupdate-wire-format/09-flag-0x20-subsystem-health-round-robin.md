# Flag 0x20 — Subsystem Health (Round-Robin)


Server-to-client only. Carries subsystem health data using a round-robin scheme that cycles through the ship's **top-level subsystem list** over multiple updates.

```
+0      1     u8      start_index        Position in subsystem list where this batch begins
+1      var   bytes   subsystem_data     Per-subsystem WriteState output (variable length)
```

**No count field**: the receiver reads subsystem data until the stream is exhausted (stream position reaches total data length). Both sides have identical lists from the same checksum-verified hardpoint script, so the formats always match.

### Subsystem List Order

**There is no fixed index table.** The `start_index` is a position in the ship's serialization list, whose contents and order are determined by the ship's hardpoint script (`LoadPropertySet()` call order). A Sovereign has 11 top-level entries; a Bird of Prey may have a different count.

Only **top-level system containers** are in the list. Individual weapons (phaser banks, torpedo tubes) and engines are children of their parent systems and are serialized **recursively** within the parent's WriteState output.

### Per-Subsystem WriteState Formats

Each subsystem writes variable-length data via a virtual method (WriteState). Three implementations exist, determined by subsystem type:

**Format 1: Base** (Hull, Shield Generator, individual children):
```
[condition: u8]           // HP fraction: truncate(currentHP / maxHP * 255)
                          //   0xFF = 100% health, 0x00 = destroyed
[child_0 WriteState]      // Recursive: each child writes its own block
[child_1 WriteState]
...
```

**Format 2: Powered** (Sensors, Engines, Weapons, Cloak, Repair, Tractors):
```
[base format]             // Condition byte + recursive children (Format 1)
[has_power_data: bit]     // Bit-packed boolean (0x21=yes, 0x20=no)
if has_power_data:
    [power_pct: u8]       // Requested power allocation: truncate(powerPercentageWanted * 100)
                          //   Range 0-100, where 100 = full power requested
```

The `has_power_data` bit is **true for remote ships** (the receiver doesn't know their power allocation) and **false for the local player's own ship** (the owner already has this state locally). This is a bandwidth optimization.

**Format 3: Power** (Warp Core / Reactor only):
```
[base format]             // Condition byte + recursive children (Format 1)
[main_battery: u8]        // Main battery level: truncate(mainPower / mainLimit * 255)
[backup_battery: u8]      // Backup battery level: truncate(backupPower / backupLimit * 255)
```

Power subsystem **always** writes both battery bytes regardless of whether this is the local player's ship or a remote ship.

### Bit Packing

The `has_power_data` bits from consecutive Powered subsystems are packed using the standard bit-packing format (`[count:3][values:5]`, up to 5 bits per byte). WriteByte calls for condition/power bytes interleave independently with the bit stream.

### Round-Robin Budget

The serializer writes subsystems sequentially starting from `start_index` until **10 bytes** of total stream space are consumed (including the `start_index` byte itself), then stops. The next tick picks up where it left off. When the cursor reaches the end of the list, it wraps to index 0 and continues. The serializer also stops if it completes a full cycle (cursor returns to its starting position).

A full cycle of a Sovereign's 11 top-level systems takes ~4 ticks (~400ms at 10 Hz).

For the complete subsystem hierarchy, type constants, Sovereign-class example, and behavioral guarantees, see [ship-subsystems/](../../game-systems/ship-subsystems/).

**Size**: Variable (typically 7-15 bytes per update, depends on child count).

