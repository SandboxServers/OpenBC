# OpenBC: Data Registry Specification

## Document Status
- **Created**: 2026-02-15
- **Revised**: 2026-02-15 (complete rewrite: verified data only, all hallucinations removed)
- **Replaces**: SWIG API Surface Catalog (2026-02-08)
- **Sources**: All data traced to readable Python scripts (hardpoint files, multiplayer scripts) unless otherwise noted
- **Context**: Phase 1 is a standalone C server with data-driven configuration. No Python scripting layer. All game data previously loaded via Python scripts is expressed as static data files (TOML for human-authored, JSON for tool-generated).

---

## 1. Species ID Registry

### 1.1 Source

Species numeric IDs are assigned via `SetSpecies()` calls in individual hardpoint scripts (`ships/Hardpoints/*.py`). The definitive mapping is below.

### 1.2 Complete Species ID Table

| ID | Ship/Object | Hardpoint File | Faction |
|----|-------------|----------------|---------|
| 1 | GenericTemplate | GenericTemplate.py | -- |
| 101 | Galaxy | galaxy.py | Federation |
| 102 | Sovereign | sovereign.py | Federation |
| 103 | Akira | akira.py | Federation |
| 104 | Ambassador | ambassador.py | Federation |
| 105 | Nebula | nebula.py | Federation |
| 105 | Peregrine (alias) | peregrine.py | Federation |
| 106 | Shuttle | shuttle.py | Federation |
| 107 | Transport | transport.py | Federation |
| 108 | Freighter | freighter.py | Federation |
| 201 | Galor | galor.py | Cardassian |
| 202 | Keldon | keldon.py | Cardassian |
| 203 | CardFreighter | cardfreighter.py | Cardassian |
| 204 | CardHybrid | cardhybrid.py | Cardassian |
| 301 | Warbird | warbird.py | Romulan |
| 401 | BirdOfPrey | birdofprey.py | Klingon |
| 402 | Vor'cha | vorcha.py | Klingon |
| 501 | KessokHeavy | kessokheavy.py | Kessok |
| 502 | KessokLight | kessoklight.py | Kessok |
| 503 | KessokMine | kessokmine.py | Kessok |
| 601 | Marauder | marauder.py | Ferengi |
| 701 | FedStarbase | fedstarbase.py | Federation |
| 702 | FedOutpost | fedoutpost.py | Federation |
| 703 | CardStarbase | cardstarbase.py | Cardassian |
| 704 | CardOutpost, CardFacility | cardoutpost.py, cardfacility.py | Cardassian |
| 705 | CardStation | cardstation.py | Cardassian |
| 706 | DryDock | drydock.py | Federation |
| 707 | SpaceFacility | spacefacility.py | -- |
| 708 | CommArray, CommLight | commarray.py, commlight.py | -- |
| 710 | Probe | probe.py | -- |
| 711 | Probe2 | probe2.py | -- |
| 712 | Asteroid (all types) | asteroid*.py, amagon.py | -- |
| 713 | SunBuster | sunbuster.py | -- |
| 714 | EscapePod | escapepod.py | -- |

### 1.3 Numbering Scheme

- **1xx** = Federation ships
- **2xx** = Cardassian ships
- **3xx** = Romulan ships
- **4xx** = Klingon ships
- **5xx** = Kessok ships/objects
- **6xx** = Ferengi ships
- **7xx** = Stations and fixed objects
- **710+** = Probes, asteroids, misc objects

### 1.4 Shared IDs

Several objects share species IDs:
- **105**: Nebula and Peregrine (Peregrine is an alias for Nebula)
- **704**: CardOutpost and CardFacility
- **708**: CommArray and CommLight
- **712**: All asteroid variants (Asteroid, AsteroidField, Amagon, etc.)

### 1.5 GlobalPropertyTemplates Discrepancy

`GlobalPropertyTemplates.py` assigns species IDs that sometimes differ from the hardpoint `SetSpecies()` calls:
- CardStarbase: template has species 702, hardpoint has 703
- CardOutpost: template has species 702, hardpoint has 704

The hardpoint `SetSpecies()` values are authoritative -- they are what the game engine uses at runtime.

---

## 2. Ship Manifest (kSpeciesTuple)

### 2.1 Source

`Multiplayer/SpeciesToShip.py` defines `kSpeciesTuple` -- the master table mapping ship script names to species constants, factions, and modifier classes. This is the definitive list of all objects in the multiplayer system.

### 2.2 Key Constants

- `MAX_SHIPS = 46` (total entries in kSpeciesTuple)
- `MAX_FLYABLE_SHIPS = 16` (first 16 entries are player-selectable ships)

### 2.3 Structure

Each kSpeciesTuple entry is a 4-element tuple:
```
(ScriptName, App.SPECIES_*, Faction, ModifierClass)
```

Exception: CardFreighter has a 5-element tuple (extra display name field).

### 2.4 Critical Finding: All Modifier Classes Are 1

**Every single entry in vanilla kSpeciesTuple has `modifier_class = 1`.** The modifier table has a 3.0 multiplier at position `[2][1]` (class 2 attacker vs class 1 target), but since no vanilla ship is class 2, this multiplier **never triggers** in stock multiplayer. It exists only for potential mod use.

### 2.5 Ship Aliases

kSpeciesTuple contains aliased entries where a script name maps to a different ship's species:
- `Enterprise` -> `App.SPECIES_SOVEREIGN` (alias for Sovereign)
- `Geronimo` -> `App.SPECIES_AKIRA` (alias for Akira)
- `Peregrine` -> `App.SPECIES_NEBULA` (alias for Nebula, species 105)
- `BiranuStation` -> `App.SPECIES_SPACE_FACILITY` (alias for SpaceFacility)
- All asteroid variants -> `App.SPECIES_ASTEROID` (species 712)

### 2.6 Flyable Ships (Indices 0-15)

The first 16 entries in kSpeciesTuple are the ships available for player selection in multiplayer. The exact order determines the ship selection index sent over the wire.

---

## 3. Modifier Table

### 3.1 Source

`Multiplayer/Modifier.py` defines `g_kModifierTable` and `GetModifier()`.

### 3.2 Table

```python
g_kModifierTable = (
    (1.0, 1.0, 1.0),   # Class 0 attacking class 0, 1, 2
    (1.0, 1.0, 1.0),   # Class 1 attacking class 0, 1, 2
    (1.0, 3.0, 1.0),   # Class 2 attacking class 0, 1, 2
)
```

### 3.3 Lookup Function

```python
def GetModifier(attackerClass, killedClass):
    return g_kModifierTable[attackerClass][killedClass]
```

Direct table lookup with **no bounds checking**. Out-of-range indices will crash.

### 3.4 Effective Behavior

Since all vanilla ships are modifier class 1 (Section 2.4), the effective modifier for any vanilla kill is always `g_kModifierTable[1][1] = 1.0`. The 3.0 penalty at `[2][1]` was designed to penalize heavy ships (class 2) killing light ships (class 1), but no vanilla ships are assigned to either class 0 or class 2.

---

## 4. Map Registry

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

## 5. Checksum Manifest

### 5.1 Source

Verified from wire format specification and protocol traces.

### 5.2 Checksum Directory Rounds

The server sends 4 sequential checksum requests (opcode 0x20). Each round validates a specific directory scope:

| Round | Directory | Filter | Recursive | Notes |
|-------|-----------|--------|-----------|-------|
| 0 | `scripts/` | `App.pyc` | No | Single file |
| 1 | `scripts/` | `Autoexec.pyc` | No | Single file |
| 2 | `scripts/ships/` | `*.pyc` | Yes | All ship + hardpoint scripts |
| 3 | `scripts/mainmenu/` | `*.pyc` | No | Main menu scripts only |

**`scripts/Custom/` is EXEMPT from all checksum validation.** This is where mods install (DedicatedServer.py, Foundation Technologies, NanoFX, etc.).

### 5.3 Hash Algorithms

**StringHash**: 4-lane Pearson hash using four 256-byte substitution tables (1,024 bytes total, extracted via hash manifest tool).

```c
uint32_t StringHash(const char *str) {
    uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0;
    while (*str) {
        uint8_t c = (uint8_t)*str++;
        h0 = TABLE_0[c ^ h0];
        h1 = TABLE_1[c ^ h1];
        h2 = TABLE_2[c ^ h2];
        h3 = TABLE_3[c ^ h3];
    }
    return (h0 << 24) | (h1 << 16) | (h2 << 8) | h3;
}
```

Used for: directory name hashing, filename hashing, version string hashing.

**FileHash**: Rotate-XOR over file contents.

```c
uint32_t FileHash(const uint8_t *data, size_t len) {
    uint32_t hash = 0;
    const uint32_t *dwords = (const uint32_t *)data;
    size_t count = len / 4;
    for (size_t i = 0; i < count; i++) {
        if (i == 1) continue;  // Skip DWORD index 1 (bytes 4-7 = .pyc timestamp)
        hash ^= dwords[i];
        hash = (hash << 1) | (hash >> 31);  // ROL 1
    }
    // Remaining bytes (len % 4): MOVSX sign-extension before XOR
    size_t remainder = len % 4;
    if (remainder > 0) {
        const uint8_t *tail = data + (count * 4);
        for (size_t i = 0; i < remainder; i++) {
            int32_t extended = (int8_t)tail[i];  // MOVSX
            hash ^= (uint32_t)extended;
            hash = (hash << 1) | (hash >> 31);
        }
    }
    return hash;
}
```

Deliberately skips bytes 4-7 (.pyc modification timestamp) so that the same bytecode produces the same hash regardless of compile time.

### 5.4 Version String Gate

- Version string: `"60"`
- Version hash: `StringHash("60") = 0x7E0CE243`
- Checked in the first checksum round (index 0). Version mismatch causes immediate rejection via opcode 0x23.

### 5.5 Hash Manifest JSON Schema

```json
{
  "meta": {
    "name": "Star Trek: Bridge Commander 1.1",
    "description": "Vanilla BC 1.1 (GOG release)",
    "generated": "2026-02-15T00:00:00Z",
    "generator_version": "1.0.0",
    "game_version": "60"
  },
  "version_string": "60",
  "version_string_hash": "0x7E0CE243",
  "directories": [
    {
      "index": 0,
      "path": "scripts/",
      "filter": "App.pyc",
      "recursive": false,
      "dir_name_hash": "0x...",
      "files": [
        {
          "filename": "App.pyc",
          "name_hash": "0x...",
          "content_hash": "0x..."
        }
      ],
      "subdirs": []
    },
    {
      "index": 1,
      "path": "scripts/",
      "filter": "Autoexec.pyc",
      "recursive": false,
      "dir_name_hash": "0x...",
      "files": [
        {
          "filename": "Autoexec.pyc",
          "name_hash": "0x...",
          "content_hash": "0x..."
        }
      ],
      "subdirs": []
    },
    {
      "index": 2,
      "path": "scripts/ships/",
      "filter": "*.pyc",
      "recursive": true,
      "dir_name_hash": "0x...",
      "files": [],
      "subdirs": [
        {
          "name": "Hardpoints",
          "name_hash": "0x...",
          "files": [
            {
              "filename": "sovereign.pyc",
              "name_hash": "0x...",
              "content_hash": "0x..."
            }
          ]
        }
      ]
    },
    {
      "index": 3,
      "path": "scripts/mainmenu/",
      "filter": "*.pyc",
      "recursive": false,
      "dir_name_hash": "0x...",
      "files": [
        {
          "filename": "mainmenu.pyc",
          "name_hash": "0x...",
          "content_hash": "0x..."
        }
      ],
      "subdirs": []
    }
  ]
}
```

### 5.6 Validation Flow

1. Client connects. Server sends 4x opcode 0x20 (checksum request), one per directory.
2. Client hashes its local files, sends 4x opcode 0x21 (checksum response trees).
3. Server walks each response tree against the active manifest(s):
   - Version string hash checked first (index 0 only) -- reject on mismatch (opcode 0x23)
   - Directory name hashes compared
   - File name hashes compared (sorted order)
   - File content hashes compared
4. All match any active manifest --> fire ChecksumComplete, send Settings (0x00) + GameInit (0x01)
5. Mismatch --> send opcode 0x22 (file mismatch) or 0x23 (version mismatch) with failing filename

---

## 6. Settings Packet (0x00)

> Wire format moved to [../protocol/protocol-reference.md](../protocol/protocol-reference.md) (Section 6.1).

---

## 7. ObjCreateTeam (0x03) Format

> Wire format moved to [../wire-formats/objcreate-wire-format.md](../wire-formats/objcreate-wire-format.md).

---

## 8. StateUpdate (0x1C) Format

> Wire format moved to [../wire-formats/stateupdate-wire-format.md](../wire-formats/stateupdate-wire-format.md). Subsystem details in [ship-subsystems.md](ship-subsystems.md).

---

## 9. Ship Physics Data

### 9.1 Source

Impulse engine parameters extracted from readable hardpoint scripts (`ships/Hardpoints/*.py`). Mass and inertia from `GlobalPropertyTemplates.py`.

### 9.2 Impulse Engine Parameters (27 ships verified)

| Ship | MaxAccel | MaxAngularAccel | MaxAngularVelocity | MaxSpeed |
|------|----------|-----------------|--------------------|---------:|
| Sovereign | 1.60 | 0.150 | 0.300 | 7.50 |
| Vor'cha | 1.30 | 0.110 | 0.220 | 7.60 |
| Akira | 3.00 | 0.150 | 0.400 | 6.60 |
| Galaxy | 1.50 | 0.120 | 0.280 | 6.30 |
| Bird of Prey | 2.50 | 0.350 | 0.500 | 6.20 |
| Nebula | 1.40 | 0.150 | 0.250 | 6.00 |
| Peregrine | 1.40 | 0.150 | 0.300 | 6.00 |
| Keldon | 1.50 | 0.150 | 0.300 | 5.70 |
| Marauder | 1.60 | 0.190 | 0.360 | 5.50 |
| Ambassador | 1.00 | 0.110 | 0.260 | 5.50 |
| Galor | 1.50 | 0.150 | 0.300 | 5.40 |
| Card Hybrid | 1.40 | 0.160 | 0.280 | 5.40 |
| Warbird | 1.80 | 0.070 | 0.200 | 4.50 |
| E2M0 Warbird | 1.00 | 0.070 | 0.200 | 4.50 |
| Transport | 0.50 | 0.050 | 0.120 | 4.00 |
| Shuttle | 2.50 | 0.600 | 0.800 | 4.00 |
| Comm Array | 1.00 | 0.100 | 0.250 | 4.00 |
| Comm Light | 2.00 | 0.100 | 0.250 | 4.00 |
| Kessok Light | 0.80 | 0.150 | 0.280 | 3.80 |
| Kessok Heavy | 2.50 | 0.110 | 0.220 | 3.70 |
| Freighter | 0.40 | 0.010 | 0.050 | 3.00 |
| Card Freighter | 0.80 | 0.080 | 0.150 | 3.00 |
| Sunbuster | 0.20 | 0.010 | 0.150 | 3.00 |
| Escape Pod | 0.50 | 0.300 | 0.700 | 2.00 |
| Probe | 3.00 | 0.100 | 0.300 | 8.00 |
| Probe 2 | 3.00 | 0.100 | 0.300 | 8.00 |
| Kessok Mine | 0.05 | 0.400 | 0.500 | 0.10 |

GenericTemplate (MaxAccel=20.0, MaxAngularAccel=0.1, MaxAngularVelocity=0.25, MaxSpeed=20.0) is a debug/placeholder entry not used in normal gameplay.

### 9.3 Mass and Inertia (12 ships from GlobalPropertyTemplates.py)

| Ship | Mass | Rotational Inertia | Genus | Species (template) |
|------|------|--------------------|-------|-------------------|
| Ambassador | 100.0 | 100.0 | 1 (Ship) | 104 |
| Bird of Prey | 75.0 | 100.0 | 1 (Ship) | 401 |
| Marauder | 100.0 | 100.0 | 1 (Ship) | 601 |
| Nebula | 100.0 | 100.0 | 1 (Ship) | 105 |
| Warbird | 150.0 | 100.0 | 1 (Ship) | 301 |
| Shuttle | 10.0 | 10.0 | 1 (Ship) | 106 |
| Transport | 100.0 | 100.0 | 1 (Ship) | 107 |
| Kessok Light | 100.0 | 100.0 | 1 (Ship) | 502 |
| Vor'cha | 150.0 | 100.0 | 1 (Ship) | 402 |
| Fed Starbase | 1,000,000.0 | 1,000,000.0 | 2 (Station) | 701 |
| Card Starbase | 1,000,000.0 | 1,000,000.0 | 2 (Station) | 702 |
| Card Outpost | 500.0 | 100.0 | 2 (Station) | 702 |

**Ships NOT in GlobalPropertyTemplates**: Galaxy, Sovereign, Akira, Keldon, Galor, CardHybrid, KessokHeavy, Freighter, CardFreighter, and all small objects (probes, asteroids, etc.). These likely use engine default values. Exact defaults need extraction from `Appc.pyd` or further RE work.

---

## 10. Data File Schemas

### 10.1 Overview

The OpenBC server loads all game data from TOML files at startup. These schemas describe the format for each file.

| File | Format | Purpose | Replaces |
|------|--------|---------|----------|
| `data/species.toml` | TOML | Species ID constants and class mappings | `kSpeciesTuple` in SpeciesToShip.py, `SetSpecies()` calls |
| `data/modifiers.toml` | TOML | Damage/score modifier table | `Multiplayer/Modifier.py` |
| `data/ships.toml` | TOML | Ship class definitions (stats, subsystems) | Hardpoint scripts in `ships/Hardpoints/*.py` |
| `data/maps.toml` | TOML | Multiplayer map definitions | `Systems/Multi*/*.py` scripts |
| `data/rules.toml` | TOML | Game mode configuration | Constants in `Multiplayer/Episode/Mission*.py` |
| `manifests/*.json` | JSON | Precomputed checksum hashes | Runtime hash computation |
| `server.toml` | TOML | Server configuration | N/A (new) |

### 10.2 Species Registry (`data/species.toml`)

```toml
# data/species.toml -- Species ID registry
# Each [species.<key>] maps a species name to its numeric ID.
# The numeric ID must match the original App.SPECIES_* constant exactly
# (transmitted over the network via SetNetType/GetNetType).

[species.generic_template]
id = 1
modifier_class = 1
display_name = "Generic Template"

# --- Federation Ships (1xx) ---
[species.galaxy]
id = 101
modifier_class = 1
display_name = "Galaxy Class"

[species.sovereign]
id = 102
modifier_class = 1
display_name = "Sovereign Class"

[species.akira]
id = 103
modifier_class = 1
display_name = "Akira Class"

[species.ambassador]
id = 104
modifier_class = 1
display_name = "Ambassador Class"

[species.nebula]
id = 105
modifier_class = 1
display_name = "Nebula Class"

[species.shuttle]
id = 106
modifier_class = 1
display_name = "Shuttle"

[species.transport]
id = 107
modifier_class = 1
display_name = "Transport"

[species.freighter]
id = 108
modifier_class = 1
display_name = "Freighter"

# --- Cardassian Ships (2xx) ---
[species.galor]
id = 201
modifier_class = 1
display_name = "Galor Class"

[species.keldon]
id = 202
modifier_class = 1
display_name = "Keldon Class"

[species.cardfreighter]
id = 203
modifier_class = 1
display_name = "Cardassian Freighter"

[species.cardhybrid]
id = 204
modifier_class = 1
display_name = "Cardassian Hybrid"

# --- Romulan Ships (3xx) ---
[species.warbird]
id = 301
modifier_class = 1
display_name = "D'deridex Warbird"

# --- Klingon Ships (4xx) ---
[species.birdofprey]
id = 401
modifier_class = 1
display_name = "Bird of Prey"

[species.vorcha]
id = 402
modifier_class = 1
display_name = "Vor'cha Class"

# --- Kessok (5xx) ---
[species.kessokheavy]
id = 501
modifier_class = 1
display_name = "Kessok Heavy Cruiser"

[species.kessoklight]
id = 502
modifier_class = 1
display_name = "Kessok Light Cruiser"

[species.kessokmine]
id = 503
modifier_class = 1
display_name = "Kessok Mine"

# --- Ferengi (6xx) ---
[species.marauder]
id = 601
modifier_class = 1
display_name = "Marauder"

# --- Stations (7xx) ---
[species.fedstarbase]
id = 701
modifier_class = 1
display_name = "Federation Starbase"

[species.fedoutpost]
id = 702
modifier_class = 1
display_name = "Federation Outpost"

[species.cardstarbase]
id = 703
modifier_class = 1
display_name = "Cardassian Starbase"

[species.cardoutpost]
id = 704
modifier_class = 1
display_name = "Cardassian Outpost"

[species.cardstation]
id = 705
modifier_class = 1
display_name = "Cardassian Station"

[species.drydock]
id = 706
modifier_class = 1
display_name = "Drydock"

[species.spacefacility]
id = 707
modifier_class = 1
display_name = "Space Facility"

# --- Objects (708+) ---
[species.commarray]
id = 708
modifier_class = 1
display_name = "Comm Array"

[species.probe]
id = 710
modifier_class = 1
display_name = "Probe"

[species.probe2]
id = 711
modifier_class = 1
display_name = "Probe Type 2"

[species.asteroid]
id = 712
modifier_class = 1
display_name = "Asteroid"

[species.sunbuster]
id = 713
modifier_class = 1
display_name = "Sunbuster"

[species.escapepod]
id = 714
modifier_class = 1
display_name = "Escape Pod"
```

### 10.3 Modifier Table (`data/modifiers.toml`)

```toml
# data/modifiers.toml -- Score modifier table from Modifier.py
# table[attacker_class][killed_class]
# All vanilla ships are class 1, so effective modifier is always 1.0.

[modifiers]
classes = ["class_0", "class_1", "class_2"]

table = [
    [1.0, 1.0, 1.0],   # Class 0 attacking class 0, 1, 2
    [1.0, 1.0, 1.0],   # Class 1 attacking class 0, 1, 2
    [1.0, 3.0, 1.0],   # Class 2 attacking class 0, 1, 2
]
```

### 10.4 Map Registry (`data/maps.toml`)

```toml
# data/maps.toml -- Multiplayer map definitions
# protocol_name is the string sent in the Settings packet (0x00).
# It must exactly match what the client expects.

[maps.multi1]
protocol_name = "Systems.Multi1.Multi1"
# display_name and spawn_points: extract from Systems/Multi1/Multi1.py

[maps.multi2]
protocol_name = "Systems.Multi2.Multi2"

[maps.multi3]
protocol_name = "Systems.Multi3.Multi3"

[maps.multi4]
protocol_name = "Systems.Multi4.Multi4"

[maps.multi5]
protocol_name = "Systems.Multi5.Multi5"

[maps.multi6]
protocol_name = "Systems.Multi6.Multi6"

[maps.multi7]
protocol_name = "Systems.Multi7.Multi7"
```

Display names and spawn point coordinates are not included here because they have not been extracted from the individual map scripts yet. They must be extracted from each `Systems/Multi*/Multi*.py` file's `SetTranslate()` calls.

---

## 11. Server Configuration (`server.toml`)

```toml
[server]
name = "OpenBC Deathmatch"
port = 22101                       # Default BC port (0x5655)
max_players = 8                    # 1-16
tick_rate = 30                     # Hz

[server.game]
map = "multi1"                     # Key from maps.toml
game_mode = "deathmatch"           # Key from rules.toml

[server.network]
lan_discovery = true

[server.network.master_server]
enabled = true
address = "master.333networks.com"
port = 28900
heartbeat_interval = 60

[checksum]
manifests = ["manifests/vanilla-1.1.json"]

[mods]
active = []
```

---

## 12. Mod Pack Structure

### 12.1 Directory Layout

```
mods/
  my-mod/
    mod.toml               # Mod metadata (required)
    ships.toml              # Additional/modified ship definitions (optional)
    maps.toml               # Additional maps (optional)
    rules.toml              # Custom game rules (optional)
    species.toml            # Additional species definitions (optional)
    modifiers.toml          # Replacement modifier table (optional)
```

### 12.2 Merge Semantics

| Data Type | Merge Behavior |
|-----------|---------------|
| `ships.toml` | Additive with override. New keys added, existing keys fully replaced. |
| `maps.toml` | Additive with override. |
| `rules.toml` | Additive with override. |
| `species.toml` | Additive. Duplicate species IDs are an error. |
| `modifiers.toml` | Full replacement. Last-loaded table wins entirely. |

### 12.3 Client-Side Behavior

- Mods affecting only server-side data (stats, rules, modifiers) require **no** client changes.
- Mods adding new ships require matching client scripts/models. The mod's hash manifest validates this.
- Mods in `scripts/Custom/` bypass checksumming entirely.

---

## 13. Confidence Annotations

Every data point in this document is sourced from readable Python scripts and protocol captures. Confidence levels:

### Verified (high confidence)

| Data | Source File |
|------|-------------|
| Species IDs (Section 1) | `ships/Hardpoints/*.py` SetSpecies() calls |
| kSpeciesTuple structure (Section 2) | `Multiplayer/SpeciesToShip.py` |
| All modifier classes = 1 (Section 2.4) | `Multiplayer/SpeciesToShip.py` |
| Modifier table (Section 3) | `Multiplayer/Modifier.py` |
| Map list (Section 4) | `Multiplayer/SpeciesToSystem.py` |
| Checksum directories (Section 5.2) | `wire-format-spec.md` + protocol traces |
| StringHash algorithm (Section 5.3) | Algorithm verified via hash manifest tool output against known inputs |
| FileHash algorithm (Section 5.3) | Algorithm verified via hash manifest tool output against known inputs |
| Version string + hash (Section 5.4) | Extracted via hash manifest tool + verified in protocol |
| Impulse engine parameters (Section 9.2) | `ships/Hardpoints/*.py` ImpulseEngineProperty calls |
| Mass/inertia for 12 ships (Section 9.3) | `GlobalPropertyTemplates.py` |
| Settings packet toggles (Section 6.2) | Behavioral analysis of Settings packet in protocol captures |

### Needs Further Extraction

| Data | What's Missing | Where to Get It |
|------|---------------|-----------------|
| Map display names | Not in SpeciesToSystem.py | Each `Systems/Multi*/Multi*.py` script |
| Map spawn points | Coordinate data | Each map script's SetTranslate() calls |
| Ship mass for ~20 ships | Not in GlobalPropertyTemplates | Engine defaults in Appc.pyd or hardpoints |
| Ship subsystem counts | Per-ship subsystem list | Each hardpoint script's subsystem creation calls |
| Settings packet full layout | Complete byte-level format | Decompiled MultiplayerWindow handler |
| ObjCreateTeam full layout | Packet byte map | `objcreate-team-format.md` (partially documented) |
| Hull HP for most ships | Only Galaxy (15000) and BoP (4000) extracted | Each hardpoint script's HullProperty calls |
| Weapon parameters | Only partial (Sovereign phasers, Galaxy torpedoes, BoP disruptors) | Each hardpoint script's weapon property calls |
| Game mode constants | Frag limits, time limits from Mission*.py | `Multiplayer/Episode/Mission*/Mission*.py` |
