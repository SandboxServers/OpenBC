# ObjNotFound and RequestObj Wire Formats (Opcodes 0x1D and 0x1E)

Wire format specifications for the ObjNotFound and RequestObj messages in Bridge Commander multiplayer. These two opcodes form an **object recovery protocol**: when a client encounters an unknown object ID, it initiates a request-response cycle with the host to obtain the full object state.

**Clean room statement**: This document contains no decompiled code, no binary addresses, no internal memory offsets, and no handler function names. All formats are derived from observable wire data, protocol structure analysis, and the public SWIG scripting API.

---

## Overview

| Opcode | Name        | Python Constant        | Direction       |
|--------|-------------|------------------------|-----------------|
| 0x1D   | ObjNotFound | (none)                 | Client → Host   |
| 0x1E   | RequestObj  | `App.SEND_OBJECT_MESSAGE` | Client → Host, Host → Client |

These opcodes always appear as a pair in the recovery flow:

```
CLIENT                          HOST
  |                               |
  | -- 0x1D (missing objID) ----> |
  |                               | [look up object, serialize]
  | <-- 0x1E (ObjCreate) -------- |  (response is ObjCreate 0x02/0x03, not 0x1E)
  |    + optional 0x29 replays    |
```

**Priority**: Low. Only fires when a client receives a message referencing an object it hasn't yet created locally (packet reordering, late-join lag). Zero instances expected in normal low-latency play.

---

## Opcode 0x1D — ObjNotFound

### Wire Format

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode      0x1D
1       4     i32     objectID    Unknown object's network ID (LE)
```

Total size: 5 bytes.

### Direction

Client → Host (unicast, reliable)

### Semantics

A client sends 0x1D when it receives ANY game message (StateUpdate, weapon fire, collision, etc.) that references an object ID it cannot find in its local object registry.

The receiving host is expected to respond with a full object serialization for that ID via opcode 0x02/0x03 (ObjCreate/ObjCreateTeam).

---

## Opcode 0x1E — RequestObj

### Wire Format (as sent: Client → Host)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode      0x1E
1       4     i32     objectID    ID of the object to request (LE)
```

Total size: 5 bytes.

### Response (Host → requesting Client)

The host responds with a standard ObjCreate packet — NOT with a 0x1E wrapper. The response format is:

**Non-player objects:**
```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode          0x02 (ObjCreate)
1       1     u8      playerSlot      (objectID - 0x3FFFFFFF) >> 18
2       var   bytes   objectData      Full WriteToStream serialization
```

**Player ships:**
```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode          0x03 (ObjCreateTeam)
1       1     u8      playerSlot      (objectID - 0x3FFFFFFF) >> 18
2       1     u8      species         Ship species/net-type identifier
3       var   bytes   objectData      Full WriteToStream serialization
```

The `objectData` is the same serialization chain used during initial object creation in `NewPlayerInGame` (opcode 0x2A). A ship's full state includes position, orientation, velocity, all subsystem states, weapons, and shields.

After the ObjCreate response, the host also replays any queued explosion events for that object as individual 0x29 (Explosion) packets, sent directly to the requesting client. This ensures the client gets a complete picture of in-progress combat state.

### Direction

Request: Client → Host (unicast, reliable)
Response: Host → requesting Client (unicast, reliable, NOT broadcast)

---

## Host-Side Processing Gates

The host only sends the ObjCreate response if ALL of the following are true:

1. The object exists in the host's registry
2. The object is "networked" (flagged for network replication)
3. If the object is a `DamageableObject` (ship, station): its current HP is above a minimum threshold AND it is not yet fully dead

If any gate fails, the host silently drops the request. This prevents wasteful resending of objects that are about to be destroyed anyway.

---

## Recovery Flow: ObjNotFound vs. RequestObj

Both 0x1D and 0x1E trigger the same host-side response (an ObjCreate packet). The difference is their source:

- **0x1D** is sent by a client that received a message referencing an unknown object — the client noticed the gap.
- **0x1E** is sent by a client that explicitly wants the full state of a specific object (e.g., EnterSet 0x1F received an unknown objectID and internally triggered a RequestObj).

In practice, 0x1D is the primary recovery path. Opcode 0x1E is more of an internal signal used by the EnterSet handler.

---

## Implementation Notes

### Receiving 0x1D (host behavior)

1. Parse the 4-byte objectID.
2. Look up the object. If not found, drop silently.
3. Check gates (networked, alive, sufficient HP).
4. If all pass: build ObjCreate (0x02/0x03) and send unicast to the requesting connection, reliable.
5. Re-send any queued explosion events for the object, also unicast to that connection.

### Receiving 0x1E (host behavior)

Same as 0x1D. The host does not distinguish the trigger — the response is always an ObjCreate to the requester.

### Sending 0x1D (client behavior)

Whenever a message handler cannot find a referenced object:
1. Build a 5-byte `[0x1D][objectID]` packet.
2. Send to the host (connection 0), guaranteed.

### Rate limiting

There is no explicit rate-limiting in the reference implementation. However, because these are guaranteed (reliable) packets, the game's reliable delivery layer handles retransmission. An implementation may want to track recently-requested IDs to avoid flooding the host if the object persistently doesn't exist.

---

## Related Documents

- **[enter-set-wire-format.md](enter-set-wire-format.md)** — EnterSet (0x1F) which internally triggers 0x1E
- **[objcreate-wire-format.md](objcreate-wire-format.md)** — ObjCreate/ObjCreateTeam (0x02/0x03) response format
- **[explosion-wire-format.md](explosion-wire-format.md)** — Explosion (0x29) replayed as part of 0x1E response
- **[../protocol/protocol-reference.md](../protocol/protocol-reference.md)** — Complete opcode table
