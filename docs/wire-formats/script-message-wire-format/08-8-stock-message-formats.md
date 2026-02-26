# 8. Stock Message Formats


### CHAT_MESSAGE (0x2C)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x2C
1       4     i32     sender_network_id (from pNetwork.GetLocalID())
5       2     u16     text_length
7       var   bytes   message_text (ASCII, no null terminator)
```

Direction: Client → Host → ALL clients (including sender)

**Correction (Feb 2026)**: Previous documentation stated relay uses the "NoMe" group (excludes
sender). A stock dedi server trace showed 5 chat messages received → 10 sent (2 per message
in a 2-player game), confirming the server echoes chat to ALL clients including the original
sender. This uses broadcast-to-all, not the "NoMe" group.

### TEAM_CHAT_MESSAGE (0x2D)

Same format as CHAT_MESSAGE but relayed only to team members.

### MISSION_INIT_MESSAGE (0x35)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x35
1       1     u8      current_player_count
2       1     u8      system_index
3       1     u8      time_limit (0xFF = no limit)
[if time_limit != 0xFF:]
4       4     i32     end_time
[end if]
+0      1     u8      frag_limit
```

> **Correction**: The first payload byte is `current_player_count` (dynamic, u8), not
> `player_limit` (i32). Stock servers send the number of currently connected players.
> The join-flow document has the authoritative format; this section is corrected to match.

Direction: Host → joining client (sent after client sends NewPlayerInGame 0x2A)

### SCORE_CHANGE_MESSAGE (0x36)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x36
1       4     i32     killer_player_id (0 if suicide/environment)
[if killer_player_id != 0:]
5       4     i32     killer_kills
9       4     i32     killer_score
[end if]
+0      4     i32     victim_player_id
+4      4     i32     victim_deaths
+8      1     u8      update_count (number of additional score entries)
[repeated update_count times:]
  +0    4     i32     player_id
  +4    4     i32     score
[end repeat]
```

Direction: Host → all clients (broadcast on kill)
All player IDs in this message use the network ID domain (`GetNetID()` / wire slot).

### SCORE_MESSAGE (0x37)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x37
1       4     i32     player_id
5       4     i32     kills
9       4     i32     deaths
13      4     i32     score
```

Direction: Host → joining client (full score roster sync, one message per player)
`player_id` uses the network ID domain (`GetNetID()` / wire slot).

### END_GAME_MESSAGE (0x38)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x38
1       4     i32     reason_code
```

Reason codes: 0-6 (game-specific end conditions)
Direction: Host → all clients (broadcast)

### RESTART_GAME_MESSAGE (0x39)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x39
```

Direction: Host → all clients (broadcast, 1 byte, opcode only)

---

