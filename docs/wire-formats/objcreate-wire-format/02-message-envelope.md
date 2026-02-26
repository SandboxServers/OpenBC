# Message Envelope


The game message payload (after transport-layer framing) begins with:

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode              0x02 or 0x03
1       1     i8      owner_player_slot   Player slot index (0-15)
```

If the opcode is 0x03, an additional byte follows:

```
2       1     i8      team_id             Team assignment (e.g., 2 or 3)
```

**Envelope size**: 2 bytes for opcode 0x02, 3 bytes for opcode 0x03.

The remainder of the message is a serialized object stream.

