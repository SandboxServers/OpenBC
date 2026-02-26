# Decoded Packet Examples


### Example 1: Akira at position (88, -66, -73)

Full game message bytes (after transport framing):

```
03 00 02 08 80 00 00 FF FF FF 3F 01 00 00 B0 42 00 00 84 C2 00 00 92 C2 ...
```

Field decode:

| Bytes | Field | Value |
|-------|-------|-------|
| `03` | opcode | 0x03 (ObjCreateTeam) |
| `00` | owner_player_slot | 0 (host) |
| `02` | team_id | 2 |
| `08 80 00 00` | class_id | 0x00008008 (Ship) |
| `FF FF FF 3F` | object_id | 0x3FFFFFFF (player 0 base) |
| `01` | species_type | 1 (Akira) |
| `00 00 B0 42` | position_x | 88.0 |
| `00 00 84 C2` | position_y | -66.0 |
| `00 00 92 C2` | position_z | -73.0 |
| ... | orientation, speed, name, set, subsystems | (continues) |

### Example 2: Sovereign at position (38, -49, -35)

```
03 00 02 08 80 00 00 FF FF FF 3F 05 00 00 18 42 00 00 44 C2 00 00 0C C2 ...
```

| Bytes | Field | Value |
|-------|-------|-------|
| `03` | opcode | 0x03 (ObjCreateTeam) |
| `00` | owner_player_slot | 0 (host) |
| `02` | team_id | 2 |
| `08 80 00 00` | class_id | 0x00008008 (Ship) |
| `FF FF FF 3F` | object_id | 0x3FFFFFFF (player 0 base) |
| `05` | species_type | 5 (Sovereign) |
| `00 00 18 42` | position_x | 38.0 |
| `00 00 44 C2` | position_y | -49.0 |
| `00 00 0C C2` | position_z | -35.0 |
| ... | orientation, speed, name, set, subsystems | (continues) |

Both examples: same player (slot 0), same team (2), same object ID base — but different ship species and spawn positions. This is consistent with a player changing ship selection (the second creation replaces the first).

