# 4. Map Registry


### 4.1 Source

`Multiplayer/SpeciesToSystem.py` defines the map list.

### 4.2 Map Table

| Index | Key | Load Path | Type |
|-------|-----|-----------|------|
| 0 | Multi1 | `Systems.Multi1.Multi1` | Multiplayer |
| 1 | Multi2 | `Systems.Multi2.Multi2` | Multiplayer |
| 2 | Multi3 | `Systems.Multi3.Multi3` | Multiplayer |
| 3 | Multi4 | `Systems.Multi4.Multi4` | Multiplayer |
| 4 | Multi5 | `Systems.Multi5.Multi5` | Multiplayer |
| 5 | Multi6 | `Systems.Multi6.Multi6` | Multiplayer |
| 6 | Multi7 | `Systems.Multi7.Multi7` | Multiplayer |
| 7 | Albirea | `Systems.Albirea.Albirea` | Campaign |
| 8 | Poseidon | `Systems.Poseidon.Poseidon` | Campaign |

- `MAX_SYSTEMS = 10` (array size, 9 entries used)
- Module load path format: `Systems.{MapName}.{MapName}`
- The `protocol_name` sent in the Settings packet (0x00) is this load path string

### 4.3 Notes

- Map display names (e.g., "Vesuvi System" for Multi1) are not in SpeciesToSystem.py. They are set in each map's own script. Extracting display names requires reading each `Systems/Multi*/Multi*.py` script.
- Spawn point positions must be extracted from each map script's `SetTranslate()` / `SetTranslateXYZ()` calls.
- Campaign maps (Albirea, Poseidon) are in the array but not normally selectable in multiplayer.

---

