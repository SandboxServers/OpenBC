# Phase 1 Mod Compatibility Analysis
## Standalone Server - Mod Requirements Research

### 1. Server-Side Mod Landscape

#### 1.1 Ship Mods (HIGH IMPACT on server)
Ship mods are the most common type of BC mod and they absolutely affect the server.

**What they modify:**
- `scripts/ships/NewShip.py` - Ship stats (model paths, hardpoint file, species ID, name)
- `scripts/ships/Hardpoints/newship.py` - Full subsystem configuration (weapons, shields, hull, engines, sensors, repair)
- `scripts/GlobalPropertyTemplates.py` - Ship property registration (mass, genus, species, affiliation)

**Server-side impact:**
- The server must load ship definitions to create ships (`SpeciesToShip.CreateShip()` calls `ShipClass_Create`)
- The server must load hardpoints to configure weapon damage, shield values, hull strength
- The server tracks damage, kills, scores -- all based on ship properties
- Weapon damage calculation in `Mission1.DamageHandler` uses `pEvent.GetDamage()` which comes from hardpoint values

**Critical API needed:**
All 19 `*Property_Create` functions, all ~132 Set* methods on properties, plus:
- `App.ShipClass_Create`, `App.ShipClass_Cast`, `App.ShipClass_GetObject`
- `App.g_kModelPropertyManager.RegisterLocalTemplate/RegisterGlobalTemplate/ClearLocalTemplates`
- `App.g_kLODModelManager.Create/Contains` (but server may not need model LOD loading)
- `App.TGModelPropertySet` (for difficulty adjustment)

**Mod pattern:** Modders add entries to `SpeciesToShip.kSpeciesTuple`, add new SPECIES_* constants, and create new ship/hardpoint scripts. Foundation Technologies automates this with a ship registry.

#### 1.2 Weapon Mods (HIGH IMPACT on server)
Custom torpedo types and weapon configurations.

**What they modify:**
- `scripts/Tactical/Projectiles/NewTorpedo.py` - Torpedo creation and configuration
- `scripts/Multiplayer/SpeciesToTorp.py` - Torpedo type registry
- Hardpoint files (phaser/pulse weapon properties)

**Server-side impact:**
- Server creates torpedoes via `SpeciesToTorp.CreateTorpedoFromSpecies()` -> `App.Torpedo_Create()`
- Torpedo damage values affect scoring
- Custom torpedo types need matching entries in SpeciesToTorp.kSpeciesTuple

#### 1.3 Map/Mission Mods (MEDIUM IMPACT on server)
Custom multiplayer maps and game modes.

**What they modify:**
- `scripts/Systems/NewSystem/NewSystem.py` - Star system creation (placement objects, lights, nebulae, asteroids)
- `scripts/Multiplayer/Episode/MissionX/` - Custom game mode scripts
- `scripts/Multiplayer/SpeciesToSystem.py` - System registry

**Server-side impact:**
- Server creates the system set via `SpeciesToSystem.CreateSystemFromSpecies()` -> System module's `Initialize()`
- System scripts create placement objects, grids, asteroid fields, planets
- Server must track proximity (ProximityManager), collision damage
- Custom missions define scoring rules, team mechanics, win conditions

**API needed for systems:**
- `App.SetClass_Create`, `App.g_kSetManager.AddSet/GetSet/DeleteSet`
- `App.LightPlacement_Create` (lights are needed for some gameplay mechanics even headless)
- `App.GridClass_Create` (always created in systems)
- `App.PlacementObject_GetObject`, `App.ObjectClass_Cast`
- `App.AsteroidFieldPlacement_Create` (if custom systems have asteroid fields)
- `App.Planet_Create` (static objects in systems)
- `App.MetaNebula_Create` (nebula regions affect sensors/visibility)

#### 1.4 Foundation Technologies (CRITICAL ANALYSIS)
Foundation Technologies is NOT present in the vanilla game scripts. It is installed by modders into the `scripts/Custom/` directory (checksum-exempt).

**Key question: Does Foundation affect server-side multiplayer?**

Foundation's primary features and their server impact:
1. **Ship Registry** - Replaces the hardcoded `SpeciesToShip.kSpeciesTuple` with a dynamic registry. Ship mods register themselves via Foundation plugins. SERVER IMPACT: HIGH - without this, custom ships can't be used in multiplayer.
2. **Dynamic Weapon System** - Extends torpedo/weapon types beyond the hardcoded list. SERVER IMPACT: HIGH - custom weapons need server validation.
3. **Plugin Architecture** - `scripts/Custom/Autoload/` plugins that run at startup. SERVER IMPACT: MEDIUM - some plugins modify server behavior.
4. **Import Hooks** - Foundation uses `__import__` hooks to intercept module loading. SERVER IMPACT: HIGH - this is exactly the same technique the STBC-Dedicated-Server uses.
5. **Enhanced AI** - Custom AI behaviors. SERVER IMPACT: LOW for multiplayer (AI is client-side for player ships).

**Foundation for Phase 1 verdict:** Phase 1 CAN work without Foundation for vanilla multiplayer. However, the moment a server wants to support custom ships (which is the #1 reason to mod), Foundation support becomes critical. Foundation should be a Phase 1.5 goal.

#### 1.5 KM (Kobayashi Maru)
KM extends multiplayer with additional ship classes, balancing, and game modes. It depends on Foundation Technologies.

**Server-side changes:**
- Additional ship definitions with rebalanced stats
- Custom multiplayer mission scripts
- Modified scoring systems

**Phase 1 verdict:** Can defer. KM requires Foundation first.

### 2. Script Analysis

#### 2.1 Multiplayer Scripts (`reference/scripts/Multiplayer/`)

| Script | Lines | Purpose | Server-Side? |
|--------|-------|---------|-------------|
| `MultiplayerGame.py` | 168 | Game lifecycle (init, music, terminate) | Partially - music is client-only |
| `MultiplayerMenus.py` | ~3000+ | Lobby UI, server browser, connection UI | Almost entirely UI - but contains host/join logic |
| `MissionShared.py` | 434 | Common mission logic, network messages, timer, scoring | YES - core server logic |
| `MissionMenusShared.py` | ~1500+ | Mission UI (ship select, scores, end game) | Mixed - has server config variables |
| `SpeciesToShip.py` | 199 | Ship type registry, ship creation from species ID | YES - critical for server |
| `SpeciesToSystem.py` | 60 | Map/system registry, system creation | YES - server creates the map |
| `SpeciesToTorp.py` | 72 | Torpedo type registry, torpedo creation | YES - server validates torpedoes |
| `Modifier.py` | 20 | Score modifier table (ship class vs ship class) | YES - affects server scoring |

#### 2.2 Mission Scripts (server-critical event handlers)

Server-side event handlers registered by Mission1.SetupEventHandlers():
- `ET_OBJECT_EXPLODING` -> ObjectKilledHandler (scoring, frag counting)
- `ET_WEAPON_HIT` -> DamageEventHandler (damage tracking for scoring)
- `ET_NEW_PLAYER_IN_GAME` -> NewPlayerHandler (add player to score dicts)
- `ET_NETWORK_DELETE_PLAYER` -> DeletePlayerHandler (preserve scores)
- `ET_OBJECT_CREATED_NOTIFY` -> ObjectCreatedHandler (enemy/friendly groups)
- `ET_NETWORK_MESSAGE_EVENT` -> ProcessMessageHandler (network protocol)
- `ET_NETWORK_NAME_CHANGE_EVENT` -> ProcessNameChangeHandler (UI update)

Server-side network messages (custom protocol on top of TGNetwork):
- `MISSION_INIT_MESSAGE` - Server->Client: system ID, time/frag limits, player limit
- `SCORE_CHANGE_MESSAGE` - Server->All: player kills, deaths, score updates
- `SCORE_MESSAGE` - Server->Joining Client: full score state sync
- `END_GAME_MESSAGE` - Server->All: game over reason
- `RESTART_GAME_MESSAGE` - Server->All: game restart signal

#### 2.3 Ship Definition Format

Each ship has two files:
1. `ships/Galaxy.py` - Stats dictionary:
```python
{"FilenameHigh": "...", "FilenameMed": "...", "FilenameLow": "...",
 "Name": "Galaxy", "HardpointFile": "galaxy",
 "Species": Multiplayer.SpeciesToShip.GALAXY, "SpecularCoef": 0.55}
```
2. `ships/Hardpoints/galaxy.py` - Property configuration (auto-generated, 500+ lines):
   - TorpedoTubeProperty (tubes, reload, damage)
   - PhaserProperty (arcs, damage, charge, colors)
   - ShieldProperty (6-facing shield values, recharge)
   - HullProperty (hull HP)
   - PowerProperty (power output, conduits)
   - EngineProperty (impulse speed, warp)
   - SensorProperty (range)
   - RepairSubsystemProperty (teams, repair rate)

#### 2.4 Custom/ Directory
The vanilla Custom/ only contains Tutorial scripts. However, the checksum exemption means this is where ALL mod frameworks install:
- `Custom/Autoload/` - Foundation plugin directory
- `Custom/Ships/` - Foundation ship registrations
- `Custom/QBautostart/` - Quick Battle mods
- `Custom/NanoFX/` - NanoFX visual effects
- `Custom/DedicatedServer.py` - Server automation (from STBC-Dedicated-Server project)
- `Custom/UnifiedMainMenu/` - Custom menu systems

### 3. Mod Compatibility Requirements for Phase 1

#### MUST SUPPORT (Critical for basic modded server play)

1. **Vanilla ship definitions loading** - All 50+ ship/hardpoint scripts must execute
   - 19 Property_Create functions
   - ~132 Set* methods on property objects
   - Property registration (RegisterLocalTemplate/RegisterGlobalTemplate)
   - Ship creation (ShipClass_Create) and property setup (SetupProperties)

2. **Vanilla multiplayer missions (1-5)** - Core game modes
   - Network message handling (TGMessage, TGBufferStream, TGNetwork)
   - Event system (ET_* event types, handler registration, broadcast handlers)
   - Timer system (g_kTimerManager, g_kRealtimeTimerManager)
   - Score tracking (dictionaries in Mission scripts)
   - System/set creation (SetClass_Create, g_kSetManager)

3. **Custom/ directory support** - Checksum-exempt script loading
   - Must load scripts from Custom/ in addition to standard paths
   - sys.path manipulation must work

4. **IsHost/IsClient/IsMultiplayer flags** - Core mode detection
   - `App.g_kUtopiaModule.IsHost()` must return True for server
   - `App.g_kUtopiaModule.IsClient()` must return False for dedicated server
   - `App.g_kUtopiaModule.IsMultiplayer()` must return True

5. **Network protocol** - Player connection, checksum exchange, message routing
   - TGNetwork creation, connect/disconnect
   - Player list management
   - Message send/receive (guaranteed + unreliable)
   - Group messaging ("NoMe" group for broadcast-except-self)

#### SHOULD SUPPORT (Enhances server experience)

1. **Custom ship definitions** - Modded ships with different stats
   - Extended SpeciesToShip.kSpeciesTuple (more species IDs)
   - Custom hardpoint files with same Property API
   - Custom GlobalPropertyTemplates entries

2. **Custom multiplayer maps** - New Systems/* scripts
   - SetClass_Create with custom placements
   - LightPlacement, GridClass, asteroid fields
   - Extended SpeciesToSystem.kSpeciesTuple

3. **__import__ hook compatibility** - Foundation and mods use import hooks
   - `__builtin__.__import__` replacement must work
   - Package attribute resolution (Python 1.5 / 3.x compat layer)

4. **Local.py loading** - Server customization entry point
   - Loaded by Autoexec.py via `import Local`
   - Must support try/except wrapping (mod may not exist)

5. **VarManager** - Game variables for mission configuration
   - MakeEpisodeEventType (custom event types)
   - GetStringVariable/SetStringVariable (mission selection)

6. **Localization (TGL files)** - String databases for mission messages
   - g_kLocalizationManager.Load/Unload
   - Database.GetString().GetCString()

#### CAN DEFER (Phase 2+)

1. **Foundation Technologies framework** - Ship registry, plugin system
   - Complex __import__ hooks
   - Dynamic ship/weapon registration
   - Custom AI behaviors
   - Phase 2 target: enables 80% of community mods

2. **KM (Kobayashi Maru)** - Depends on Foundation

3. **NanoFX** - Visual effects only, no server impact

4. **Custom torpedo projectile scripts** - Beyond vanilla torpedo types
   - Custom Tactical/Projectiles/ scripts
   - Only needed when modded torpedo types are used

5. **Difficulty adjustment system** - `AdjustShipForDifficulty` in loadspacehelper.py
   - Uses TGModelPropertySet comparison with hardpoints
   - Complex subsystem iteration (GetPropertiesByType, TGBeginIteration, etc.)

6. **Save/Load** - Mission state persistence
   - g_kUtopiaModule.SaveToFile/LoadFromFile
   - Not needed for dedicated server

#### WON'T WORK (Inherently client-side)

1. **Bridge scenes** - All of `scripts/Bridge/`, `LoadBridge.py`
2. **Camera/rendering** - Camera*, Display.py, Effects.py
3. **Sound/Music** - DynamicMusic.py, LoadTacticalSounds.py, SoundConfig.py
4. **UI elements** - All ST*Button, ST*Menu, TGPane, StylizedWindow
5. **Input handling** - KeyConfig.py, keyboard bindings
6. **Warp sequence visualization** - WarpSequence.py
7. **NIF model rendering** - LOD model visual loading (but model data still needed for collision geometry)
8. **BC Mod Installer** - Client-side tool, irrelevant to server

### 4. API Surface Analysis

#### Vanilla Server API: 298 functions + 124 constants + 29 singletons = 451 total symbols

Breakdown by category:
- **Ship/Object creation**: ShipClass_Create, Torpedo_Create, DamageableObject_Create, etc. (~20 functions)
- **Property system**: 19 *Property_Create + RegisterTemplate + ClearLocalTemplates (~25 functions)
- **Event system**: EventManager handlers, TGEvent_Create, event types (~15 functions + ~75 ET_* constants)
- **Network**: TGMessage, TGBufferStream, TGNetwork send/receive (~25 functions)
- **Set/World management**: SetClass_Create, g_kSetManager, placement objects (~20 functions)
- **Game state**: Game_GetCurrentGame, MultiplayerGame_Cast, timer management (~15 functions)
- **Utility**: TGPoint3, TGColorA, TGString, IsNull, casts (~20 functions)
- **UI (stubs needed)**: ~80+ functions that must exist but can return None/no-op on server
- **Species constants**: ~30 SPECIES_* values
- **Config**: g_kConfigMapping, VarManager (~10 functions)
- **Singleton globals**: 29 g_k* globals (some can be None on server)

#### Mod-Extended API Estimate
Foundation Technologies alone would add approximately:
- Custom event types via MakeEpisodeEventType
- Dynamic ship/weapon registration APIs
- Import hook infrastructure
- Additional species constants

**Conservative estimate: Phase 1 with basic mod support needs ~350-400 implemented API functions.**
**With Foundation support: likely ~450-500.**

### 5. Checksum System Implications

The checksum system is critical for mod compatibility:

| Index | Directory | Filter | Recursive | Impact |
|-------|-----------|--------|-----------|--------|
| 0 | scripts/ | App.pyc | No | Must match - this IS the SWIG module |
| 1 | scripts/ | Autoexec.pyc | No | Must match - startup script |
| 2 | scripts/ships/ | *.pyc | Yes | Must match - ALL ship definitions |
| 3 | scripts/mainmenu/ | *.pyc | No | Must match - main menu scripts |

**Key insight:** `scripts/Custom/` is NOT checksummed. This means:
- Mods that only add Custom/ scripts work with vanilla clients
- Ship mods that modify `scripts/ships/` require ALL clients to have the same mod
- Foundation Technologies modifies scripts OUTSIDE Custom/ and thus affects checksums
- OpenBC Phase 1 must implement the checksum exchange protocol to support mixed mod states

**For OpenBC dedicated server:** We can optionally skip checksum verification (the original game has a SkipChecksum flag), allowing any client to connect regardless of script differences. This is a common community request.

### 6. The Dedicated Server Pattern (from STBC-Dedicated-Server)

The existing DDraw proxy dedicated server project reveals the exact headless mode requirements:

1. **GUI Stubbing** - ~80+ UI functions must be replaced with no-ops
   - All of MultiplayerMenus.py (it's all UI)
   - LoadBridge.CreateCharacterMenus and friends
   - Mission*Menus.BuildMission*Menus, RebuildPlayerList, etc.
   - SortedRegionMenu_GetWarpButton returns None -> must handle gracefully

2. **Import Hook** - `__builtin__.__import__` replacement needed for:
   - Package attribute resolution (Python 1.5 `from X import Y` quirk)
   - Mission module patching for headless safety
   - Error swallowing for GUI-related imports

3. **Shadow Class vs. Raw SWIG** - When running headless:
   - Some App functions return raw SWIG pointer strings instead of shadow class instances
   - Mission scripts assume shadow class methods (pMessage.SetGuaranteed(1))
   - Dedicated server falls back to functional Appc API (Appc.TGMessage_SetGuaranteed(ptr, 1))
   - OpenBC should ensure consistent object wrapping

4. **Event Handler Safety** - Mission event handlers must be wrapped in try/except
   - func_code replacement technique (Python 1.5 has no closures)
   - Default argument capture for _orig function and _log function
   - This is needed because handlers reference GUI widgets that don't exist headless
