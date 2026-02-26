# Ship Object Body (class_id = 0x8008)


After the 8-byte object header, ship objects serialize the following fields:

```
Offset  Size  Type      Field               Description
------  ----  ----      -----               -----------
0       1     u8        species_type        Ship type index (see Species Tables below)
1       4     f32       position_x          World X coordinate
5       4     f32       position_y          World Y coordinate
9       4     f32       position_z          World Z coordinate
13      4     f32       orientation_w        Quaternion W component
17      4     f32       orientation_x        Quaternion X component
21      4     f32       orientation_y        Quaternion Y component
25      4     f32       orientation_z        Quaternion Z component
29      4     f32       speed               Speed magnitude (typically 0.0 at spawn)
33      3     u8[3]     reserved            Always observed as 0x00 0x00 0x00
36      1     u8        player_name_len     Length of player name string
37      var   ascii     player_name         Player display name (NOT null-terminated)
+0      1     u8        set_name_len        Length of star system name string
+1      var   ascii     set_name            Star system name (e.g. "Multi1")
+0      var   data      subsystem_state     Per-subsystem health data (ship-type dependent)
```

All multi-byte numeric fields are little-endian. Offsets after `player_name` are relative since the name is variable-length.

### Field Notes

**species_type**: Indexes into the `SpeciesToShip` table (see below). Determines which ship model, hardpoints, and subsystems are loaded. Values 1-15 are playable ships; 16-45 are NPCs, stations, and asteroids.

**position / orientation**: Spawn location. Orientation is a quaternion (W, X, Y, Z). All floats are IEEE 754 single-precision.

**speed**: Initial speed magnitude. Usually 0.0 for newly spawned ships.

**reserved**: Three bytes always observed as zero. May be reserved for future state flags.

**set_name**: The star system the object spawns into, NOT the ship class name. Maps to `SpeciesToSystem` entries (see below). The ship class is determined solely by `species_type`.

**subsystem_state**: Variable-length blob encoding per-subsystem health. Format varies by ship type (different ships have different numbers and types of subsystems). Appears to encode floating-point health values per subsystem.

