# Gameplay API Expansion: Lobby-Only -> Playable Dedicated Server

## Architecture Context

BC multiplayer is **PEER-TO-PEER RELAY**. The server:
- Relays TGMessages between clients (clients run physics/rendering/combat locally)
- Runs Python mission scripts for **scoring, game flow, and team management**
- Creates ship objects as **data containers** (not simulated) for player tracking
- Creates AI ships only in Mission5 (coop starbase) which DO need some server-side behavior
- Broadcasts events (ET_OBJECT_EXPLODING, ET_WEAPON_HIT) that scripts listen to for scoring

The server does NOT: simulate physics, run AI for player ships, render anything, play audio.

## Delta Summary

| Category | Lobby-Only | Gameplay Expansion | Combined Total |
|----------|-----------|-------------------|----------------|
| **Must Implement** | ~185 | ~145 | ~330 |
| **Stub OK** | ~80 | ~110 | ~190 |
| **Not Needed** | ~45 | ~30 | ~75 |
| **Constants (new)** | ~280 | ~65 | ~345 |
| **Total Functions** | ~310 | ~285 | ~595 |

### New Categories Not In Lobby-Only Catalog
1. **Ship/Object Lifecycle** - ShipClass_Create, SetupModel, SetupProperties, property system
2. **Combat Events** - ET_WEAPON_HIT, ET_OBJECT_EXPLODING, damage/firing player queries
3. **Scoring Infrastructure** - GetFiringPlayerID, GetDamage, IsHullHit, GetNetPlayerID
4. **Object Group Management** - ObjectGroup, ObjectGroupWithInfo (enemy/friendly groups)
5. **Property System** - 16 property types with Create functions (hardpoint loading)
6. **Ship Object Hierarchy** - BaseObjectClass -> ObjectClass -> PhysicsObjectClass -> DamageableObject -> ShipClass
7. **Torpedo Lifecycle** - Torpedo_Create, torpedo property system
8. **Action/Sequence System** - TGAction, TGSequence, TGScriptAction, TGSoundAction, SubtitleAction
9. **3D Math Types** - TGPoint3/NiPoint3, TGColorA (for property configuration)
10. **Proximity/Spatial** - ProximityManager, SetClass.IsLocationEmptyTG (Mission5 starbase placement)
11. **Difficulty System** - Game_GetDifficulty, Game_GetOffensiveDifficultyMultiplier
12. **Model Property Manager** - TGModelPropertyManager, TGModelPropertySet

---

## NEW FUNCTIONS BY CATEGORY

### Category 1: Ship Object Hierarchy (~55 functions) - MUST IMPLEMENT

These form the inheritance chain: BaseObjectClass -> ObjectClass -> PhysicsObjectClass -> DamageableObject -> ShipClass.
The server needs ships as data containers with identity (name, ID, player ID, net type) and basic spatial data.

#### BaseObjectClass (App.py line 3794) - inherits TGEventHandlerObject
| Function | Classification | Notes |
|----------|---------------|-------|
| `BaseObjectClass_GetName(self)` -> str | MUST IMPLEMENT | Used by scoring scripts to identify ships |
| `BaseObjectClass_SetName(self, name)` | MUST IMPLEMENT | Set ship name |
| `BaseObjectClass_GetDisplayName(self)` -> TGStringPtr | STUB OK | UI display name |
| `BaseObjectClass_SetDisplayName(self, TGString)` | STUB OK | UI display name |
| `BaseObjectClass_GetContainingSet(self)` -> SetClassPtr | MUST IMPLEMENT | Mission5 uses to get set for starbase placement |
| `BaseObjectClass_Update(self)` | STUB OK | Rendering update, server no-op |
| `BaseObjectClass_UpdateNodeOnly(self)` | MUST IMPLEMENT | Called after ship creation, updates internal state |
| `BaseObjectClass_SetTranslate(self, point)` | MUST IMPLEMENT | Mission5 starbase placement |
| `BaseObjectClass_SetTranslateXYZ(self, x, y, z)` | MUST IMPLEMENT | Position setting |
| `BaseObjectClass_SetHidden(self, flag)` | STUB OK | Rendering visibility |
| `BaseObjectClass_IsHidden(self)` -> int | STUB OK | Returns 0 |
| `BaseObjectClass_SetDeleteMe(self)` | MUST IMPLEMENT | Object deletion request |
| `BaseObjectClass_Rotate(self, ...)` | STUB OK | Server doesn't need rotation rendering |
| `BaseObjectClass_SetAngleAxisRotation(self, ...)` | STUB OK | |
| `BaseObjectClass_SetMatrixRotation(self, ...)` | STUB OK | |
| `BaseObjectClass_SetScale(self, scale)` | STUB OK | |
| `BaseObjectClass_GetScale(self)` -> float | STUB OK | Returns 1.0 |
| `BaseObjectClass_AlignToVectors(self, ...)` | STUB OK | |
| `BaseObjectClass_GetWorldLocation(self)` -> TGPoint3Ptr | MUST IMPLEMENT | Spatial queries for starbase placement |
| `BaseObjectClass_GetTranslate(self)` -> TGPoint3Ptr | MUST IMPLEMENT | Position queries |
| `BaseObjectClass_GetRotation(self)` -> TGMatrix3Ptr | STUB OK | |
| `BaseObjectClass_GetWorldRotation(self)` -> TGMatrix3Ptr | STUB OK | |
| `BaseObjectClass_GetWorldForwardTG(self)` -> TGPoint3Ptr | STUB OK | |
| `BaseObjectClass_AttachObject(self, ...)` | STUB OK | |
| `BaseObjectClass_DetachObject(self, ...)` | STUB OK | |

#### ObjectClass (App.py line 3889) - inherits BaseObjectClass
| Function | Classification | Notes |
|----------|---------------|-------|
| `ObjectClass_GetRadius(self)` -> float | MUST IMPLEMENT | Mission5 uses for starbase placement spacing |
| `ObjectClass_RandomOrientation(self)` | MUST IMPLEMENT | Mission5 calls on starbase |
| `ObjectClass_PlaceObjectByName(self, name)` | STUB OK | |
| `ObjectClass_IsTargetable(self)` -> int | STUB OK | |
| `ObjectClass_CanTargetObject(self, obj)` | STUB OK | |
| `ObjectClass_SetHailable(self, flag)` | STUB OK | |
| `ObjectClass_SetScannable(self, flag)` | STUB OK | |
| `ObjectClass_SetCollisionFlags(self, flags)` | STUB OK | |
| `ObjectClass_GetCollisionFlags(self)` -> int | STUB OK | |
| Constants: `CFB_NO_COLLISIONS`, `CFB_IN_PROXIMITY_MANAGER`, `CFB_DEFAULTS`, etc. | MUST DEFINE | |

#### PhysicsObjectClass (App.py line 5238) - inherits ObjectClass
| Function | Classification | Notes |
|----------|---------------|-------|
| `PhysicsObjectClass_SetupModel(self, name)` | MUST IMPLEMENT | Ship creation pipeline - loads model/hardpoints |
| `PhysicsObjectClass_SetNetType(self, type)` | MUST IMPLEMENT | Set species type for network identification |
| `PhysicsObjectClass_GetNetType(self)` -> int | MUST IMPLEMENT | Used by scoring to identify ship type |
| `PhysicsObjectClass_SetAI(self, ai)` | MUST IMPLEMENT | Mission5 sets AI on starbase |
| `PhysicsObjectClass_ClearAI(self)` | STUB OK | |
| `PhysicsObjectClass_GetAI(self)` -> ArtificialIntelligencePtr | STUB OK | Returns None |
| `PhysicsObjectClass_SetStatic(self, flag)` | STUB OK | |
| `PhysicsObjectClass_IsStatic(self)` -> int | STUB OK | |
| `PhysicsObjectClass_SetDoNetUpdate(self, flag)` | STUB OK | |
| `PhysicsObjectClass_IsDoingNetUpdate(self)` -> int | STUB OK | |
| `PhysicsObjectClass_SetUsePhysics(self, flag)` | STUB OK | Server doesn't simulate physics |
| `PhysicsObjectClass_IsUsingPhysics(self)` -> int | STUB OK | |
| `PhysicsObjectClass_SetVelocity(self, ...)` | STUB OK | |
| `PhysicsObjectClass_SetAngularVelocity(self, ...)` | STUB OK | |
| `PhysicsObjectClass_SetAcceleration(self, ...)` | STUB OK | |
| `PhysicsObjectClass_GetVelocityTG(self)` -> TGPoint3Ptr | STUB OK | |
| `PhysicsObjectClass_GetMass(self)` -> float | STUB OK | |
| `PhysicsObjectClass_SetMass(self, mass)` | STUB OK | |
| Constants: `DIRECTION_MODEL_SPACE`, `DIRECTION_WORLD_SPACE` | MUST DEFINE | |

#### DamageableObject (App.py line 5326) - inherits PhysicsObjectClass
| Function | Classification | Notes |
|----------|---------------|-------|
| `DamageableObject_GetPropertySet(self)` -> TGModelPropertySetPtr | MUST IMPLEMENT | Ship creation: hardpoint property loading |
| `DamageableObject_SetupProperties(self)` | MUST IMPLEMENT | Ship creation: finalizes hardpoint setup |
| `DamageableObject_IsDying(self)` -> int | MUST IMPLEMENT | Mission5 checks before deleting starbase |
| `DamageableObject_IsDead(self)` -> int | MUST IMPLEMENT | Mission scripts check death state |
| `DamageableObject_SetDead(self)` | STUB OK | |
| `DamageableObject_SetLifeTime(self, time)` | STUB OK | |
| `DamageableObject_GetLifeTime(self)` -> float | STUB OK | |
| `DamageableObject_SetCollisionsOn(self, flag)` | STUB OK | |
| `DamageableObject_CanCollide(self)` -> int | STUB OK | |
| `DamageableObject_SetSplashDamage(self, ...)` | STUB OK | |
| `DamageableObject_SetVisibleDamageRadiusModifier(self, ...)` | STUB OK | Rendering only |
| `DamageableObject_DisableGlowAlphaMaps(self)` | STUB OK | Rendering only |

#### ShipClass (App.py line 5370) - inherits DamageableObject
| Function | Classification | Notes |
|----------|---------------|-------|
| `ShipClass_Create(name)` -> ShipClassPtr | MUST IMPLEMENT | Static factory - creates ship objects |
| `ShipClass_Cast(obj)` -> ShipClassPtr | MUST IMPLEMENT | Cast from event source to ShipClass |
| `ShipClass_SetScript(self, module)` | MUST IMPLEMENT | Ship creation pipeline |
| `ShipClass_GetScript(self)` -> str | MUST IMPLEMENT | Script module lookup |
| `ShipClass_SetNetPlayerID(self, id)` | MUST IMPLEMENT | Associate ship with player |
| `ShipClass_GetNetPlayerID(self)` -> int | MUST IMPLEMENT | Scoring: identify which player's ship |
| `ShipClass_IsPlayerShip(self)` -> int | MUST IMPLEMENT | Scoring: distinguish player vs NPC ships |
| `ShipClass_DisableCollisionDamage(self, flag)` | MUST IMPLEMENT | Mission5: starbase creation |
| `ShipClass_IsCollisionDamageDisabled(self)` -> int | STUB OK | |
| `ShipClass_SetInvincible(self, flag)` | STUB OK | |
| `ShipClass_IsInvincible(self)` -> int | STUB OK | |
| `ShipClass_SetHurtable(self, flag)` | STUB OK | |
| `ShipClass_IsHurtable(self)` -> int | STUB OK | |
| `ShipClass_SetTargetable(self, flag)` | STUB OK | |
| `ShipClass_IsTargetable(self)` -> int | STUB OK | |
| `ShipClass_GetHull(self)` -> HullClassPtr | STUB OK | Returns None on server |
| `ShipClass_GetShields(self)` -> ShieldClassPtr | STUB OK | Returns None on server |
| `ShipClass_GetAffiliation(self)` -> int | STUB OK | |
| `ShipClass_SetAffiliation(self, affil)` | STUB OK | |
| `ShipClass_GetAlertLevel(self)` -> int | STUB OK | |
| `ShipClass_SetAlertLevel(self, level)` | STUB OK | |
| `ShipClass_SetDeathScript(self, script)` | STUB OK | |
| `ShipClass_GetDeathScript(self)` -> str | STUB OK | |
| `ShipClass_SetTarget(self, obj)` | STUB OK | Client-side targeting |
| `ShipClass_StopFiringWeapons(self)` | STUB OK | |
| `ShipClass_CompleteStop(self)` | STUB OK | |
| `ShipClass_AddSubsystem(self, subsystem)` | MUST IMPLEMENT | Ship creation: hardpoint setup may call this |
| `ShipClass_IncrementAIDoneIgnore(self)` | STUB OK | |
| `ShipClass_GetShipProperty(self)` -> ShipPropertyPtr | STUB OK | |
| Constants: `WG_INVALID`, `WG_PRIMARY`, `WG_SECONDARY`, `WG_TERTIARY`, `WG_TRACTOR` | MUST DEFINE | |
| Constants: `GREEN_ALERT`, `YELLOW_ALERT`, `RED_ALERT` | MUST DEFINE | |

---

### Category 2: Combat/Gameplay Events (~15 functions) - MUST IMPLEMENT

These event types and event accessor methods are needed for scoring scripts.

#### New Event Types (Constants)
| Constant | Classification | Used By |
|----------|---------------|---------|
| `ET_OBJECT_EXPLODING` | MUST DEFINE | Mission1-5: ObjectKilledHandler |
| `ET_WEAPON_HIT` | MUST DEFINE | Mission1-5: DamageEventHandler |
| `ET_NEW_PLAYER_IN_GAME` | MUST DEFINE | Mission1-5: NewPlayerHandler |
| `ET_OBJECT_CREATED_NOTIFY` | MUST DEFINE | Mission1-5: ObjectCreatedHandler |
| `ET_OBJECT_DESTROYED` | MUST DEFINE | Mission5: ObjectDestroyedHandler (starbase) |
| `ET_SCAN` | MUST DEFINE | MissionShared: ScanHandler |
| `ET_WARP_BUTTON_PRESSED` | MUST DEFINE | MissionShared: WarpHandler |
| `ET_DELETE_OBJECT_PUBLIC` | MUST DEFINE | loadspacehelper: ship deletion event |
| `ET_FRIENDLY_FIRE_DAMAGE` | MUST DEFINE | MissionShared: friendly fire tracking |
| `ET_FRIENDLY_FIRE_REPORT` | MUST DEFINE | MissionShared |
| `ET_FRIENDLY_FIRE_GAME_OVER` | MUST DEFINE | MissionShared |
| `ET_INPUT_TOGGLE_SCORE_WINDOW` | MUST DEFINE | MissionMenusShared |
| `ET_INPUT_TOGGLE_CHAT_WINDOW` | MUST DEFINE | MissionMenusShared |

#### Event Accessor Methods (on event objects passed to handlers)
| Function | Classification | Notes |
|----------|---------------|-------|
| `TGEvent_GetFiringPlayerID(self)` -> int | MUST IMPLEMENT | ET_WEAPON_HIT event: who fired |
| `TGEvent_GetDamage(self)` -> float | MUST IMPLEMENT | ET_WEAPON_HIT event: how much damage |
| `TGEvent_IsHullHit(self)` -> int | MUST IMPLEMENT | ET_WEAPON_HIT event: hull vs shield |

**NOTE**: These methods (`GetFiringPlayerID`, `GetDamage`, `IsHullHit`) are NOT on the base TGEvent class. They are on a specialized event type (likely a WeaponHitEvent or similar C++ class). The mission scripts receive the event object from the event handler callback and call these methods directly. The server must provide event objects with these methods populated. In practice, these events are generated by the C++ combat system on clients and relayed as TGMessages -- the server may receive them as deserialized events.

**BEHAVIORAL QUESTION**: Does the server generate these events, or do clients send them? Analysis of the DamageEventHandler code shows it runs on the HOST (checked via `pUtopiaModule.IsHost()`). The host IS a client in normal BC -- it runs combat locally. For a dedicated server that doesn't simulate combat, we need to determine: do clients send damage/kill messages that the server then processes? Or does the server need to generate these events from relayed combat messages?

**ANSWER FROM SCRIPT ANALYSIS**: Looking at MissionShared.py, damage/kill events are processed on the HOST side only. In a peer-to-peer game, the host has a game client running combat. For a dedicated server, the EXISTING DedicatedServer.py mod works because all these handlers only run `if pUtopiaModule.IsHost()` -- and the dedicated server IS the host. BUT the dedicated server doesn't receive ET_WEAPON_HIT or ET_OBJECT_EXPLODING events from its own combat engine (it has none). Instead, these events must be dispatched by the message relay system when combat messages arrive from clients. The existing DedicatedServer.py mod appears to stub/skip actual combat event processing.

**IMPLEMENTATION STRATEGY**: For Phase 1, these event types must exist as constants, and the accessor methods must exist on event objects. However, since the server doesn't simulate combat, these handlers will either: (a) never fire (if clients process scoring locally and sync via messages), or (b) need a bridge from relayed combat messages to local events. Analysis of the network message types is needed to determine which approach.

---

### Category 3: Object Group Management (~12 functions) - MUST IMPLEMENT

Used by Mission1-5 for tracking enemy/friendly teams.

#### ObjectGroup (App.py line 5530) - inherits TGEventHandlerObject
| Function | Classification | Notes |
|----------|---------------|-------|
| `new_ObjectGroup(*args)` | MUST IMPLEMENT | Constructor |
| `delete_ObjectGroup(self)` | MUST IMPLEMENT | Destructor |
| `ObjectGroup_AddName(self, name)` | MUST IMPLEMENT | Add ship name to group |
| `ObjectGroup_RemoveName(self, name)` | MUST IMPLEMENT | Remove ship from group |
| `ObjectGroup_RemoveAllNames(self)` | MUST IMPLEMENT | Clear group |
| `ObjectGroup_IsNameInGroup(self, name)` -> int | MUST IMPLEMENT | Check group membership |
| `ObjectGroup_GetNumActiveObjects(self)` -> int | MUST IMPLEMENT | Count active members |
| `ObjectGroup_SetEventFlag(self, flag)` | STUB OK | Event notification flags |
| `ObjectGroup_ClearEventFlag(self, flag)` | STUB OK | |
| `ObjectGroup_IsEventFlagSet(self, flag)` -> int | STUB OK | Returns 0 |
| Constants: `GROUP_CHANGED`, `ENTERED_SET`, `EXITED_SET`, `DESTROYED` | MUST DEFINE | |

#### ObjectGroupWithInfo (App.py line 5560) - inherits ObjectGroup
| Function | Classification | Notes |
|----------|---------------|-------|
| `new_ObjectGroupWithInfo(*args)` | MUST IMPLEMENT | Mission5 creates these |
| `delete_ObjectGroupWithInfo(self)` | MUST IMPLEMENT | |

#### Mission.GetEnemyGroup / GetFriendlyGroup
| Function | Classification | Notes |
|----------|---------------|-------|
| `Mission_GetEnemyGroup(self)` -> ObjectGroupPtr | MUST IMPLEMENT | C++ native, NOT in App.py shadow, but in Appc. Confirmed in decompiled code. |
| `Mission_GetFriendlyGroup(self)` -> ObjectGroupPtr | MUST IMPLEMENT | Same. Both return ObjectGroup instances managed by Mission. |

---

### Category 4: Action/Sequence System (~20 functions) - MIXED

Used by MissionShared.py for game flow (end game sequences, announcements).

#### TGAction (App.py line 2370) - inherits TGObject
| Function | Classification | Notes |
|----------|---------------|-------|
| `TGAction_IsPlaying(self)` -> int | STUB OK | |
| `TGAction_Play(self)` | MUST IMPLEMENT | Needed by sequences |
| `TGAction_Completed(self)` | MUST IMPLEMENT | Marks action done |
| `TGAction_AddCompletedEvent(self, event)` | MUST IMPLEMENT | Chain actions |
| `TGAction_SetSkippable(self, flag)` | STUB OK | |
| `TGAction_IsSkippable(self)` -> int | STUB OK | |
| `TGAction_Skip(self)` | STUB OK | |
| `TGAction_Abort(self)` | STUB OK | |
| `TGAction_SetUseRealTime(self, flag)` | STUB OK | |
| `TGAction_GetSequence(self)` -> TGSequencePtr | STUB OK | |
| `TGAction_Cast(obj)` -> TGActionPtr | STUB OK | Static |
| `TGAction_CreateNull()` -> TGActionPtr | STUB OK | Static |

#### TGSequence (App.py line 2422) - inherits TGAction
| Function | Classification | Notes |
|----------|---------------|-------|
| `TGSequence_Create()` -> TGSequencePtr | MUST IMPLEMENT | Static factory |
| `TGSequence_Cast(obj)` -> TGSequencePtr | MUST IMPLEMENT | Static cast |
| `TGSequence_AddAction(self, action)` | MUST IMPLEMENT | Add action to sequence |
| `TGSequence_AppendAction(self, action)` | MUST IMPLEMENT | Append action |
| `TGSequence_Play(self)` | MUST IMPLEMENT | Execute sequence |
| `TGSequence_Skip(self)` | STUB OK | |
| `TGSequence_GetNumActions(self)` -> int | STUB OK | |
| `TGSequence_GetAction(self, idx)` -> TGActionPtr | STUB OK | |

#### TGScriptAction (App.py line 2445) - inherits TGAction
| Function | Classification | Notes |
|----------|---------------|-------|
| `TGScriptAction_Create(module, func, ...)` -> TGScriptActionPtr | MUST IMPLEMENT | Used by MissionShared, MissionLib for callbacks |

**NOTE**: TGScriptAction_Create takes variable args. From script usage:
- `App.TGScriptAction_Create("MissionShared", "EndGameHandler")` -- 2 args (module, function)
- The args after module/func are passed to the Python function when the action fires.

#### TGSoundAction (STUB OK for server)
| Function | Classification | Notes |
|----------|---------------|-------|
| `TGSoundAction_Create(name)` -> TGSoundActionPtr | STUB OK | Returns stub action, server plays no audio |

#### SubtitleAction (STUB OK for server)
| Function | Classification | Notes |
|----------|---------------|-------|
| `SubtitleAction_Create(db, name)` -> SubtitleActionPtr | STUB OK | Server has no subtitles |
| `SubtitleAction_CreateC(string)` -> SubtitleActionPtr | STUB OK | |
| `SubtitleAction.SetDuration(self, secs)` | STUB OK | |

---

### Category 5: Property System (~18 Create functions + ~200 Set* methods) - DATA CONTAINER

The property system is used by hardpoint files (e.g., sovereign.py) to configure ship subsystems.
Each ship has a TGModelPropertySet containing multiple property objects (hull, shields, phasers, etc.).
The server needs these to exist but may not need them to DO anything.

**KEY INSIGHT**: SpeciesToShip.InitObject() calls `__import__("ships.Hardpoints." + pcScript)` which
runs the hardpoint file (e.g., sovereign.py). The hardpoint file creates property objects, configures
them via many Set* calls, and registers them with g_kModelPropertyManager. Then SetupProperties()
on the ship reads these properties to configure subsystems.

For a relay server, the properties need to EXIST (so scripts don't crash) but don't need to
drive actual simulation. The Set* methods can be no-ops that store values but trigger no side effects.

#### Property Create Functions (all return PropertyPtr types)
| Function | Classification | Notes |
|----------|---------------|-------|
| `PhaserProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `HullProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `ShieldProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `SensorProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `PowerProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `ImpulseEngineProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `TorpedoSystemProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `TorpedoTubeProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `EngineProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `RepairSubsystemProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `ObjectEmitterProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `ShipProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `WeaponSystemProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `WarpEngineProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `TractorBeamProperty_Create(name)` | MUST IMPLEMENT | Data container |
| `PositionOrientationProperty_Create(name)` | MUST IMPLEMENT | Data container |

#### Property Set* Methods (~200 total across all property types)
| Category | Classification | Notes |
|----------|---------------|-------|
| All Set* methods on property objects | STUB OK | No-op, server doesn't simulate subsystems. Each property type has 10-30 Set* methods for configuring parameters like damage, range, fire rate, etc. These are called by hardpoint files. |

**IMPLEMENTATION APPROACH**: Create a generic property base class that accepts any Set*/Get* method
call as a no-op. Store values in a dict if needed for Get* calls, or just ignore them. This avoids
implementing ~200 individual methods.

#### TGModelPropertyManager (App.py line 2252)
| Function | Classification | Notes |
|----------|---------------|-------|
| `TGModelPropertyManager_RegisterLocalTemplate(self, prop)` | MUST IMPLEMENT | Hardpoint files register each property |
| `TGModelPropertyManager_RegisterGlobalTemplate(self, prop)` | STUB OK | |
| `TGModelPropertyManager_ClearLocalTemplates(self)` | MUST IMPLEMENT | Called before loading new hardpoint |
| `TGModelPropertyManager_ClearRegisteredFilters(self)` | STUB OK | |
| `TGModelPropertyManager_FindByNameAndType(self, name, type)` -> TGModelPropertyPtr | STUB OK | |
| `TGModelPropertyManager_FindByName(self, name)` -> TGModelPropertyPtr | STUB OK | |
| Constants: `GLOBAL_TEMPLATES`, `LOCAL_TEMPLATES` | MUST DEFINE | |

#### TGModelPropertySet (App.py line 2289)
| Function | Classification | Notes |
|----------|---------------|-------|
| `new_TGModelPropertySet(*args)` | MUST IMPLEMENT | Constructor |
| `delete_TGModelPropertySet(self)` | MUST IMPLEMENT | Destructor |
| `TGModelPropertySet_Create()` -> TGModelPropertySetPtr | MUST IMPLEMENT | Static factory |
| `TGModelPropertySet_GetPropertyList(self)` -> TGModelPropertyListPtr | STUB OK | |
| `TGModelPropertySet_GetPropertiesByType(self, type)` -> TGModelPropertyListPtr | STUB OK | |

---

### Category 6: Torpedo Lifecycle (~5 functions) - MUST IMPLEMENT

#### Torpedo (App.py line 5908) - inherits PhysicsObjectClass, WeaponPayload
| Function | Classification | Notes |
|----------|---------------|-------|
| `Torpedo_Create(name)` -> TorpedoPtr | MUST IMPLEMENT | SpeciesToTorp creates torpedoes |
| `Torpedo_GetModuleName(self)` -> str | STUB OK | |
| `Torpedo_SetDamage(self, dmg)` | STUB OK | Data container on server |
| `Torpedo_SetLifetime(self, time)` | STUB OK | |
| `Torpedo_SetParent(self, ship)` | STUB OK | |
| `Torpedo_SetTarget(self, obj)` | STUB OK | |
| `Torpedo_SetMaxAngularAccel(self, val)` | STUB OK | |
| `Torpedo_SetGuidanceLifetime(self, val)` | STUB OK | |
| `Torpedo_SetPlayerID(self, id)` | STUB OK | |
| `Torpedo_GetTargetID(self)` -> int | STUB OK | |
| `Torpedo_GetParentID(self)` -> int | STUB OK | |
| `Torpedo_GetLaunchSpeed(self)` -> float | STUB OK | |
| `Torpedo_CreateDisruptorModel(self)` | STUB OK | Rendering |
| `Torpedo_CreateTorpedoModel(self)` | STUB OK | Rendering |

---

### Category 7: 3D Math Types (~25 functions) - MUST IMPLEMENT

Used by Mission5 for starbase placement and by hardpoint files for property configuration.

#### NiPoint3 (App.py line 189) - base class
| Function | Classification | Notes |
|----------|---------------|-------|
| `new_NiPoint3(*args)` | MUST IMPLEMENT | Constructor |
| `delete_NiPoint3(self)` | MUST IMPLEMENT | Destructor |
| `NiPoint3_x_get/set` | MUST IMPLEMENT | Property accessor for x |
| `NiPoint3_y_get/set` | MUST IMPLEMENT | Property accessor for y |
| `NiPoint3_z_get/set` | MUST IMPLEMENT | Property accessor for z |
| `NiPoint3_Cross(self, other)` -> NiPoint3Ptr | STUB OK | |

#### TGPoint3 (App.py line 3382) - inherits NiPoint3
| Function | Classification | Notes |
|----------|---------------|-------|
| `new_TGPoint3(*args)` | MUST IMPLEMENT | Mission5 constructs TGPoint3 for positioning |
| `delete_TGPoint3(self)` | MUST IMPLEMENT | |
| `TGPoint3_GetX(self)` -> float | MUST IMPLEMENT | Mission5 starbase placement |
| `TGPoint3_GetY(self)` -> float | MUST IMPLEMENT | |
| `TGPoint3_GetZ(self)` -> float | MUST IMPLEMENT | |
| `TGPoint3_SetX(self, val)` | MUST IMPLEMENT | |
| `TGPoint3_SetY(self, val)` | MUST IMPLEMENT | |
| `TGPoint3_SetZ(self, val)` | MUST IMPLEMENT | |
| `TGPoint3_SetXYZ(self, x, y, z)` | MUST IMPLEMENT | |
| `TGPoint3_Set(self, other)` | MUST IMPLEMENT | Copy from another TGPoint3 |
| `TGPoint3_Add(self, other)` | MUST IMPLEMENT | Vector addition |
| `TGPoint3_Subtract(self, other)` | STUB OK | |
| `TGPoint3_Scale(self, factor)` | STUB OK | |
| `TGPoint3_x_get/set` | MUST IMPLEMENT | Property accessor |
| `TGPoint3_y_get/set` | MUST IMPLEMENT | Property accessor |
| `TGPoint3_z_get/set` | MUST IMPLEMENT | Property accessor |
| `TGPoint3_Cross(self, other)` -> TGPoint3Ptr | STUB OK | |
| `TGPoint3_UnitCross(self, other)` -> TGPoint3Ptr | STUB OK | |
| `TGPoint3_MultMatrix(self, matrix)` | STUB OK | |
| `TGPoint3_LoadBinary(self, stream)` | STUB OK | |
| `TGPoint3_SaveBinary(self, stream)` | STUB OK | |

#### TGColorA (App.py line 3450) - used by hardpoint property Set* calls
| Function | Classification | Notes |
|----------|---------------|-------|
| `new_TGColorA(*args)` | MUST IMPLEMENT | Hardpoint files create colors |
| `delete_TGColorA(self)` | MUST IMPLEMENT | |
| `TGColorA_r_get/set` | MUST IMPLEMENT | RGBA property accessors |
| `TGColorA_g_get/set` | MUST IMPLEMENT | |
| `TGColorA_b_get/set` | MUST IMPLEMENT | |
| `TGColorA_a_get/set` | MUST IMPLEMENT | |

---

### Category 8: Proximity/Spatial (~8 functions) - Mission5 Only

Used by Mission5 for finding empty locations to place the starbase.

#### ProximityManager (App.py line 6109)
| Function | Classification | Notes |
|----------|---------------|-------|
| `new_ProximityManager(*args)` | STUB OK | Server probably won't create these directly |
| `delete_ProximityManager(self)` | STUB OK | |
| `ProximityManager_UpdateObject(self, obj)` | MUST IMPLEMENT | Mission5: update after starbase placement |
| `ProximityManager_AddObject(self, obj)` | STUB OK | |
| `ProximityManager_RemoveObject(self, obj)` | STUB OK | |
| `ProximityManager_GetNearObjects(self, ...)` | STUB OK | |
| `ProximityManager_Update(self)` | STUB OK | |

#### SetClass additional methods
| Function | Classification | Notes |
|----------|---------------|-------|
| `SetClass_IsLocationEmptyTG(self, point, radius, flag)` -> int | MUST IMPLEMENT | Mission5: check starbase placement spot |
| `SetClass_GetProximityManager(self)` -> ProximityManagerPtr | MUST IMPLEMENT | Mission5: get proximity mgr for set |
| `SetClass_GetDisplayName(self)` -> str | STUB OK | |

---

### Category 9: Difficulty System (~5 functions) - MUST IMPLEMENT

Used by loadspacehelper.py during ship creation.

| Function | Classification | Notes |
|----------|---------------|-------|
| `Game_GetDifficulty()` -> int | MUST IMPLEMENT | Already partially in lobby catalog |
| `Game_SetDifficulty(val)` | MUST IMPLEMENT | Already partially in lobby catalog |
| `Game_GetOffensiveDifficultyMultiplier()` -> float | MUST IMPLEMENT | loadspacehelper uses for damage scaling |
| `Game_GetDefensiveDifficultyMultiplier()` -> float | MUST IMPLEMENT | loadspacehelper uses for defense scaling |
| `Game_SetDifficultyMultipliers(...)` | STUB OK | |
| `Game_SetDefaultDifficultyMultipliers(...)` | STUB OK | |
| `Game_SetDifficultyReallyIMeanIt(val)` | STUB OK | |
| `Game_SetPlayerHardpointFileName(name)` | STUB OK | |

---

### Category 10: Additional Mission/Episode Methods

Functions needed for gameplay flow that were not fully enumerated in lobby catalog.

#### Mission class - additional methods
| Function | Classification | Notes |
|----------|---------------|-------|
| `Mission_GetEnemyGroup(self)` -> ObjectGroupPtr | MUST IMPLEMENT | C++ native, not in App.py shadow |
| `Mission_GetFriendlyGroup(self)` -> ObjectGroupPtr | MUST IMPLEMENT | C++ native, not in App.py shadow |

These are registered in the Appc module via SWIG but NOT generated as App.py shadow methods (confirmed
in decompiled code at utopia_app.c line 4899-4900). They must be exposed as Appc functions and made
accessible through the Mission shadow class.

#### MultiplayerGame additional methods (already in lobby catalog but now confirmed needed)
| Function | Classification | Notes |
|----------|---------------|-------|
| `MultiplayerGame_DeleteObjectFromGame(self, obj)` | MUST IMPLEMENT | Mission5: delete starbase |
| `MultiplayerGame_DeletePlayerShipsAndTorps(self)` | MUST IMPLEMENT | End game cleanup |
| `MultiplayerGame_SetReadyForNewPlayers(self, flag)` | MUST IMPLEMENT | Game flow |

---

### Category 11: loadspacehelper Dependencies (~10 functions)

loadspacehelper.py is the standard ship creation helper. It's called by Mission5's CreateStarbase
and by the ship creation pipeline generally.

| Function | Classification | Notes |
|----------|---------------|-------|
| `TGEvent_Create()` -> TGEventPtr | Already in lobby catalog | Confirmed needed |
| `TGScriptAction_Create(module, func, ...)` | Already in Category 4 | Confirmed needed |
| `TGSequence_Create()` | Already in Category 4 | Confirmed needed |
| `CT_SUBSYSTEM_PROPERTY` | MUST DEFINE | Type constant |
| `CT_ENERGY_WEAPON` | MUST DEFINE | Type constant |
| `CT_SHIELD_SUBSYSTEM` | MUST DEFINE | Type constant |
| `SubsystemProperty_Cast(obj)` -> SubsystemPropertyPtr | STUB OK | loadspacehelper type casting |
| `EnergyWeapon_Cast(obj)` -> EnergyWeaponPtr | STUB OK | |
| `EnergyWeaponProperty_Cast(obj)` -> EnergyWeaponPropertyPtr | STUB OK | |
| `ShieldClass_Cast(obj)` -> ShieldClassPtr | STUB OK | |
| `ShieldProperty_Cast(obj)` -> ShieldPropertyPtr | STUB OK | |
| `ShieldProperty.FRONT_SHIELDS` etc. (6 constants) | MUST DEFINE | Shield facing constants |

---

### Category 12: Network Connection Status Constants

Already partially in lobby catalog but now confirmed used in gameplay scripts.

| Constant | Classification | Notes |
|----------|---------------|-------|
| `TGNETWORK_CONNECTED` | MUST DEFINE | Mission scripts check connection status |
| `TGNETWORK_CONNECT_IN_PROGRESS` | MUST DEFINE | |
| `TGNETWORK_DISCONNECTED` | MUST DEFINE | |
| `TGNETWORK_NOT_CONNECTED` | MUST DEFINE | |

---

### Category 13: UI Stubs (STUB OK, but must exist)

MissionMenusShared.py references these. They don't need real behavior on server.

| Function | Classification | Notes |
|----------|---------------|-------|
| `TopWindow_GetTopWindow()` -> TopWindowPtr | STUB OK | Returns None on server |
| `MultiplayerWindow_Cast(obj)` -> MultiplayerWindowPtr | STUB OK | Returns None |
| `STTargetMenu_GetTargetMenu()` -> STTargetMenuPtr | STUB OK | Returns None |
| `SortedRegionMenu_GetWarpButton()` -> ... | STUB OK | Returns None |
| `MWT_MULTIPLAYER` | MUST DEFINE | Window type constant |
| Various UI class constructors (STButton, STRoundedButton, etc.) | NOT NEEDED | Only used in client-side UI construction |

---

## SPECIES Constants (New, ~35 total)

From SpeciesToShip.py, these map to ship types for network identification.

```
SPECIES_UNKNOWN, SPECIES_AKIRA, SPECIES_AMBASSADOR, SPECIES_GALAXY,
SPECIES_NEBULA, SPECIES_SOVEREIGN, SPECIES_BIRD_OF_PREY, SPECIES_VORCHA,
SPECIES_WARBIRD, SPECIES_MARAUDER, SPECIES_GALOR, SPECIES_KELDON,
SPECIES_CARDHYBRID, SPECIES_KESSOK_HEAVY, SPECIES_KESSOK_LIGHT,
SPECIES_SHUTTLE, SPECIES_CARDFREIGHTER, SPECIES_FREIGHTER,
SPECIES_TRANSPORT, SPECIES_SPACE_FACILITY, SPECIES_COMMARRAY,
SPECIES_COMMLIGHT, SPECIES_DRYDOCK, SPECIES_PROBE, SPECIES_PROBETYPE2,
SPECIES_SUNBUSTER, SPECIES_CARD_OUTPOST, SPECIES_CARD_STARBASE,
SPECIES_CARD_STATION, SPECIES_FED_OUTPOST, SPECIES_FED_STARBASE,
SPECIES_ASTEROID, SPECIES_ESCAPEPOD, SPECIES_KESSOKMINE, SPECIES_BORG
```

Classification: MUST DEFINE (already partially noted in lobby catalog but not fully enumerated)

---

## Implementation Priority for Gameplay Expansion

### Priority 1: Ship Creation Pipeline (blocks everything else)
1. ShipClass_Create, SetScript, SetNetType, SetupModel, SetupProperties, UpdateNodeOnly
2. TGPoint3 (constructor, Get/Set XYZ, Add, Set)
3. TGColorA (constructor, RGBA accessors)
4. All 16 Property_Create functions + generic Set* stub mechanism
5. TGModelPropertyManager (RegisterLocalTemplate, ClearLocalTemplates)
6. DamageableObject.GetPropertySet, Torpedo_Create

### Priority 2: Scoring Infrastructure (needed for any game mode)
7. ObjectGroup / ObjectGroupWithInfo (constructor, AddName, RemoveName, IsNameInGroup)
8. Mission.GetEnemyGroup, Mission.GetFriendlyGroup
9. ShipClass_Cast, GetNetPlayerID, IsPlayerShip
10. Combat event types (ET_OBJECT_EXPLODING, ET_WEAPON_HIT, etc.)
11. Event accessor stubs (GetFiringPlayerID, GetDamage, IsHullHit)

### Priority 3: Game Flow
12. TGSequence_Create, AppendAction/AddAction, Play
13. TGScriptAction_Create
14. TGSoundAction_Create (stub), SubtitleAction_Create/CreateC (stub)
15. MultiplayerGame.DeleteObjectFromGame, DeletePlayerShipsAndTorps

### Priority 4: Mission5 (Coop) Specifics
16. ProximityManager.UpdateObject
17. SetClass.IsLocationEmptyTG, SetClass.GetProximityManager
18. g_kSystemWrapper.GetRandomNumber
19. DamageableObject.IsDying, IsDead

### Priority 5: Polish
20. Difficulty multiplier functions
21. loadspacehelper cast functions (SubsystemProperty_Cast, etc.)
22. UI stub functions (TopWindow_GetTopWindow, etc.)

---

## Open Questions

1. **Combat Event Generation**: How does a dedicated server (with no combat engine) generate
   ET_WEAPON_HIT and ET_OBJECT_EXPLODING events? Options:
   a. Clients send these as network messages, server dispatches as local events
   b. The scoring system only runs on the client-host, not the dedicated server
   c. The existing DedicatedServer.py mod handles this by being the "host" client

2. **Property System Depth**: Do hardpoint Set* methods need to store values, or can they be
   pure no-ops? If loadspacehelper.py or other scripts read back property values (Get* calls),
   we need storage. If they only write, no-ops suffice.

3. **Ship Object Lifetime**: Are ship objects on the server created when a player joins and
   destroyed when they leave? Or are they created/destroyed based on spawning/death events?
   This affects how much ship state the server needs to maintain.

4. **TGScriptAction Execution**: On a dedicated server without a rendering loop, how do
   TGSequences and TGScriptActions execute? They need some kind of tick/update mechanism.
   The server's game loop must call the action system's update.
