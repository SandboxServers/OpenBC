# Flag 0x01 — Absolute Position


Sent periodically to reset the delta compression baseline and provide authoritative position.

```
Offset  Size  Type           Field           Description
------  ----  ----           -----           -----------
+0      4     f32            position_x      World X coordinate
+4      4     f32            position_y      World Y coordinate
+8      4     f32            position_z      World Z coordinate
+12     1     bitpacked      has_hash        Boolean: is integrity hash present?
[if has_hash AND multiplayer:]
+13     2     u16            subsys_hash     XOR-folded subsystem integrity hash
```

All floats are IEEE 754 single-precision, little-endian.

The `has_hash` field is a **bit-packed boolean** (see Bit Packing below), NOT a raw byte. On the wire it appears as `0x21` (true) or `0x20` (false).

When absolute position is sent, the delta compression baseline is reset: saved position becomes the current position, and the delta direction/magnitude are cleared to zero.

**Total size**: 13 bytes (no hash) or 15 bytes (with hash).

### Subsystem Integrity Hash

The hash is an anti-cheat mechanism. The sender computes a 32-bit hash from all subsystem health and property values, XOR-folds it to 16 bits, and transmits it. The receiver computes the same hash locally and compares.

**Hash algorithm**:
```
accumulator = 0x00000000

For each subsystem (shields, hull, sensors, engines, weapons, cloak, repair, crew, power):
    For each health/property float value in the subsystem:
        ival = truncate_to_int(abs(value))
        if value < 0: ival = -ival
        accumulator = accumulator XOR ival  (byte-by-byte XOR)
        accumulator = rotate_left(accumulator, 1)

wire_hash = (accumulator >> 16) XOR (accumulator & 0xFFFF)
```

If the received hash does not match the locally computed hash, the sender is kicked from the game (anti-cheat violation).

