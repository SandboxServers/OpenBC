# Message Structure


```
Offset  Size  Type    Field          Notes
------  ----  ----    -----          -----
0       1     u8      opcode         Always 0x06
1       4     i32     factory_id     Event class type (determines remaining layout)
5       4     i32     event_type     Event type constant (0x008000xx)
9       4     i32     source_obj_id  Source object reference
13      4     i32     dest_obj_id    Destination/related object reference
[class-specific extension follows]
```

The first 17 bytes are common to all event classes (base event fields). The payload
after byte 16 depends on `factory_id`.

All multi-byte values are **little-endian**.

---

