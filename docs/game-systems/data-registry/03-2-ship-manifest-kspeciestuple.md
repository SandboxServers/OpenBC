# 2. Ship Manifest (kSpeciesTuple)


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

