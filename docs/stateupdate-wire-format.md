# StateUpdate Wire Format (Opcode 0x1C)

Wire format specification for Star Trek: Bridge Commander's state synchronization message, documented from network packet captures and the game's shipped scripting API.

## Overview

Opcode 0x1C (StateUpdate) is the most frequently sent message in the multiplayer protocol, accounting for approximately 97% of all game traffic. It carries per-ship position, orientation, speed, subsystem health, and weapon status using a dirty-flag system that transmits only changed fields.

StateUpdate is the only game message sent with **unreliable** delivery (fire-and-forget, no ACK required). All other game messages use reliable delivery. This is a deliberate bandwidth optimization — at ~10 updates per ship per second, retransmitting lost updates would be wasteful since the next update supersedes the lost one.

## Message Header

```
Offset  Size  Type    Field           Description
------  ----  ----    -----           -----------
0       1     u8      opcode          Always 0x1C
1       4     i32     object_id       Ship's network object ID (little-endian)
5       4     f32     game_time       Current game clock timestamp (little-endian)
9       1     u8      dirty_flags     Bitmask of which fields follow
```

**Header size**: 10 bytes, always present. The `dirty_flags` byte determines which variable-length fields follow the header.

## Dirty Flags

Each bit in the `dirty_flags` byte indicates whether the corresponding field is present in this message:

| Bit | Mask | Name | Data Size | Description |
|-----|------|------|-----------|-------------|
| 0 | 0x01 | POSITION | 12-15 bytes | Absolute world position + optional integrity hash |
| 1 | 0x02 | DELTA | 5 bytes | Compressed position delta from last absolute |
| 2 | 0x04 | FORWARD | 3 bytes | Forward orientation vector |
| 3 | 0x08 | UP | 3 bytes | Up orientation vector |
| 4 | 0x10 | SPEED | 2 bytes | Current speed |
| 5 | 0x20 | SUBSYSTEMS | variable | Subsystem health round-robin |
| 6 | 0x40 | CLOAK | 1 byte | Cloaking device state |
| 7 | 0x80 | WEAPONS | variable | Weapon health round-robin |

Fields are serialized in flag-bit order (0x01 first, 0x80 last). Only fields whose flag bit is set are present.

### Direction-Based Flag Split

Verified across 199,541 StateUpdate messages from a 34-minute 3-player combat session:

| Direction | Always Includes | Never Includes |
|-----------|-----------------|----------------|
| **Ship owner -> server** | 0x80 (WEAPONS) | 0x20 (SUBSYSTEMS) |
| **Server -> all clients** | 0x20 (SUBSYSTEMS) | 0x80 (WEAPONS) |

This reflects the authority model:
- **Ship owners** are authoritative for their own position, orientation, speed, and weapon charge/cooldown state
- **The server** is authoritative for subsystem health (damage is computed server-side)

The two flags are **mutually exclusive by direction** in multiplayer. A StateUpdate never contains both 0x20 and 0x80 simultaneously.

**Common client-to-server flag patterns** (observed frequencies):
- `0x9E` — DELTA + FORWARD + UP + SPEED + WEAPONS (full movement + weapons)
- `0x96` — DELTA + FORWARD + SPEED + WEAPONS
- `0x92` — DELTA + SPEED + WEAPONS
- `0x9D` — POSITION + FORWARD + UP + SPEED + WEAPONS (periodic absolute sync)

**Common server-to-client flag patterns**:
- `0x20` — SUBSYSTEMS only (most common; server sends health data without position)
- `0x3E` — DELTA + FORWARD + UP + SPEED + SUBSYSTEMS
- `0x3D` — POSITION + FORWARD + UP + SPEED + SUBSYSTEMS

## Flag 0x01 — Absolute Position

Sent periodically to reset the delta compression baseline and provide authoritative position.

```
Offset  Size  Type           Field           Description
------  ----  ----           -----           -----------
+0      4     f32            position_x      World X coordinate
+4      4     f32            position_y      World Y coordinate
+8      4     f32            position_z      World Z coordinate
+12     1     bitpacked      has_hash        Boolean: is integrity hash present?
[if has_hash AND multiplayer:]
+13     2     u16            subsys_hash     XOR-folded subsystem integrity hash
```

All floats are IEEE 754 single-precision, little-endian.

The `has_hash` field is a **bit-packed boolean** (see Bit Packing below), NOT a raw byte. On the wire it appears as `0x21` (true) or `0x20` (false).

When absolute position is sent, the delta compression baseline is reset: saved position becomes the current position, and the delta direction/magnitude are cleared to zero.

**Total size**: 13 bytes (no hash) or 15 bytes (with hash).

### Subsystem Integrity Hash

The hash is an anti-cheat mechanism. The sender computes a 32-bit hash from all subsystem health and property values, XOR-folds it to 16 bits, and transmits it. The receiver computes the same hash locally and compares.

**Hash algorithm**:
```
accumulator = 0x00000000

For each subsystem (shields, hull, sensors, engines, weapons, cloak, repair, crew, power):
    For each health/property float value in the subsystem:
        ival = truncate_to_int(abs(value))
        if value < 0: ival = -ival
        accumulator = accumulator XOR ival  (byte-by-byte XOR)
        accumulator = rotate_left(accumulator, 1)

wire_hash = (accumulator >> 16) XOR (accumulator & 0xFFFF)
```

If the received hash does not match the locally computed hash, the sender is kicked from the game (anti-cheat violation).

## Flag 0x02 — Position Delta (Compressed)

Sent on most updates. Encodes the change in position since the last absolute position was sent.

```
+0      5     cv4     position_delta     CompressedVector4 (see Compressed Types)
```

The delta is: `(current_x - saved_x, current_y - saved_y, current_z - saved_z)`.

Wire format: 3 direction bytes + 2-byte CompressedFloat16 magnitude = **5 bytes total**.

Only sent when the delta direction or magnitude has changed from the previously sent values.

## Flag 0x04 — Forward Orientation

```
+0      3     cv3     forward_vector     CompressedVector3 (see Compressed Types)
```

The ship's forward-facing direction as a unit vector. Each byte is a signed direction component: `truncate_to_int(component * 127.0)`, giving a range of -1.0 to +1.0 per axis.

**Size**: 3 bytes.

## Flag 0x08 — Up Orientation

```
+0      3     cv3     up_vector          CompressedVector3
```

The ship's up direction as a unit vector. Same encoding as forward orientation.

**Size**: 3 bytes.

Together, forward + up fully define the ship's 3D orientation (the right vector can be derived via cross product).

## Flag 0x10 — Speed

```
+0      2     u16     speed_compressed   CompressedFloat16 (see Compressed Types)
```

Current speed magnitude. Negative values indicate reverse thrust (the sign is encoded in the CompressedFloat16).

**Size**: 2 bytes.

## Flag 0x20 — Subsystem Health (Round-Robin)

Server-to-client only. Carries subsystem health data using a round-robin scheme that cycles through all subsystems over multiple updates.

```
+0      1     u8      start_index        Index of first subsystem in this batch
+1      var   bytes   subsystem_data     Per-subsystem health (type-dependent)
```

The serializer writes subsystem data starting from `start_index`, advancing through the ship's subsystem list. Each update sends approximately 10 bytes of subsystem data before stopping (bandwidth budget). The next update picks up where this one left off, wrapping around to the beginning when the end of the list is reached.

Each subsystem writes its own state in a type-dependent format. The common pattern is one or more health bytes where `0xFF = 100% health` and `0x00 = 0% (destroyed)`.

For the complete subsystem index table and per-type details, see [ship-subsystems.md](ship-subsystems.md).

**Size**: Variable (typically 7-11 bytes per update).

## Flag 0x40 — Cloak State

```
+0      1     bitpacked   cloak_active     Boolean: 0x20=decloaked, 0x21=cloaked
```

The cloak field is a **bit-packed boolean**, NOT a raw 0/1 byte. Writing a plain `0x00`/`0x01` instead of `0x20`/`0x21` will desynchronize the stream parser.

Only sent when the cloak state changes.

**Size**: 1 byte.

## Flag 0x80 — Weapon Health (Round-Robin)

Client-to-server only. Reports weapon subsystem health using a round-robin scheme similar to flag 0x20.

```
[repeated while within ~6-byte budget:]
  +0    1     u8      weapon_index       Index of this weapon in the ship's weapon list
  +1    1     u8      health_byte        Scaled health: truncate(health * scale_factor)
```

Each weapon entry is 2 bytes: `[index:u8][health:u8]`. The serializer iterates through the weapon list, sending a few entries per update and cycling through all weapons over time.

**Size**: Variable (typically 4-6 bytes per update).

## Compressed Data Types

StateUpdate uses three compressed types for bandwidth efficiency.

### Bit-Packed Booleans

Multiple boolean values are packed into a single byte:

```
Byte layout:  [count:3][bits:count]
              MSB              LSB

count (bits 7-5): Number of booleans packed (1-5), stored as count
bits  (bits 4-0): The boolean values, one per bit position
```

Examples:
- Single false: `0x20` = `0b001_00000` (count=1, bit0=0)
- Single true: `0x21` = `0b001_00001` (count=1, bit0=1)
- Three booleans (true, false, false): `0x61` = `0b011_00001`

Up to 5 booleans can share a single byte. After 5 bits, the next boolean starts a new byte.

**Critical**: Writing a plain `0x00`/`0x01` instead of bit-packed `0x20`/`0x21` will misalign all subsequent field reads in the stream.

### CompressedFloat16 (cf16)

16-bit logarithmic float encoding used for speed, damage, and distance values.

```
Format: [sign:1][scale:3][mantissa:12]
        Bit 15     = sign (1 = negative)
        Bits 14-12 = scale exponent (0-7)
        Bits 11-0  = mantissa (0-4095)
```

**Encoding**:
1. If value < 0: set sign bit, negate value
2. Find scale (0-7) such that value falls within the range for that scale octave. Each successive scale covers a range BASE * MULT^scale
3. Compute mantissa: `truncate(value / range * 4096)`
4. If scale overflows (>= 8): clamp to scale=7, mantissa=4096
5. Result: `(sign_flag * 8 + scale) * 0x1000 + mantissa`

**Decoding**:
1. mantissa = encoded & 0xFFF
2. raw_scale = encoded >> 12
3. sign = (raw_scale >> 3) & 1
4. scale = raw_scale & 0x7
5. Compute range boundaries: lo=0, hi=BASE; for each scale step: lo=hi, hi=hi*MULT
6. result = (hi - lo) * mantissa / 4095.0 + lo
7. If sign: result = -result

**Precision note**: The encoder divides by 4096.0 while the decoder divides by 4095.0. This intentional asymmetry means round-trip encode/decode has less than 0.025% error.

Observed decoded values from packet traces:
- Speed: 5.130, 7.618, 7.598
- Damage: 50.0, 10.1
- Radius: 5997.8

### CompressedVector3 (cv3)

3-byte direction vector. Used for forward/up orientation and torpedo directions.

```
Wire format: [dirX:u8][dirY:u8][dirZ:u8] = 3 bytes

Each byte: truncate_to_int(component * 127.0)
Range: -1.0 to +1.0 per component (signed byte interpretation)
```

Example from TorpedoFire: bytes `DF 87 11` = direction (-0.26, -0.95, 0.13).

### CompressedVector4 (cv4)

5-byte position delta. A cv3 direction plus a CompressedFloat16 magnitude.

```
Wire format: [dirX:u8][dirY:u8][dirZ:u8][magnitude:u16] = 5 bytes

Direction: same as cv3 (signed bytes / 127.0)
Magnitude: CompressedFloat16 encoding (2 bytes, little-endian)
```

To reconstruct the delta vector: `(dirX * magnitude, dirY * magnitude, dirZ * magnitude)`.

Used for position deltas in StateUpdate and impact positions in Explosion messages.

## Force-Update Timing

The serializer tracks per-field timestamps for when each field was last sent. A field is force-sent when the elapsed time since last transmission exceeds a global threshold, even if the value hasn't changed. This ensures all fields are periodically refreshed despite unreliable delivery.

When all dirty fields are sent simultaneously, the master force-update timer resets.

## Decoded Packet Examples

### Example 1: Client StateUpdate (flags=0x9D — POSITION + FORWARD + UP + SPEED + WEAPONS)

```
Client -> Server (embedded in 208-byte packet):

Raw bytes:
1C FF FF FF 3F 00 80 E1 41 9D 00 00 B0 42 00 00
84 C2 00 00 92 C2 21 37 FB 0B 68 46 30 BB 5E 00
00 01 CC 02 CC 04 CC
```

| Bytes | Field | Decoded Value |
|-------|-------|---------------|
| `1C` | opcode | 0x1C (StateUpdate) |
| `FF FF FF 3F` | object_id | 0x3FFFFFFF (player 0's ship) |
| `00 80 E1 41` | game_time | 28.19 seconds |
| `9D` | dirty_flags | 0x9D = POSITION + FORWARD + UP + SPEED + WEAPONS |
| `00 00 B0 42` | position_x | 88.0 |
| `00 00 84 C2` | position_y | -66.0 |
| `00 00 92 C2` | position_z | -73.0 |
| `21` | has_hash | bit-packed true (0x21) |
| `37 FB` | subsys_hash | 0xFB37 |
| `0B 68 46` | forward_vector | (0.09, 0.82, 0.55) |
| `30 BB 5E` | up_vector | (0.38, -0.54, 0.74) |
| `00 00` | speed | 0.0 (stationary) |
| `01 CC 02 CC 04 CC` | weapons | [subsys1:100%, subsys2:100%, subsys4:100%] |

Note: This is from a freshly spawned ship (speed 0, full weapon health). The subsystem hash 0xFB37 is consistent across all position updates for this ship in the session.

### Example 2: Server StateUpdate (flags=0x20 — SUBSYSTEMS only)

```
Server -> Client, 20-byte game payload (within 30-byte transport frame):

Raw bytes:
1C FF FF FF 3F 00 A0 1B 42 20 08 FF 60 FF FF FF
FF FF FF FF FF FF FF FF FF

Decoded:
  opcode: 0x1C (StateUpdate)
  object_id: 0x3FFFFFFF (player 0's ship)
  game_time: 38.91 seconds
  dirty_flags: 0x20 (SUBSYSTEMS only)
  start_index: 8
  health bytes: FF 60 FF FF FF FF FF FF FF FF FF FF FF
```

| Bytes | Field | Decoded Value |
|-------|-------|---------------|
| `1C` | opcode | 0x1C (StateUpdate) |
| `FF FF FF 3F` | object_id | 0x3FFFFFFF |
| `00 A0 1B 42` | game_time | 38.91 seconds |
| `20` | dirty_flags | 0x20 (SUBSYSTEMS) |
| `08` | start_index | Start at subsystem index 8 |
| `FF` | subsystem 8 | 100% health |
| `60` | subsystem 9 | ~38% health (damaged!) |
| `FF FF ...` | subsystems 10+ | 100% health (remaining subsystems) |

This shows the server sending a round-robin subsystem health update. Subsystem index 9 has taken damage (38% health). All other subsystems in this window are at full health.

### Minimal StateUpdate

The smallest valid StateUpdate is 10 bytes (header only, no field data):

```
1C [object_id:4] [game_time:4] 00
```

With `dirty_flags = 0x00`, no conditional fields follow. This is a heartbeat — it confirms the object exists and is at the same game time, but reports no state changes. This is observed when the server's ship object has no subsystems loaded (the flag 0x20 is suppressed).

## Authority Model Summary

```
           Ship Owner (Client)                    Server
           =====================                  ======
Sends:     Position (0x01/0x02)                   Subsystem health (0x20)
           Orientation (0x04, 0x08)
           Speed (0x10)
           Weapon status (0x80)
           Cloak (0x40)

Delivery:  Unreliable (0x32 flags=0x00)           Unreliable (0x32 flags=0x00)

Rate:      ~10 Hz per ship                        ~10 Hz per ship

Anti-cheat: Sends subsys hash with Position       Verifies hash, kicks on mismatch
```

## Bandwidth Profile

From a 34-minute 3-player combat session:

| Metric | Value |
|--------|-------|
| Total StateUpdate messages | 199,541 |
| Percentage of all game messages | ~97% |
| Average rate | ~98 messages/second (3 ships x ~10 Hz x 2 directions + overhead) |
| Typical client->server size | 15-25 bytes |
| Typical server->client size | 15-25 bytes |
| Delivery mode | Unreliable (no ACK, no retransmit) |

## Related Documents

- **[ship-subsystems.md](ship-subsystems.md)** — Fixed subsystem index table, per-type health encoding, subsystem categories
- **[phase1-verified-protocol.md](phase1-verified-protocol.md)** — Full protocol overview including StateUpdate (Section 8)
- **[objcreate-wire-format.md](objcreate-wire-format.md)** — Ship creation message (ships must exist before receiving StateUpdates)
- **[combat-system.md](combat-system.md)** — Damage pipeline and how subsystem health changes
- **[transport-cipher.md](transport-cipher.md)** — AlbyRules stream cipher applied to all game packets
