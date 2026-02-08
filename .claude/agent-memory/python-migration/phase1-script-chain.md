# Phase 1 Server Script Dependency Chain

## Boot sequence (C engine calls)
1. Engine initializes Python, registers App/Appc modules
2. Engine calls into UtopiaModule which loads scripts
3. For dedicated server: Multiplayer flow starts

## Script dependency graph (server path)

### Core Framework (always loaded)
- App.py (SWIG wrapper, 14K lines) -> imports Appc, new
- string.py (1.5.2 version) -> imports strop, re
- copy_reg.py (pickle helper)

### Server Boot
- Autoexec.py -> App, cPickle, sys, FontsAndIcons(?), Tactical.TacticalIcons(?), UITheme(?), LoadInterface(?)
  NOTE: Autoexec likely needs a server-specific version or conditional paths

### Multiplayer Flow
- Multiplayer/__init__.py (empty marker)
- Multiplayer/MultiplayerMenus.py -> App, Tactical.Tactical, UIHelpers, MainMenu.mainmenu, MissionLib
- Multiplayer/MultiplayerGame.py -> App, LoadTacticalSounds, DynamicMusic, Multiplayer.MultiplayerMenus
- Multiplayer/MissionShared.py -> App, MissionMenusShared, MissionLib
- Multiplayer/MissionMenusShared.py -> App (huge UI file)
- Multiplayer/SpeciesToShip.py -> App, ships.*
- Multiplayer/SpeciesToSystem.py -> App, Systems.*
- Multiplayer/SpeciesToTorp.py -> App, Tactical.Projectiles.*
- Multiplayer/Modifier.py -> App

### Episode/Mission
- Multiplayer/Episode/__init__.py
- Multiplayer/Episode/Episode.py -> App
- Multiplayer/Episode/Mission1/Mission1.py -> App, loadspacehelper, MissionLib
- Multiplayer/Episode/Mission1/Mission1Menus.py -> App, MissionLib
- (Mission2, Mission3, Mission5 follow same pattern)

### Support
- MissionLib.py -> App, loadspacehelper, Bridge.* handlers
- loadspacehelper.py -> App, imp, Actions.EffectScriptActions

## App Functions Used by Server Scripts (sampled)
- App.g_kUtopiaModule.IsHost(), .IsClient(), .GetNetwork(), .GetRealTime(), .GetGameTime()
- App.g_kUtopiaModule.SetFriendlyFireWarningPoints()
- App.g_kEventManager.AddBroadcastPythonFuncHandler()
- App.g_kTimerManager.AddTimer(), .DeleteTimer()
- App.g_kVarManager.MakeEpisodeEventType(), .GetStringVariable()
- App.g_kSetManager.ClearRenderedSet(), .DeleteAllSets(), .GetAllSets()
- App.g_kLocalizationManager.Load(), .Unload()
- App.g_kModelPropertyManager.ClearLocalTemplates()
- App.TGMessage_Create(), App.TGBufferStream(), App.TGEvent_Create()
- App.TGTimer_Create(), App.TGSequence_Create()
- App.ShipClass_Create(), App.ShipClass_Cast()
- App.MultiplayerGame_Cast(), App.Game_GetCurrentGame()
- App.Torpedo_Create(), App.ProximityCheck_Create()
- App.IsNull(), App.Game_GetNextEventType()
- App.SortedRegionMenu_GetWarpButton()
- App.TopWindow_GetTopWindow(), App.MultiplayerWindow_Cast()

## Constants Used by Server Scripts
ET_NETWORK_MESSAGE_EVENT, ET_SCAN, ET_WARP_BUTTON_PRESSED, ET_OBJECT_EXPLODING,
ET_WEAPON_HIT, ET_NEW_PLAYER_IN_GAME, ET_NETWORK_DELETE_PLAYER,
ET_OBJECT_CREATED_NOTIFY, ET_NETWORK_NAME_CHANGE_EVENT, ET_MISSION_START,
MAX_MESSAGE_TYPES, NULL_ID, CT_SHIP, CT_OBJECT, TGNETWORK_CONNECTED,
TGNETWORK_CONNECT_IN_PROGRESS, SPECIES_* (all ship species constants)
