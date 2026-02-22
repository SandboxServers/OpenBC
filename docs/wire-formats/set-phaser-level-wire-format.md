# SetPhaserLevel Wire Format (Opcode 0x12)

Wire format specification for Star Trek: Bridge Commander's phaser power level message,
documented from network packet captures and the game's shipped Python scripting API.

## Overview

Opcode 0x12 (SetPhaserLevel) carries a phaser beam intensity change from the originating
player to all other peers. When a player toggles their phaser power setting between LOW,
MEDIUM, and HIGH, this message broadcasts the change so other clients can update their
local representation of that ship's phaser state.

**Important**: This opcode controls phaser beam intensity (the LOW/MED/HIGH toggle) — **not**
the engineering power distribution sliders (weapons/shields/engines/sensors), which use a
different mechanism.

**Direction**: Bidirectional (any peer to all other peers, relayed by host)
**Reliability**: Sent reliably (ACK required)
**Frequency**: Approximately 33 per 15-minute session in a 3-player combat match (infrequent — players rarely toggle phaser level mid-combat)

## Message Layout

```
Offset  Size  Type    Field                    Notes
------  ----  ----    -----                    -----
0       1     u8      opcode                   Always 0x12
1       4     i32     event_class_id           Always 0x00000105 (char event class)
5       4     i32     event_code               Always 0x008000E0 (set phaser level event)
9       4     i32     source_object_id         Ship that changed phaser level (BC object ID)
13      4     i32     related_object_id        Always 0 or -1 (unused for this event)
17      1     u8      phaser_level             Power level: 0, 1, or 2
```

**Total size**: 18 bytes (fixed — no variable-length fields).

All multi-byte numeric fields are **little-endian**.

## Field Descriptions

### Opcode (byte 0)

Always `0x12`.

### Event Class ID (bytes 1-4)

Always `0x00000105`. Identifies this as a "char event" — a generic event type that carries
a single byte payload. The same event class is reused by other subsystem events that only
need a one-byte value.

### Event Code (bytes 5-8)

Always `0x008000E0`. Identifies this specific event as a phaser level change. This code is
the same on both the sending and receiving sides (no sender/receiver code pairing for this
opcode).

### Source Object ID (bytes 9-12)

The network object ID of the ship whose phaser level changed. Uses the standard BC object
ID allocation:

```
Player N base = 0x3FFFFFFF + (N * 0x40000)
```

A value of `0x00000000` indicates NULL (no source — should not occur in practice).

### Related Object ID (bytes 13-16)

Typically `0x00000000` (NULL) or `0xFFFFFFFF` (-1, sentinel). This field is part of the
base event wire format but is not used by the phaser level event.

### Phaser Level (byte 17)

The phaser power setting:

| Value | Level | Python Constant | Effect |
|-------|-------|-----------------|--------|
| 0 | LOW | `App.PhaserSystem.PP_LOW` | Low intensity — reduced damage, lower power draw |
| 1 | MEDIUM | `App.PhaserSystem.PP_MEDIUM` | Balanced intensity |
| 2 | HIGH | `App.PhaserSystem.PP_HIGH` | High intensity — increased damage, higher power draw |

Values outside 0-2 are not expected from the stock game.

## Decoded Packet Examples

### Example 1: Player 0 sets phasers to HIGH

```
12                    opcode = 0x12 (SetPhaserLevel)
05 01 00 00           event_class_id = 0x00000105
E0 00 80 00           event_code = 0x008000E0
FF FF FF 3F           source_obj = 0x3FFFFFFF (Player 0 base)
00 00 00 00           related_obj = NULL
02                    phaser_level = 2 (HIGH)
```

Total: 18 bytes.

### Example 2: Player 1 sets phasers to LOW

```
12                    opcode = 0x12 (SetPhaserLevel)
05 01 00 00           event_class_id = 0x00000105
E0 00 80 00           event_code = 0x008000E0
FF FF 03 40           source_obj = 0x400003FF (Player 1 base)
00 00 00 00           related_obj = NULL
00                    phaser_level = 0 (LOW)
```

Total: 18 bytes.

## Send Behavior

### When It's Sent

This message is generated when the **local player** changes their phaser power setting
through the tactical UI (the phaser intensity toggle). The change is applied locally first,
then broadcast to all peers.

### Sender Gating

The multiplayer handler only forwards phaser level events that originate from the local
player's own ship. Events received from remote players (via the generic event forward
mechanism) are NOT re-forwarded, preventing infinite relay loops.

### Immediate Local Application

On the sending side, the phaser level change is applied **immediately** to all phaser
subsystems on the ship — the system iterates each child energy weapon and updates its
power setting. The network message is sent concurrently via the event system.

## Receive Behavior

### Generic Event Forward Pattern

Opcode 0x12 uses the **generic event forward** mechanism shared with opcodes 0x07-0x11
and 0x1B. On the receive side:

1. **Host relay**: If the receiver is the host, the raw message is forwarded to all other
   connected peers (via the "Forward" group, excluding the original sender). This happens
   before any local processing.

2. **Local deserialization**: The event is deserialized from the message payload. The
   factory system uses the event class ID (0x105) to construct the appropriate event
   object, then reads the event code, object references, and the phaser level byte.

3. **Local event dispatch**: The deserialized event is posted to the local event system
   with its original event code (0x008000E0). No event code override is applied for this
   opcode.

4. **Application**: The phaser system's event handler stores the received level value.

### Receiver Asymmetry

The receiver does **not** immediately apply the level to individual phaser subsystems the
way the sender does. It only stores the level value on the phaser system object. The actual
intensity change on remote machines propagates through either:
- The phaser system's periodic update cycle reading the stored value, or
- Individual weapon intensity values carried in StateUpdate (opcode 0x1C) serialization

This asymmetry means there may be a brief delay (one tick) before remote machines reflect
the phaser level change on individual weapons.

## Shared Handler Group

Opcode 0x12 shares its receive-side handler with these other event forward opcodes:

| Opcode | Name | Event Code Override |
|--------|------|---------------------|
| 0x07 | StartFiring | Overridden to 0x008000D7 |
| 0x08 | StopFiring | Overridden to 0x008000D9 |
| 0x09 | StopFiringAtTarget | Overridden to 0x008000DB |
| 0x0A | SubsystemStatusChanged | Overridden to 0x0080006C |
| 0x0B | AddToRepairList | No override (uses event's own code) |
| 0x0C | ClientEvent | No override |
| 0x0E | StartCloaking | Overridden to 0x008000E3 |
| 0x0F | StopCloaking | Overridden to 0x008000E5 |
| 0x10 | StartWarp | Overridden to 0x008000ED |
| 0x11 | RepairListPriority | No override |
| **0x12** | **SetPhaserLevel** | **No override** |
| 0x1B | TorpedoTypeChange | Overridden to 0x008000FD |

Opcodes with "No override" deliver the event with its original event code from the wire.
Opcodes with an override replace the event code after deserialization — this implements
sender/receiver event code pairing (e.g., a sender posts "start firing notify" locally,
but the receiver posts "start firing" instead).

SetPhaserLevel uses the same event code (0x008000E0) on both sender and receiver — no
pairing is needed.

## Python Scripting API

The phaser system exposes these constants and methods through the shipped scripting API:

### Power Level Constants

```python
App.PhaserSystem.PP_LOW      # = 0
App.PhaserSystem.PP_MEDIUM   # = 1
App.PhaserSystem.PP_HIGH     # = 2
```

### Methods

| Method | Description |
|--------|-------------|
| `PhaserSystem.SetPowerLevel(level)` | Sets the phaser intensity (triggers this message) |
| `PhaserSystem.GetPowerLevel()` | Returns the current power level (0, 1, or 2) |

### Usage Example

```python
pShip = App.ShipClass_GetObject(pAction)
pPhasers = pShip.GetPhaserSystem()
pPhasers.SetPowerLevel(App.PhaserSystem.PP_HIGH)
```

## Relationship to Other Opcodes

| Opcode | Name | Relationship |
|--------|------|-------------|
| 0x12 | SetPhaserLevel | This message (phaser intensity toggle) |
| 0x07 | StartFiring | Phaser begins firing at current intensity |
| 0x08 | StopFiring | Phaser stops firing |
| 0x0A | SubsysStatus | May report phaser subsystem state changes |
| 0x1C | StateUpdate | Carries per-weapon intensity in subsystem health data |

## Server Implementation Notes

### Minimal Implementation

A server only needs to:
1. Parse the 18-byte message to extract the phaser level value
2. Store the level on the appropriate ship's phaser system state
3. Relay the raw message to all other connected peers

No validation is required beyond the standard relay — the phaser level is purely
informational for remote clients, and the host does not need to enforce level boundaries.

### Full Implementation

For a server that manages authoritative game state:
1. Validate the phaser level is in range 0-2
2. Verify the source object belongs to the sending player
3. Update the phaser system's stored level
4. Relay to all other peers
5. The per-weapon intensity will be reflected in subsequent StateUpdate messages

## Event Class Details

The "char event" class (ID 0x105) is a generic event that extends the base event with a
single byte field. Its class hierarchy is:

```
Event (base, ID 0x02)
  └── SubsystemEvent (ID 0x101)
        └── CharEvent (ID 0x105)
```

The `CharEvent` is reused by multiple subsystem events that carry a single-byte payload.
For SetPhaserLevel, the byte value is the phaser power level (0/1/2).

The event's `IsA` check reports true for all three IDs in the hierarchy (0x105, 0x101, 0x02),
allowing handlers to match at any level of specificity.
