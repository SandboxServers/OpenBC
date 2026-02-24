# EnterSet Wire Format (Opcode 0x1F)

Wire format specification for the EnterSet message in Bridge Commander multiplayer, documented from network packet captures, the game's shipped Python scripting API, and confirmed via binary analysis.

**Clean room statement**: This document contains no decompiled code, no binary addresses, no internal memory offsets, and no handler function names. All formats are derived from observable wire data, protocol structure analysis, and the public SWIG scripting API.

---

## Overview

EnterSet (opcode 0x1F — `VERIFY_ENTER_SET_MESSAGE`) notifies the host that a client's ship is entering a named game Set (scene region / star system zone).

**Direction**: Client → Host only
**Delivery**: Reliable (ACK required)
**Trigger**: A client sends this when its own ship begins a warp transit and its destination Set is NOT the default space combat set.

**Priority**: Low. Zero instances observed in any standard multiplayer FFA trace (3-player, 33.5 minutes). All ships stay in the default space Set for standard game modes. This opcode is only relevant for custom multi-zone missions.

---

## Wire Format

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode          0x1F
1       4     i32     objectID        Network object ID of the ship (LE)
5       var   cstr    setName         Target Set name (null-terminated ASCII)
```

Total size: 6 + strlen(setName) + 1 bytes.

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

1. **Look up the ship** by `objectID`. If not found, send a RequestObj (0x1E) back to the source (the host itself, connection 0) and stop.

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

2. **String encoding**: Null-terminated ASCII — NOT a length-prefixed string. The string is immediately after the 4-byte object ID and continues until a null byte (0x00).

3. **Mod relevance**: Custom mission mods that implement warp-between-systems gameplay would use this opcode. An implementation should handle it for mod compatibility even if standard modes don't trigger it.

4. **Direction correction**: This opcode flows Client → Host only, not Server → Clients. The host acts on it locally and does not relay it as-is.

---

## Related Documents

- **[objnotfound-requestobj-wire-format.md](objnotfound-requestobj-wire-format.md)** — Opcodes 0x1D and 0x1E: object recovery protocol
- **[objcreate-wire-format.md](objcreate-wire-format.md)** — Object creation wire format (response to 0x1E)
- **[../protocol/protocol-reference.md](../protocol/protocol-reference.md)** — Complete opcode table
