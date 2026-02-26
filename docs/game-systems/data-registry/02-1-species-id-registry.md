# 1. Species ID Registry


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

