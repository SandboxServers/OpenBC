# TorpedoFire Wire Format (Opcode 0x19)

Wire format specification for torpedo launch messages in Bridge Commander multiplayer, documented from network packet captures and the game's shipped Python scripting API.

**Clean room statement**: This document contains no decompiled code, no binary addresses, no internal memory offsets, and no handler function names. All formats are derived from observable wire data and the public SWIG scripting API.

---

## Overview

TorpedoFire (opcode 0x19) is sent when a player launches a torpedo. It is the 4th most frequent game opcode in active combat sessions (1,089 wire packets in a 33.5-minute, 3-player battle).

**Direction**: Client (owner) → Server → all other clients (opaque relay)
**Delivery**: Reliable (ACK required)
**Server behavior**: Pure relay — no damage computation, no validation. Each receiving peer independently creates and simulates the torpedo.

---

## Wire Format

### Without Target (10 bytes)

When the torpedo has no lock-on target (bit 2 of flags set):

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode              0x19
1       4     i32     subsystemObjectID   Torpedo tube network object ID (LE)
5       1     u8      torpedoTypeIndex    Torpedo model index (see SpeciesToTorp below)
6       1     u8      flags               Bit field (see Flags below)
7       3     cv3     velocity            Ship velocity direction (CompressedVector3)
```

### With Target (18 bytes)

When the torpedo has a lock-on target (bit 2 of flags clear):

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode              0x19
1       4     i32     subsystemObjectID   Torpedo tube network object ID (LE)
5       1     u8      torpedoTypeIndex    Torpedo model index
6       1     u8      flags               Bit field (see Flags below)
7       3     cv3     velocity            Ship velocity direction (CompressedVector3)
10      4     i32     targetObjectID      Target ship network object ID (LE)
14      4     cv4     targetBBox          Target bounding position (CompressedVector4)
```

---

## Field Descriptions

### subsystemObjectID (i32)

The network object ID of the torpedo tube subsystem that fired. This is NOT the ship's object ID — it is the specific TorpedoTube subsystem's ID, allocated from the owning player's object ID range.

To determine which player fired: extract the player slot from the object ID using the standard formula `(objectId - 0x3FFFFFFF) >> 18`.

### torpedoTypeIndex (u8)

Indexes into the `SpeciesToTorp` table (see below). Determines which torpedo model, speed, and damage characteristics the receiving peer uses when creating the torpedo locally.

### flags (u8)

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | isReloading | Torpedo tube is in reload state |
| 1 | subsysState | Subsystem enabled/disabled state |
| 2 | noTarget | If set: no target data follows (unguided torpedo) |

**Conditional fields**: `targetObjectID` and `targetBBox` are present ONLY when bit 2 is clear (torpedo has a target).

### velocity (cv3)

Ship velocity direction at the moment of fire. CompressedVector3: three signed bytes, each encoding a direction component as `round(component * 127.0)`, range -1.0 to +1.0.

### targetObjectID (i32)

The network object ID of the torpedo's lock-on target. Only present when `!(flags & 0x04)`.

### targetBBox (cv4)

Target bounding position, encoded as CompressedVector4: three direction bytes (same as cv3) plus a u16 magnitude (CompressedFloat16). The bounding sphere radius used for homing is an external scale parameter from the target's model data, not transmitted on the wire.

Only present when `!(flags & 0x04)`.

---

## Torpedo Type Table

Source: `scripts/Multiplayer/SpeciesToTorp.py` (shipped with the game)

| Index | Constant | Torpedo Script |
|-------|----------|----------------|
| 0 | UNKNOWN | — |
| 1 | DISRUPTOR | Disruptor |
| 2 | PHOTON | PhotonTorpedo |
| 3 | QUANTUM | QuantumTorpedo |
| 4 | ANTIMATTER | AntimatterTorpedo |
| 5 | CARDTORP | CardassianTorpedo |
| 6 | KLINGONTORP | KlingonTorpedo |
| 7 | POSITRON | PositronTorpedo |
| 8 | PULSEDISRUPT | PulseDisruptor |
| 9 | FUSIONBOLT | FusionBolt |
| 10 | CARDASSIANDISRUPTOR | CardassianDisruptor |
| 11 | KESSOKDISRUPTOR | KessokDisruptor |
| 12 | PHASEDPLASMA | PhasedPlasma |
| 13 | POSITRON2 | Positron2 |
| 14 | PHOTON2 | PhotonTorpedo2 |
| 15 | ROMULANCANNON | RomulanCannon |

MAX_TORPS = 16 (indices 0-15; only 1-15 are valid).

---

## Relay Behavior

The server performs **opaque relay** of TorpedoFire messages:

1. Receives the message from the firing client
2. Forwards an identical copy to all other connected peers (excluding the sender)
3. Also applies the torpedo creation locally on the server (the server creates and simulates the torpedo in its own game state)

The server performs **no validation** of:
- Whether the firing subsystem exists or is functional
- Whether the torpedo type is valid for the firing ship
- Whether the target object exists
- Reload timing or ammunition counts

Each receiving peer independently:
1. Looks up the torpedo tube subsystem by `subsystemObjectID`
2. Creates a torpedo of the specified type
3. If a target is specified, sets up homing behavior
4. Simulates the torpedo flight and hit detection locally
5. Computes damage independently if the torpedo hits

**Weapon damage is peer-local**: there is no authoritative server-side torpedo damage calculation. Each peer computes torpedo hit detection and damage independently.

---

## Observed Frequency

From a 33.5-minute, 3-player FFA combat session:

| Metric | Value |
|--------|-------|
| Total wire packets | 1,089 |
| Unique torpedo events (pre-relay) | 363 |
| Relay ratio | 3:1 (consistent with 3-player relay) |
| Average rate | ~0.54 torpedoes/second |

---

## Implementation Notes

1. **Variable length**: Message is 10 bytes without a target, 18 bytes with a target. Parse the `flags` byte before reading target fields.

2. **Subsystem ID, not ship ID**: The `subsystemObjectID` identifies the specific torpedo tube, not the ship. Use the object ID range formula to determine the owning player.

3. **Mod compatibility**: The `torpedoTypeIndex` is a single byte (0-255). Mods may define torpedo types beyond index 15. An implementation should not reject unknown indices — pass them through for the mod's scripts to handle.

4. **Relay-only server**: The server's role is relay + local simulation. It does NOT need to validate or modify the torpedo data.

---

## Related Documents

- **[stateupdate-wire-format.md](stateupdate-wire-format.md)** — Position data for tracking torpedo tube subsystems
- **[explosion-wire-format.md](explosion-wire-format.md)** — Explosion messages generated on torpedo impact
- **[objcreate-wire-format.md](objcreate-wire-format.md)** — SpeciesToTorp table reference
- **[../protocol/protocol-reference.md](../protocol/protocol-reference.md)** — Complete opcode table
