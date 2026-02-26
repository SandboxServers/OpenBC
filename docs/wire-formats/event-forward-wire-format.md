# GenericEventForward Wire Format (Opcodes 0x07-0x12, 0x1B)

Wire format specification for Bridge Commander's generic event forwarding mechanism.
This group of opcodes shares a common handler, payload structure, and relay behavior.
Documented from network packet captures of stock dedicated servers.

**Clean room statement**: This document contains no decompiled code, no binary addresses,
no internal memory offsets. All formats and behaviors are derived from observable wire data
and the shipped Python scripting API.

---

## Overview

Opcodes 0x07 through 0x12 and 0x1B form a **shared event forwarding group**. They all use
the same receive-side handler, the same relay mechanism, and the same payload structure.
The only difference between them is the **event code** applied on the receive side — some
opcodes override the event code from the wire with a fixed value, while others pass through
the event's own code unchanged.

These opcodes implement the "I did a thing, tell everyone" pattern: a client performs an
action locally, then broadcasts a notification to all other peers via the server.

**Direction**: Client → Server → All other clients (star topology relay)
**Reliability**: All sent reliably (ACK required)
**Relay behavior**: Server forwards the raw message to all connected peers except the
original sender. No validation, no filtering, no deduplication.

---

## Opcode Table

| Opcode | Name | Event Code Override | Factory | Frequency (3p / 2p) |
|--------|------|---------------------|---------|---------------------|
| 0x07 | StartFiring | 0x008000D7 | 0x8128 | 2,918 / 346 |
| 0x08 | StopFiring | 0x008000D9 | 0x0101 | 1,448 / 339 |
| 0x09 | StopFiringAtTarget | 0x008000DB | (unconfirmed) | 0 / 0 |
| 0x0A | SubsystemStatusChanged | 0x0080006C | 0x0104 | 63 / 132 |
| 0x0B | AddToRepairList | None (pass-through) | (unconfirmed) | 0 / 0 |
| 0x0C | ClientEvent | None (pass-through) | (unconfirmed) | 0 / 0 |
| 0x0E | StartCloaking | 0x008000E3 | (dead code) | 0 / 0 |
| 0x0F | StopCloaking | 0x008000E5 | (dead code) | 0 / 0 |
| 0x10 | StartWarp | 0x008000ED | 0x812A | 1 / 4 |
| 0x11 | RepairListPriority | None (pass-through) | 0x010C | 0 / 8 |
| 0x12 | SetPhaserLevel | None (pass-through) | 0x0105 | 0 / 10 |
| 0x1B | TorpedoTypeChange | 0x008000FD | 0x0105 | 12 / 2 |

**Correction (Feb 2026)**: 0x0E and 0x0F are **dead code in multiplayer**. Zero instances
observed across two stock dedicated server traces (3-player, 33.5 min + 2-player, 21 min)
despite active cloaking by both Bird of Prey and Warbird. Cloak state propagates exclusively
via StateUpdate CLK flag (bit 0x40). See [cloaking-system.md](../game-systems/cloaking-system.md).

**Correction (Feb 2026)**: 0x11, 0x12, and 0x1B showed zero in the 3-player trace due to a
packet trace decoder bug (missing name labels). The 2-player trace confirmed their presence
and factory IDs.

Opcodes with "None (pass-through)" deliver the event with its original event code from the
wire. Opcodes with an override replace the event code after deserialization.

---

## Message Layout

All opcodes in this group share the same payload structure. The payload is a serialized
event object using the standard event streaming format:

```
Offset  Size  Type    Field                    Notes
------  ----  ----    -----                    -----
0       1     u8      opcode                   0x07-0x12 or 0x1B
1       4     i32 LE  event_class_id           Event factory ID (determines payload type)
5       4     i32 LE  event_code               Event type code (may be overridden by opcode)
9       4     i32 LE  source_object_id         Object that generated the event
13      4     i32 LE  related_object_id        Related object (often NULL or -1)
[17+]   var   ...     class-specific fields    Additional fields based on event_class_id
```

**Minimum size**: 17 bytes (base event with no class-specific fields).

### Event Class Payloads

| Class ID | Name | Extra Fields | Total Size |
|----------|------|-------------|------------|
| 0x0002 | Event (base) | None | 17 bytes |
| 0x0101 | SubsystemEvent | None (same as base) | 17 bytes |
| 0x0104 | SubsystemControlEvent | +1 byte value | 18 bytes |
| 0x0105 | CharEvent | +1 byte value | 18 bytes |
| 0x010C | ObjPtrEvent | +4 byte object ID | 21 bytes |
| 0x0866 | DeletePlayerEvent | +1 byte (team) | 18 bytes |
| 0x8128 | StartFiringEvent | +8 bytes (unknown) | 25 bytes |
| 0x812A | StartWarpEvent | +1 str_len + name + 4×f32 | Variable |

**Notes on newly documented factories** (verified from 2-player stock dedi trace, Feb 2026):
- **0x0104**: Used exclusively by SubsysStatus (0x0A). Same wire layout as CharEvent (0x0105)
  — 18 bytes, with one extra byte beyond the base event. Sibling class, different factory ID.
- **0x0866**: Used exclusively by DeletePlayerUI (0x17). 18 bytes total, extra byte is team number.
- **0x8128**: Used exclusively by StartFiring (0x07). 25 bytes total (17 base + 8 extra). Content
  of the 8 extra bytes is opaque on the wire. StartFiring is always sent as a **duplicate pair**
  (two identical reliable messages in the same datagram).
- **0x812A**: Used exclusively by StartWarp (0x10). Variable length. After the 17-byte base event:
  1 byte string length, N bytes set name, then 4 × float32 (destination X/Y/Z + warp speed).

---

## Relay Behavior

### Server Processing

When the server receives a GenericEventForward message:

1. **Relay first**: Forward the raw message bytes to all connected peers except the sender
   (the "Forward" routing group). This happens before any local processing.

2. **Deserialize**: Construct the appropriate event object from the payload using the
   event class ID as a factory key.

3. **Event code override** (if applicable): For opcodes with a fixed event code override
   (see table above), replace the deserialized event's code with the override value.

4. **Local dispatch**: Post the event to the local event system for processing.

### Sender Gating

The handler only forwards events that originate from the **local player's own ship**. Events
received from remote players are NOT re-forwarded, preventing infinite relay loops. The check
is: `source_object_id` must belong to the receiving peer's player slot.

### Relay Ratio

In an N-player game, each received event is relayed to (N-1) other peers. Verified from
a 3-player stock trace:

| Opcode | Received | Wire Total | Ratio |
|--------|----------|------------|-------|
| 0x07 StartFiring | 978 | 2,918 | 3:1 (N-1 + 1) |
| 0x08 StopFiring | 477 | 1,448 | 3:1 |
| 0x19 TorpedoFire* | 363 | 1,089 | 3:1 |
| 0x1A BeamFire* | 52 | 156 | 3:1 |
| 0x1B TorpTypeChange | 4 | 12 | 3:1 |

*TorpedoFire (0x19) and BeamFire (0x1A) are NOT part of this handler group — they have
their own specialized handlers — but they follow the same relay pattern.

---

## Event Code Override Mechanism

Some opcodes use **sender/receiver event code pairing**. The sender posts one event code
locally (e.g., "start firing notify"), and the receiver posts a different event code
(e.g., "start firing"). This is implemented by the opcode-specific override:

| Opcode | Sender Posts Locally | Override (Receiver Posts) |
|--------|---------------------|--------------------------|
| 0x07 | Start firing notify | 0x008000D7 (start firing) |
| 0x08 | Stop firing notify | 0x008000D9 (stop firing) |
| 0x09 | Stop firing at target notify | 0x008000DB (stop firing at target) |
| 0x0A | Subsystem status notify | 0x0080006C (subsystem status) |
| 0x0E | Start cloaking notify | 0x008000E3 (start cloaking) |
| 0x0F | Stop cloaking notify | 0x008000E5 (stop cloaking) |
| 0x10 | Start warp notify | 0x008000ED (start warp) |
| 0x1B | Torpedo type change notify | 0x008000FD (torpedo type changed) |

Opcodes without overrides (0x0B, 0x0C, 0x11, 0x12) use the same event code on both sides.

---

## Server Implementation Notes

### Minimal Implementation

A server only needs to:
1. Receive the message from the sending client
2. Relay the raw bytes to all other connected peers
3. No parsing or validation required for basic relay

### Full Implementation

For a server that tracks game state:
1. Parse the event payload (class ID, event code, source object)
2. Verify the source object belongs to the sending player
3. Apply the event code override if applicable for the opcode
4. Update local state (e.g., track which ships are cloaked, firing, etc.)
5. Relay to all other peers

### StartFiring Duplicate Pair Behavior

StartFiring (0x07) uses factory 0x8128 and is **always sent as a duplicate pair** — two
identical reliable messages in the same UDP datagram. This behavior is consistent across all
observed instances (88 messages = 44 actual fire events in a 2-player trace). The receiving
side should handle or deduplicate both copies. StopFiring (0x08) uses factory 0x0101 and is
sent as a single message (NOT paired).

### Factory Differences Between Opcodes

Not all opcodes in this group use the same event class factory:

| Opcode | Observed Factory | Wire Size | Notes |
|--------|-----------------|-----------|-------|
| 0x07 StartFiring | 0x8128 | 25 bytes | Always duplicate pair, +8 extra bytes |
| 0x08 StopFiring | 0x0101 | 17 bytes | Standard SubsystemEvent, single message |
| 0x0A SubsysStatus | 0x0104 | 18 bytes | NOT 0x0105; sibling class, same +1 byte layout |
| 0x10 StartWarp | 0x812A | Variable | +string + 4×f32 (destination + speed) |
| 0x11 RepairListPriority | 0x010C | 21 bytes | ObjPtrEvent (+4 byte subsystem obj ID), event 0x00800076 |
| 0x12 SetPhaserLevel | 0x0105 | 18 bytes | CharEvent (+1 byte), event 0x008000E0, values 0x00/0x02 |
| 0x1B TorpTypeChange | 0x0105 | 18 bytes | CharEvent (+1 byte), event 0x008000FE |

Opcodes 0x09, 0x0B, 0x0C were not observed in available traces and their factories are
unconfirmed. Opcodes 0x0E and 0x0F are dead code in multiplayer (zero instances despite
active cloaking).

### Rate Limiting

For anti-cheat purposes, the server can rate-limit these messages per source ship:
- StartFiring/StopFiring: maximum 1 pair per weapon cooldown interval
- TorpTypeChange: maximum ~1/second
- StartCloak: maximum ~1/30 seconds (cloak transition time)
- SetPhaserLevel: maximum ~1/second

---

## Relationship to Other Event Opcodes

| Opcode Group | Handler | Relay? | Notes |
|--------------|---------|--------|-------|
| 0x07-0x12, 0x1B | GenericEventForward | Yes (N-1 relay) | This document |
| 0x06 | PythonEvent | Host-generated | Host creates and sends directly to clients |
| 0x0D | PythonEvent2 | C→S only, NOT relayed | Client targeting updates, absorbed by server |
| 0x19 | TorpedoFire | Yes (N-1 relay) | Separate handler, same relay pattern |
| 0x1A | BeamFire | Yes (N-1 relay) | Separate handler, same relay pattern |
| 0x13 | HostMsg | C→S only | Not relayed — server processes locally |
| 0x15 | CollisionEffect | C→S only | Not relayed — server applies damage, generates 0x06 events |

**Verified (Feb 2026 stock dedi server trace)**: 0x0D (31 C→S, 0 S→C), 0x15 (2 C→S, 0 S→C),
0x13 (3 C→S, 0 S→C). All three are absorbed by the server without relay. 0x0D carries
TARGET_WAS_CHANGED (0x00800058) exclusively. 0x15 triggers server-side damage + PythonEvent
(0x06) generation for repair queue updates.

---

## Related Documents

- **[set-phaser-level-wire-format.md](set-phaser-level-wire-format.md)** — Detailed spec for opcode 0x12 (one member of this group)
- **[pythonevent-wire-format.md](pythonevent-wire-format.md)** — PythonEvent (0x06) — server-generated events
- **[protocol-reference.md](../protocol/protocol-reference.md)** — Complete opcode table
- **[../architecture/server-authority.md](../architecture/server-authority.md)** — Authority model
