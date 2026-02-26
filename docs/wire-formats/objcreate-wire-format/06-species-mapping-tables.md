# Species Mapping Tables


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

