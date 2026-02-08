# Gameplay-Scope Mod Compatibility Analysis

## Phase 1 Re-scoped: Full Multiplayer Matches (Not Just Lobby)

The Phase 1 server now handles gameplay, not just lobby. This analysis covers
the mod compatibility implications of running full scoring, damage tracking,
ship creation, and game-end logic on the server.

---

## 1. Ship Mods During Gameplay

### What the server needs

When a modded ship is in play, the server-side scoring scripts call:

1. `pShip.GetNetType()` -- returns the species integer the ship was created with
2. `SpeciesToShip.GetClassFromSpecies(iSpecies)` -- maps species to class integer (0, 1, 2)
3. `Modifier.GetModifier(iAttackerClass, iKilledClass)` -- looks up score multiplier from table

The server also calls during ship creation (via `SpeciesToShip.CreateShip()`):
- `GetShipFromSpecies()` -> imports `ships.ShipName` module, loads `GetShipStats()`
- `__import__("ships.Hardpoints." + kStats['HardpointFile'])` -> full hardpoint load
- `ShipClass_Create()`, `SetScript()`, `GetPropertySet()`, `SetupProperties()`, `UpdateNodeOnly()`
- `SetNetType()` to record which species this ship is

### Does the server need full hardpoint data?

**YES.** The server creates ship objects via `SpeciesToShip.CreateShip()` which
calls `LoadPropertySet()` from the hardpoint file. This sets shield HP, hull HP,
weapon damage values, and all subsystem properties. The C engine uses these
properties for:
- Damage distribution across shield facings and hull
- Subsystem destruction tracking
- Ship death detection (triggers `ET_OBJECT_EXPLODING`)
- Weapon damage values for `pEvent.GetDamage()` in scoring

The `SpeciesToShip.InitObject()` function is called from C code when a ship is
serialized over the network -- it also loads the full hardpoint file.

### What breaks with modded ships?

For **standalone custom ships** (ships that add entries to `kSpeciesTuple` directly):
- The server must have the ship script files (`ships/NewShip.py`, `ships/Hardpoints/newship.py`)
- The server must have matching SPECIES_* constants in App module
- The `kSpeciesTuple` must include the new ship entry with correct class
- All the Property_Create and Set* methods must work

For **Foundation-managed ships** (ships registered via Foundation plugins):
- Foundation replaces `kSpeciesTuple` with a dynamic registry
- The same underlying ship/hardpoint scripts are used
- The Foundation ship registry must be loaded on the server

### Server-side API surface for ship creation

The full property system is needed:
- 19 `*Property_Create` functions
- ~132 `Set*` methods on property objects
- `g_kModelPropertyManager.RegisterLocalTemplate/RegisterGlobalTemplate/ClearLocalTemplates`
- `ShipClass_Create`, `ShipClass_Cast`, `ShipClass_GetObject`
- `SetupProperties()`, `UpdateNodeOnly()`, `SetNetType()`, `GetNetType()`
- `GetPropertySet()`, `SetScript()`
- `IsPlayerShip()`, `GetNetPlayerID()`, `GetObjID()`, `GetName()`
- `IsDying()`, `IsDead()`, `IsTypeOf(App.CT_SHIP)`

---

## 2. Custom Weapon Mods

### Is damage computed client-side or server-side?

**The damage value in `pEvent.GetDamage()` comes from the C engine's damage
simulation, which runs on the HOST.** Here is the evidence:

1. The `DamageEventHandler` and `ObjectKilledHandler` are ONLY registered
   on the host: `if (App.g_kUtopiaModule.IsHost()):` (Mission1.py line 193)
2. The host processes ET_WEAPON_HIT events, computes scores, then sends
   SCORE_CHANGE_MESSAGE to all clients
3. The damage model (shield absorption, hull penetration, subsystem damage)
   runs in the C engine on the host based on the weapon properties set in
   hardpoints

### What the server needs for custom weapons

The server needs the weapon property definitions from hardpoints:
- `PhaserProperty` with damage, arc, and charge values
- `TorpedoTubeProperty` with damage, reload, and tube count
- `PulseWeaponProperty` with damage per pulse

For custom torpedo types (new entries in `SpeciesToTorp.py`):
- The torpedo creation script (`Tactical/Projectiles/NewTorpedo.py`)
- An entry in `SpeciesToTorp.kSpeciesTuple`
- The `Torpedo_Create()` API function

### Does the server just relay damage events?

**No.** The server is the AUTHORITY for damage. The C engine on the host:
1. Detects weapon impacts based on geometry and weapon arcs
2. Computes damage based on weapon properties from hardpoints
3. Applies damage to shield/hull subsystems
4. Fires ET_WEAPON_HIT with the computed damage value
5. Fires ET_OBJECT_EXPLODING when hull reaches zero

The Python scoring scripts then use `pEvent.GetDamage()` for score calculation,
applying the Modifier table multiplier on top.

### Implication for OpenBC

The server must have a functional damage simulation. This means:
- Weapon properties from hardpoints must be loaded and operational
- Shield/hull subsystem simulation must run
- Collision/impact detection must work (at least for scoring)
- ProximityManager must track objects for collision damage

Custom weapon mods will work as long as:
1. Their hardpoint property definitions load correctly (same API)
2. Custom torpedo scripts create torpedoes with correct properties
3. The damage simulation uses the same formulas as the original engine

---

## 3. Custom Game Modes

### Vanilla mission types

| Mission | Type | Special Server Logic |
|---------|------|---------------------|
| Mission1 | Free-For-All Deathmatch | Standard scoring |
| Mission2 | Team Deathmatch | Team scoring, team assignment, team damage, `IsSameTeam()` |
| Mission3 | Fed vs NonFed teams | Same as Mission2 but with faction-locked teams |
| Mission5 | Attack/Defend Starbase | AI starbase creation, `StarbaseAI`, `ObjectGroupWithInfo`, `g_pAttackerGroup` |

### What API surface does Mission5 add beyond Mission1?

Mission5 (Starbase Assault) introduces:
- `App.ObjectGroupWithInfo()` -- object group for AI targeting
- `loadspacehelper.CreateShip()` -- server creates NPC starbase
- `SetAI()` / `AI.Compound.StarbaseAttack` / `App.ConditionalAI_Create` -- AI system
- `App.ConditionScript_Create()` -- AI conditions
- `DisableCollisionDamage()` on the starbase
- `RandomOrientation()`, `SetTranslate()` -- placement
- `GetRadius()`, `IsLocationEmptyTG()` -- collision avoidance
- `GetProximityManager()`, `UpdateObject()` -- proximity tracking
- `App.ObjectGroup_FromModule()` -- cross-module object group reference
- `DeleteObjectFromGame()` on the MultiplayerGame
- `pMission.GetEnemyGroup()`, `pMission.GetFriendlyGroup()` -- group management
- `ObjectGroupWithInfo.AddName()`, `RemoveName()`, `RemoveAllNames()`, `IsNameInGroup()`

### MissionShared.py references Mission6, Mission7, Mission9

MissionShared.py line 242-264 has hardcoded references to three non-existent
mission scripts:
- `Multiplayer.Episode.Mission6.Mission6` -- "Starbase Destroyed" end condition
- `Multiplayer.Episode.Mission7.Mission7` -- "Borg Destroyed" end condition
- `Multiplayer.Episode.Mission9.Mission9` -- "Enterprise Destroyed" end condition

These are clearly RESERVED slots for mod game modes. The vanilla game ships
only Mission1, Mission2, Mission3, Mission5. The MissionShared code already
anticipates custom missions adding `g_bStarbaseDead`, `g_bBorgDead`, or
`g_bEnterpriseDead` flags.

### Custom mission API surface (beyond vanilla)

Custom game modes typically use:
1. **Everything vanilla missions use** (see full list in section 8)
2. **Custom message types**: `App.MAX_MESSAGE_TYPES + N` (N >= 20 for mission-specific)
3. **AI system**: `ConditionalAI_Create`, `ConditionScript_Create`, `CompoundAI`, `SetAI`
4. **Ship/object creation on server**: `loadspacehelper.CreateShip()`
5. **Object groups**: `ObjectGroupWithInfo`, `ObjectGroup_FromModule`
6. **Custom event types**: `App.g_kVarManager.MakeEpisodeEventType(N)`
7. **Timer system**: `MissionLib.CreateTimer()` for game logic timers
8. **pMission.AddPythonFuncHandlerForInstance()** for custom event types

### Risk assessment for custom game modes

Custom game modes follow the same structural pattern as vanilla missions.
If Mission1/2/3/5 work, custom missions WILL work because they use the
exact same API surface. The main risk is custom missions that use additional
APIs not present in vanilla missions (e.g., custom AI behaviors, special
ship types, environment effects).

---

## 4. Foundation Technologies During Gameplay

### Foundation's ship registry and SpeciesToShip

Foundation Technologies replaces the hardcoded `SpeciesToShip.kSpeciesTuple`
with a dynamic ship registry. Key changes:

1. **Import hook**: Foundation installs `__builtin__.__import__` to intercept
   module loading, exactly like the DedicatedServer.py does
2. **Ship registration**: Foundation plugins in `Custom/Autoload/` call
   `Foundation.ShipDef()` to register ships, which dynamically adds entries
   to a replacement for `kSpeciesTuple`
3. **GetClassFromSpecies()**: Foundation either patches this function or
   maintains its own mapping from species to class

### Does Foundation change the scoring flow?

**Partially.** The scoring flow is:
1. `DamageHandler` calls `pHitterShip.GetNetType()` -- this returns a species int
2. `SpeciesToShip.GetClassFromSpecies(species)` -- looks up class from tuple
3. `Modifier.GetModifier(attackerClass, killedClass)` -- looks up score modifier

Foundation changes step 2 by replacing `kSpeciesTuple` with its own registry.
The `GetClassFromSpecies()` function reads from `kSpeciesTuple[iSpecies][3]`
(the 4th element, the class integer). Foundation ships must include a class
value in their registration.

Foundation may also modify step 3 by extending the `g_kModifierTable` in
`Modifier.py` to have more rows/columns for additional ship classes.

### Key Foundation compatibility requirement

For Foundation to work on the OpenBC server:
1. `Custom/` directory scripts must load (already supported -- checksum exempt)
2. `__builtin__.__import__` hook must work (Python 3.x compat layer needed)
3. Foundation's ship registration must execute before any ship creation
4. `SpeciesToShip.kSpeciesTuple` must be replaceable at runtime
5. Foundation's `MAX_SHIPS` must be increased to accommodate new species IDs

---

## 5. The Modifier System

### How it works

`Modifier.py` defines a 3x3 table (vanilla):

```
g_kModifierTable = (
    (1.0, 1.0, 1.0),   # Class 0 (unknown)
    (1.0, 1.0, 1.0),   # Class 1 (e.g. light ships)
    (1.0, 3.0, 1.0))   # Class 2 (e.g. heavy ships)
```

`GetModifier(iAttackerClass, iKilledClass)` returns the multiplier.

Currently, class 2 attacking class 1 gets a 3x score multiplier. This is
the only non-1.0 entry in the vanilla table. It penalizes heavy ships for
killing light ships (or rewards light ships for surviving against heavies,
depending on perspective).

### How mods change it

Mods modify `Modifier.py` in two ways:
1. **Extend the table**: Add more rows/columns for additional ship classes
   (e.g., class 3 for stations, class 4 for fighters)
2. **Change multipliers**: Rebalance the scoring weights

### How important is this for server gameplay?

**CRITICAL for scoring accuracy.** The modifier table directly affects score
calculations. Every `DamageHandler` call in every mission type (1/2/3/5)
uses `Modifier.GetModifier()`. If the modifier table is wrong or missing,
scores will be calculated incorrectly.

However, the modifier system is purely Python -- no C API dependencies beyond
what's already needed. If the Python scripts load, the modifier works.

### Risk

- If a mod extends the table to 5x5 but the ship classes go up to 4,
  `GetClassFromSpecies()` must return values 0-4 correctly
- Out-of-bounds class values will crash with IndexError (no bounds checking
  in `GetModifier()`)
- OpenBC should consider adding bounds checking: if class >= len(table),
  fall back to class 0

---

## 6. Custom Network Messages

### The message numbering scheme

Vanilla defines base message types starting at `App.MAX_MESSAGE_TYPES + 10`:

```
MISSION_INIT_MESSAGE     = App.MAX_MESSAGE_TYPES + 10
SCORE_CHANGE_MESSAGE     = App.MAX_MESSAGE_TYPES + 11
SCORE_MESSAGE            = App.MAX_MESSAGE_TYPES + 12
END_GAME_MESSAGE         = App.MAX_MESSAGE_TYPES + 13
RESTART_GAME_MESSAGE     = App.MAX_MESSAGE_TYPES + 14
```

Mission-specific messages start at `App.MAX_MESSAGE_TYPES + 20`:

```
SCORE_INIT_MESSAGE       = App.MAX_MESSAGE_TYPES + 20  (Mission2/3/5)
TEAM_SCORE_MESSAGE       = App.MAX_MESSAGE_TYPES + 21  (Mission2/3/5)
TEAM_MESSAGE             = App.MAX_MESSAGE_TYPES + 22  (Mission2/3/5)
```

### Does the server need to understand custom messages?

**It depends on whether the server IS the host.**

In the original BC architecture, the host runs both server and client. The
host's Python scripts handle ALL message processing -- both creating and
parsing messages. The server doesn't "relay" messages blindly; the C engine
handles network routing, but the Python scripts on the host are responsible
for:

1. **Creating** messages (InitNetwork, ObjectKilledHandler, RestartGameHandler)
2. **Processing** messages (ProcessMessageHandler)
3. **Forwarding** messages to other clients (TEAM_MESSAGE forwarding in Mission2/3/5)

In the dedicated server case (OpenBC), the server IS the host. So:
- The server MUST understand all message types it creates (init, score, end)
- The server MUST understand messages it receives from clients (team selection)
- Custom mod messages follow the same pattern

### Pattern for custom mod messages

Mods define custom message types by adding `App.MAX_MESSAGE_TYPES + N` with
higher N values. The pattern is always:
1. Create TGMessage, set guaranteed
2. Create TGBufferStream, open buffer
3. Write message type byte as first field
4. Write payload data
5. Send via TGNetwork

The server processes these in `ProcessMessageHandler` by reading the type byte
and dispatching. The server does NOT need to parse messages it doesn't handle --
unrecognized type bytes are simply ignored (they fall through the if/elif chain).

### TEAM_MESSAGE forwarding pattern

Mission2/3/5 have a specific pattern where the HOST must forward TEAM_MESSAGE
to other clients:

```python
if (App.g_kUtopiaModule.IsHost()):
    pCopyMessage = pMessage.Copy()
    pNetwork.SendTGMessageToGroup("NoMe", pCopyMessage)
```

This is a server-side relay responsibility. Custom mods may use similar
forwarding patterns. The key APIs needed:
- `pMessage.Copy()` -- TGMessage.Copy()
- `pNetwork.SendTGMessageToGroup("NoMe", pMessage)` -- broadcast to all except sender

---

## 7. Gameplay Stubs That Break Mods

### DedicatedServer.py stub analysis

The DedicatedServer.py stubs these categories of functions:

**GUI functions (safe to stub):**
- `LoadBridge.CreateCharacterMenus` and friends -- bridge UI only
- `MultiplayerMenus.*` -- all functions stubbed (it's all lobby UI)
- `Mission1Menus.BuildMission1Menus` -- ship select UI
- `RebuildPlayerList`, `RebuildInfoPane`, `RebuildShipPane` -- UI updates
- `ConfigureTeamPane`, `CreateInfoPane`, etc. -- UI construction

**Gameplay-critical functions that DedicatedServer.py wraps (NOT stubs):**
- `MissionShared.SetupEventHandlers` -- wrapped in try/except, NOT stubbed
- `MissionShared.Initialize` -- wrapped in try/except, NOT stubbed
- `Mission1.SetupEventHandlers` -- wrapped in try/except, NOT stubbed
- `NewPlayerHandler`, `DeletePlayerHandler`, `StartGame` -- wrapped, NOT stubbed

### Potential problems in the stub list

1. **`MissionLib.SetupFriendlyFireNoGameOver`**: DedicatedServer.py stubs this
   if it doesn't exist. In vanilla, this function registers friendly fire
   tracking. If stubbed, friendly fire scoring won't work correctly. However,
   the function may not exist in all BC versions.

2. **`MissionLib.LoadDatabaseSoundInGroup`**: Stubbed if missing. This loads
   sound data which is not needed on a headless server, but
   `MissionShared.Initialize()` calls `pGame.LoadDatabaseSoundInGroup()` which
   is a C-side function on the Game object. If the C implementation crashes on
   a headless server (no audio device), this needs to be a no-op in OpenBC.

3. **`SortedRegionMenu_GetWarpButton()`**: Returns None on headless. The
   `MissionShared.SetupEventHandlers()` calls this and tries to
   `AddPythonFuncHandlerForInstance` on the result. This crashes with
   AttributeError if None. The DedicatedServer.py wraps the entire
   SetupEventHandlers in try/except, which catches this but may also
   swallow legitimate errors.

4. **`App.STTargetMenu_GetTargetMenu()`**: Called by `MissionShared.ClearShips()`.
   Returns None on headless. The code does check for None, so this is safe.

5. **`App.TopWindow_GetTopWindow()`**: Called by `RestartGame()` in all missions
   to hide chat window. Will crash on headless server during game restart.
   DedicatedServer.py does NOT specifically handle this.

6. **`App.MultiplayerWindow_Cast()`**: Called alongside TopWindow in
   RestartGame(). Same crash risk.

7. **`App.SubtitleAction_CreateC()`**: Called by `DoKillSubtitle()` in all
   missions. This creates a visual subtitle. On headless server, should
   return a no-op action object or None that won't crash when
   `SetDuration()` and `Play()` are called.

8. **`DynamicMusic.PlayFanfare()`**: Called by `GetWinString()`. Client-only.
   On headless, this should no-op.

### Functions that ARE gameplay-critical and must NOT be stubbed

These functions run on the HOST and affect game state:

| Function | Purpose | Must Work |
|----------|---------|-----------|
| `MissionShared.Initialize()` | Loads TGL databases, registers event handlers | YES |
| `MissionShared.SetupEventHandlers()` | Registers network message handler, scan handler | YES (except warp button) |
| `MissionShared.EndGame()` | Sends END_GAME_MESSAGE to all clients | YES |
| `MissionShared.ClearShips()` | Removes all player ships from game | YES |
| `MissionShared.CreateTimeLeftTimer()` | Starts time limit countdown | YES |
| `MissionShared.UpdateTimeLeftHandler()` | Decrements timer, triggers EndGame | YES |
| `Mission1.SetupEventHandlers()` | Registers scoring handlers (ET_WEAPON_HIT, ET_OBJECT_EXPLODING) | YES |
| `Mission1.InitNetwork()` | Sends game config to joining players | YES |
| `Mission1.ProcessMessageHandler()` | Processes all network messages | YES |
| `Mission1.DamageHandler()` | Records damage for scoring | YES |
| `Mission1.ObjectKilledHandler()` | Awards kills/deaths, sends score updates | YES |
| `Mission1.NewPlayerHandler()` | Initializes score dictionaries for new players | YES |
| `Mission1.ResetEnemyFriendlyGroups()` | Sets up targeting groups | YES (affects AI targeting) |
| `Mission5.CreateStarbase()` | Creates the AI-controlled starbase (host only) | YES |

### Specific OpenBC recommendations

For OpenBC, instead of the try/except wrapping approach used by DedicatedServer.py,
the clean solution is:

1. `SortedRegionMenu_GetWarpButton()` should return a dummy object on headless
   that accepts `AddPythonFuncHandlerForInstance()` as a no-op
2. `SubtitleAction_CreateC()` should return a dummy action on headless
3. `DynamicMusic.PlayFanfare()` should no-op on headless
4. `LoadDatabaseSoundInGroup()` should no-op on headless
5. `TopWindow_GetTopWindow()` should return a dummy window chain on headless
6. All actual event handler registrations must proceed normally

---

## 8. Revised Mod Compatibility Matrix

### Full API surface for gameplay (server-side, host role)

**Event types needed (server-side):**
- ET_OBJECT_EXPLODING (scoring)
- ET_WEAPON_HIT (damage tracking)
- ET_NEW_PLAYER_IN_GAME (player management)
- ET_NETWORK_DELETE_PLAYER (player management)
- ET_OBJECT_CREATED_NOTIFY (group management)
- ET_NETWORK_MESSAGE_EVENT (network protocol)
- ET_NETWORK_NAME_CHANGE_EVENT (UI update -- can no-op on server)
- ET_OBJECT_DESTROYED (Mission5 starbase cleanup)
- ET_SCAN (client-only, safe to no-op on server)
- ET_START (game state transition)
- ET_WARP_BUTTON_PRESSED (client-only, safe to no-op)

**Ship/Object API (server-side):**
- ShipClass_Create, ShipClass_Cast
- ShipClass methods: GetNetType, SetNetType, GetNetPlayerID, GetObjID, GetName,
  IsPlayerShip, IsDying, IsDead, IsTypeOf, GetPropertySet, SetScript,
  SetupProperties, UpdateNodeOnly, SetupModel, GetContainingSet, SetTranslate,
  RandomOrientation, GetRadius, DisableCollisionDamage, SetAI
- DamageableObject: GetDamage (on event), IsHullHit, GetFiringPlayerID,
  GetDestination
- Torpedo_Create

**Multiplayer API (server-side):**
- MultiplayerGame_Cast, GetShipFromPlayerID, DeletePlayerShipsAndTorps,
  DeleteObjectFromGame, SetReadyForNewPlayers, IsReadyForNewPlayers,
  GetMaxPlayers
- TGNetwork: GetNetwork, GetHostID, GetLocalID, GetNumPlayers, IsHost,
  GetPlayerList, SendTGMessage, SendTGMessageToGroup, GetConnectStatus,
  SetConnectionTimeout, SetName
- PlayerList: GetNumPlayers, GetPlayerAtIndex, GetPlayer
- Player: GetNetID, GetName
- TGMessage_Create, SetGuaranteed, SetDataFromStream, GetBufferStream, Copy
- TGBufferStream: OpenBuffer, CloseBuffer, Close, ReadChar, ReadInt, ReadLong,
  WriteChar, WriteInt, WriteLong

**Mission/Game API:**
- Game_GetCurrentGame, Game_GetCurrentEpisode, Game_LoadEpisode, Game_SetDifficulty
- Episode_GetCurrentMission, Episode_LoadMission
- MissionLib.GetMission, pMission.GetScript, pMission.GetEnemyGroup,
  pMission.GetFriendlyGroup, pMission.AddPythonFuncHandlerForInstance
- ObjectGroupWithInfo: AddName, RemoveName, RemoveAllNames, IsNameInGroup
- ObjectGroup_FromModule

**AI API (Mission5 only):**
- ConditionalAI_Create, ConditionScript_Create
- AI.Compound.StarbaseAttack (module import)
- ArtificialIntelligence.US_ACTIVE, US_DORMANT, US_DONE
- pAI.SetInterruptable, SetContainedAI, AddCondition, SetEvaluationFunction

**System/Set API:**
- SetClass_Create, SetClass methods: SetRegionModule, SetProximityManagerActive,
  IsLocationEmptyTG, GetProximityManager
- SetManager: AddSet, GetSet, DeleteSet
- ProximityManager: UpdateObject, SetPlayerCollisionsEnabled,
  SetMultiplayerPlayerCollisionsEnabled

**Timer API:**
- MissionLib.CreateTimer -> g_kTimerManager
- g_kTimerManager.DeleteTimer

**Localization:**
- g_kLocalizationManager.Load, Unload
- Database.GetString().GetCString()

**Utility:**
- TGPoint3: SetXYZ, GetX/Y/Z, Set, Add
- TGString (new_TGString)
- IsNull
- g_kSystemWrapper.GetRandomNumber
- g_kVarManager.MakeEpisodeEventType

**Constants:**
- MAX_MESSAGE_TYPES, NULL_ID, CT_SHIP
- TGNETWORK_CONNECTED, TGNETWORK_CONNECT_IN_PROGRESS, TGNETWORK_INVALID_ID
- All SPECIES_* constants (30+ vanilla, more with mods)
- All ET_* event type constants

### Compatibility ratings

| Mod Category | Rating | Notes |
|--------------|--------|-------|
| **Vanilla ships** | WORKS | All ship/hardpoint scripts use the same 19 Property_Create + Set* API. Server loads hardpoints for damage model. |
| **Custom ships (standalone)** | PARTIAL | Works IF: (1) ship/hardpoint files present on server, (2) kSpeciesTuple entries added, (3) matching SPECIES_* constants exist. Breaks if: species ID exceeds table bounds, or ship script uses APIs not in vanilla hardpoints. |
| **Custom ships (Foundation)** | PARTIAL | Foundation's __import__ hook must work under Python 3.x compat layer. Foundation's ship registry must load before game start. Foundation's plugin system (Custom/Autoload/) must execute. Risk: Foundation's import hook may conflict with OpenBC's own module loading. |
| **Custom weapons** | WORKS | Weapon properties are loaded from hardpoints using the same Property API. Custom torpedo types need SpeciesToTorp entries. The damage simulation runs in C and uses loaded properties -- no extra Python API needed beyond what vanilla hardpoints use. |
| **Custom game modes** | PARTIAL | Custom missions follow identical structural patterns to vanilla missions. They work IF: (1) they only use APIs present in vanilla missions, (2) their MissionXMenus functions are properly stubbed on headless server, (3) any custom AI they create uses the same AI API as StarbaseAI. Risk: missions that add novel gameplay (custom objects, special effects, physics) may need additional API. |
| **KM rebalance** | PARTIAL | KM depends on Foundation. KM modifies Modifier.py table, ship stats, and adds game modes. Works IF Foundation works and modifier table dimensions match ship class range. KM's multiplayer rebalancing is primarily stat changes in hardpoints -- same API. |

### Detailed breakdown by risk level

**HIGH CONFIDENCE (will work):**
- Vanilla ship definitions (all 46 ships)
- Vanilla multiplayer missions (1/2/3/5)
- Score tracking and broadcasting
- Player join/leave handling
- Time/frag limit enforcement
- Game restart
- Weapon damage from vanilla hardpoints
- Modifier table lookups (vanilla 3x3)

**MEDIUM CONFIDENCE (likely works with minor fixes):**
- Custom ships with standalone kSpeciesTuple entries
- Custom weapon properties in hardpoints
- Custom torpedo types with SpeciesToTorp entries
- Extended Modifier tables (need bounds checking)
- Custom game modes using standard mission patterns
- AI-controlled NPCs (Mission5 starbase pattern)

**LOW CONFIDENCE (needs investigation):**
- Foundation Technologies framework on Python 3.x
- Foundation's __import__ hook vs OpenBC's own module system
- Foundation's dynamic species ID allocation
- KM's extended ship class system
- Custom missions with novel gameplay mechanics
- Mods that monkey-patch C-side API functions

**WILL NOT WORK (needs specific support):**
- Mods that use DLL injection or direct memory manipulation
- Mods that require the rendering pipeline for gameplay (rare)
- NanoFX (visual effects only, irrelevant to server)
- Mods that depend on specific Python 1.5 quirks that the compat shim doesn't cover

---

## Key Architecture Insights for OpenBC Implementation

### 1. The server IS the damage authority

The host computes all damage. The `pEvent.GetDamage()` value comes from the
C engine's weapon simulation running on the host. The Python scripts then
use this value for scoring with modifiers applied on top. This means OpenBC
must implement the actual damage simulation, not just relay events.

### 2. Ship hardpoints are needed for game physics, not just display

Even on a headless server, the full property system must be loaded because:
- Shield HP values determine when shields drop
- Hull HP determines when ships die (triggering ET_OBJECT_EXPLODING)
- Weapon damage values determine how much damage each hit does
- Engine properties affect movement simulation

### 3. Custom messages are safe -- they self-describe

All custom messages start with a type byte. Unrecognized types fall through
the if/elif chain harmlessly. The server only needs to handle messages
it creates or processes. Client-to-server messages it doesn't understand
are simply ignored.

### 4. The GUI stub approach is fragile but workable

DedicatedServer.py's approach of wrapping event handlers in try/except is
fragile because it may swallow gameplay-critical errors. OpenBC should
instead provide proper headless stubs that return appropriate dummy objects
rather than None, preventing the crashes at the source.

### 5. Mission4 is missing from the vanilla game

There is no Mission4 in the vanilla mission set. The numbering goes 1, 2, 3, 5.
MissionShared.py references Mission6, Mission7, Mission9 as potential mod slots.
DedicatedServer.py only patches Mission1, Mission2, Mission3, Mission5.

---

## Additional Server-Side API Functions Identified (Beyond Phase 1 Lobby)

These APIs were not in the lobby-only Phase 1 analysis but are needed for gameplay:

### Scoring/Damage
- `pEvent.GetDamage()` -- WeaponHitEvent
- `pEvent.IsHullHit()` -- WeaponHitEvent
- `pEvent.GetFiringPlayerID()` -- WeaponHitEvent and ObjectExplodingEvent
- `pEvent.GetDestination()` -- all events
- `pEvent.GetMessage()` -- NetworkMessageEvent
- `pEvent.GetPlayerID()` -- NewPlayerEvent
- `pEvent.GetSource()` -- general
- `pEvent.GetInt()` -- ScanEvent (client-only)
- `pKilledObject.IsTypeOf(App.CT_SHIP)` -- type checking

### Ship Operations
- `pShip.GetNetType()` -- species lookup
- `pShip.GetNetPlayerID()` -- player ID for ship
- `pShip.GetObjID()` -- unique object ID
- `pShip.IsPlayerShip()` -- is this a player-controlled ship?
- `pShip.IsDying()` -- death state check
- `pShip.IsDead()` -- death state check
- `pShip.DisableCollisionDamage()` -- Mission5 starbase
- `pShip.RandomOrientation()` -- Mission5 starbase placement
- `pShip.SetTranslate()` -- position setting
- `pShip.SetAI()` -- AI assignment (Mission5)
- `pShip.GetContainingSet()` -- get the set this ship is in
- `pShip.GetRadius()` -- for placement collision avoidance

### Multiplayer Game
- `pGame.GetShipFromPlayerID(iPlayerID)` -- critical for scoring
- `pGame.DeletePlayerShipsAndTorps()` -- game restart
- `pGame.DeleteObjectFromGame(pObj)` -- Mission5 starbase cleanup
- `pGame.GetPlayer()` -- get local player (may be None on dedicated)
- `pGame.LoadDatabaseSoundInGroup()` -- needs headless stub

### Network
- `pMessage.Copy()` -- for message forwarding
- `pNetwork.SendTGMessageToGroup("NoMe", pMessage)` -- broadcast
- `pNetwork.GetConnectStatus()` -- connection state check
- `pPlayerList.GetPlayer(iPlayerID)` -- individual player lookup
- `pPlayer.GetName().GetCString()` -- player name

### Mission/Group
- `pMission.GetEnemyGroup()` -- enemy targeting group
- `pMission.GetFriendlyGroup()` -- friendly targeting group
- `pMission.GetScript()` -- mission script name
- `pGroup.AddName()`, `RemoveName()`, `RemoveAllNames()` -- group management
- `App.ObjectGroupWithInfo()` -- constructor
- `App.ObjectGroup_FromModule()` -- cross-module reference

### Set Operations
- `pSet.IsLocationEmptyTG(kLocation, fRadius, 1)` -- collision check for placement
- `pSet.GetProximityManager()` -- proximity tracking
- `pSet.GetName()` -- set name

### AI (Mission5 only)
- `App.ConditionalAI_Create(pShip, name)` -- AI creation
- `App.ConditionScript_Create(script, name, ...)` -- AI conditions
- `pAI.SetInterruptable()`, `SetContainedAI()`, `AddCondition()`, `SetEvaluationFunction()` -- AI config
- `App.ArtificialIntelligence.US_ACTIVE`, `US_DORMANT`, `US_DONE` -- AI states

### Utility
- `App.g_kSystemWrapper.GetRandomNumber(N)` -- random number generation
- `App.TGSequence_Create()` -- needed for subtitles (can be no-op on server)
- `App.SubtitleAction_CreateC()` -- can be no-op on server
- `App.TGSoundAction_Create()` -- can be no-op on server
- `App.TGScriptAction_Create()` -- can be no-op on server

---

## Estimated API Count Update

Previous estimate (lobby only): ~298 functions + 124 constants + 29 singletons = ~451 symbols

Gameplay additions: ~60 new function/method signatures identified above

**Revised estimate for Phase 1 with full gameplay: ~360 functions + 130 constants + 29 singletons = ~519 symbols**

With Foundation support: ~420-470 functions.
With Foundation + KM: ~500-550 functions.
