# Message Header


```
Offset  Size  Type    Field           Description
------  ----  ----    -----           -----------
0       1     u8      opcode          Always 0x1C
1       4     i32     object_id       Ship's network object ID (little-endian)
5       4     f32     game_time       Current game clock timestamp (little-endian)
9       1     u8      dirty_flags     Bitmask of which fields follow
```

**Header size**: 10 bytes, always present. The `dirty_flags` byte determines which variable-length fields follow the header.

