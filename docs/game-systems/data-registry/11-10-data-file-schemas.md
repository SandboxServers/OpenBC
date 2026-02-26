# 10. Data File Schemas


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

