# Decoded Packet Examples


### Example 1: Client StateUpdate (flags=0x9D — POSITION + FORWARD + UP + SPEED + WEAPONS)

```
Client -> Server (embedded in 208-byte packet):

Raw bytes:
1C FF FF FF 3F 00 80 E1 41 9D 00 00 B0 42 00 00
84 C2 00 00 92 C2 21 37 FB 0B 68 46 30 BB 5E 00
00 01 CC 02 CC 04 CC
```

| Bytes | Field | Decoded Value |
|-------|-------|---------------|
| `1C` | opcode | 0x1C (StateUpdate) |
| `FF FF FF 3F` | object_id | 0x3FFFFFFF (player 0's ship) |
| `00 80 E1 41` | game_time | 28.19 seconds |
| `9D` | dirty_flags | 0x9D = POSITION + FORWARD + UP + SPEED + WEAPONS |
| `00 00 B0 42` | position_x | 88.0 |
| `00 00 84 C2` | position_y | -66.0 |
| `00 00 92 C2` | position_z | -73.0 |
| `21` | has_hash | bit-packed true (0x21) |
| `37 FB` | subsys_hash | 0xFB37 |
| `0B 68 46` | forward_vector | (0.09, 0.82, 0.55) |
| `30 BB 5E` | up_vector | (0.38, -0.54, 0.74) |
| `00 00` | speed | 0.0 (stationary) |
| `01 CC 02 CC 04 CC` | weapons | [subsys1:100%, subsys2:100%, subsys4:100%] |

Note: This is from a freshly spawned ship (speed 0, full weapon health). The subsystem hash 0xFB37 is consistent across all position updates for this ship in the session.

### Example 2: Server StateUpdate (flags=0x20 — SUBSYSTEMS only)

```
Server -> Client, 20-byte game payload (within 30-byte transport frame):

Raw bytes:
1C FF FF FF 3F 00 A0 1B 42 20 08 FF 60 FF FF FF
FF FF FF FF FF FF FF FF FF

Decoded:
  opcode: 0x1C (StateUpdate)
  object_id: 0x3FFFFFFF (player 0's ship)
  game_time: 38.91 seconds
  dirty_flags: 0x20 (SUBSYSTEMS only)
  start_index: 8
  health bytes: FF 60 FF FF FF FF FF FF FF FF FF FF FF
```

| Bytes | Field | Decoded Value |
|-------|-------|---------------|
| `1C` | opcode | 0x1C (StateUpdate) |
| `FF FF FF 3F` | object_id | 0x3FFFFFFF |
| `00 A0 1B 42` | game_time | 38.91 seconds |
| `20` | dirty_flags | 0x20 (SUBSYSTEMS) |
| `08` | start_index | Start at subsystem index 8 |
| `FF` | subsystem 8 | 100% health |
| `60` | subsystem 9 | ~38% health (damaged!) |
| `FF FF ...` | subsystems 10+ | 100% health (remaining subsystems) |

This shows the server sending a round-robin subsystem health update. Subsystem index 9 has taken damage (38% health). All other subsystems in this window are at full health.

### Minimal StateUpdate

The smallest valid StateUpdate is 10 bytes (header only, no field data):

```
1C [object_id:4] [game_time:4] 00
```

With `dirty_flags = 0x00`, no conditional fields follow. This is a heartbeat — it confirms the object exists and is at the same game time, but reports no state changes. This is observed when the server's ship object has no subsystems loaded (the flag 0x20 is suppressed).

