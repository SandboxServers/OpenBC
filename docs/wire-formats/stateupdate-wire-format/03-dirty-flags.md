# Dirty Flags


Each bit in the `dirty_flags` byte indicates whether the corresponding field is present in this message:

| Bit | Mask | Name | Data Size | Description |
|-----|------|------|-----------|-------------|
| 0 | 0x01 | POSITION | 12-15 bytes | Absolute world position + optional integrity hash |
| 1 | 0x02 | DELTA | 5 bytes | Compressed position delta from last absolute |
| 2 | 0x04 | FORWARD | 3 bytes | Forward orientation vector |
| 3 | 0x08 | UP | 3 bytes | Up orientation vector |
| 4 | 0x10 | SPEED | 2 bytes | Current speed |
| 5 | 0x20 | SUBSYSTEMS | variable | Subsystem health round-robin |
| 6 | 0x40 | CLOAK | 1 byte | Cloaking device state |
| 7 | 0x80 | WEAPONS | variable | Weapon health round-robin |

Fields are serialized in flag-bit order (0x01 first, 0x80 last). Only fields whose flag bit is set are present.

### Direction-Based Flag Split

Verified across 199,541 StateUpdate messages from a 34-minute 3-player combat session:

| Direction | Predominantly Includes | Rarely Includes |
|-----------|----------------------|-----------------|
| **Ship owner -> server** | 0x80 (WEAPONS) | 0x20 (SUBSYSTEMS) |
| **Server -> all clients** | 0x20 (SUBSYSTEMS) | 0x80 (WEAPONS) |

This reflects the authority model:
- **Ship owners** are authoritative for their own position, orientation, speed, and weapon charge/cooldown state
- **The server** is authoritative for subsystem health (damage is computed server-side)

The two flags are **predominantly split by direction** (~96% correlation). The host's own ship object sends WEAPONS (0x80) in the server-to-client direction because the host is also a player — its weapon charge/cooldown state must be broadcast to clients like any other player's. The parser must accept weapon health flags on server-to-client packets, and the sender must include weapon data for the host's own ship when broadcasting state updates.

**Common client-to-server flag patterns** (observed frequencies):
- `0x9E` — DELTA + FORWARD + UP + SPEED + WEAPONS (full movement + weapons)
- `0x96` — DELTA + FORWARD + SPEED + WEAPONS
- `0x92` — DELTA + SPEED + WEAPONS
- `0x9D` — POSITION + FORWARD + UP + SPEED + WEAPONS (periodic absolute sync)

**Common server-to-client flag patterns**:
- `0x20` — SUBSYSTEMS only (most common; server sends health data without position)
- `0x3E` — DELTA + FORWARD + UP + SPEED + SUBSYSTEMS
- `0x3D` — POSITION + FORWARD + UP + SPEED + SUBSYSTEMS

