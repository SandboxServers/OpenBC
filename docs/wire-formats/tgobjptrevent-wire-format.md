# TGObjPtrEvent Wire Format — Clean Room Specification

Wire format specification for TGObjPtrEvent, a TGEvent subclass that carries a third object reference as an int32 network ID. This event class accounts for approximately **45% of all PythonEvent messages** during combat.

**Clean room statement**: This specification describes wire format behavior as observable from network packet captures and the game's shipped Python scripting API. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

TGObjPtrEvent is one of five event classes that can be serialized inside PythonEvent (opcode 0x06) or PythonEvent2 (opcode 0x0D) messages. It extends TGEvent with a single int32 field containing a TGObject network ID — allowing events to reference three objects (source, destination, and an additional object pointer).

### Event Class Hierarchy

```
TGEvent (factory 0x02, 17 bytes on wire)
  └── TGSubsystemEvent (factory 0x0101, 17 bytes, no extra fields)
        ├── TGCharEvent (factory 0x0105, 18 bytes, +1 byte char)
        └── TGObjPtrEvent (factory 0x010C, 21 bytes, +4 byte int32)
  └── ObjectExplodingEvent (factory 0x8129, 25 bytes, +4 int32 + 4 float)
```

See also:
- [pythonevent-wire-format/](pythonevent-wire-format/) — PythonEvent (0x06) overall wire format and factory dispatch
- [set-phaser-level-wire-format.md](set-phaser-level-wire-format.md) — TGCharEvent (0x0105) wire format

---

## Wire Format (21 bytes, fixed)

TGObjPtrEvent is identified by factory ID `0x0000010C` in the PythonEvent envelope.

```
Offset  Size  Type    Field            Description
------  ----  ----    -----            -----------
0       1     u8      opcode           0x06 (PythonEvent) or 0x0D (PythonEvent2)
1       4     i32     factoryID        0x0000010C (identifies TGObjPtrEvent)
5       4     i32     eventType        Event type constant (e.g., ET_WEAPON_FIRED)
9       4     i32     sourceObjID      Source object network ID (0=NULL, -1=sentinel)
13      4     i32     destObjID        Destination object network ID (same encoding)
17      4     i32     objPtrID         Third object reference (TGObject network ID)
```

**Total**: 21 bytes (17-byte base TGEvent + 4-byte int32 extension)

### Object ID Encoding

All three object ID fields use the same encoding:
- `0` = NULL (no object)
- `-1` = sentinel value
- Any other value = the object's network ID (from TGObject ID allocation)

---

## IsA Chain

TGObjPtrEvent responds to factory ID checks for:
- `0x010C` (TGObjPtrEvent — itself)
- `0x0101` (TGSubsystemEvent — parent)
- `0x02` (TGEvent — grandparent)

This means code that checks `IsA(0x0101)` (TGSubsystemEvent) will match TGObjPtrEvent instances. Implementations must preserve this inheritance chain.

---

## Difference from TGCharEvent (0x0105)

Both TGObjPtrEvent and TGCharEvent are subclasses of TGSubsystemEvent, but they carry different payload types:

| Property | TGObjPtrEvent (0x010C) | TGCharEvent (0x0105) |
|----------|----------------------|---------------------|
| Extension field | int32 (object network ID) | byte (single char value) |
| Wire extension | 4 bytes | 1 byte |
| Total wire size | 21 bytes | 18 bytes |
| Primary use | Weapon fire, target change, repair | Phaser intensity, subsystem control |

---

## Python API

The scripting API exposes TGObjPtrEvent through SWIG bindings:

```python
# Create a TGObjPtrEvent
pEvent = App.TGObjPtrEvent_Create()
pEvent.SetSource(sourceObject)
pEvent.SetDestination(destObject)
pEvent.SetObjPtr(thirdObject)         # sets the int32 object ID field
pEvent.SetEventType(App.ET_WEAPON_FIRED)
App.g_kEventManager.AddEvent(pEvent)

# Read the object pointer (resolves ID via hash table)
pObject = pEvent.GetObjPtr()
```

The SWIG `SetObjPtr` and `GetObjPtr` functions are called **exclusively from Python** — they have zero C++ call sites. All C++ producers write the field directly.

---

## C++ Event Types Carried by TGObjPtrEvent

### Network-Forwarded Events (cross the wire)

| Event Type | ET_ Constant | Context | objPtrID Contains |
|-----------|-------------|---------|-------------------|
| ET_WEAPON_FIRED | Offset 0x7C | All weapon fires (phaser, torpedo, tractor) | Target ID or 0 |
| ET_PHASER_STARTED_FIRING | Offset 0x81 | Phaser beam begins | Target ID |
| ET_PHASER_STOPPED_FIRING | Offset 0x83 | Phaser beam ends | Target ID |
| ET_TRACTOR_BEAM_STARTED_FIRING | Offset 0x7D | Tractor beam begins | Target ID |
| ET_REPAIR_INCREASE_PRIORITY | Offset 0x76 | Repair priority reorder (opcode 0x11) | Repair target subsystem ID |
| ET_STOP_FIRING_AT_TARGET_NOTIFY | Offset 0xDC | Stop firing notification (opcode 0x09, host-only) | Target ship ID or 0 |

### Local-Only Events (never sent over network)

| Event Type | ET_ Constant | Context | objPtrID Contains |
|-----------|-------------|---------|-------------------|
| ET_SET_PLAYER | Offset 0x0E | Player assignment | New player ship ID |
| ET_TARGET_WAS_CHANGED | Offset 0x58 | Ship re-targets | **Previous** target ID (not new) |
| ET_SUBSYSTEM_HIT | Offset 0x6B | Subsystem takes damage | Subsystem's own ID |
| ET_TRACTOR_TARGET_DOCKED | Offset 0x85 | Tractor docking complete | Docked ship ID |
| ET_SENSORS_SHIP_IDENTIFIED | Offset 0x88 | Sensor scan identifies ship | Identified ship ID |

Event type constants are offsets from `ET_TEMP_TYPE` (the base event constant defined in the scripting API).

### Dual-Fire Pattern

Weapon fire functions create **two** TGObjPtrEvent events simultaneously:

- **Phaser fire**: ET_PHASER_STARTED_FIRING + ET_WEAPON_FIRED
- **Tractor fire**: ET_TRACTOR_BEAM_STARTED_FIRING + ET_WEAPON_FIRED
- **Torpedo fire**: ET_WEAPON_FIRED only

This dual-fire pattern means every phaser/tractor firing cycle generates at least 4 TGObjPtrEvent messages (start_specific + weapon_fired + stopped_specific + stop_notify).

### ET_TARGET_WAS_CHANGED — Previous Target

Uniquely, ET_TARGET_WAS_CHANGED stores the **previous** target's ID in objPtrID, not the new target. This allows handlers to clean up references to the old target before the new one is applied.

### ET_STOP_FIRING_AT_TARGET_NOTIFY — Host-Only

Both phaser and tractor producers of this event gate on being the host before creating it. This event is only generated on the host, never on clients.

---

## Python Script Event Types (72+ call sites)

Python scripts create TGObjPtrEvents for 27+ additional event types, all **local-only** (never network-forwarded). Most common:

| ET_ Constant | Usage | Game System |
|-------------|-------|-------------|
| ET_ACTION_COMPLETED | 54 sites | Action/sequence callbacks |
| ET_CHARACTER_ANIMATION_DONE | 46 sites | Bridge crew animation completion |
| ET_SET_ALERT_LEVEL | 15 sites | Red/Yellow/Green alert |
| ET_MISSION_START | 11 sites | Mission initialization |
| ET_PLAYER_BOOT_EVENT | 8 sites | Player boot from server |
| ET_HAIL | 2 sites | Ship hailing |

---

## Why 45% of Combat PythonEvents

In a 33.5-minute 3-player battle (59 kills, 84 collisions):
- **1,718 of 3,825 PythonEvents** used factory 0x010C (TGObjPtrEvent)
- The dual-fire pattern is the primary driver: each phaser cycle produces start + weapon_fired + stopped events
- With 2,283 StartFiring events and 897 TorpedoFire events in that session, the weapon event volume directly explains the 1,718 TGObjPtrEvent count

---

## Decoded Packet Example

```
06                    opcode = 0x06 (PythonEvent)
0C 01 00 00           factoryID = 0x0000010C (TGObjPtrEvent)
7C 00 80 00           eventType = ET_WEAPON_FIRED
FF FF FF 3F           sourceObjID = 0x3FFFFFFF (Player 0's ship base ID)
FF FF FF 3F           destObjID = 0x3FFFFFFF (same ship — self-reference)
2A 00 00 00           objPtrID = 0x0000002A (weapon subsystem's TGObject ID)
```

---

## Implementation Requirements

An OpenBC implementation SHALL:

1. **Register factory ID 0x010C** in the TGStreamedObject factory for PythonEvent deserialization
2. **Serialize/deserialize the int32 objPtrID** as the last field after the base TGEvent fields
3. **Preserve the IsA chain**: 0x010C → 0x0101 → 0x02
4. **Support the dual-fire pattern**: weapon fire must emit both type-specific and generic ET_WEAPON_FIRED events
5. **Gate ET_STOP_FIRING_AT_TARGET_NOTIFY** to host-only generation
6. **Store previous target ID** (not new) in ET_TARGET_WAS_CHANGED events
7. **Forward network events** through the PythonEvent path (opcode 0x06 server→client, 0x0D client→server) or GenericEventForward path (opcodes 0x09, 0x11) as appropriate

---

## Factory ID Summary Table

| Factory ID | Class | Wire Size | Extension |
|-----------|-------|-----------|-----------|
| 0x0002 | TGEvent | 17 bytes | (none) |
| 0x0101 | TGSubsystemEvent | 17 bytes | (none) |
| 0x0105 | TGCharEvent | 18 bytes | +1 byte (char) |
| **0x010C** | **TGObjPtrEvent** | **21 bytes** | **+4 bytes (int32 object ID)** |
| 0x8129 | ObjectExplodingEvent | 25 bytes | +4 int32 + 4 float |
