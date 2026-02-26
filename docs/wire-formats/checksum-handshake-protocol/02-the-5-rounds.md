# The 5 Rounds


| Round Index | Directory | Filter | Recursive | Purpose |
|-------------|-----------|--------|-----------|---------|
| `0x00` | `scripts/` | `App.pyc` | No | Core application module |
| `0x01` | `scripts/` | `Autoexec.pyc` | No | Startup script |
| `0x02` | `scripts/ships` | `*.pyc` | **Yes** | All ship definition modules |
| `0x03` | `scripts/mainmenu` | `*.pyc` | No | Menu system modules |
| `0xFF` | `Scripts/Multiplayer` | `*.pyc` | **Yes** | Multiplayer mission scripts |

Notes:
- Round 0xFF is a special "final" round with index 255, not sequential with 0-3
- Directory strings do NOT have trailing path separators (no trailing `/` or `\`)
- Round 0xFF uses capital-S `Scripts/Multiplayer` (observed on wire)
- The recursive flag means "scan subdirectories" for wildcard filters

---

