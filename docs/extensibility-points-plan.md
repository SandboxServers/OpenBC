# Mod Compatibility Analysis: Kobayashi Maru & BC Remastered

## Context

Both Kobayashi Maru (KM, 2011) and Bridge Commander Remastered (BCRM/Orion, 2024) are total-conversion mods that massively expand Bridge Commander's ship roster, weapons, AI, and multiplayer missions. This analysis catalogs what they change, what APIs they use, and -- most importantly -- what **extensibility points** OpenBC must expose to support them.

**Design philosophy**: OpenBC does not blindly relay unknown messages. Every message type is an explicit extensibility point. Stock behaviors are defined through the **same extension mechanism** that mods use (eat our own dogfood). Scrapeable data gets tooling. Human-entered data gets **TOML**. Machine-generated data stays **JSON**.

---

## 1. What the Mods Bring

### Kobayashi Maru (KOBMARU-2011.10)
| Category | Scale | Details |
|----------|-------|---------|
| Ships | **187 definitions** | All factions: Fed, Klingon, Romulan, Cardassian, Dominion, Breen, Borg, Ferengi, Kessok |
| Framework | **Foundation** (Dasher42) | Registry, MutatorDef, OverrideDef, ShipDef hierarchy, Autoload system |
| Technologies | **FoundationTech + FTB** | Cloak timing, weapon yields, carrier ops, 31-file launcher subsystem |
| MVAM | 5 ship scripts | Ships that split into independent controllable sections mid-game |
| AI | 30+ compound strategies | Per-faction, difficulty-aware, cloak-aware combat AI |
| Music | Dynamic system | State-aware tracks (confident/neutral/panic) per faction |
| Effects | NanoFXv2 | Particle effects framework |
| Multiplayer | 29 episode missions | Wave Defense, Starbase Assault, custom scoring |
| Network | MultiplayerLib | Custom messages: AI assignment (213/214), ship swap (205), object deletion (211) |

### BC Remastered (BCRM)
| Category | Scale | Details |
|----------|-------|---------|
| Ships | **291 definitions** | TMP and TNG era variants, 148 models + 10 starbases |
| Assets | **2 GB** | NIF models, TGA textures/icons, WAV sounds |
| Weapons | 7 custom types | Borg drain, Tholian web, Chroniton, Hellbore, Plasma snare, Breen drain, Malon spatial |
| Species map | **148-entry enum** | `SpeciesToShip.py` -- far beyond stock 16 |
| Multiplayer | 29 episode missions | Wave Defense, Starbase Assault, team scoring |
| Config | OrionConfig.py | User-configurable attackers, defenders, starbase race per mode |
| Sounds | 45+ custom | Weapon fire, impact, explosion sounds via Foundation SoundDef |

---

## 2. The Foundation Framework

Both mods build on Dasher42's Foundation (`Foundation.py`, `Registry.py`). This is the universal mod architecture.

### Core Components
- **Registry** -- ordered name->object map (`_keyList` dict + `_arrayList` ordered list)
- **MutatorDef** -- composable game mode (ships + sounds + overrides + systems)
- **MutatorElementDef** -- base for things that belong in a mode
- **OverrideDef** -- runtime monkey-patching via `__import__` + dict swap
- **ShipDef** -- ship prototype (species, race, name, icon, bridge, AI module, warp stats)
- **SoundDef, SystemDef, BridgeDef, TGLDef, RaceDef** -- other registrable elements

### Ship Registration Pattern
```python
# Both mods use the same pattern:
Foundation.ShipDef.Accuser = Foundation.KlingonShipDef(
    'Accuser', App.SPECIES_GALAXY,
    {'name': 'Accuser DN', 'iconName': 'Accuser', 'shipFile': 'Accuser'}
)
Foundation.ShipDef.Accuser.RegisterQBShipMenu('Klingon Ships')
```

### Autoload Mechanism
`Foundation.LoadExtraPlugins('scripts\\Custom\\Autoload')` -- scans directory, `__import__`s every `.py`/`.pyc` alphabetically. `Foundation.LoadExtraShips(dir)` -- scans ship directory, calls `GetShipStats()` on each module.

---

## 3. Extensibility Points OpenBC Must Expose

The server will **not** blindly relay unknown opcodes. Instead, each behavior is an explicit extensibility point. Stock BC behaviors are defined through the same mechanism mods use.

### 3.1 Game Opcode Handlers (Extensibility Point: Opcode Registry)

**Current state**: `main.c` has a switch statement on `payload[0]`. Unknown opcodes are logged and **dropped**. This is correct behavior -- we don't relay what we don't understand.

**Extension model**: Define each opcode handler as a registered handler with:
- Parse function (extract structured data from wire bytes)
- Validate function (anti-cheat, authority checks)
- Action function (server-side state mutation)
- Relay policy (reliable/unreliable, to-all/to-others/to-host/drop)

Stock handlers (already implemented):
| Opcode | Name | Relay | Server Action |
|--------|------|-------|---------------|
| 0x03 | ObjectCreateTeam | reliable, to-others | Init ship state from registry |
| 0x0E/0x0F | Cloak start/stop | reliable, to-others | Track cloak state, validate |
| 0x10 | StartWarp | reliable, to-others | Check warp subsys alive |
| 0x14 | DestroyObject | DROP if authoritative | Server decides destruction |
| 0x19 | TorpedoFire | reliable, to-others | Cloak check, spawn tracker |
| 0x1A | BeamFire | reliable, to-others | Cloak check, range check |
| 0x1C | StateUpdate | unreliable, to-others | None (client-authoritative pos) |
| 0x29 | Explosion | unreliable, to-others | Log damage |
| 0x2C/0x2D | Chat/TeamChat | reliable, to-others | Log |
| 0x35 | MissionInit | reliable, to-all | Mission setup |
| 0x36 | ScoreChange | reliable, to-all | Track scores |
| 0x37 | Score | reliable, to-all | Score display |
| 0x38 | EndGame | reliable, to-all | Game end |
| 0x39 | Restart | reliable, to-all | Reset state |

**Mod extension**: Mods define additional script message types (carried inside the game data stream). KM defines types 205-214. BCRM defines MAX_MESSAGE_TYPES+10..+14. These are **script-layer messages** inside `TGMessage` payloads, not transport opcodes.

**What we need to know (dirty room question)**: How are script messages framed inside the game data stream? Are they carried as opaque payloads inside specific opcodes, or do they have their own framing? The mods use `App.TGMessage_Create()` + `TGBufferStream.WriteChar(chr(messageType))` -- how does this map to wire opcodes?

### 3.2 Ship Data Registry (Extensibility Point: Data Packs)

**Current state**: `data/vanilla-1.1.json` has 16 ships + 15 projectiles. `bc_registry_load()` reads this at startup. Species lookup by `u16 species_id`.

**Extension model**: Data packs loaded from a directory. Each pack is a JSON file (machine-generated from mod scripts by tooling). Server loads `data/vanilla-1.1.json` as the base, then overlays mod pack JSON on top.

**Tooling needed**: Extend `tools/scrape_bc.py` to scrape mod ship definitions:
- KM: Parse `scripts/Custom/Ships/*.py` for Foundation ShipDef registrations + `scripts/Ships/Hardpoints/*.py` for weapon/subsystem data
- BCRM: Parse `py/Orion/Ships/*.py` for `GetShipStats()` return dicts + `Scripts/Ships/Hardpoints/*.py`
- Output: `data/<modname>.json` in same schema as `vanilla-1.1.json`

**Why JSON not TOML**: Ship data is machine-generated from mod Python scripts. 187-291 ships with 20+ subsystems each -- nobody is editing this by hand. Scraper generates it.

### 3.3 Server Configuration (Extensibility Point: TOML Config)

**Current state**: Config is **hardcoded** in `main.c` globals:
```c
static bool g_collision_dmg = true;
static bool g_friendly_fire = false;
static const char *g_map_name = "Multiplayer.Episode.Mission1.Mission1";
static int g_time_limit = -1;
static int g_frag_limit = -1;
```

**Extension model**: `server.toml` for human-authored server configuration:
```toml
[server]
port = 22101
max_players = 6
name = "My OpenBC Server"

[game]
map = "Multiplayer.Episode.Mission1.Mission1"
system = 1
time_limit = -1      # -1 = no limit
frag_limit = -1      # -1 = no limit
collision_damage = true
friendly_fire = false
difficulty = 1       # 0=Easy, 1=Normal, 2=Hard

[data]
registry = "data/vanilla-1.1.json"
# mod_packs = ["data/kobayashi-maru.json", "data/bcrm-orion.json"]

[gamespy]
enabled = true
master = "master.gamespy.com"
lan_discovery = true
```

### 3.4 ObjectCreate Flexibility (Extensibility Point: Object Factory)

**Current state**: ObjectCreateTeam (0x03) parses species_id and looks up in registry. If found, inits full server-side ship state. If not found, logs warning and **still relays**.

**What needs to change**:
1. The "not found → relay anyway" fallback should become an explicit **relay-only mode** for unknown species, not a silent fallback
2. ObjectCreate should support mod-registered species through the data pack system
3. Runtime ship creation (MVAM splits, carrier launches) creates new object IDs -- server must track these
4. DestroyObject (0x14) authority model needs to be clear: server-authoritative when registry loaded, relay when not

### 3.5 Checksum Manifests (Extensibility Point: Mod-Aware Hashing)

**Current state**: `manifests/vanilla-1.1.json` contains file hashes for stock BC 1.1. Server validates client checksums against this manifest during handshake.

**Extension model**: Mod packs include their own manifest files. Server loads base manifest + mod manifests. Client checksum handshake validates against the combined set.

**Tooling needed**: `openbc-hash.exe` already generates manifests. Need a mode that generates a manifest for a mod directory tree, then a server config option to load multiple manifests.

### 3.6 Damage Values (Extensibility Point: Match Stock Behavior)

**Current state**: Damage is parsed as `f32` from explosion events. Our combat system applies damage to subsystems based on distance and shield facing.

**The question**: BCRM uses "magic" damage float values (15.0=Borg drain, 273.0=Hellbore, 2063.0=Plasma snare) as weapon type identifiers. The client-side `Effects.py` checks these values to apply visual effects.

**What we need**: Do whatever stock BC does with damage values. The server should relay damage faithfully (which it already does). But we need to verify:
- Does CF16 compression preserve these magic values? (CF16 is lossy)
- Are explosion damage values sent as raw f32 or CF16-compressed?

**Dirty room prompt if needed**:
> "In Bridge Commander's multiplayer wire protocol, when an Explosion event (opcode 0x29) is sent, how is the damage value encoded? Is it a raw IEEE 754 float32, or is it compressed using the CF16 compact float format? We need to know if damage values like 273.0 or 2063.0 survive encoding/decoding faithfully."

---

## 4. Tooling Plan

### 4.1 Mod Ship Scraper (extend `tools/scrape_bc.py`)

The existing scraper handles stock BC hardpoint files. Needs to also handle:

**KM pattern** (Foundation ShipDef in `Custom/Ships/*.py`):
```python
Foundation.ShipDef.Ambassador.sBridge = 'EnterpriseCbridge'
Foundation.ShipDef.Ambassador.fMaxWarp = 9.7 + 0.0001
Foundation.ShipDef.Ambassador.fCruiseWarp = 7.2 + 0.0001
```

**BCRM pattern** (`GetShipStats()` in ship modules):
```python
def GetShipStats():
    return {
        "FilenameHigh": "data/Models/Ships/Accuser/Accuser.nif",
        "Name": "Accuser",
        "HardpointFile": "Accuser",
        "Species": Multiplayer.SpeciesToShip.ACCUSER
    }
```

**BCRM species enum** (`SpeciesToShip.py`):
```python
SABRE = 1
MIRANDA = 2
...  # 148 entries
```

Output: JSON data pack in same schema as `vanilla-1.1.json`, with species IDs mapped from the mod's enum.

### 4.2 Mod Manifest Generator

`openbc-hash.exe --mod-dir mods/BCRM/ --output manifests/bcrm-orion.json`

Generates per-mod manifest that server loads alongside vanilla manifest.

### 4.3 TOML Config Loader

New module: `src/server/config.c` -- loads `server.toml`, populates server config struct. Replaces hardcoded globals in `main.c`.

Needs a lightweight TOML parser. Options:
- [toml-c](https://github.com/arp242/toml-c) -- small, single-file
- Hand-rolled minimal parser (our config is simple)
- Or: use our existing JSON parser for config too (less user-friendly but no new dependency)

---

## 5. Summary: What OpenBC Must Support

### Server-Side (Now)
1. **Opcode registry** -- explicit handler registration; stock uses same mechanism as mods
2. **Data pack system** -- overlay mod JSON on top of vanilla JSON
3. **TOML server config** -- replace hardcoded globals
4. **Mod-aware manifests** -- per-mod checksum files
5. **Flexible ObjectCreate** -- explicit relay-only mode for unknown species
6. **Faithful damage relay** -- verify CF16 doesn't mangle magic damage values

### Tooling (Now)
7. **Mod ship scraper** -- extend `scrape_bc.py` for KM and BCRM patterns
8. **Mod manifest generator** -- extend `openbc-hash.exe` for mod directories

### Client-Side (Future, Not This Phase)
9. Foundation plugin system (Python scripting)
10. OverrideDef monkey-patching
11. TGL localization
12. Visual effects (NanoFXv2, custom explosions)
13. MVAM (ship splitting)
14. Dynamic music
15. Custom UI
16. Carrier operations

---

## 6. Dirty Room Prompts

All four prompts will be saved to `docs/dirty-room-prompts.md` in the repo for sending to the reverse engineering side.

### Prompt 1: Script Message Wire Format
### Prompt 2: Explosion Damage Encoding
### Prompt 3: ObjectCreate for Unknown Species
### Prompt 4: Custom Message Routing in Stock Dedicated Server

---

## 7. Files Examined

### Foundation Framework
- `mods/KOBMARU-2011.10/scripts/Foundation.py` (800+ lines) -- MutatorDef, ShipDef, OverrideDef, Autoload
- `mods/KOBMARU-2011.10/scripts/Registry.py` (117 lines) -- ordered name->object map
- `mods/KOBMARU-2011.10/scripts/FoundationTech.py` -- technology/trigger system

### Multiplayer Code
- `mods/KOBMARU-2011.10/scripts/Custom/MultiplayerExtra/MultiplayerLib.py` -- custom network messages
- `mods/BCRM/py/Orion/Multiplayer/MissionShared.py` -- mission framework
- `mods/BCRM/py/Orion/Multiplayer/SpeciesToShip.py` -- 148-entry species enum

### OpenBC Server Code
- `include/openbc/opcodes.h` -- opcode definitions
- `src/server/main.c` -- game dispatch switch, relay logic, ObjectCreate handler
- `src/protocol/game_events.c` -- torpedo/beam/explosion/destroy parsers
- `src/protocol/game_builders.c` -- message construction
- `src/game/ship_data.c` -- JSON registry loader
- `include/openbc/ship_data.h` -- ship class struct
- `docs/phase1-api-surface.md` -- data registry spec (mentions TOML for human-authored)