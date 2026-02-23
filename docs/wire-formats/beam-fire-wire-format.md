# BeamFire Wire Format (Opcode 0x1A)

Wire format specification for beam weapon hit messages in Bridge Commander multiplayer, documented from network packet captures and the game's shipped Python scripting API.

**Clean room statement**: This document contains no decompiled code, no binary addresses, no internal memory offsets, and no handler function names. All formats are derived from observable wire data and the public SWIG scripting API.

---

## Overview

BeamFire (opcode 0x1A) reports a beam weapon (phaser) hit event. Unlike TorpedoFire which creates a projectile, BeamFire represents an instantaneous hit — the beam has already been traced and the hit point determined by the firing client.

**Direction**: Client (owner) → Server → all other clients (opaque relay)
**Delivery**: Reliable (ACK required)
**Server behavior**: Pure relay + local application. No damage computation or validation. Each receiving peer applies the beam effect and computes damage independently.

---

## Wire Format

### Without Target (10 bytes)

When the beam has no specific target (bit 1 of flags clear):

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode              0x1A
1       4     i32     subsystemObjectID   Beam weapon network object ID (LE)
5       1     u8      beamTypeIndex       Beam model index
6       3     cv3     hitPosition         World-space hit point (CompressedVector3)
9       1     u8      flags               Bit field (see Flags below)
```

### With Target (14 bytes)

When the beam hit a specific target (bit 1 of flags set):

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode              0x1A
1       4     i32     subsystemObjectID   Beam weapon network object ID (LE)
5       1     u8      beamTypeIndex       Beam model index
6       3     cv3     hitPosition         World-space hit point (CompressedVector3)
9       1     u8      flags               Bit field (see Flags below)
10      4     i32     targetObjectID      Target ship network object ID (LE)
```

---

## Field Descriptions

### subsystemObjectID (i32)

The network object ID of the beam weapon subsystem (phaser emitter) that fired. This is NOT the ship's object ID — it is the specific PhaserEmitter subsystem's ID, allocated from the owning player's object ID range.

To determine which player fired: extract the player slot from the object ID using the standard formula `(objectId - 0x3FFFFFFF) >> 18`.

### beamTypeIndex (u8)

Beam model index. Determines the visual appearance of the beam effect on receiving peers. Stock beam types correspond to the phaser variants defined in ship hardpoint scripts.

### hitPosition (cv3)

World-space hit point, encoded as CompressedVector3: three signed bytes, each encoding a direction component as `round(component * 127.0)`, range -1.0 to +1.0. This represents the direction from the weapon to the hit point (normalized).

### flags (u8)

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | isDualFire | Beam is dual-fire mode (both emitters firing simultaneously) |
| 1 | hasTarget | If set: `targetObjectID` field follows |

**Conditional field**: `targetObjectID` is present ONLY when bit 1 is set.

### targetObjectID (i32)

The network object ID of the ship that was hit by the beam. Only present when `flags & 0x02`.

---

## Corrections to Earlier Documentation

Previous protocol documentation had field ordering errors. The verified order is:

1. `subsystemObjectID` (4 bytes)
2. `beamTypeIndex` (1 byte)
3. `hitPosition` as cv3 (3 bytes)
4. `flags` (1 byte) — comes AFTER the cv3, not before
5. `targetObjectID` (4 bytes, conditional)

There is **no cv4 field** in BeamFire. The CompressedVector4 encoding is used only in TorpedoFire (0x19) for the target bounding position.

---

## Relay Behavior

The server performs **opaque relay** of BeamFire messages:

1. Receives the message from the firing client
2. Forwards an identical copy to all other connected peers (excluding the sender)
3. Also applies the beam effect locally on the server

The server performs **no validation** of:
- Whether the firing subsystem exists or is functional
- Whether the target is within weapon range
- Whether the firing ship is alive or uncloaked
- Beam damage amounts

Each receiving peer independently:
1. Looks up the beam weapon subsystem by `subsystemObjectID`
2. Creates the beam visual effect at the hit position
3. If a target is specified, runs the weapon damage pipeline against the target
4. Computes shield absorption, subsystem damage, and hull damage locally

**Weapon damage is peer-local**: there is no authoritative server-side beam damage calculation. Each peer computes beam damage independently. This means weapon kills can occasionally be observed at slightly different times on different peers.

---

## Observed Frequency

From a 33.5-minute, 3-player FFA combat session:

| Metric | Value |
|--------|-------|
| Total wire packets | 157 |
| Unique beam events (pre-relay) | ~52 |
| Relay ratio | 3:1 (consistent with 3-player relay) |

BeamFire is less frequent than TorpedoFire (157 vs 1,089) because beams are continuous — a single StartFiring (0x07) event begins the beam, and the BeamFire message is sent only when a hit is registered, not continuously while the beam is active.

---

## Beam Fire vs Start/Stop Firing

The BeamFire (0x1A) message is distinct from the StartFiring (0x07) / StopFiring (0x08) messages:

| Message | Purpose | When Sent |
|---------|---------|-----------|
| StartFiring (0x07) | "I began firing my weapon" | When player engages weapon |
| StopFiring (0x08) | "I stopped firing" | When player disengages weapon |
| BeamFire (0x1A) | "My beam hit something" | When beam traces to a target |

StartFiring/StopFiring control the beam visual and audio. BeamFire carries the actual hit data for damage computation.

---

## Implementation Notes

1. **Variable length**: Message is 10 bytes without a target, 14 bytes with a target. Check bit 1 of `flags` before reading the target field.

2. **Subsystem ID, not ship ID**: The `subsystemObjectID` identifies the specific phaser emitter, not the ship.

3. **No cv4**: Unlike TorpedoFire, BeamFire uses only cv3 for the hit position. Do not attempt to read a cv4.

4. **Flags after cv3**: The flags byte comes AFTER the 3-byte compressed vector, not before. Getting this wrong will desynchronize the stream parser.

5. **Relay-only server**: The server's role is relay + local effect application. It does NOT need to validate or modify the beam data.

---

## Related Documents

- **[torpedo-fire-wire-format.md](torpedo-fire-wire-format.md)** — TorpedoFire (0x19) wire format
- **[event-forward-wire-format.md](event-forward-wire-format.md)** — StartFiring/StopFiring (0x07/0x08) that bracket beam activity
- **[stateupdate-wire-format.md](stateupdate-wire-format.md)** — Subsystem health updates affected by beam damage
- **[../protocol/protocol-reference.md](../protocol/protocol-reference.md)** — Complete opcode table
