# SWIG API Compatibility Agent Memory

## Key Reference Locations
- **App.py**: `/mnt/c/users/Steve/source/projects/STBC-Dedicated-Server/reference/scripts/App.py` (~14,069 lines)
- **swig-api.md**: `/mnt/c/users/Steve/source/projects/STBC-Dedicated-Server/docs/swig-api.md`
- **Decompiled code**: `/mnt/c/users/Steve/source/projects/STBC-Dedicated-Server/reference/decompiled/`
- **MP scripts**: `/mnt/c/users/Steve/source/projects/STBC-Dedicated-Server/reference/scripts/Multiplayer/`

## API Catalogs
- **Lobby-only catalog**: [phase1-api-catalog.md](phase1-api-catalog.md) (~310 functions, ~280 constants)
- **Gameplay expansion**: [gameplay-api-expansion.md](gameplay-api-expansion.md) (~285 new functions, ~65 new constants)
- **Combined playable server**: ~595 functions, ~345 constants
  - Must Implement: ~330 | Stub OK: ~190 | Not Needed: ~75

## Architecture Insight: Peer-to-Peer Relay
- BC multiplayer is P2P RELAY: server relays messages, runs scoring scripts
- Server does NOT simulate physics, AI, combat, rendering, or audio
- Ship objects exist as data containers on server (name, ID, player ID, net type)
- Hardpoint properties must EXIST but don't need to drive simulation
- Generic Set* stub mechanism viable: accept any Set* call as no-op

## SWIG 1.x Binding Patterns (Verified from App.py)
- Classes in App.py wrap `Appc` flat C functions
- Pattern: `ClassName.Method = new.instancemethod(Appc.ClassName_Method, None, ClassName)`
- Constructors: `self.this = apply(Appc.new_ClassName, args)` with `self.thisown = 1`
- Ptr classes: `ClassNamePtr(val)` sets `self.thisown = 0` and `self.__class__ = ClassName`
- Cast pattern wraps return in `ClassNamePtr(val)`

## Class Hierarchy (Server-Relevant, Expanded)
```
TGObject -> TGAttrObject -> TGTemplatedAttrObject -> TGEventHandlerObject
  TGEventHandlerObject -> TGNetwork -> TGWinsockNetwork
  TGEventHandlerObject -> ScriptObject -> Game -> MultiplayerGame
  TGEventHandlerObject -> SetClass
  TGEventHandlerObject -> VarManagerClass
  TGEventHandlerObject -> BaseObjectClass -> ObjectClass -> PhysicsObjectClass -> DamageableObject -> ShipClass
  TGEventHandlerObject -> ObjectGroup -> ObjectGroupWithInfo
TGObject -> TGEvent -> TGBoolEvent, TGIntEvent, TGFloatEvent, TGStringEvent, TGMessageEvent, TGPlayerEvent
TGObject -> TGTimer
TGObject -> TGAction -> TGSequence, TGScriptAction, TGSoundAction, SubtitleAction
NiPoint3 -> TGPoint3
NiColorA -> TGColorA
PhysicsObjectClass + WeaponPayload -> Torpedo
```

## Key Discoveries
- `Mission.GetEnemyGroup()` / `GetFriendlyGroup()` are C++ native, NOT in App.py shadow class, confirmed in decompiled code (utopia_app.c:4899-4900). Must expose via Appc.
- ~200 property Set* methods across 16 property types can be generic no-ops (store in dict or ignore)
- `NULL_ID = Appc.NULL_ID` (line 13183), `MAX_MESSAGE_TYPES = Appc.MAX_MESSAGE_TYPES` (line 14069)
- DamageableObject.__del__ calls delete_TGEventHandlerObject (not its own destructor) -- SWIG pattern
- TGScriptAction_Create takes variable args: (module, func, *extra_args_passed_to_callback)

## Globals Needed for Phase 1
- `g_kUtopiaModule`, `g_kEventManager`, `g_kVarManager`, `g_kConfigMapping`
- `g_kTimerManager`, `g_kRealtimeTimerManager`, `g_kSetManager`
- `g_kLocalizationManager`, `g_kSystemWrapper`, `g_kModelPropertyManager`

## Open Questions (Gameplay)
1. How does dedicated server receive combat events (ET_WEAPON_HIT, ET_OBJECT_EXPLODING)?
2. Do hardpoint Set* methods need value storage, or are they write-only on server?
3. Ship object lifecycle: created on join? On spawn? Per-death/respawn?
4. TGSequence/TGScriptAction execution: needs tick mechanism in server game loop

## Server-Side Scripts (Phase 1)
- Multiplayer/MultiplayerGame.py, MissionShared.py, MissionMenusShared.py
- Multiplayer/SpeciesToShip.py (CRITICAL), SpeciesToTorp.py, SpeciesToSystem.py
- Multiplayer/Episode/Mission1-5/ (scoring, game flow)
- MissionLib.py, loadspacehelper.py, Modifier.py
