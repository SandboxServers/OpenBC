# DeletePlayerUI (Opcode 0x17) Wire Format

**Clean room statement**: All wire formats are derived from packet captures of stock BC clients and servers. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

Opcode 0x17 is a **player list event transport** that carries a serialized event structure to the client's engine event system. Despite the name "DeletePlayerUI", it is used for **both** adding players (at join time) and removing players (at disconnect time).

The client's engine deserializes the event using its factory system and dispatches it to internal handlers. At join time, this populates the engine's internal player list. At disconnect time, it removes the player from the list.

---

## Wire Format

```
Offset  Size  Type     Field            Notes
------  ----  ----     -----            -----
0       1     u8       opcode           Always 0x17
1       4     u32le    factory_class_id Event class identifier (always 0x00000866)
5       4     u32le    event_code       Event type code (context-dependent)
9       4     u32le    src_obj_id       Source object ID (typically 0x00000000)
13      4     u32le    tgt_obj_id       Target object ID (ship or player object)
17      1     u8       wire_peer_id     Wire peer slot (1-based)
```

**Total**: 18 bytes (1 opcode + 17 payload).

### Factory Class ID

Always `0x00000866`. This identifies the base event class. The engine's factory deserializer uses this to construct the appropriate event object from the stream.

### Event Codes

| Context | Event Code | Meaning |
|---------|-----------|---------|
| Player join | `0x008000F1` | New player notification — adds to engine player list |
| Player disconnect | `0x00060005` | Player removed notification — removes from engine player list |

### Source and Target Objects

- `src_obj_id`: Typically `0x00000000` (no source) in both join and disconnect contexts
- `tgt_obj_id`: A session-specific object ID. Varies per session and per player.
- `wire_peer_id`: The player's wire peer slot (1-based index), matching the slot assigned during connection.

---

## Usage Contexts

### Join Time (Server -> Client)

Sent by the server after receiving NewPlayerInGame (0x2A) from the client. Delivered alongside MissionInit (0x35) in the same UDP packet.

```
[0x17][66 08 00 00][F1 00 80 00][00 00 00 00][xx xx xx xx][peer_id]
       class_id     NEW_PLAYER   src=NULL     tgt=objID    slot
```

This populates the engine's internal player list. Without this message, the player list accessed by `GetPlayerList()` remains empty, and the scoreboard UI cannot display any players.

### Disconnect Time (Server -> Remaining Clients)

Sent by the server to all remaining clients when a player disconnects (graceful, timeout, or kick). Part of the disconnect cleanup sequence alongside DestroyObject (0x14) and DeletePlayerAnim (0x18).

```
[0x17][66 08 00 00][05 00 06 00][00 00 00 00][xx xx xx xx][peer_id]
       class_id     DEL_PLAYER   src=NULL     tgt=objID    slot
```

---

## Scoreboard Dependency

The client's scoreboard requires **two** conditions to display a player entry:

1. **Engine player list**: The player must exist in the internal player list (populated by opcode 0x17 at join time)
2. **Score dictionary**: The player's network ID must appear in the score tracking dictionaries (populated by SCORE_MESSAGE 0x37 or SCORE_CHANGE 0x36)

If opcode 0x17 is missing or malformed at join time, the player list stays empty and the scoreboard shows nothing regardless of score data.

---

## Hex Example (from stock packet capture)

Join-time message for player 2:

```
17 66 08 00 00 F1 00 80 00 00 00 00 00 4F 06 00 00 02
│  │           │           │           │           │
│  │           │           │           │           └─ wire_peer_id = 2
│  │           │           │           └─ tgt_obj_id = 0x0000064F
│  │           │           └─ src_obj_id = 0x00000000
│  │           └─ event_code = 0x008000F1 (new player)
│  └─ factory_class_id = 0x00000866
└─ opcode = 0x17
```

---

## Related Documents

- [../network-flows/join-flow.md](../network-flows/join-flow.md) -- Join sequence (sends 0x17 after 0x2A)
- [../network-flows/disconnect-flow.md](../network-flows/disconnect-flow.md) -- Disconnect cleanup (sends 0x17 to remaining clients)
