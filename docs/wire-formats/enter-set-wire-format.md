# EnterSet Wire Format (Opcode 0x1F)

Wire format specification for the EnterSet message in Bridge Commander multiplayer, documented from network packet captures and the game's shipped Python scripting API.

**Clean room statement**: This document contains no decompiled code, no binary addresses, no internal memory offsets, and no handler function names. All formats are derived from observable wire data, protocol structure analysis, and the public SWIG scripting API.

---

## Overview

EnterSet (opcode 0x1F) moves a game object from one Set (scene region / star system) to another. This is used in custom missions with multiple zones — for example, warping a ship from one star system to another.

**Direction**: Server → all clients (excluding the object's owner)
**Delivery**: Reliable (ACK required)
**Trigger**: The server catches the `ET_ENTER_SET` event when an object moves between named Sets, then broadcasts the transition to all other peers.

**Priority**: Low. Zero instances observed in any standard multiplayer trace (FFA deathmatch, 3-player, 33.5 minutes). All ships stay in a single Set for standard game modes. This opcode is only relevant for custom missions with multi-zone maps.

---

## Wire Format

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode          0x1F
1       4     i32     objectID        Network object ID being moved (LE)
5       2     u16     setNameLen      Length of target Set name string (LE)
7       var   bytes   setName         Target Set name (ASCII, not null-terminated)
```

Total size: 7 + setNameLen bytes.

---

## Field Descriptions

### objectID (i32)

The network object ID of the game object being moved to a new Set. Typically a ship. To determine which player owns the object, use the standard formula `(objectId - 0x3FFFFFFF) >> 18`.

### setName (string)

The name of the target Set (scene region) the object is moving into. Standard multiplayer Set names are:

| Set Name | Description |
|----------|-------------|
| Multi1 | Default FFA map zone 1 |
| Multi2-Multi7 | Additional map zones |
| Albirea | Campaign system |
| Poseidon | Campaign system |

Source: `scripts/Multiplayer/SpeciesToSystem.py` (shipped with the game). Custom missions may define additional Set names.

---

## Receiver Behavior

When a client receives an EnterSet message:

1. **Look up the object** by `objectID`. If not found, send a RequestObject (0x1E) back to the server to request the missing object's data, then stop processing.

2. **Validate the object** has a parent Set but is NOT nested more than one level deep (must be a top-level Set member, not a sub-object within another object's scene graph).

3. **Look up the target Set** by `setName` from the game's Set registry.

4. **Remove the object** from its current Set.

5. **Add the object** to the target Set.

This effectively "teleports" the object from one scene region to another. All clients that receive the message update their local game world accordingly.

---

## Server-Side Generation

The server generates EnterSet messages in response to the `ET_ENTER_SET` game event (observable via the Python scripting API as `App.ET_ENTER_SET`). When a ship enters a named Set on the server:

1. The server checks that the ship is in a named sub-Set (not the root scene node)
2. Serializes opcode 0x1F with the object ID and Set name
3. Sends reliably to all other peers (excluding the object's owner)

In standard multiplayer modes (FFA, TDM), all ships spawn into a single Set and never leave it, so this event never fires.

---

## Implementation Notes

1. **Dead code in standard modes**: This opcode can be safely stubbed for FFA/TDM implementations. It only matters for custom multi-zone missions.

2. **RequestObject fallback**: If the receiving client doesn't have the object, it requests it via opcode 0x1E. An implementation must handle this graceful fallback.

3. **Mod relevance**: Custom mission mods that implement warp-between-systems gameplay would use this opcode. An implementation should handle it for mod compatibility even if standard modes don't trigger it.

4. **String encoding**: The Set name uses a u16 length prefix (little-endian) followed by raw ASCII bytes, the same encoding used by other string fields in the protocol (e.g., map name in Settings 0x00).

---

## Related Documents

- **[objcreate-wire-format.md](objcreate-wire-format.md)** — SpeciesToSystem table (Set/map names)
- **[../protocol/protocol-reference.md](../protocol/protocol-reference.md)** — Complete opcode table
- **[../network-flows/join-flow.md](../network-flows/join-flow.md)** — Settings packet includes map name
