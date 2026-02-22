# CollisionEffect Wire Format (Opcode 0x15)

Wire format specification for Star Trek: Bridge Commander's collision effect message, documented from network packet captures and the game's shipped Python scripting API.

## Overview

Opcode 0x15 (CollisionEffect) is sent by a client to the host when that client's local collision detection system detects a collision involving one of the client's objects. The host validates the report, applies authoritative collision damage to the target ship's subsystems, and broadcasts visual explosion effects to all peers.

This is a **client-to-host** message. The host never sends opcode 0x15; instead, it applies damage locally and broadcasts effects through the event system.

**Frequency**: Approximately 84 per 15-minute session in a 3-player combat match (4th most common combat opcode after PythonEvent, StartFiring, and TorpedoFire).

## Message Layout

```
Offset  Size  Type    Field                    Notes
------  ----  ----    -----                    -----
0       1     u8      opcode                   Always 0x15
1       4     i32     event_type_class_id      Always 0x00008124 (collision event class)
5       4     i32     event_code               Always 0x00800050 (collision event code)
9       4     i32     source_object_id         Other colliding object (0 = environment)
13      4     i32     target_object_id         Ship reporting the collision (BC object ID)
17      1     u8      contact_count            Number of contact points (typically 1-2)
[repeated contact_count times:]
  +0    1     s8      dir_x                    Compressed contact direction X
  +1    1     s8      dir_y                    Compressed contact direction Y
  +2    1     s8      dir_z                    Compressed contact direction Z
  +3    1     u8      magnitude_byte           Compressed distance from ship center
[end repeat]
+0      4     f32     collision_force          IEEE 754 float: impact force magnitude
```

**Total size**: `22 + contact_count * 4` bytes.

All multi-byte numeric fields are **little-endian**.

## Field Descriptions

### Constant Header (bytes 0-12)

The first 13 bytes are constant across all observed CollisionEffect packets:

```
15 24 81 00 00 50 00 80 00 00 00 00 00
```

| Bytes | Value | Meaning |
|-------|-------|---------|
| `15` | 0x15 | Opcode |
| `24 81 00 00` | 0x00008124 | Event type class identifier (collision event) |
| `50 00 80 00` | 0x00800050 | Event code (collision effect) |
| `00 00 00 00` | 0x00000000 | Source object ID = 0 (environment/asteroid collision) |

The source object ID is 0 in all observed single-player-vs-environment collisions. In ship-vs-ship collisions, this field would contain the other ship's network object ID.

### Target Object ID (bytes 13-16)

The network object ID of the ship reporting the collision. Uses the standard BC object ID allocation:

```
Player N base = 0x3FFFFFFF + (N * 0x40000)
```

### Contact Count (byte 17)

Number of contact points in this collision event. Observed values: 1 (most common) or 2. Each contact point adds 4 bytes to the message.

### Contact Points (4 bytes each)

Each contact point encodes the position where the collision occurred, relative to the ship's center, in a compressed format:

| Byte | Type | Description |
|------|------|-------------|
| 0 | s8 | Direction X component (signed, normalized) |
| 1 | s8 | Direction Y component (signed, normalized) |
| 2 | s8 | Direction Z component (signed, normalized) |
| 3 | u8 | Magnitude (distance from ship center, scaled by bounding radius) |

The three direction bytes encode a normalized unit vector pointing from the ship's center to the contact point, in ship-local coordinates. The magnitude byte encodes how far along that direction the contact occurred, scaled relative to the ship's bounding sphere radius.

**Decompression algorithm** (observed behavior):

1. Convert the 3 direction bytes to floats by dividing by a scale factor
2. Use the target ship's bounding sphere radius as the reference distance
3. Multiply the normalized direction by `(magnitude_byte / scale) * bounding_radius`
4. This produces a Vec3 world-space contact position relative to the ship

The exact scale constants are determined by the engine's compressed vector implementation. A server that only needs to apply hull damage (not subsystem-specific damage) can ignore the contact positions entirely and use only `collision_force`.

### Collision Force (last 4 bytes)

IEEE 754 single-precision float representing the impact force magnitude. Observed range:

| Value | Interpretation |
|-------|---------------|
| ~0.07 | Very light glancing touch |
| ~580 | Moderate collision |
| ~927 | Significant impact |
| ~1281 | Heavy collision |

This value directly determines how much hull/subsystem damage to apply.

## Decoded Packet Examples

### Example 1: Single contact, heavy impact

```
15                    opcode = 0x15 (CollisionEffect)
24 81 00 00           event_type_class_id = 0x00008124
50 00 80 00           event_code = 0x00800050
00 00 00 00           source_obj = 0 (environment)
FF FF FF 3F           target_obj = 0x3FFFFFFF (Player 0 base)
01                    contact_count = 1
0D 7E 00 D9           contact[0]: dir=(+13,+126,+0) mag=217
BB 20 A0 44           force = 1281.02
```

Total: 26 bytes.

### Example 2: Two contact points

```
15                    opcode = 0x15
24 81 00 00           event_type_class_id = 0x00008124
50 00 80 00           event_code = 0x00800050
00 00 00 00           source_obj = 0 (environment)
FF FF FF 3F           target_obj = 0x3FFFFFFF
02                    contact_count = 2
0F 7E 00 DA           contact[0]: dir=(+15,+126,+0) mag=218
00 7E FF D8           contact[1]: dir=(+0,+126,-1) mag=216
51 C3 67 44           force = 927.05
```

Total: 30 bytes.

### Example 3: Different player, 3-player combat

```
15                    opcode = 0x15
24 81 00 00           event_type_class_id = 0x00008124
50 00 80 00           event_code = 0x00800050
00 00 00 00           source_obj = 0 (environment)
FF FF 03 40           target_obj = 0x400003FF (Player 0 range, offset +1024)
01                    contact_count = 1
27 77 11 B8           contact[0]: dir=(+39,+119,+17) mag=184
9D 47 25 44           force = 661.12
```

Total: 26 bytes.

## Host Processing Behavior

When the host receives a CollisionEffect message, the stock game performs three validation checks before accepting the damage report:

### 1. Ownership Validation

The sender's ship (identified by their connection/peer ID) must be either the `source_object_id` or the `target_object_id` in the collision event. This prevents a malicious client from spoofing collision damage to ships they don't control.

### 2. Deduplication

If the sender's ship is the `source_object` and the `target_object` is the host's own ship, the message is dropped. This prevents double-counting: when two player-controlled ships collide, both clients detect and report the collision. The host only processes the report from the ship that is NOT the host's own ship, since the host already detected its own collision locally.

### 3. Proximity Check

The host computes the distance between the two colliding objects (using their current positions and bounding sphere radii). If the gap between bounding spheres exceeds a threshold, the collision is rejected as implausible.

### Damage Application

If all checks pass:

1. The collision event is re-categorized internally as a "host collision" (distinct event code)
2. For each contact point, the engine:
   - Decompresses the contact position back to world-space Vec3
   - Determines which subsystem is nearest to the contact point
   - Applies damage based on `collision_force` to that subsystem
3. A secondary event is posted for visual/audio effects (explosions at contact points)
4. Other connected clients see the collision effects via the event broadcast system

## Server Implementation Notes

### Minimal Implementation (hull damage only)

A server that only needs to apply total hull damage (without per-subsystem distribution) can:

1. Parse the header (13 constant bytes + target_object_id)
2. Read contact_count and skip `contact_count * 4` bytes
3. Read collision_force (last 4 bytes)
4. Apply damage proportional to collision_force to the target ship
5. Skip the three validation checks if you trust your clients (LAN play)

### Full Implementation (subsystem damage)

For proper subsystem-specific damage:

1. Parse all fields including contact points
2. Decompress each contact point to ship-relative Vec3
3. Map each contact position to the nearest subsystem on the ship model
4. Distribute collision_force across the affected subsystems
5. Perform ownership and proximity validation

### Python Scripting API

The game's shipped Python API (available to mods) exposes these methods on collision events:

| Method | Returns | Description |
|--------|---------|-------------|
| `GetNumPoints()` | int | Number of contact points |
| `GetPoint(index)` | Vec3 | Decompressed world-space contact position |
| `GetCollisionForce()` | float | Impact force magnitude |

These are accessible in collision event handlers registered through the standard event system.

## Relationship to Other Opcodes

| Opcode | Name | Relationship |
|--------|------|-------------|
| 0x15 | CollisionEffect | This message (client reports collision) |
| 0x14 | DestroyObject | Sent if collision destroys a ship |
| 0x29 | Explosion | Sent for explosion visual effects |
| 0x0A | SubsysStatus | Subsystem state changes from damage |
| 0x06 | PythonEvent | May carry collision-related script events |

## Open Questions

- **Ship-vs-ship source_object_id**: All captured samples show `source_object_id = 0` (environment collisions). In ship-vs-ship combat, this field should contain the other ship's object ID, but no captures of this case are available yet.
- **Contact decompression constants**: The exact scale factors used in the CompressedVec4_Byte compression/decompression are engine internals. The algorithm is described behaviorally above; the specific numeric constants need further captures to reverse-engineer purely from packet data.
- **Maximum contact_count**: Observed range is 1-2. The theoretical maximum is bounded by the collision detection system; values above 4 are unlikely.
