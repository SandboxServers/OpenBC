# ObjCreate / ObjCreateTeam Wire Format (Opcodes 0x02 / 0x03)

Wire format specification for Star Trek: Bridge Commander's object creation messages, documented from network packet captures and the game's shipped Python scripting API.

## Overview

Opcodes 0x02 (ObjCreate) and 0x03 (ObjCreateTeam) are sent by the host to create game objects — ships, torpedoes, stations, and asteroids — on all connected clients. The only difference is that 0x03 includes a team assignment byte.

These messages are relayed: when the host creates an object, it sends the message to every other connected peer.

## Message Envelope

The game message payload (after transport-layer framing) begins with:

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode              0x02 or 0x03
1       1     i8      owner_player_slot   Player slot index (0-15)
```

If the opcode is 0x03, an additional byte follows:

```
2       1     i8      team_id             Team assignment (e.g., 2 or 3)
```

**Envelope size**: 2 bytes for opcode 0x02, 3 bytes for opcode 0x03.

The remainder of the message is a serialized object stream.

## Serialized Object Stream

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

## Ship Object Body (class_id = 0x8008)

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

## Torpedo Object Body (class_id = 0x8009)

Torpedoes use the same `species_type` byte at the start of their body, but the value indexes into the `SpeciesToTorp` table instead. Torpedo serialization does not include spatial tracking data — torpedo position and movement are handled by dedicated fire messages (opcodes 0x19 TorpedoFire, 0x1A BeamFire).

## Species Mapping Tables

These tables are from the game's shipped Python scripts (`scripts/Multiplayer/`), which form the public scripting/modding API.

### SpeciesToShip — Playable Ships (species 1-15)

Source: `Multiplayer/SpeciesToShip.py`

| ID | Constant | Ship Script | Faction |
|----|----------|-------------|---------|
| 0 | UNKNOWN | — | Neutral |
| 1 | AKIRA | Akira | Federation |
| 2 | AMBASSADOR | Ambassador | Federation |
| 3 | GALAXY | Galaxy | Federation |
| 4 | NEBULA | Nebula | Federation |
| 5 | SOVEREIGN | Sovereign | Federation |
| 6 | BIRDOFPREY | BirdOfPrey | Klingon |
| 7 | VORCHA | Vorcha | Klingon |
| 8 | WARBIRD | Warbird | Romulan |
| 9 | MARAUDER | Marauder | Ferengi |
| 10 | GALOR | Galor | Cardassian |
| 11 | KELDON | Keldon | Cardassian |
| 12 | CARDHYBRID | CardHybrid | Cardassian |
| 13 | KESSOKHEAVY | KessokHeavy | Kessok |
| 14 | KESSOKLIGHT | KessokLight | Kessok |
| 15 | SHUTTLE | Shuttle | Federation |

MAX_FLYABLE_SHIPS = 16 (IDs 0-15; only 1-15 are valid playable ships).

### SpeciesToShip — Non-Playable Objects (species 16-45)

| ID | Constant | Ship Script | Faction |
|----|----------|-------------|---------|
| 16 | CARDFREIGHTER | CardFreighter | Cardassian |
| 17 | FREIGHTER | Freighter | Federation |
| 18 | TRANSPORT | Transport | Federation |
| 19 | SPACEFACILITY | SpaceFacility | Federation |
| 20 | COMMARRAY | CommArray | Federation |
| 21 | COMMLIGHT | CommLight | Cardassian |
| 22 | DRYDOCK | DryDock | Federation |
| 23 | PROBE | Probe | Federation |
| 24 | DECOY | Decoy | Federation |
| 25 | SUNBUSTER | Sunbuster | Kessok |
| 26 | CARDOUTPOST | CardOutpost | Cardassian |
| 27 | CARDSTARBASE | CardStarbase | Cardassian |
| 28 | CARDSTATION | CardStation | Cardassian |
| 29 | FEDOUTPOST | FedOutpost | Federation |
| 30 | FEDSTARBASE | FedStarbase | Federation |
| 31 | ASTEROID | Asteroid | Neutral |
| 32 | ASTEROID1 | Asteroid1 | Neutral |
| 33 | ASTEROID2 | Asteroid2 | Neutral |
| 34 | ASTEROID3 | Asteroid3 | Neutral |
| 35 | AMAGON | Amagon | Cardassian |
| 36 | BIRANUSTATION | BiranuStation | Neutral |
| 37 | ENTERPRISE | Enterprise | Federation |
| 38 | GERONIMO | Geronimo | Federation |
| 39 | PEREGRINE | Peregrine | Federation |
| 40 | ASTEROIDH1 | Asteroidh1 | Neutral |
| 41 | ASTEROIDH2 | Asteroidh2 | Neutral |
| 42 | ASTEROIDH3 | Asteroidh3 | Neutral |
| 43 | ESCAPEPOD | Escapepod | Neutral |
| 44 | KESSOKMINE | KessokMine | Kessok |
| 45 | BORGCUBE | BorgCube | Borg |

MAX_SHIPS = 46 (IDs 0-45).

### SpeciesToTorp — Torpedo Types

Source: `Multiplayer/SpeciesToTorp.py`

| ID | Constant | Torpedo Script |
|----|----------|----------------|
| 0 | UNKNOWN | — |
| 1 | DISRUPTOR | Disruptor |
| 2 | PHOTON | PhotonTorpedo |
| 3 | QUANTUM | QuantumTorpedo |
| 4 | ANTIMATTER | AntimatterTorpedo |
| 5 | CARDTORP | CardassianTorpedo |
| 6 | KLINGONTORP | KlingonTorpedo |
| 7 | POSITRON | PositronTorpedo |
| 8 | PULSEDISRUPT | PulseDisruptor |
| 9 | FUSIONBOLT | FusionBolt |
| 10 | CARDASSIANDISRUPTOR | CardassianDisruptor |
| 11 | KESSOKDISRUPTOR | KessokDisruptor |
| 12 | PHASEDPLASMA | PhasedPlasma |
| 13 | POSITRON2 | Positron2 |
| 14 | PHOTON2 | PhotonTorpedo2 |
| 15 | ROMULANCANNON | RomulanCannon |

MAX_TORPS = 16 (IDs 0-15; only 1-15 are valid).

### SpeciesToSystem — Star Systems (Map Names)

Source: `Multiplayer/SpeciesToSystem.py`

| ID | Constant | System Name |
|----|----------|-------------|
| 0 | UNKNOWN | — |
| 1 | MULTI1 | Multi1 |
| 2 | MULTI2 | Multi2 |
| 3 | MULTI3 | Multi3 |
| 4 | MULTI4 | Multi4 |
| 5 | MULTI5 | Multi5 |
| 6 | MULTI6 | Multi6 |
| 7 | MULTI7 | Multi7 |
| 8 | ALBIREA | Albirea |
| 9 | POSEIDON | Poseidon |

MAX_SYSTEMS = 10 (IDs 0-9; only 1-9 are valid).

## Receiver Behavior

When a peer receives an ObjCreate or ObjCreateTeam message, it:

1. Reads the envelope (opcode, owner slot, and team if opcode 0x03)
2. Temporarily sets the active player context to the owner's slot (so object IDs are allocated from the correct range)
3. Reads the object header (class_id, object_id)
4. Checks for duplicate object_id — if the object already exists, processing stops
5. Creates a new object instance based on class_id
6. Deserializes the object body (species, position, orientation, etc.)
7. For ships (class_id 0x8008): the species_type is used to load the correct ship model, hardpoints, and subsystem configuration via the `SpeciesToShip` scripting API
8. For torpedoes (class_id 0x8009): the species_type loads the torpedo definition via `SpeciesToTorp`
9. If team (opcode 0x03): assigns the team_id to the object
10. If the host is processing: relays the message to all other connected peers (excluding the sender)
11. For ships only: attaches a network position/velocity tracker for state synchronization

## Host Relay Behavior

When the host processes an ObjCreate/ObjCreateTeam from a client, it relays the original message (unmodified) to every other connected peer. This ensures all clients receive object creations regardless of which player originated them.

The host iterates over all 16 possible player slots and sends to each connected peer that is neither the original sender nor the host itself.

## Decoded Packet Examples

### Example 1: Akira at position (88, -66, -73)

Full game message bytes (after transport framing):

```
03 00 02 08 80 00 00 FF FF FF 3F 01 00 00 B0 42 00 00 84 C2 00 00 92 C2 ...
```

Field decode:

| Bytes | Field | Value |
|-------|-------|-------|
| `03` | opcode | 0x03 (ObjCreateTeam) |
| `00` | owner_player_slot | 0 (host) |
| `02` | team_id | 2 |
| `08 80 00 00` | class_id | 0x00008008 (Ship) |
| `FF FF FF 3F` | object_id | 0x3FFFFFFF (player 0 base) |
| `01` | species_type | 1 (Akira) |
| `00 00 B0 42` | position_x | 88.0 |
| `00 00 84 C2` | position_y | -66.0 |
| `00 00 92 C2` | position_z | -73.0 |
| ... | orientation, speed, name, set, subsystems | (continues) |

### Example 2: Sovereign at position (38, -49, -35)

```
03 00 02 08 80 00 00 FF FF FF 3F 05 00 00 18 42 00 00 44 C2 00 00 0C C2 ...
```

| Bytes | Field | Value |
|-------|-------|-------|
| `03` | opcode | 0x03 (ObjCreateTeam) |
| `00` | owner_player_slot | 0 (host) |
| `02` | team_id | 2 |
| `08 80 00 00` | class_id | 0x00008008 (Ship) |
| `FF FF FF 3F` | object_id | 0x3FFFFFFF (player 0 base) |
| `05` | species_type | 5 (Sovereign) |
| `00 00 18 42` | position_x | 38.0 |
| `00 00 44 C2` | position_y | -49.0 |
| `00 00 0C C2` | position_z | -35.0 |
| ... | orientation, speed, name, set, subsystems | (continues) |

Both examples: same player (slot 0), same team (2), same object ID base — but different ship species and spawn positions. This is consistent with a player changing ship selection (the second creation replaces the first).

## Open Questions

- **Reserved bytes**: The 3 bytes after speed are always observed as zero. They may encode initial state flags (cloak, warp, shield status) but this has not been confirmed.
- **Subsystem state format**: The trailing blob varies by ship type. It likely encodes per-subsystem health as floating-point values, but the exact layout per ship class has not been fully documented.
- **Orientation encoding**: Four consecutive floats after position are consistent with a quaternion (W, X, Y, Z), but Euler angles (with one unused float) have not been ruled out.
