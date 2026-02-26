# Serialized Object Stream


### Object Header (8 bytes)

Every serialized object begins with:

```
Offset  Size  Type    Field
------  ----  ----    -----
0       4     i32     class_id            Object class identifier (little-endian)
4       4     i32     object_id           Unique network object ID (little-endian)
```

The `class_id` determines what type of object to instantiate. The `object_id` is globally unique — if an object with that ID already exists, the message is ignored (duplicate protection).

#### Object ID Allocation

Each player slot is assigned a range of 262,143 object IDs:

```
Player N base = 0x3FFFFFFF + (N * 0x40000)
```

To extract the owning player slot from an object ID:

```
slot = (object_id - 0x3FFFFFFF) >> 18
```

### Class ID Table

| Class ID | Hex Bytes (LE) | Object Type | Notes |
|----------|----------------|-------------|-------|
| 0x00008008 | `08 80 00 00` | Ship / Station | Full spatial tracking (position, velocity) |
| 0x00008009 | `09 80 00 00` | Torpedo / Projectile | No spatial tracking (uses dedicated fire messages) |

