# EnterSet Wire Format (Opcode 0x1F)

Wire format specification for the EnterSet message in Bridge Commander multiplayer, documented from network packet captures and the game's shipped Python scripting API.

**Clean room statement**: This document contains no decompiled code, no binary addresses, no internal memory offsets, and no handler function names. All formats are derived from observable wire data, protocol structure analysis, and the public SWIG scripting API.

---

## Overview

EnterSet (opcode 0x1F — `VERIFY_ENTER_SET_MESSAGE`) notifies the host that a client's ship is entering a named game Set (scene region / star system zone).

**Direction**: Host → Client(s) (server-generated broadcast)
**Delivery**: Reliable (ACK required)
**Trigger**: The server sends this to clients when a ship enters a named game Set during warp transit.

**Correction (Feb 2026)**: Previous documentation stated "Client → Host only." A stock dedi
server-side trace showed 4 instances S→C (sent by server) and 0 instances C→S (received from
clients). The server generates EnterSet broadcasts; clients do not send this opcode.

**Priority**: Low. Zero instances observed in any standard multiplayer FFA trace (3-player, 33.5 minutes). All ships stay in the default space Set for standard game modes. This opcode is only relevant for custom multi-zone missions.

---

## Wire Format

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode          0x1F
1       4     i32     objectID        Network object ID of the ship (LE)
5       4     i32     nameLength      Length of set name string (LE)
9       var   ascii   setName         Target Set name (nameLength bytes, ASCII)
+0      1     u8      null            Null terminator (0x00)
```

Total size: 10 + nameLength bytes.

**Correction (Feb 2026)**: Previous documentation stated the set name was "null-terminated
ASCII — NOT a length-prefixed string." A 2-player client-side trace captured 2 EnterSet
messages showing the string IS written with a 4-byte int32 length prefix (via WriteInt32)
AND is null-terminated. Both the length prefix and the null terminator are present.

---

## Field Descriptions

### objectID (i32)

The network object ID of the ship entering the new Set. To determine which player owns the ship, use `(objectId - 0x3FFFFFFF) >> 18`.

### setName (null-terminated ASCII)

The name of the target Set (scene region) the ship is moving into. Standard multiplayer Set names are:

| Set Name | Description |
|----------|-------------|
| Multi1   | FFA map zone 1 |
| Multi2–Multi7 | Additional map zones |
| Albirea  | Campaign system |
| Poseidon | Campaign system |

Source: `scripts/Multiplayer/SpeciesToSystem.py` (shipped with the game). Custom missions may define additional Set names.

---

## Host-Side Behavior (Receiver)

When the host receives an EnterSet message:

1. **Look up the ship** by `objectID`. If not found, send an ObjNotFound (0x1D) response back to the originating client connection and stop. (Note: EnterSet is Client → Host, so the "source" here is the client that sent the message, not the host. The host should direct the recovery response to that client.)

2. **Validate**: the ship must have a warp engine subsystem AND must not currently be mid-warp-transition.

3. **Look up the target Set** by name from the game's Set manager.

4. **If ship is already in the target Set**: no-op.

5. **Otherwise**:
   - Notify the ship's current Set that the ship is departing.
   - Move the ship into the target Set with its current placement.

The transition is authoritative on the host. The host does NOT broadcast an EnterSet to other clients — the client that sent this message is the only one whose ship moves; other clients learn of the new position via StateUpdate (opcode 0x1C).

---

## Client-Side Generation

A client sends 0x1F when its local ship changes Set membership during an in-system warp AND the destination is a non-default named Set (not the default space combat arena). The condition is:

1. Ship's warp engine is in-flight (warp state flag is non-zero)
2. Current Set name != default space Set name

If the warp engine is NOT active (unexpected set change), the client sends 0x1D (ObjNotFound) instead, which triggers a recovery sequence.

The message is sent to the "NoMe" network group (all peers except self).

---

## Implementation Notes

1. **Dead code in standard modes**: This opcode can be safely stubbed for FFA/TDM implementations. It only matters for custom multi-zone missions.

2. **String encoding**: The set name is **both** length-prefixed (4-byte int32) AND null-terminated.
   The int32 length prefix precedes the ASCII string, which ends with a 0x00 null terminator.

3. **Mod relevance**: Custom mission mods that implement warp-between-systems gameplay would use this opcode. An implementation should handle it for mod compatibility even if standard modes don't trigger it.

4. **Direction correction (Feb 2026)**: This opcode flows Host → Client(s), not Client → Host. The server generates EnterSet broadcasts when a ship transitions to a new Set during warp. Zero instances were received from clients in a stock dedi trace.

---

## Related Documents

- **[objnotfound-requestobj-wire-format.md](objnotfound-requestobj-wire-format.md)** — Opcodes 0x1D and 0x1E: object recovery protocol
- **[objcreate-wire-format.md](objcreate-wire-format.md)** — Object creation wire format (response to 0x1E)
- **[../protocol/protocol-reference.md](../protocol/protocol-reference.md)** — Complete opcode table
