# Flag 0x80 — Weapon Health (Round-Robin)


Client-to-server only. Reports weapon subsystem health using a round-robin scheme similar to flag 0x20.

```
[repeated while within ~6-byte budget:]
  +0    1     u8      weapon_index       Index of this weapon in the ship's weapon list
  +1    1     u8      health_byte        Scaled health: truncate(health * scale_factor)
```

Each weapon entry is 2 bytes: `[index:u8][health:u8]`. The serializer iterates through the weapon list, sending a few entries per update and cycling through all weapons over time.

**Size**: Variable (typically 4-6 bytes per update).

