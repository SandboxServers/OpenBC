# Multiplayer Gameplay Script Analysis for Phase 1 Dedicated Server

## 1. Complete Script Execution Chain

### Startup Flow (Server + Client)
```
MultiplayerGame.py::Initialize(pGame)
  -> LoadTacticalSounds.LoadSounds()              [CLIENT-ONLY: audio]
  -> pGame.LoadSound(...)                          [CLIENT-ONLY: audio]
  -> SetupMusic(pGame) -> DynamicMusic.Initialize  [CLIENT-ONLY: music]
  -> App.g_kSetManager.ClearRenderedSet()          [SERVER: noop OK]
  -> pGame.LoadEpisode("Multiplayer.Episode.Episode")
     -> Episode.py::Initialize(pEpisode)
        -> App.g_kVarManager.GetStringVariable("Multiplayer", "Mission")
        -> pEpisode.LoadMission(pcMissionScript, pMissionStartEvent)
           -> Mission1.py::Initialize(pMission)      [or Mission2/3/5]
              -> MissionShared.Initialize(pMission)
                 -> import Multiplayer.MultiplayerMenus  [SERVER: stub all]
                 -> import LoadBridge                     [SERVER: stub all]
                 -> import MissionLib
                 -> LoadBridge.CreateCharacterMenus()     [SERVER: noop]
                 -> MissionLib.SetupFriendlyFireNoGameOver()
                 -> App.g_kLocalizationManager.Load(...)  [3 TGL databases]
                 -> SetupEventHandlers(pMission)
                    -> AddBroadcastPythonFuncHandler(ET_NETWORK_MESSAGE_EVENT, ...)  [CRITICAL]
                    -> AddBroadcastPythonFuncHandler(ET_SCAN, ...)                   [CLIENT-ONLY]
                    -> App.SortedRegionMenu_GetWarpButton()                          [SERVER: returns None -> crash]
                 -> Multiplayer.MultiplayerMenus.g_bExitPressed = 0                 [SERVER: needs stub]
                 -> pGame.LoadDatabaseSoundInGroup(...)                              [SERVER: noop]
              -> Mission1Menus.BuildMission1Menus()   [HOST-ONLY, SERVER: noop]
              -> SetupEventHandlers(pMission)          [CRITICAL: registers event handlers]
                 -> ET_OBJECT_EXPLODING handler (HOST-ONLY: scoring)
                 -> ET_WEAPON_HIT handler (HOST-ONLY: damage tracking)
                 -> ET_NEW_PLAYER_IN_GAME handler
                 -> ET_NETWORK_DELETE_PLAYER handler
                 -> ET_OBJECT_CREATED_NOTIFY handler
                 -> ET_NETWORK_NAME_CHANGE_EVENT handler
                 -> ET_NETWORK_MESSAGE_EVENT handler   [CRITICAL: protocol messages]
                 -> ET_RESTART_GAME handler
```

### Gameplay Event Flow (Server-Critical)

**When a player joins:**
```
NewPlayerHandler(TGObject, pEvent)
  -> g_kKillsDictionary[iPlayerID] = 0
  -> g_kDeathsDictionary[iPlayerID] = 0
  -> Mission1Menus.RebuildPlayerList()   [SERVER: noop]

InitNetwork(iToID)                        [HOST sends state to new player]
  -> Sends MISSION_INIT_MESSAGE (system, limits, time)
  -> Sends SCORE_MESSAGE for each player in kills/deaths/scores dicts
```

**When damage occurs (HOST-ONLY):**
```
DamageEventHandler -> DamageHandler
  -> import Multiplayer.Modifier
  -> import Multiplayer.SpeciesToShip
  -> Tracks damage in g_kDamageDictionary[shipObjID][hitterPlayerID]
  -> Applies Modifier.GetModifier() class-based scaling
```

**When a ship is destroyed (HOST-ONLY):**
```
ObjectKilledHandler
  -> Updates g_kKillsDictionary, g_kDeathsDictionary, g_kScoresDictionary
  -> Sends SCORE_CHANGE_MESSAGE to all clients
  -> CheckFragLimit() -> may call EndGame()
```

**When network message arrives:**
```
ProcessMessageHandler
  -> Reads message type from stream
  -> MISSION_INIT_MESSAGE: set limits, create system, build menus
  -> SCORE_CHANGE_MESSAGE: update local score dicts, UpdateScore()
  -> SCORE_MESSAGE: set score dict entries directly
  -> RESTART_GAME_MESSAGE: RestartGame()
  -> (Mission2/3/5 also handle TEAM_MESSAGE, TEAM_SCORE_MESSAGE, SCORE_INIT_MESSAGE)
```

**When game ends:**
```
EndGame(iReason)                     [MissionShared.py, HOST-ONLY]
  -> Creates TGMessage with END_GAME_MESSAGE
  -> Sends to all players
  -> Sets ReadyForNewPlayers(0)

MissionShared.ProcessMessageHandler  [ALL: receives END_GAME_MESSAGE]
  -> Sets g_bGameOver = 1
  -> ClearShips()
  -> MissionMenusShared.DoEndGameDialog()  [CLIENT-ONLY: UI]
```

**Time limit handling:**
```
CreateTimeLeftTimer(iTimeLeft)        [MissionShared.py]
  -> MissionLib.CreateTimer(ET_UPDATE_TIME_LEFT, ...)
  -> UpdateTimeLeftHandler called periodically
     -> Decrements g_iTimeLeft
     -> If reaches 0 and IsHost(): EndGame(END_TIME_UP)
     -> MissionMenusShared.UpdateTimeLeftDisplay()  [CLIENT-ONLY: UI]
```

### Complete Import Graph (Server-Relevant)

```
Mission1.py
  +-- App
  +-- loadspacehelper
  +-- MissionLib
  +-- Multiplayer.MissionShared
  |     +-- App
  |     +-- MissionLib
  |     +-- Multiplayer.MultiplayerMenus  [STUB for server]
  |     +-- LoadBridge                     [STUB for server]
  +-- Mission1Menus                        [STUB for server]
  |     +-- App, UIHelpers, MissionLib
  |     +-- Mission1 (circular - module-level ref)
  |     +-- MainMenu.mainmenu             [STUB for server]
  |     +-- Multiplayer.MissionShared
  |     +-- Multiplayer.MissionMenusShared
  |     +-- Multiplayer.SpeciesToSystem
  |     +-- Multiplayer.SpeciesToShip
  |     +-- Multiplayer.MultiplayerMenus
  +-- Multiplayer.MissionMenusShared      [server reads globals only]
  |     +-- App
  |     +-- MainMenu.mainmenu             [STUB for server]
  |     +-- MissionLib
  |     +-- Multiplayer.MissionShared
  +-- Multiplayer.SpeciesToSystem
  |     +-- App
  |     +-- Systems.MultiN.MultiN (dynamic __import__)
  +-- Multiplayer.SpeciesToShip
  |     +-- App
  |     +-- ships.X (dynamic __import__)
  |     +-- ships.Hardpoints.X (dynamic __import__)
  +-- Multiplayer.Modifier
  |     +-- App
  +-- Multiplayer.SpeciesToTorp
  |     +-- App
  |     +-- Tactical.Projectiles.X (dynamic __import__)
  +-- DynamicMusic                         [CLIENT-ONLY]
```

### Mission Variants

| Mission | Type | Extra Features |
|---------|------|----------------|
| Mission1 | Deathmatch (FFA) | Score = damage/10 + kills |
| Mission2 | Team Deathmatch | 2 teams, team scores, TEAM_MESSAGE protocol |
| Mission3 | Team DM (Fed vs Non-Fed) | Same as M2 but fixed team names |
| Mission5 | Starbase Assault | Starbase AI, attackers/defenders, StarbaseAI.py |

All missions share the same base structure. Mission2/3/5 add:
- `g_kTeamDictionary`, `g_kTeamScoreDictionary`, `g_kTeamKillsDictionary`
- Extra message types: `TEAM_MESSAGE`, `TEAM_SCORE_MESSAGE`, `SCORE_INIT_MESSAGE`
- `IsSameTeam()` function for friendly fire detection
- Mission5 uniquely adds `StarbaseAI.py`, `g_pStarbase`, `g_pAttackerGroup`


## 2. Python 1.5.2 Constructs in Gameplay Scripts

### dict.has_key() -- 193 occurrences across 8 files
**This is the most used 1.5.2 construct.** Every scoring dictionary check uses it.
Examples:
```python
g_kKillsDictionary.has_key(iFiringPlayerID)
g_kDamageDictionary.has_key(iShipID)
pDamageByDict.has_key(iHitterID)
g_kTeamDictionary.has_key(iPlayerID)
```
**Shim requirement:** `dict.has_key = lambda self, key: key in self`

### chr()/ord() for network protocol -- 103 occurrences across 9 files
Used extensively in message serialization:
```python
kStream.WriteChar(chr(Multiplayer.MissionShared.MISSION_INIT_MESSAGE))
cType = ord(kStream.ReadChar())
kStream.WriteChar(chr(255))
```
**In Python 3.x:** `chr()` and `ord()` work the same for values 0-127.
For values 128-255, the behavior depends on string encoding.
`kStream.ReadChar()` returns a single-char string in BC's Python 1.5.2.
In Python 3.x, if this returns `bytes`, `ord()` would work differently.
**CRITICAL:** The App/Appc `ReadChar`/`WriteChar` implementation must return
`str` (not `bytes`) for `ord()` to work without source transforms.

### list.sort(cmp_func) -- 11 occurrences
**BREAKING CHANGE in Python 3.x:** `list.sort()` no longer accepts a `cmp` parameter.
```python
pSortList.sort(ComparePlayer)
pSortList.sort(Mission1Menus.ComparePlayer)
pTeamScoresList.sort(CompareTeams)
```
The `ComparePlayer` functions return -1/0/1 (classic cmp-style).
**Shim requirement:** Must provide `functools.cmp_to_key` wrapper OR
monkey-patch `list.sort` to detect cmp-style callable and auto-wrap:
```python
_orig_sort = list.sort
def _compat_sort(self, *args, **kwargs):
    if args and not kwargs.get('key') and callable(args[0]):
        import functools
        kwargs['key'] = functools.cmp_to_key(args[0])
        args = args[1:]
    _orig_sort(self, *args, **kwargs)
list.sort = _compat_sort
```

### print statements (commented out) -- all commented with `#`
All `print` statements in the Multiplayer scripts are COMMENTED OUT.
Examples: `#print ("Not bRebuild")`, `#print "Rebuilding..."`
**No source transform needed** for these specific scripts, but the shim
must handle them anyway because:
1. DedicatedServer.py uses `print "text"` (no parens) -- 3 active uses
2. Mod scripts may have active print statements

### reload() -- 2 occurrences in SpeciesToShip.py
```python
reload(mod)  # After __import__("ships.Hardpoints." + ...)
```
**Shim requirement:** `builtins.reload = importlib.reload`

### __import__() with Python 1.5 semantics -- 17 occurrences
Python 1.5 `__import__('A.B.C')` returns module A (top-level).
Python 3.x `__import__('A.B.C')` also returns top-level by default.
BUT: the scripts then do `pModule.CreateMenus()` expecting the attributes
from the deepest submodule. This works in 1.5 because `__import__` populates
`sys.modules` and sets parent attributes.
Key dynamic imports:
```python
__import__("Systems." + pcScript + "." + pcScript)    # SpeciesToSystem
__import__("ships." + pcScript)                         # SpeciesToShip
__import__("ships.Hardpoints." + kStats['HardpointFile'])
__import__("Tactical.Projectiles." + pcScript)          # SpeciesToTorp
__import__(pcMissionName)                               # MissionShared (dynamic mission load)
__import__(MissionLib.GetMission().GetScript() + "Menus")
```
**Shim requirement:** Import hook must ensure parent package attributes are set.
This is exactly what DedicatedServer.py's `_ds_safe_import` does.

### No except-with-comma in gameplay scripts
`except Exception, e:` does NOT appear in any Multiplayer/ reference scripts.
It DOES appear in DedicatedServer.py (8 occurrences), but that's our code.

### No apply() in gameplay scripts
Not used in any Multiplayer/ scripts. Only in App.py (SWIG wrapper).

### No string module usage in gameplay scripts
All string operations use `%` formatting and method calls on string objects.


## 3. DedicatedServer.py Headless Mode Patching

### Import Hook (`_ds_safe_import`)
Intercepts all imports and:
1. Fixes parent package submodule attributes (sets `pkg.submod = sys.modules['pkg.submod']`)
2. Fixes `fromlist` resolution (ensures `from X import Y` works)
3. Patches mission module handlers when a Mission1/2/3/5 module is imported

### Event Handlers Wrapped via func_code Replacement
These handlers are called by the C event system which holds direct function references:
- `NewPlayerHandler` -- player joins game
- `DeletePlayerHandler` -- player leaves game
- `StartGame` -- game start event

### GUI Functions Replaced with No-ops
These are called from within handler functions and access UI widgets:

**In mission modules (Mission1/2/3/5):**
- `RebuildPlayerList` -- scoreboard UI
- `RebuildInfoPane` -- player info UI
- `RebuildShipPane` -- ship selection UI
- `RebuildTeamPane` -- team selection UI (Mission2/3/5)
- `RebuildReadyButton` -- ready button state
- `UpdateShipList` -- ship list UI
- `UpdatePlayerList` -- player list UI
- `ConfigureTeamPane` -- team pane setup
- `ConfigureInfoPane` -- info pane setup
- `ConfigureShipPane` -- ship pane setup
- `ConfigureReadyButton` -- ready button setup
- `CreateInfoPane` -- create info pane
- `CreateShipPane` -- create ship selection pane
- `CreateTeamPane` -- create team selection pane

**In LoadBridge:**
- `CreateCharacterMenus`, `CreateMenus`, `CreateBridgeMenus`
- `CreateScienceMenus`, `CreateEngineeringMenus`
- `CreateCommunicationsMenus`, `CreateTacticalMenus`
- `CreateHelmMenus`, `CreateBridge`, `CreateCharacterMenuBitmaps`

**In MultiplayerMenus:**
- ALL functions (entire module is UI code)

**In Mission1Menus (and M2/3/5 Menus):**
- `BuildMission1Menus`, `RebuildPlayerList`, `RebuildInfoPane`
- `RebuildShipPane`, `ConfigureTeamPane`, `CreateMenus`
- `BuildShipSelect`, `BuildTeamSelect`

**In MissionLib (if missing):**
- `SetupFriendlyFireNoGameOver`
- `SetupFriendlyFire`
- `LoadDatabaseSoundInGroup`

### Functions Wrapped with try/except
- `MissionShared.SetupEventHandlers` -- crashes on `SortedRegionMenu_GetWarpButton()` returning None
- `MissionShared.Initialize` -- crashes at line ~173 accessing MultiplayerMenus GUI globals
- `Mission1.SetupEventHandlers` -- may crash on None GUI widget handlers
- `Mission1.InitNetwork` -- replaced entirely with functional-API version

### InitNetwork Complete Replacement
`Mission1.InitNetwork` is REPLACED (not wrapped) with a version that uses
`Appc` functional API (e.g., `Appc.TGMessage_Create()`, `Appc.TGMessage_SetGuaranteed()`)
instead of shadow class methods (`pMessage.SetGuaranteed(1)`), because
in headless mode `App.TGMessage_Create()` returns a raw SWIG pointer string,
not a shadow class instance.


## 4. Module Globals as Server State

### MissionMenusShared.py -- Central Configuration Store
These globals are READ by mission scripts during gameplay:

| Global | Type | Purpose | Set By |
|--------|------|---------|--------|
| `g_iSystem` | int | Star system index (1-9) | Host menu / DedicatedServer config |
| `g_iTimeLimit` | int | -1 or minutes | Host menu / DedicatedServer config |
| `g_iFragLimit` | int | -1 or kill count | Host menu / DedicatedServer config |
| `g_iPlayerLimit` | int | Max players (2-8) | Host menu / DedicatedServer config |
| `g_bGameStarted` | int | 0/1 flag | Mission1Menus.StartMission() |
| `g_bShipSelectState` | int | 0/1 flag | ShowShipSelectScreen()/FinishedSelect |
| `g_iUseScoreLimit` | int | 0/1 flag | BuildMissionXMenus() |
| `g_iSpecies` | int | Ship species | SelectSpecies() |
| `g_pChosenSpecies` | obj/None | UI button ref | SelectSpeciesHandler() |
| `g_pChosenSystem` | obj/None | UI button ref | SelectSystemHandler() |
| `g_bAllowNoTimeLimit` | int | 0/1 flag | Reset in Terminate() |

**Server impact:** These must be pre-populated before `Mission*.Initialize()`.
DedicatedServer.py does this explicitly:
```python
mms.g_iSystem = SERVER_SYSTEM
mms.g_iTimeLimit = SERVER_TIME_LIMIT
mms.g_iFragLimit = SERVER_FRAG_LIMIT
mms.g_iPlayerLimit = SERVER_PLAYER_LIMIT
mms.g_iUseScoreLimit = 0
```

### MissionShared.py -- Runtime Game State

| Global | Type | Purpose |
|--------|------|---------|
| `g_pStartingSet` | SetClass/None | The active star system Set |
| `g_pDatabase` | TGLocalizationDB | Multiplayer.tgl strings |
| `g_pShipDatabase` | TGLocalizationDB | Ships.tgl strings |
| `g_pSystemDatabase` | TGLocalizationDB | Systems.tgl strings |
| `g_bGameOver` | int | 0/1 game over flag |
| `g_iTimeLeft` | float | Seconds remaining |
| `g_idTimeLeftTimer` | int | Timer object ID |

### MissionN.py -- Per-Mission Scoring State

| Global | Type | Purpose |
|--------|------|---------|
| `g_kKillsDictionary` | dict{playerID: int} | Kill counts |
| `g_kDeathsDictionary` | dict{playerID: int} | Death counts |
| `g_kScoresDictionary` | dict{playerID: int} | Score (damage/10) |
| `g_kDamageDictionary` | dict{shipObjID: dict{playerID: [shield,hull]}} | Damage tracking |
| `g_kTeamDictionary` | dict{playerID: int} | Team assignment (M2/3/5) |
| `g_kTeamScoreDictionary` | dict{teamID: int} | Team scores (M2/3/5) |
| `g_kTeamKillsDictionary` | dict{teamID: int} | Team kills (M2/3/5) |
| `g_pStarbase` | ShipClass/None | Starbase reference (M5 only) |
| `g_pAttackerGroup` | ObjectGroupWithInfo | Attacker group (M5 only) |

**Server design implication:** All game state lives in Python module globals.
The server must preserve these across the game session. Module reloading
would reset them. Restart is handled by `RestartGame()` which zeroes dicts
but keeps dict keys (preserving player registration).


## 5. Dynamic Module Loading During Gameplay

### __import__() calls during active gameplay:

1. **`__import__(pcMissionName)` in MissionShared.ProcessMessageHandler**
   Used when receiving END_GAME_MESSAGE for Mission5/7/9 to set
   `g_bStarbaseDead`/`g_bBorgDead`/`g_bEnterpriseDead`.
   `pcMissionName` comes from `MissionLib.GetMission().GetScript()`.

2. **`__import__("Systems." + pcScript + "." + pcScript)` in SpeciesToSystem**
   Called when creating the star system. On server, this loads the system
   module which calls `CreateMenus()` and `InitializeAllSets()`.
   **Headless concern:** System scripts may reference visual objects.
   DedicatedServer.py bypasses this entirely with raw Appc calls.

3. **`__import__("ships." + pcScript)` in SpeciesToShip**
   Called to get ship stats when creating/initializing player ships.
   Also loads `ships.Hardpoints.X` for hardpoint configuration.
   **Headless concern:** Ship scripts may load models/sounds.
   The server needs these for physics but not for rendering.

4. **`__import__(MissionLib.GetMission().GetScript() + "Menus")` in MissionMenusShared**
   Called for `PostRebuildAfterResChange` -- pure UI, never fires on server.

5. **`__import__(App.GraphicsModeInfo_GetCurrentMode().GetLcarsModule())` in MultiplayerMenus**
   Pure UI, never fires on server.

### Modules that would fail without the import hook:
- `Multiplayer.MultiplayerMenus` -- entire module is UI, crashes on import
  at module level because it accesses `App.TopWindow_GetTopWindow()` etc.
- `LoadBridge` -- similar UI dependencies
- `MainMenu.mainmenu` -- UI globals
- `DynamicMusic` -- audio system
- Any `Systems.X.X` module -- may reference visual objects
- Any `ships.X` module -- may call `App.ShipClass_Create()` needing model paths


## 6. func_code Replacement -- Python 3.x Migration

### The Problem
DedicatedServer.py uses Python 1.5.2's `func_code` attribute to replace
a function's code in-place, because the event system holds direct references
to function objects. Simply replacing the module attribute doesn't work.

```python
# Python 1.5.2 (current)
import new
copy_fn = new.function(orig_fn.func_code, orig_fn.func_globals, 'name')
orig_fn.func_code = wrapper.func_code
orig_fn.func_defaults = wrapper.func_defaults
```

### Python 3.x Equivalents
| 1.5.2 Attribute | 3.x Attribute | Notes |
|-----------------|---------------|-------|
| `func_code` | `__code__` | Direct rename |
| `func_defaults` | `__defaults__` | Direct rename |
| `func_globals` | `__globals__` | Direct rename (read-only in 3.x!) |
| `func_name` | `__name__` | Direct rename |
| `new.function(code, globals, name)` | `types.FunctionType(code, globals, name)` | Module change |

### Cleanest Migration Strategy

**Option A: Compat shim adds attribute aliases (RECOMMENDED)**
```python
# In compatibility_shim.py
import types

# Make func_code/func_defaults/func_globals work as aliases
# Python 3.x function objects have __code__, __defaults__, __globals__
# Add 1.5.2-style aliases:
_orig_getattr = types.FunctionType.__getattribute__
_orig_setattr = types.FunctionType.__setattr__  # Doesn't exist by default

# Can't easily monkey-patch built-in types in Python 3.x
# Instead, handle in source transform or in the import hook
```

**Option B: Source transform (more reliable)**
The import hook's source transformer replaces:
```
func_code   -> __code__
func_defaults -> __defaults__
func_globals  -> __globals__
```
This is safe because these attribute names are unique to function objects.

**Option C: OpenBC's DedicatedServer rewrite (BEST for our code)**
Since DedicatedServer.py is OUR code (not a game script), we rewrite it
for Python 3.x directly. The shim only needs to handle `func_code` etc.
for third-party mod scripts that might use these patterns.

### Important: `__globals__` is read-only in Python 3.x
```python
orig_fn.__globals__  # Works (read)
orig_fn.__globals__ = new_globals  # FAILS in Python 3.x
```
The `func_globals` attribute was writable in 1.5.2 but `__globals__` is
read-only in 3.x. If any script tries to SET `func_globals`, the shim
must handle this specially (create new function with `types.FunctionType`).

### Replacing `import new` with `types`
```python
# 1.5.2:  import new; new.function(code, globals, name)
# 3.x:    import types; types.FunctionType(code, globals, name)
```
The shim should provide a `new` module compat:
```python
# new_compat.py
import types
def function(code, globals, name='', argdefs=None):
    f = types.FunctionType(code, globals, name)
    if argdefs:
        f.__defaults__ = argdefs
    return f
instancemethod = ... # Not needed for gameplay scripts
```


## 7. Server API Requirements Summary

### App Module Functions Used by Gameplay Scripts (Server-Critical)

**Global singletons:**
- `App.g_kUtopiaModule` -> `.IsHost()`, `.IsClient()`, `.GetNetwork()`, `.GetGameTime()`, `.GetRealTime()`, `.SetFriendlyFireWarningPoints()`
- `App.g_kEventManager` -> `.AddBroadcastPythonFuncHandler()`, `.AddEvent()`, `.RemoveAllBroadcastHandlersForObject()`
- `App.g_kVarManager` -> `.MakeEpisodeEventType()`, `.GetStringVariable()`
- `App.g_kTimerManager` -> `.DeleteTimer()`
- `App.g_kSetManager` -> `.ClearRenderedSet()`, `.DeleteAllSets()`, `.AddSet()`
- `App.g_kLocalizationManager` -> `.Load()`, `.Unload()`
- `App.g_kModelPropertyManager` -> `.ClearLocalTemplates()`
- `App.g_kSystemWrapper` -> `.GetRandomNumber()`

**Game/Network:**
- `App.Game_GetCurrentGame()` -> `.GetPlayer()`, `.LoadEpisode()`, `.GetCurrentEpisode()`, `.LoadDatabaseSoundInGroup()`
- `App.MultiplayerGame_Cast()` -> `.SetReadyForNewPlayers()`, `.IsReadyForNewPlayers()`, `.GetShipFromPlayerID()`, `.SetPlayer()`, `.DeletePlayerShipsAndTorps()`, `.DeleteObjectFromGame()`
- `App.TGNetwork` -> `.GetHostID()`, `.GetLocalID()`, `.GetNumPlayers()`, `.IsHost()`, `.GetPlayerList()`, `.SendTGMessage()`, `.SendTGMessageToGroup()`, `.GetConnectStatus()`, `.GetCName()`
- `App.TGPlayerList` -> `.GetNumPlayers()`, `.GetPlayerAtIndex()`, `.GetPlayer()`

**Events/Messages:**
- `App.TGEvent_Create()`, `App.TGMessage_Create()`
- `App.TGBufferStream()` -> `.OpenBuffer()`, `.CloseBuffer()`, `.ReadChar()`, `.ReadInt()`, `.ReadLong()`, `.WriteChar()`, `.WriteInt()`, `.WriteLong()`, `.Close()`
- Event types: `ET_NETWORK_MESSAGE_EVENT`, `ET_OBJECT_EXPLODING`, `ET_WEAPON_HIT`, `ET_NEW_PLAYER_IN_GAME`, `ET_NETWORK_DELETE_PLAYER`, `ET_OBJECT_CREATED_NOTIFY`, `ET_NETWORK_NAME_CHANGE_EVENT`, `ET_OBJECT_DESTROYED`, `ET_DELETE_OBJECT_PUBLIC`, `ET_WARP_BUTTON_PRESSED`, `ET_MISSION_START`, `ET_START`, `ET_SCAN`, `ET_END_GAME_OKAY`, `ET_PLAYER_BOOT_EVENT`

**Ship/Object:**
- `App.ShipClass_Cast()`, `App.ShipClass_Create()`
- Ship methods: `.IsPlayerShip()`, `.GetNetPlayerID()`, `.GetNetType()`, `.SetNetType()`, `.GetObjID()`, `.GetName()`, `.IsDying()`, `.IsDead()`, `.SetNetPlayerID()`, `.RandomOrientation()`, `.UpdateNodeOnly()`, `.SetTranslate()`, `.GetRadius()`, `.SetScript()`, `.GetPropertySet()`, `.SetupProperties()`, `.GetTorpedoSystem()`, `.DisableCollisionDamage()`, `.SetAI()`, `.GetContainingSet()`
- `App.BaseObjectClass_GetObject()`
- `App.IsNull()`

**Mission/Episode:**
- `App.Mission` -> `.AddPythonFuncHandlerForInstance()`, `.GetEnemyGroup()`, `.GetFriendlyGroup()`, `.GetScript()`
- `MissionLib.GetMission()`, `MissionLib.CreateTimer()`
- `App.ObjectGroupWithInfo()` -> `.AddName()`, `.RemoveName()`, `.RemoveAllNames()`, `.IsNameInGroup()`

**Constants:**
- `App.MAX_MESSAGE_TYPES`, `App.NULL_ID`
- `App.TGNETWORK_CONNECTED`, `App.TGNETWORK_CONNECT_IN_PROGRESS`
- `App.CT_SHIP`
- Species constants: `App.SPECIES_AKIRA`, `App.SPECIES_AMBASSADOR`, etc.
- `App.TGNetwork.TGNETWORK_INVALID_ID`

**Localization:**
- `App.TGString()`, `App.new_TGString()`
- TGLocalizationDB -> `.GetString()` -> `.GetCString()`


## 8. Compat Shim Requirements Checklist for Phase 1

| Construct | Count | Shim Method | Priority |
|-----------|-------|-------------|----------|
| `dict.has_key()` | 193 | Runtime: monkey-patch dict | P0 |
| `list.sort(cmp_func)` | 11 | Runtime: monkey-patch list.sort | P0 |
| `chr()`/`ord()` | 103 | Ensure App returns str not bytes | P0 |
| `reload(mod)` | 2 | Runtime: builtins.reload | P1 |
| `__import__()` 1.5 semantics | 17 | Import hook: fix pkg attrs | P0 |
| `func_code`/`func_defaults` | DS only | Source transform OR rewrite DS | P1 |
| `import new` | DS only | Provide new_compat module | P1 |
| `import strop` | DS only | Provide strop_compat module | P1 |
| `print "text"` | DS only | Source transform (if not rewritten) | P2 |
| `except E, e:` | DS only | Source transform (if not rewritten) | P2 |
| `cmp()` builtin | 0 direct | Runtime: builtins.cmp (for sort compat) | P1 |

### Key Design Decision
DedicatedServer.py is our own code and should be rewritten for Python 3.x.
The gameplay scripts (Multiplayer/*) need the shim but are simpler -- their
main 1.5.2 constructs are `has_key()`, `sort(cmp)`, and `__import__()` semantics.
