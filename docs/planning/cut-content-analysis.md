# Cut, Incomplete, and Hidden Features

Bridge Commander contains a remarkable amount of partially-implemented, disabled, and developer-only functionality. This document catalogs these features and assesses restoration feasibility for a reimplementation.

**Clean room statement**: All information in this document comes from the game's shipped Python scripts, hardpoint files, the public scripting API, and observable in-game behavior. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Executive Summary

The multiplayer design documents clearly planned at least 9 game modes, but only 4 shipped. Code references to unshipped modes (cooperative Borg Hunt, asymmetric Enterprise Assault) survive in the shared multiplayer Python scripts. The AI fleet command system, starbase docking/repair, tractor beam docking, and ship class scoring modifiers were all built for a multiplayer experience far more ambitious than the deathmatch that shipped.

---

## 1. Cut Multiplayer Modes (Ghost Missions)

### End-Game Reason Constants

From `scripts/Multiplayer/MissionShared.py`:

```python
END_ITS_JUST_OVER = 0
END_TIME_UP = 1
END_NUM_FRAGS_REACHED = 2
END_SCORE_LIMIT_REACHED = 3
END_STARBASE_DEAD = 4        # Used by Mission5 (shipped)
END_BORG_DEAD = 5            # Mission7 (CUT)
END_ENTERPRISE_DEAD = 6      # Mission9 (CUT)
```

### Mission Map

| Mission | Status | Description |
|---------|--------|-------------|
| Mission1 | **SHIPPED** | Free-for-all Deathmatch |
| Mission2 | **SHIPPED** | Team Deathmatch |
| Mission3 | **SHIPPED** | Team Deathmatch variant |
| Mission4 | **CUT** | Unknown (gap in numbering, no code references survive) |
| Mission5 | **SHIPPED** | Assault/Defend Starbase |
| Mission6 | **CUT** | Starbase variant. Referenced in MissionShared.py alongside Mission5, shares the `g_bStarbaseDead` flag. Likely reversed teams or different victory conditions. |
| Mission7 | **CUT** | **Cooperative Borg Hunt**. Referenced in MissionShared.py. Checks `g_bBorgDead`, uses `END_BORG_DEAD`. String lookup: `"Borg Destroyed"`. BORGCUBE species (index 45) exists in `SpeciesToShip.py`. All shared infrastructure in place -- only the mission script folder is missing. |
| Mission8 | **CUT** | Unknown (gap in numbering, no code references survive) |
| Mission9 | **CUT** | **Destroy the Enterprise**. Referenced in MissionShared.py. Checks `g_bEnterpriseDead`, uses `END_ENTERPRISE_DEAD`. String lookup: `"Enterprise Destroyed"`. ENTERPRISE species (37, Sovereign-class variant) exists. Asymmetric mode where players try to destroy a specific named ship. |

### Restoration Feasibility

**Mission7 (Borg Hunt)** is the highest-value restoration target:
- `MissionShared.py` already processes `END_BORG_DEAD` and `g_bBorgDead`
- BORGCUBE species exists with ship definition
- The `ObjectGroupWithInfo` class (used in Mission5) provides team grouping
- The AI fleet command system could give the Borg Cube AI behaviors
- Only needed: a `Multiplayer/Episode/Mission7/` folder with mission script, menus, and map setup

**Mission9 (Enterprise Assault)** is similarly close:
- `MissionShared.py` already processes `END_ENTERPRISE_DEAD` and `g_bEnterpriseDead`
- ENTERPRISE species (37) exists as a Sovereign-class variant
- Asymmetric scoring could use the existing `Modifier.py` class system

---

## 2. Cut Multiplayer Systems

### Fleet Command AI

The `scripts/AI/Fleet/` directory contains a complete tactical vocabulary for commanding AI wingmen:

| Command | Script | Behavior |
|---------|--------|----------|
| DefendTarget | `AI/Fleet/DefendTarget.py` | Orders wingman to defend a specific target |
| DestroyTarget | `AI/Fleet/DestroyTarget.py` | Orders attack on a specific target |
| DisableTarget | `AI/Fleet/DisableTarget.py` | Orders to disable (not destroy) a target |
| DockStarbase | `AI/Fleet/DockStarbase.py` | Orders docking with a starbase |
| HelpMe | `AI/Fleet/HelpMe.py` | Distress call -- wingman comes to your aid |

These were designed for commanding AI wingmen. The scripting API exposes them generically -- they work on any ship with any AI. However, no shipped multiplayer mission uses them. Potential applications:
- Cooperative missions with AI wingmen (each human commanding a small fleet)
- Fleet battles (human commanders with AI-controlled escorts)
- Asymmetric modes (one player as fleet commander, others as wingmen)

The `ObjectGroupWithInfo` class provides dynamic ship grouping, and the AI already takes group-level orders.

**Restoration feasibility**: MODERATE. The AI commands work. Needs UI for issuing fleet orders in MP context and network messages to synchronize orders.

### Ship Class Scoring Modifiers

`scripts/Multiplayer/Modifier.py` contains a ship class multiplier table:

```python
g_kModifierTable = (
    (1.0, 1.0, 1.0),    # Class 0 (unknown)
    (1.0, 1.0, 1.0),    # Class 1
    (1.0, 3.0, 1.0))    # Class 2
```

`GetModifier(attackerClass, killedClass)` returns a score multiplier. Class 2 killing Class 1 yields a 3x score bonus. The scoring path calls `GetModifier` on every kill in Mission5's `DamageHandler`.

**The problem**: Every flyable ship in `SpeciesToShip.py` is assigned class 1. All player-selectable ships are the same class, so the modifier system is a no-op. This was a balance feature designed to incentivize diverse ship choices (small ship kills big ship = bonus points) that was never tuned.

**Restoration feasibility**: TRIVIAL. Assign class values in `SpeciesToShip.py`. The multiplier table and scoring path already work.

### Multi-Set Multiplayer Maps

Opcode 0x1F (EnterSet) handles moving objects between sets over the network. The "Set" system allows multiple playable areas connected by warp points. In single-player, missions use multiple sets (warp from one system to another). In multiplayer, all maps are single-set.

The infrastructure for multi-set multiplayer exists:
- Network opcode for set transitions (0x1F)
- WarpDrive subsystem on every ship
- Opcode 0x10 (StartWarp) for warp state forwarding
- `WarpHandler` in MissionShared.py processes warp events
- 9 map systems defined (Multi1-Multi7, Albirea, Poseidon) -- more maps than shipped modes

**Potential**: Convoy escort across systems, multi-arena battles, strategic retreat to repair at a different location.

**Restoration feasibility**: MODERATE. Create multi-set MP maps with warp routes. The network protocol already handles set transitions.

### Sensor Array in Multiplayer

The SensorArray subsystem exists on every ship. The `ScanHandler` in MissionShared.py processes scan events in MP. But scanning has no gameplay effect in multiplayer -- it plays a voice line and nothing else.

In cooperative/objective modes, scanning could reveal: enemy positions, objective information, environmental hazards, cloaked ship detection.

**Restoration feasibility**: MODERATE. Subsystem exists, needs game logic for scan results in MP context.

---

## 3. Developer Tools (Left in Game)

### Python Debug Console

**Status**: FULLY FUNCTIONAL (shipped in retail, not exposed to players)

The console is a full Python REPL window. `TopWindow.ToggleConsole()` creates and shows it. `TGConsole.EvalString(string)` evaluates arbitrary Python code. The `SimpleAPI.py` module provides convenience functions: `Edit()` toggles edit mode and console, `Speed(f)` sets game speed, `Save(filename)` saves state.

**Access**: Call `App.TopWindow_GetTopWindow().ToggleConsole()` from any Python handler.

**Restoration feasibility**: TRIVIAL. One Python call.

### Debug Cheat Commands

**Status**: FULLY FUNCTIONAL (shipped, gated behind TestMenuState >= 2)

| Cheat | Trigger | Effect |
|-------|---------|--------|
| Kill Target | Shift+K | 25% damage to targeted subsystem |
| Quick Repair | Shift+R | Fully repair targeted ship |
| God Mode | Shift+G | Invulnerability + full repair |
| Load Quantums | Ctrl+Q | +10 quantum torpedoes |
| Toggle Edit Mode | (no default key) | Enables placement editor |

All debug cheats check `GetTestMenuState() < 2` and return immediately if true. In the shipped game, this value is 0 (disabled).

**How to enable**: Call `App.g_kUtopiaApp.SetTestMenuState(2)` from Python. This unlocks all debug keys and additional main menu options (quick-start missions, skip to episodes).

**Restoration feasibility**: TRIVIAL. One Python call enables everything.

### Placement Editor (In-Game Level Editor)

**Status**: FULLY FUNCTIONAL (developer tool left in binary)

A full in-game object placement tool that can:
- Create and position objects in 3D space
- Configure asteroid fields (radius, density, size factor)
- Place waypoints with speed values
- Place lights
- Save/load scene configurations
- Switch between sets

Enabled via `TopWindow.ToggleEditMode()` or `SimpleAPI.Edit()`. Useful for creating multiplayer maps.

**Restoration feasibility**: MODERATE. Fully functional but designed for single-player scene creation.

---

## 4. Incomplete Game Features

### Tractor Beam Docking

**Status**: DEEPLY IMPLEMENTED (C++ classes, scripting API, events, campaign usage)

The tractor beam has 6 modes:

| Mode | Name | Behavior |
|------|------|----------|
| 0 | HOLD | Zero target velocity |
| 1 | TOW | Tow target behind ship |
| 2 | PULL | Pull target closer |
| 3 | PUSH | Push target away |
| 4 | DOCK_STAGE_1 | Docking approach phase |
| 5 | DOCK_STAGE_2 | Final docking alignment |

Docking IS used in the single-player campaign (tutorial, multiple episodes). Each ship has a "docked" flag readable via `IsDocked()` / `SetDocked()`. Events include: tractor started/stopped firing, tractor started/stopped hitting, target docked, player docked with starbase.

**Multiplayer status**: No network opcode for dock state synchronization. The docked flag is purely local. The 2-stage docking modes suggest a planned network-synchronized sequence that was never completed for MP.

**Restoration feasibility**: HIGH for single-player, MODERATE for multiplayer. Adding MP support requires a new opcode or Python message to sync dock state.

### Starbase Repair and Reload

Events exist for starbase repair (`ET_SB12_REPAIR`) and reload (`ET_SB12_RELOAD`). The scripting API tracks `CurrentStarbaseTorpedoLoad`, suggesting torpedoes were meant to be a finite starbase resource. The docking AI script (`AI/Compound/DockWithStarbase.py`) is ~660 lines of polished code handling approach, cutscene, repair/rearm, and undocking.

**Restoration feasibility**: MODERATE. Could be a game-changing multiplayer feature for longer matches.

### In-System Warp

**Status**: FULLY IMPLEMENTED (used in campaign)

`ShipClass.InSystemWarp(destination, speed)` warps a ship to a location WITHIN the current set (not between sets). Default speed is 575.0 units. `IsDoingInSystemWarp()` and `StopInSystemWarp()` are also exposed.

**Multiplayer potential**: Tactical warping within combat arenas. The network opcode 0x10 (StartWarp) already exists.

**Restoration feasibility**: HIGH. The code is complete. Needs a Python trigger and possibly MP state synchronization.

### Object Emitter System (Probes, Shuttles, Decoys)

Ships can emit objects via the ObjectEmitterProperty system:

| Type | Status |
|------|--------|
| Probe | FULLY FUNCTIONAL (used in campaign, disabled in MP via `pLaunch.SetDisabled()` in ScienceMenuHandlers.py) |
| Shuttle | Enum constant exists, no usage |
| Decoy | Enum constant exists, no usage |

The probe launch system is a working template. SPECIES_SHUTTLE and SPECIES_ESCAPEPOD exist as species types.

**Restoration feasibility**: MODERATE. Framework is complete; shuttles and decoys need hardpoint definitions, AI behavior, and UI.

### Friendly Fire Penalty

**Status**: DEEPLY IMPLEMENTED (tracking, events, scripting API)

The scripting API exposes: current friendly-fire points, max tolerance, warning threshold, tractor FF time tracking, and tractor warning threshold. Events fire for friendly fire damage, warnings, and game-over.

`MissionShared.py` calls `SetupFriendlyFireNoGameOver()` on MP mission init, so basic point tracking IS active in multiplayer. However, no MP scripts register event handlers for consequences.

**Restoration feasibility**: HIGH. System is already running. Just needs event handlers for warnings and consequences (kick, score penalty).

### Nebula Damage

**Status**: FULLY IMPLEMENTED (C++ + Python)

`Nebula.SetupDamage(damagePerTick, -1.0)` configures continuous damage for ships inside nebulae. `SetDamageResolution()` controls tick interval. The default resolution (15.0) is set globally in `GlobalPropertyTemplates.py`.

No multiplayer maps include damaging nebulae, but the infrastructure is complete.

**Restoration feasibility**: HIGH. Place a nebula in an MP map and call `SetupDamage()`.

### Damage Volumes

**Status**: FULLY FUNCTIONAL (shipped visual feature)

`AddObjectDamageVolume(x, y, z, radius, damage)` creates spherical deformation zones on ship models. `DamageRefresh(1)` commits zones to the 3D model, creating visible hull damage (chunks missing). Used in 25 campaign scripts across 8 missions. A DAMAGE_VOLUME_MESSAGE network message type exists.

Graphics quality levels: off, basic hull damage, volume deformation, breakable components.

**Restoration feasibility**: HIGH for MP maps. Use `AddObjectDamageVolume` in map scripts for pre-damaged derelicts, environmental hazards, or minefields.

---

## 5. Notable Unused Species Types

| Species | Status | Notes |
|---------|--------|-------|
| ESCAPEPOD | **UNUSED** | No scripts create or reference escape pods |
| SUNBUSTER | **UNUSED** | Name suggests a superweapon. No scripts reference it. |
| PROBETYPE2 | **UNUSED** | Second probe variant. Only SPECIES_PROBE is used. |

---

## 6. Restoration Priority Ranking

### Tier 1: Trivial (Python-only, no engine changes)

1. **Ship Class Scoring** -- Assign class values in `SpeciesToShip.py`. Modifier table and scoring path already work.
2. **Friendly Fire Penalties** -- Register event handlers in MP scripts. System is already active.
3. **Debug Console** -- Call `ToggleConsole()`. Instant Python REPL.
4. **Debug Cheats** -- Call `SetTestMenuState(2)`. God mode, kill target, quick repair.

### Tier 2: New Mission Scripts (Python-only)

5. **Mission7: Cooperative Borg Hunt** -- All infrastructure exists in MissionShared.py. Write the mission folder with setup script and Borg Cube spawn logic. Highest value restoration target.
6. **Mission9: Destroy the Enterprise** -- Asymmetric PvP/PvE mode. Write the mission folder.
7. **Mission6: Starbase Variant** -- Clone Mission5 with modified rules.
8. **Nebula Damage in MP Maps** -- Place nebula objects, configure damage.
9. **Damage Volumes in MP Maps** -- Place pre-damaged objects, environmental hazards.

### Tier 3: Moderate Effort (Python + new network messages)

10. **Starbase Docking/Repair** -- Wire the ~660-line `DockWithStarbase.py` to multiplayer. Needs dock state sync.
11. **In-System Warp** -- Add warp points to MP maps, Python trigger, MP state sync.
12. **Fleet Command in MP** -- Expose AI commands to human players. Needs command UI + network sync.
13. **Probe Launch in MP** -- Science menu button exists. Need MP sync for probe objects.
14. **Sensor Scanning** -- Add gameplay effects to MP scanning.

### Tier 4: Significant Effort (New game logic)

15. **Tractor Beam Docking in MP** -- Need network sync for 2-stage docking sequence.
16. **Multi-Set MP Maps** -- Create maps with warp routes between areas.
17. **Shuttle/Decoy Launch** -- Emitter framework exists, needs AI + models + UI.
18. **Escape Pods** -- Species type exists but no game logic.

### The Dream Feature

A cooperative Borg Hunt (Mission7) with:
- 4 human players each commanding a small task group (Fleet AI)
- AI wingmen following tactical orders (DefendTarget, DestroyTarget, HelpMe)
- Starbase docking for repair and rearm between engagements
- Tractor beams for rescuing escape pods
- Nebula as tactical cover with damage zones
- Ship class scoring bonuses for smaller ships engaging the Cube

Every single piece of this existed in the Bridge Commander codebase. It was never wired together.

---

## Related Documents

- **[combat-system.md](../game-systems/combat-system.md)** -- Damage pipeline, shields, weapons, cloaking, tractor, repair
- **[ship-subsystems.md](../game-systems/ship-subsystems.md)** -- Subsystem types and configurations per ship
- **[objcreate-wire-format.md](../wire-formats/objcreate-wire-format.md)** -- Species tables (includes BORGCUBE, ENTERPRISE, ESCAPEPOD)
- **[collision-detection-system.md](../game-systems/collision-detection-system.md)** -- Collision detection pipeline
- **[phase1-verified-protocol.md](phase1-verified-protocol.md)** -- All network opcodes including 0x0E-0x10, 0x1F
