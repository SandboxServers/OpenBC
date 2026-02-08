# OpenBC Phase 1: SWIG API Surface Catalog

## Document Status
- **Generated**: 2026-02-07
- **Revised**: 2026-02-08 -- Expanded from lobby-only (~310 functions) to playable dedicated server (~595 functions)
- **Source**: swig-api-compat agent analysis of App.py (14,078 lines, 630 classes), gameplay-api-expansion analysis, mod-compat-tester gameplay analysis, physics-sim relay analysis
- **Reference**: `reference/scripts/App.py` from BC game data

---

## 1. Summary

| Category | Count |
|----------|-------|
| Must Implement (full behavior) | ~330 functions |
| Stub OK (return None/0/no-op) | ~190 functions |
| Not Needed (client-only) | ~75 functions |
| **Total Functions** | **~595** |
| Constants (ET_*, CT_*, SPECIES_*, etc.) | ~345 |
| Singleton Globals (g_k*) | ~29 |
| SWIG-Wrapped Classes | 40+ |

### Delta from Lobby-Only Catalog

| Category | Lobby-Only | Gameplay Expansion | Combined Total |
|----------|-----------|-------------------|----------------|
| Must Implement | ~185 | ~145 | ~330 |
| Stub OK | ~80 | ~110 | ~190 |
| Not Needed | ~45 | ~30 | ~75 |
| Constants (new) | ~280 | ~65 | ~345 |
| Total Functions | ~310 | ~285 | ~595 |

### New Class Categories (Not In Lobby-Only Catalog)
1. **Ship Object Hierarchy** -- ShipClass, DamageableObject, PhysicsObjectClass, ObjectClass, BaseObjectClass
2. **Combat/Gameplay Events** -- WeaponHitEvent, ObjectExplodingEvent, ObjectCreatedEvent
3. **Object Group Management** -- ObjectGroup, ObjectGroupWithInfo
4. **Property System** -- 16 property types with Create + ~200 Set* methods
5. **3D Math Types** -- TGPoint3/NiPoint3, TGColorA
6. **Action/Sequence System** -- TGSequence, TGScriptAction, TGSoundAction, SubtitleAction
7. **Torpedo Lifecycle** -- Torpedo_Create and related methods
8. **Proximity/Spatial** -- ProximityManager, SetClass.IsLocationEmptyTG
9. **Game/Mission Extensions** -- Difficulty system, MultiplayerGame gameplay methods
10. **UI Stubs** -- ~80 no-op functions for headless mode

---

## 2. Class-by-Class Catalog

### 2.1 TGNetwork (28 methods + 12 constants)

**Must Implement**:
| Function | Signature | Notes |
|----------|-----------|-------|
| TGNetwork_Update | (self) | Main network tick |
| TGNetwork_Connect | (self) | Start hosting, returns 0=success |
| TGNetwork_Disconnect | (self) | Tear down connection |
| TGNetwork_GetConnectStatus | (self) -> int | 2=hosting, 3=host-active |
| TGNetwork_CreateLocalPlayer | (self, TGString) -> int | Returns player_id |
| TGNetwork_DeleteLocalPlayer | (self, ?) | Delete local player |
| TGNetwork_SetName | (self, TGString) | Set server name |
| TGNetwork_GetName | (self) -> TGStringPtr | Get server name |
| TGNetwork_GetCName | (self) -> str | Get C string name |
| TGNetwork_SendTGMessage | (self, toID, msg) | Send to player (0=all) |
| TGNetwork_SendTGMessageToGroup | (self, group, msg) | Send to group |
| TGNetwork_GetNextMessage | (self) -> TGMessagePtr/None | Poll next message |
| TGNetwork_GetHostID | (self) -> int | Returns 1 when hosting |
| TGNetwork_GetLocalID | (self) -> int | Local player ID |
| TGNetwork_IsHost | (self) -> int | 1 if hosting |
| TGNetwork_GetNumPlayers | (self) -> int | Connected players |
| TGNetwork_SetPassword | (self, TGString) | Server password |
| TGNetwork_GetPassword | (self) -> TGStringPtr | Get password |
| TGNetwork_GetPlayerList | (self) -> TGPlayerListPtr | Player list |
| TGNetwork_SetSendTimeout | (self, seconds) | Send timeout |
| TGNetwork_SetConnectionTimeout | (self, seconds) | Connection timeout |
| TGNetwork_SetBootReason | (self, reason) | Boot reason code |
| TGNetwork_GetBootReason | (self) -> int | Get boot reason |
| TGNetwork_GetHostName | (self) -> TGStringPtr | Host player name |
| TGNetwork_GetLocalIPAddress | (self) -> str | Local IP |
| TGNetwork_RegisterHandlers | () | Static |
| TGNetwork_AddClassHandlers | () | Static |
| TGNetwork_RegisterMessageType | (net, ?) | Register msg type |

**Stub OK**: SetEncryptor, GetEncryptor, AddGroup, DeleteGroup, GetGroup, EnableProfiling, GetIPPacketHeaderSize, GetTimeElapsedSinceLastHostPing

**Constants**: TGNETWORK_MAX_SENDS_PENDING, TGNETWORK_INVALID_ID, TGNETWORK_NULL_ID, DEFAULT_BOOT, TIMED_OUT, INCORRECT_PASSWORD, TOO_MANY_PLAYERS, SERVER_BOOTED_YOU, YOU_ARE_BANNED, etc.

---

### 2.2 TGWinsockNetwork (6 methods + 3 constants)

**Must Implement**:
| Function | Signature | Notes |
|----------|-----------|-------|
| new_TGWinsockNetwork | () -> TGWinsockNetwork | Create network object |
| delete_TGWinsockNetwork | (self) | Destroy |
| TGWinsockNetwork_SetPortNumber | (self, port) | Set listen port |
| TGWinsockNetwork_GetPortNumber | (self) -> int | Get port |
| TGWinsockNetwork_GetLocalIPAddress | (self) -> str | Local IP |
| TGWinsockNetwork_BanPlayerByIP | (ip) | Static: ban by IP |
| TGWinsockNetwork_BanPlayerByName | (name) | Static: ban by name |

---

### 2.3 TGMessage (47 methods + 3 constants)

**Must Implement**:
| Function | Signature | Notes |
|----------|-----------|-------|
| new_TGMessage | (*args) | Create |
| delete_TGMessage | (self) | Destroy |
| TGMessage_Create | () -> TGMessagePtr | Static factory |
| TGMessage_SetGuaranteed | (self, flag) | Mark reliable |
| TGMessage_IsGuaranteed | (self) -> int | Check reliable |
| TGMessage_SetDataFromStream | (self, stream) | Set from buffer |
| TGMessage_GetBufferStream | (self) -> TGBufferStreamPtr | Get read stream |
| TGMessage_GetData / SetData | (self, ...) | Raw data access |
| TGMessage_GetDataLength | (self) -> int | Data length |
| TGMessage_Copy | (self) -> TGMessagePtr | Copy message |
| TGMessage_Serialize | (self, stream) | Serialize |
| TGMessage_UnSerialize | (stream) -> TGMessagePtr | Static: deserialize |
| TGMessage_GetFromID / SetFromID | (self, ...) | Sender ID |
| TGMessage_GetToID / SetToID | (self, ...) | Recipient ID |
| TGMessage_SetHighPriority / IsHighPriority | (self, ...) | Priority flag |
| TGMessage_SetSequenceNumber / GetSequenceNumber | (self, ...) | Sequence |

**Stub OK**: SetFirstResendTime, SetBackoffType, SetBackoffFactor, ReadyToResend, SetTimeStamp, SetNumRetries, SetMultiPartSequenceNumber, SetMultiPart, SetAggregate, BreakUpMessage, Merge, OverrideOldPackets

---

### 2.4 TGMessage Subtypes (6 classes, ~20 methods each)

| Class | Key Methods | Purpose |
|-------|-------------|---------|
| TGNameChangeMessage | new_, delete_, Copy, Serialize, UnSerialize | Name changes |
| TGAckMessage | new_, delete_, Copy, Serialize, SetSystemMessage | ACK packets |
| TGDoNothingMessage | new_, delete_, Copy, Serialize | Keepalive |
| TGBootPlayerMessage | new_, delete_, Copy, SetBootReason, GetBootReason | Boot players |
| TGConnectMessage | new_, delete_, Copy, Serialize | Connection |
| TGDisconnectMessage | new_, delete_, Copy, Serialize | Disconnection |

---

### 2.5 TGBufferStream (30 methods)

**Must Implement**:
| Function | Signature | Notes |
|----------|-----------|-------|
| new_TGBufferStream | (*args) | Create |
| delete_TGBufferStream | (self) | Destroy |
| TGBufferStream_OpenBuffer | (self, size) | Open with size |
| TGBufferStream_Close | (self) | Close buffer |
| TGBufferStream_ReadChar / WriteChar | (self, ...) | 1 byte |
| TGBufferStream_ReadInt / WriteInt | (self, ...) | 4-byte int |
| TGBufferStream_ReadShort / WriteShort | (self, ...) | 2-byte short |
| TGBufferStream_ReadFloat / WriteFloat | (self, ...) | 4-byte float |
| TGBufferStream_ReadBool / WriteBool | (self, ...) | Bool |
| TGBufferStream_ReadCString / WriteCString | (self, ...) | C string |
| TGBufferStream_ReadCLine / WriteCLine | (self, ...) | Line |
| TGBufferStream_GetBuffer | (self) | Raw buffer |
| TGBufferStream_SetWriteMode / GetWriteMode | (self, ...) | R/W mode |

**Stub OK**: ReadDouble/WriteDouble, ReadLong/WriteLong, ReadCWLine/WriteCWLine, ReadWChar/WriteWChar, ReadID/WriteID

---

### 2.6 TGEvent System (6 classes, ~60 methods total)

**TGEvent** (17 methods) - Must Implement ALL:
```
new_TGEvent, delete_TGEvent, TGEvent_Create(), TGEvent_Cast()
GetEventType, SetEventType, GetSource, SetSource
GetDestination, SetDestination, GetTimestamp, SetTimestamp
Copy, Duplicate, IncRefCount, DecRefCount, GetRefCount
SetLogged/IsLogged, SetPrivate/IsPrivate
```

**Typed Events** - Must Implement:
| Class | Methods | Used By |
|-------|---------|---------|
| TGBoolEvent | new_, GetBool, SetBool, ToggleBool, Create, Cast | Menu toggles |
| TGIntEvent | new_, GetInt, SetInt, Create, Cast | MissionShared |
| TGFloatEvent | new_, GetFloat, SetFloat, Create, Cast | Timers |
| TGStringEvent | new_, GetString, SetString, Create, Cast | Name changes |
| TGMessageEvent | new_, GetMessage, SetMessage, Create | Network messages |
| TGPlayerEvent | new_, GetPlayerID, SetPlayerID, Create | Player events |

**Stub OK**: TGCharEvent, TGShortEvent, TGVoidPtrEvent, TGObjPtrEvent

---

### 2.7 TGEventHandlerObject (6 methods) - Must Implement ALL
```
TGEventHandlerObject_ProcessEvent(self, event)
TGEventHandlerObject_CallNextHandler(self, event)
TGEventHandlerObject_AddPythonFuncHandlerForInstance(self, evt_type, 'Module.FuncName')
TGEventHandlerObject_AddPythonMethodHandlerForInstance(self, evt_type, 'Module.FuncName')
TGEventHandlerObject_RemoveHandlerForInstance(self, evt_type, ?)
TGEventHandlerObject_RemoveAllInstanceHandlers(self)
TGEventHandlerObject_Cast(obj)  # Static
```

---

### 2.8 TGEventManager (4 methods) - Must Implement ALL
```
TGEventManager_AddBroadcastPythonFuncHandler(self, evt_type, target, 'Module.Func')
TGEventManager_AddBroadcastPythonMethodHandler(self, evt_type, target, 'Module.Func')
TGEventManager_RemoveBroadcastHandler(self, ?)
TGEventManager_RemoveAllBroadcastHandlersForObject(self, obj)
TGEventManager_AddEvent(self, event)
```

---

### 2.9 UtopiaModule (~40 methods needed)

**Must Implement**:
```
InitializeNetwork(self, wsn, TGString_name)  # NOTE: 3 args!
TerminateNetwork(self), GetNetwork(self) -> TGNetworkPtr
SetIsHost/IsHost, SetMultiplayer/IsMultiplayer, SetIsClient/IsClient
GetGameTime/SetGameTime, GetRealTime
IsGamePaused, SetTimeRate, SetTimeScale, Pause, ForceUnpause
SetGameName/GetGameName, SetGamePath/GetGamePath, SetDataPath/GetDataPath
GetCaptainName/SetCaptainName
SetPlayerNumber/GetPlayerNumber
SetProcessingPackets/IsProcessingPackets
SetUnusedClientID/GetUnusedClientID, IncrementClientID
SetIgnoreClientIDForObjectCreation/IsIgnoreClientIDForObjectCreation
SetFriendlyFireWarningPoints/GetFriendlyFireWarningPoints
GetCurrentFriendlyFire/SetCurrentFriendlyFire
SetMaxFriendlyFire, GetFriendlyFireTolerance
```

**Static**: GetNextEventType, GetGameVersion, ConvertGameUnitsToKilometers, ConvertKilometersToGameUnits

**Stub OK**: GetCamera, RenderDefaultCamera, IsDirectStart, CDCheck, SaveToFile, LoadFromFile, LoadEpisodeSounds, CreateGameSpy, TerminateGameSpy, DetermineModemConnection, GetTestMenuState

---

### 2.10 TGConfigMapping (10 methods) - Must Implement ALL
```
GetIntValue(self, section, key) -> int
GetFloatValue(self, section, key) -> float
GetStringValue(self, section, key) -> str
GetTGStringValue(self, section, key) -> TGStringPtr
SetIntValue/SetFloatValue/SetStringValue/SetTGStringValue
HasValue(self, section, key) -> int
LoadConfigFile(self, filename)
SaveConfigFile(self, filename)
```

---

### 2.11 VarManagerClass (7 methods) - Must Implement ALL
```
SetFloatVariable(self, scope, key, float_val)
GetFloatVariable(self, scope, key) -> float
SetStringVariable(self, scope, key, TGString_val)
GetStringVariable(self, scope, key) -> str
DeleteAllVariables(self)
DeleteAllScopedVariables(self, scope)
MakeEpisodeEventType(self, offset) -> int
```

---

### 2.12 MultiplayerGame (11 methods) - Must Implement ALL
```
new_MultiplayerGame(*args), delete_MultiplayerGame(self)
SetReadyForNewPlayers(self, flag), IsReadyForNewPlayers(self) -> int
SetMaxPlayers(self, n), GetMaxPlayers(self) -> int
GetNumberPlayersInGame(self) -> int
IsPlayerInGame(self, player_id) -> int
GetPlayerNumberFromID(self, id) -> int
GetPlayerName(self, player_id) -> TGStringPtr
MultiplayerGame_Cast(game), MultiplayerGame_Create(game)  # Static
MultiplayerGame_MAX_PLAYERS  # Constant
```

---

### 2.13 Game / Episode / Mission
```
Game_GetCurrentGame() -> GamePtr          # Static - CRITICAL
Game_GetCurrentPlayer() -> ShipClassPtr
Game_Create(*args), GetNextEventType()
Game_LoadEpisode(self, episode_str)
Game_GetCurrentEpisode(self) -> EpisodePtr
Game_Terminate(self), SetGodMode/InGodMode

Episode_GetCurrentMission(self) -> MissionPtr
Episode_LoadMission(self, mission_str)

LoadEpisodeAction_Create(game), LoadEpisodeAction_Play(self)
```

---

### 2.14 TGPlayerList / TGNetPlayer

**TGPlayerList** (12 methods):
```
new/delete, AddPlayer, DeletePlayer, DeletePlayerAtIndex
GetNumPlayers, GetPlayerAtIndex, GetPlayer, GetPlayerFromAddress
GetPlayerList, ChangePlayerNetID, DeleteAllPlayers
```

**TGNetPlayer** (16 methods):
```
new/delete, SetName/GetName, SetNetID/GetNetID
SetNetAddress/GetNetAddress, SetDisconnected/IsDisconnected
SetConnectUID/GetConnectUID, SetConnectAddress/GetConnectAddress
GetBytesPerSecondTo/From, Copy, Compare
```

---

### 2.15 TGString (7 methods) - Must Implement ALL
```
new_TGString(*args), delete_TGString(self)
GetLength, SetString, FindC, Find, CompareC, Compare, Append
```

---

### 2.16 TGObject (5 methods) - Must Implement
```
GetObjType(self) -> int, IsTypeOf(self, type_id) -> int
GetObjID(self) -> int, Destroy(self), Print(self)
```

---

### 2.17 TGTimer / TGTimerManager (10 methods)
```
new_TGTimer, delete_TGTimer, TGTimer_Create()
SetTimerStart/GetTimerStart, SetDelay/GetDelay
SetDuration/GetDuration, SetEvent/GetEvent
TGTimerManager_AddTimer, RemoveTimer, DeleteTimer
```

---

### 2.18 SetManager / SetClass (~20 methods)

**SetManager**: ClearRenderedSet, DeleteAllSets, AddSet, DeleteSet, GetSet, GetRenderedSet, GetNumSets, MakeRenderedSet, Terminate

**SetClass**: Create, Cast, GetName, SetName, AddObjectToSet, RemoveObjectFromSet, DeleteObjectFromSet, GetObject, GetObjectByID, GetFirstObject, GetNextObject, IsRendered

---

### 2.19 TGLocalizationManager (5 methods)
```
Load(self, filename) -> TGLocalizationDatabasePtr
Unload(self, db), GetIfRegistered(self, filename)
DeleteAll, Purge, RegisterDatabase
```

---

### 2.20 TGSystemWrapperClass (10 methods)
```
GetTimeInSeconds, GetTimeInMilliseconds
GetTimeElapsedInSeconds, GetTimeElapsedInMilliseconds
GetTimeSinceFrameStart, GetUpdateNumber
GetRandomNumber(self, max), SetRandomSeed(self, seed)
GetIniInt/GetIniFloat/GetIniString
```

---

### 2.21 Utility Functions
```
IsNull(obj) -> int
Breakpoint()
TGScriptAction_Create(module, func)
```

---

### 2.22 Ship Object Hierarchy (~55 functions) -- NEW

These form the inheritance chain: BaseObjectClass -> ObjectClass -> PhysicsObjectClass -> DamageableObject -> ShipClass. The server needs ships as lightweight data containers (~80 bytes) with identity, ownership, and basic state -- NOT simulation objects.

#### BaseObjectClass (inherits TGEventHandlerObject)

**Must Implement**:
| Function | Signature | Notes |
|----------|-----------|-------|
| BaseObjectClass_GetName | (self) -> str | Scoring scripts identify ships by name |
| BaseObjectClass_SetName | (self, name) | Set ship name |
| BaseObjectClass_GetContainingSet | (self) -> SetClassPtr | Mission5 starbase placement |
| BaseObjectClass_UpdateNodeOnly | (self) | Called after ship creation |
| BaseObjectClass_SetTranslate | (self, point) | Mission5 starbase placement |
| BaseObjectClass_SetTranslateXYZ | (self, x, y, z) | Position setting |
| BaseObjectClass_SetDeleteMe | (self) | Object deletion request |
| BaseObjectClass_GetWorldLocation | (self) -> TGPoint3Ptr | Spatial queries |
| BaseObjectClass_GetTranslate | (self) -> TGPoint3Ptr | Position queries |

**Stub OK**: GetDisplayName, SetDisplayName, Update, SetHidden, IsHidden (return 0), Rotate, SetAngleAxisRotation, SetMatrixRotation, SetScale, GetScale (return 1.0), AlignToVectors, GetRotation, GetWorldRotation, GetWorldForwardTG, AttachObject, DetachObject

#### ObjectClass (inherits BaseObjectClass)

**Must Implement**:
| Function | Signature | Notes |
|----------|-----------|-------|
| ObjectClass_GetRadius | (self) -> float | Mission5 placement spacing |
| ObjectClass_RandomOrientation | (self) | Mission5 starbase |
| ObjectClass_Cast | (obj) -> ObjectClassPtr | Static cast |

**Stub OK**: PlaceObjectByName, IsTargetable, CanTargetObject, SetHailable, SetScannable, SetCollisionFlags, GetCollisionFlags

#### PhysicsObjectClass (inherits ObjectClass)

**Must Implement**:
| Function | Signature | Notes |
|----------|-----------|-------|
| PhysicsObjectClass_SetupModel | (self, name) | Ship creation -- loads model/hardpoints |
| PhysicsObjectClass_SetNetType | (self, type) | Network species identification |
| PhysicsObjectClass_GetNetType | (self) -> int | Scoring: identify ship type |
| PhysicsObjectClass_SetAI | (self, ai) | Mission5 starbase AI |
| PhysicsObjectClass_Cast | (obj) -> PhysicsObjectClassPtr | Static cast |

**Stub OK**: ClearAI, GetAI (return None), SetStatic, IsStatic, SetDoNetUpdate, IsDoingNetUpdate, SetUsePhysics, IsUsingPhysics, SetVelocity, SetAngularVelocity, SetAcceleration, GetVelocityTG, GetMass, SetMass

#### DamageableObject (inherits PhysicsObjectClass)

**Must Implement**:
| Function | Signature | Notes |
|----------|-----------|-------|
| DamageableObject_GetPropertySet | (self) -> TGModelPropertySetPtr | Ship creation hardpoint loading |
| DamageableObject_SetupProperties | (self) | Finalizes hardpoint setup |
| DamageableObject_IsDying | (self) -> int | Mission scripts check death state |
| DamageableObject_IsDead | (self) -> int | Mission scripts check death state |

**Stub OK**: SetDead, SetLifeTime, GetLifeTime, SetCollisionsOn, CanCollide, SetSplashDamage, SetVisibleDamageRadiusModifier, DisableGlowAlphaMaps

#### ShipClass (inherits DamageableObject)

**Must Implement**:
| Function | Signature | Notes |
|----------|-----------|-------|
| ShipClass_Create | (name) -> ShipClassPtr | Static factory -- creates ship objects |
| ShipClass_Cast | (obj) -> ShipClassPtr | Cast from event source |
| ShipClass_GetObject | (name) -> ShipClassPtr | Lookup ship by name |
| ShipClass_SetScript | (self, module) | Ship creation pipeline |
| ShipClass_GetScript | (self) -> str | Script module lookup |
| ShipClass_SetNetPlayerID | (self, id) | Associate ship with player |
| ShipClass_GetNetPlayerID | (self) -> int | Scoring: identify player |
| ShipClass_IsPlayerShip | (self) -> int | Distinguish player vs NPC ships |
| ShipClass_GetName | (self) -> str | Ship name for scoring |
| ShipClass_GetObjID | (self) -> int | Unique object ID |
| ShipClass_DisableCollisionDamage | (self, flag) | Mission5 starbase |
| ShipClass_AddSubsystem | (self, subsystem) | Ship creation hardpoint setup |

**Stub OK**: IsCollisionDamageDisabled, SetInvincible, IsInvincible, SetHurtable, IsHurtable, SetTargetable, IsTargetable, GetHull (return None), GetShields (return None), GetAffiliation, SetAffiliation, GetAlertLevel, SetAlertLevel, SetDeathScript, GetDeathScript, SetTarget, StopFiringWeapons, CompleteStop, IncrementAIDoneIgnore, GetShipProperty

---

### 2.23 Combat/Gameplay Events (~15 functions) -- NEW

Event types and accessor methods needed for scoring scripts. **Phase 1: stubs** -- events are deferred to Phase 2 when the server can synthesize them from the relay stream.

#### WeaponHitEvent
| Function | Signature | Notes |
|----------|-----------|-------|
| WeaponHitEvent_GetDamage | (self) -> float | Damage amount from weapon hit |
| WeaponHitEvent_IsHullHit | (self) -> int | 0=shield, 1=hull |
| WeaponHitEvent_GetFiringPlayerID | (self) -> int | Who fired the weapon |
| WeaponHitEvent_Create | () -> WeaponHitEventPtr | Static factory |
| WeaponHitEvent_Cast | (obj) -> WeaponHitEventPtr | Static cast |

#### ObjectExplodingEvent
| Function | Signature | Notes |
|----------|-----------|-------|
| ObjectExplodingEvent_GetFiringPlayerID | (self) -> int | Who dealt killing blow |
| ObjectExplodingEvent_Create | () -> ObjectExplodingEventPtr | Static factory |
| ObjectExplodingEvent_Cast | (obj) -> ObjectExplodingEventPtr | Static cast |

#### ObjectCreatedEvent
| Function | Signature | Notes |
|----------|-----------|-------|
| ObjectCreatedEvent_Create | () -> ObjectCreatedEventPtr | Static factory |
| ObjectCreatedEvent_Cast | (obj) -> ObjectCreatedEventPtr | Static cast |

**Phase 1 Implementation Note**: These event classes must exist so that handler registration does not crash. The actual events will not fire on a relay server (no C++ damage pipeline). Phase 2 will parse relay opcodes and synthesize these events.

---

### 2.24 Object Group Management (~12 functions) -- NEW

Used by Mission1-5 for tracking enemy/friendly teams.

#### ObjectGroup (inherits TGEventHandlerObject)
| Function | Signature | Notes |
|----------|-----------|-------|
| new_ObjectGroup | (*args) | Constructor |
| delete_ObjectGroup | (self) | Destructor |
| ObjectGroup_AddName | (self, name) | Add ship name to group |
| ObjectGroup_RemoveName | (self, name) | Remove ship from group |
| ObjectGroup_RemoveAllNames | (self) | Clear group |
| ObjectGroup_IsNameInGroup | (self, name) -> int | Check membership |
| ObjectGroup_GetNumActiveObjects | (self) -> int | Count active members |

**Stub OK**: SetEventFlag, ClearEventFlag, IsEventFlagSet (return 0)

#### ObjectGroupWithInfo (inherits ObjectGroup)
| Function | Signature | Notes |
|----------|-----------|-------|
| new_ObjectGroupWithInfo | (*args) | Mission5 creates these |
| delete_ObjectGroupWithInfo | (self) | Destructor |

#### Mission Group Access
| Function | Signature | Notes |
|----------|-----------|-------|
| Mission_GetEnemyGroup | (self) -> ObjectGroupPtr | C++ native, confirmed in decompiled code |
| Mission_GetFriendlyGroup | (self) -> ObjectGroupPtr | C++ native, confirmed in decompiled code |

---

### 2.25 Property System (~18 Create + ~200 Set*) -- NEW

The property system is used by hardpoint files (e.g., sovereign.py) to configure ship subsystems. The server needs these to exist so scripts do not crash, but the relay server does not use them for damage calculation.

**IMPLEMENTATION NOTE**: Create a generic property base class that accepts any Set*/Get* method call as a no-op. This avoids implementing ~200 individual methods. Store values in a dict if Get* calls need to return them; otherwise just ignore.

#### Property Create Functions (16 types, all return PropertyPtr)
| Function | Classification | Notes |
|----------|---------------|-------|
| TorpedoTubeProperty_Create(name) | MUST IMPLEMENT | Data container |
| PhaserProperty_Create(name) | MUST IMPLEMENT | Data container |
| PulseWeaponProperty_Create(name) | MUST IMPLEMENT | Data container |
| TractorBeamProperty_Create(name) | MUST IMPLEMENT | Data container |
| ShieldProperty_Create(name) | MUST IMPLEMENT | Data container |
| HullProperty_Create(name) | MUST IMPLEMENT | Data container |
| PowerProperty_Create(name) | MUST IMPLEMENT | Data container |
| EngineProperty_Create(name) | MUST IMPLEMENT | Data container |
| ImpulseEngineProperty_Create(name) | MUST IMPLEMENT | Data container |
| WarpEngineProperty_Create(name) | MUST IMPLEMENT | Data container |
| SensorProperty_Create(name) | MUST IMPLEMENT | Data container |
| RepairSubsystemProperty_Create(name) | MUST IMPLEMENT | Data container |
| ShipProperty_Create(name) | MUST IMPLEMENT | Data container |
| CloakingSubsystemProperty_Create(name) | MUST IMPLEMENT | Data container |
| WeaponSystemProperty_Create(name) | MUST IMPLEMENT | Data container |
| TorpedoSystemProperty_Create(name) | MUST IMPLEMENT | Data container |

#### Property Set* Methods (~200 total)
All Set* methods across all 16 property types are **STUB OK** -- no-op on the relay server. Each property type has 10-30 Set* methods for damage, range, fire rate, shield facing HP, hull HP, etc. Called by hardpoint files during ship creation.

#### TGModelPropertyManager
| Function | Classification | Notes |
|----------|---------------|-------|
| RegisterLocalTemplate(self, prop) | MUST IMPLEMENT | Hardpoint files register each property |
| RegisterGlobalTemplate(self, prop) | STUB OK | |
| ClearLocalTemplates(self) | MUST IMPLEMENT | Called before loading new hardpoint |

#### TGModelPropertySet
| Function | Classification | Notes |
|----------|---------------|-------|
| GetPropertySet(self) | MUST IMPLEMENT | Returns property set for ship |
| SetupProperties(self) | MUST IMPLEMENT | Finalizes hardpoint setup |

---

### 2.26 Torpedo Lifecycle (~5 functions) -- NEW

| Function | Signature | Notes |
|----------|-----------|-------|
| Torpedo_Create | (name) -> TorpedoPtr | SpeciesToTorp creates torpedoes |
| Torpedo_Cast | (obj) -> TorpedoPtr | Static cast |
| Torpedo_SetScript | (self, module) | Torpedo script module |
| Torpedo_GetObjID | (self) -> int | Unique object ID |

**Stub OK**: SetDamage, SetLifetime, SetParent, SetTarget, SetMaxAngularAccel, SetGuidanceLifetime, SetPlayerID, GetTargetID, GetParentID, GetLaunchSpeed, CreateDisruptorModel, CreateTorpedoModel

**Phase 1 Note**: No torpedo entities needed on the server. All scoring-relevant data (damage, firing player ID, hull hit flag) is embedded in network messages.

---

### 2.27 3D Math Types (~25 functions) -- NEW

Used by Mission5 for starbase placement and by hardpoint files for property configuration.

#### TGPoint3 (inherits NiPoint3)
| Function | Signature | Notes |
|----------|-----------|-------|
| new_TGPoint3 | (*args) | Constructor (0-arg, 3-float, copy) |
| delete_TGPoint3 | (self) | Destructor |
| TGPoint3_GetX | (self) -> float | X component |
| TGPoint3_GetY | (self) -> float | Y component |
| TGPoint3_GetZ | (self) -> float | Z component |
| TGPoint3_SetX | (self, val) | Set X |
| TGPoint3_SetY | (self, val) | Set Y |
| TGPoint3_SetZ | (self, val) | Set Z |
| TGPoint3_SetXYZ | (self, x, y, z) | Set all three |
| TGPoint3_Set | (self, other) | Copy from another TGPoint3 |
| TGPoint3_Add | (self, other) | Vector addition |
| TGPoint3_Unitize | (self) | Normalize to unit length |
| TGPoint3_Length | (self) -> float | Vector magnitude |

**Stub OK**: Subtract, Scale, Cross, UnitCross, MultMatrix, LoadBinary, SaveBinary

#### NiPoint3 (base class)
| Function | Signature | Notes |
|----------|-----------|-------|
| new_NiPoint3 | (*args) | Constructor |
| delete_NiPoint3 | (self) | Destructor |
| NiPoint3_x_get/set | property | X accessor |
| NiPoint3_y_get/set | property | Y accessor |
| NiPoint3_z_get/set | property | Z accessor |

#### TGColorA
| Function | Signature | Notes |
|----------|-----------|-------|
| new_TGColorA | (*args) | Hardpoint files create colors |
| delete_TGColorA | (self) | Destructor |
| TGColorA_SetRGBA | (self, r, g, b, a) | Set all components |
| TGColorA_GetRed | (self) -> float | Red component |
| TGColorA_GetGreen | (self) -> float | Green component |
| TGColorA_GetBlue | (self) -> float | Blue component |
| TGColorA_GetAlpha | (self) -> float | Alpha component |

---

### 2.28 Action/Sequence System (~20 functions) -- NEW

Used by MissionShared.py for game flow (end game sequences, announcements).

#### TGSequence (inherits TGAction)
| Function | Signature | Notes |
|----------|-----------|-------|
| TGSequence_Create | () -> TGSequencePtr | Static factory |
| TGSequence_AddAction | (self, action) | Add action to sequence |
| TGSequence_AppendAction | (self, action) | Append action |
| TGSequence_Play | (self) | Execute sequence |
| TGSequence_Delete | (self) | Destroy sequence |

**Stub OK**: Skip, GetNumActions, GetAction, Cast

#### TGScriptAction (inherits TGAction)
| Function | Signature | Notes |
|----------|-----------|-------|
| TGScriptAction_Create | (module, func, ...) -> TGScriptActionPtr | Variable args; module+func are required |

**NOTE**: When the action fires, it calls the named Python function. On the server, the action system's update must be driven by the game loop tick.

#### TGAction (base class)
| Function | Signature | Notes |
|----------|-----------|-------|
| TGAction_Play | (self) | MUST IMPLEMENT -- needed by sequences |
| TGAction_Completed | (self) | Marks action done |
| TGAction_AddCompletedEvent | (self, event) | Chain actions |

**Stub OK**: IsPlaying, SetSkippable, IsSkippable, Skip, Abort, SetUseRealTime, GetSequence, Cast, CreateNull

#### SubtitleAction -- STUB (no-op on headless)
| Function | Notes |
|----------|-------|
| SubtitleAction_CreateC(string) | Returns stub action object |
| SubtitleAction_Create(db, name) | Returns stub action object |
| SubtitleAction.SetDuration(self, secs) | No-op |

#### TGSoundAction -- STUB (no-op on headless)
| Function | Notes |
|----------|-------|
| TGSoundAction_Create(name) | Returns stub action object |

---

### 2.29 Game/Mission Extensions (~20 functions) -- NEW

Additional methods needed for gameplay flow beyond the lobby catalog.

#### Game Difficulty System
| Function | Signature | Notes |
|----------|-----------|-------|
| Game_GetCurrentGame | () -> GamePtr | Static -- CRITICAL (already in 2.13) |
| Game_GetDifficulty | () -> int | Difficulty level |
| Game_GetOffensiveDifficultyMultiplier | () -> float | Damage scaling |
| Game_GetDefensiveDifficultyMultiplier | () -> float | Defense scaling |

**Stub OK**: SetDifficultyMultipliers, SetDefaultDifficultyMultipliers, SetDifficultyReallyIMeanIt, SetPlayerHardpointFileName

#### MultiplayerGame -- Additional Gameplay Methods
| Function | Signature | Notes |
|----------|-----------|-------|
| MultiplayerGame_GetShipFromPlayerID | (self, playerID) -> ShipClassPtr | Critical for scoring |
| MultiplayerGame_SetPlayer | (self, ship) | Set player's ship |
| MultiplayerGame_DeletePlayerShipsAndTorps | (self) | End game cleanup |
| MultiplayerGame_GetPlayerShipID | (self, playerID) -> int | Ship ID lookup |
| MultiplayerGame_DeleteObjectFromGame | (self, obj) | Mission5 starbase cleanup |

#### Mission -- Additional Methods
| Function | Signature | Notes |
|----------|-----------|-------|
| Mission_GetScript | (self) -> str | Mission script name |
| Mission_GetEnemyGroup | (self) -> ObjectGroupPtr | C++ native (see 2.24) |
| Mission_GetFriendlyGroup | (self) -> ObjectGroupPtr | C++ native (see 2.24) |

#### MissionLib Module Functions
| Function | Signature | Notes |
|----------|-----------|-------|
| MissionLib.CreateTimer | (eventType, handler, ...) | Timer creation helper |
| MissionLib.GetMission | () -> MissionPtr | Convenience: Game -> Episode -> Mission |

---

### 2.30 Proximity/Spatial (~8 functions) -- NEW

Used by Mission5 for starbase placement.

#### ProximityManager
| Function | Signature | Notes |
|----------|-----------|-------|
| ProximityManager_SetPlayerCollisionsEnabled | (flag) | Stub -- server does not simulate collisions |
| ProximityManager_SetMultiplayerPlayerCollisionsEnabled | (flag) | Stub |
| ProximityManager_UpdateObject | (self, obj) | Mission5: update after placement |

**Stub OK**: AddObject, RemoveObject, GetNearObjects, Update

#### SetClass -- Additional Methods
| Function | Signature | Notes |
|----------|-----------|-------|
| SetClass_IsLocationEmptyTG | (self, point, radius, flag) -> int | Mission5: check placement spot |
| SetClass_GetProximityManager | (self) -> ProximityManagerPtr | Mission5 |

#### PlacementObject
| Function | Signature | Notes |
|----------|-----------|-------|
| PlacementObject_GetObject | (self) -> ObjectClassPtr | Object retrieval |

---

### 2.31 UI Stubs (~80 functions) -- NEW

MissionMenusShared.py and other scripts reference UI classes. On a headless server, these must return **dummy callable objects (NOT None)** so that method calls like `pButton.SetEnabled(0)` do not crash with AttributeError.

#### Must Return Dummy Objects
| Function | Notes |
|----------|-------|
| TopWindow_GetTopWindow() | Returns dummy object with chainable methods |
| MultiplayerWindow_Cast(obj) | Returns dummy object |
| SortedRegionMenu_GetWarpButton() | Returns dummy with AddPythonFuncHandlerForInstance as no-op |
| STTargetMenu_GetTargetMenu() | Returns dummy or None (code checks for None) |

#### No-Op Functions
| Function | Notes |
|----------|-------|
| DynamicMusic.PlayFanfare() | No-op on headless |
| LoadBridge.CreateCharacterMenus() | No-op on headless |
| Game.LoadDatabaseSoundInGroup() | No-op on headless (audio) |
| Game.LoadSoundInGroup() | No-op on headless (audio) |

#### Stub Classes (return dummy objects accepting any method call)
All of the following should return dummy objects or be no-ops:
- STButton, STRoundedButton, STMenu, STSubMenu
- TGPane, StylizedWindow
- ScoreWindow, ChatWindow
- All other UI widget constructors referenced by multiplayer scripts

---

## 3. Required Constants

### 3.1 Network Event Types (~20)
```
ET_NETWORK_MESSAGE_EVENT, ET_NETWORK_CONNECT_EVENT
ET_NETWORK_DISCONNECT_EVENT, ET_NETWORK_NEW_PLAYER
ET_NETWORK_DELETE_PLAYER, ET_NETWORK_GAMESPY_MESSAGE
ET_NETWORK_NAME_CHANGE_EVENT, FIRST_TGNETWORK_MODULE_EVENT_TYPE
```

### 3.2 Server/Multiplayer Event Types (~30)
```
ET_CREATE_SERVER, ET_CREATE_CLIENT, ET_START, ET_KILL_GAME
ET_EXIT_GAME, ET_EXIT_PROGRAM
ET_CHECKSUM_COMPLETE, ET_SYSTEM_CHECKSUM_COMPLETE, ET_SYSTEM_CHECKSUM_FAILED
ET_LOAD_EPISODE, ET_LOAD_MISSION, ET_EPISODE_START, ET_MISSION_START
ET_SET_PLAYER, ET_NEW_MULTIPLAYER_GAME, ET_NEW_PLAYER_IN_GAME
ET_CANCEL, ET_OKAY, ET_CANCEL_CONNECT
ET_SET_MISSION_NAME, ET_SET_GAME_MODE
ET_LOCAL_INTERNET_HOST, ET_END_GAME_OKAY, ET_SELECT_MISSION
ET_FRIENDLY_FIRE_DAMAGE, ET_FRIENDLY_FIRE_REPORT, ET_FRIENDLY_FIRE_GAME_OVER
ET_OBJECT_DELETED, ET_OBJECT_CREATED, ET_OBJECT_CREATED_NOTIFY
ET_OBJECT_DESTROYED, ET_OBJECT_EXPLODING
```

### 3.3 Combat Event Types -- NEW (~5)
```
ET_WEAPON_HIT
ET_OBJECT_EXPLODING   (also in 3.2 above)
ET_OBJECT_CREATED_NOTIFY
ET_DELETE_OBJECT_PUBLIC
ET_SCAN, ET_WARP_BUTTON_PRESSED
ET_INPUT_TOGGLE_SCORE_WINDOW, ET_INPUT_TOGGLE_CHAT_WINDOW
```

### 3.4 Class Type Constants (~20)
```
CT_MULTIPLAYER_GAME, CT_GAME, CT_EPISODE, CT_MISSION, CT_SET
CT_SHIP, CT_TORPEDO, CT_OBJECT, CT_PHYSICS_OBJECT, CT_DAMAGEABLE_OBJECT
CT_NETWORK, CT_MESSAGE_EVENT, CT_PLAYER_EVENT
CT_TGEVENT, CT_TGEVENTHANDLEROBJECT, CT_TGTIMER
CT_VAR_MANAGER
CT_SUBSYSTEM_PROPERTY, CT_ENERGY_WEAPON, CT_SHIELD_SUBSYSTEM
```

### 3.5 Species Constants (~35) -- EXPANDED
```
SPECIES_UNKNOWN, SPECIES_GALAXY, SPECIES_SOVEREIGN, SPECIES_NEBULA
SPECIES_DEFIANT, SPECIES_AKIRA, SPECIES_STEAMRUNNER, SPECIES_INTREPID
SPECIES_SABRE, SPECIES_AMBASSADOR, SPECIES_MIRANDA, SPECIES_EXCELSIOR
SPECIES_WARBIRD, SPECIES_GALOR, SPECIES_KELDON
SPECIES_KVORT, SPECIES_NEGHVAR, SPECIES_VORCHA
SPECIES_BIRD_OF_PREY, SPECIES_MARAUDER
SPECIES_CARDHYBRID, SPECIES_KESSOK_HEAVY, SPECIES_KESSOK_LIGHT
SPECIES_SHUTTLE, SPECIES_CARDFREIGHTER, SPECIES_FREIGHTER
SPECIES_TRANSPORT, SPECIES_SPACE_FACILITY, SPECIES_COMMARRAY
SPECIES_COMMLIGHT, SPECIES_DRYDOCK, SPECIES_PROBE, SPECIES_PROBETYPE2
SPECIES_SUNBUSTER, SPECIES_CARD_OUTPOST, SPECIES_CARD_STARBASE
SPECIES_CARD_STATION, SPECIES_FED_OUTPOST, SPECIES_FED_STARBASE
SPECIES_ASTEROID, SPECIES_ESCAPEPOD, SPECIES_KESSOKMINE, SPECIES_BORG
MAX_SHIPS
```

### 3.6 Network Connection Status Constants -- NEW
```
TGNETWORK_CONNECTED
TGNETWORK_CONNECT_IN_PROGRESS
TGNETWORK_DISCONNECTED
TGNETWORK_NOT_CONNECTED
```

### 3.7 Other Constants
```
NULL_ID, MAX_MESSAGE_TYPES, NULL_STRING_INDEX
PI, TWO_PI, HALF_PI, FOURTH_PI
TEAM_* (all 10 team constants)
GENUS_* (UNKNOWN, SHIP, STATION, ASTEROID)
MWT_MULTIPLAYER (and other main window types)
ShieldProperty.FRONT_SHIELDS (and 5 other shield facing constants)
WG_INVALID, WG_PRIMARY, WG_SECONDARY, WG_TERTIARY, WG_TRACTOR
GREEN_ALERT, YELLOW_ALERT, RED_ALERT
CFB_NO_COLLISIONS, CFB_IN_PROXIMITY_MANAGER, CFB_DEFAULTS
DIRECTION_MODEL_SPACE, DIRECTION_WORLD_SPACE
GROUP_CHANGED, ENTERED_SET, EXITED_SET, DESTROYED
GLOBAL_TEMPLATES, LOCAL_TEMPLATES
```

---

## 4. Singleton Globals

The following App module globals MUST be defined:

| Global | Type | Phase 1 Implementation |
|--------|------|------------------------|
| g_kUtopiaModule | UtopiaModule | Full implementation |
| g_kEventManager | TGEventManager | Full implementation |
| g_kTimerManager | TGTimerManager | Full implementation |
| g_kRealtimeTimerManager | TGTimerManager | Full implementation |
| g_kVarManager | VarManagerClass | Full implementation |
| g_kSetManager | SetManager | Basic implementation |
| g_kConfigMapping | TGConfigMapping | Full implementation |
| g_kLocalizationManager | TGLocalizationManager | Stub/basic |
| g_kModelPropertyManager | TGModelPropertyManager | Stub (accepts RegisterLocalTemplate, ClearLocalTemplates) |
| g_kSystemWrapper | TGSystemWrapperClass | Time functions + GetRandomNumber |
| g_kRaceManager | -- | Stub (None) |
| g_kMissionDatabase | -- | Stub (None) |
| g_kTacticalControlWindow | -- | Stub (None) |
| g_kSoundManager | -- | Stub (None) |

---

## 5. Implementation Priority

### Tier A - Bootstrap (must work first)
1. All constants (ET_*, CT_*, SPECIES_*, MWT_*, etc.)
2. TGString
3. TGObject
4. TGEvent + typed events
5. TGEventHandlerObject + TGEventManager
6. Singleton globals

### Tier B - Network
7. TGWinsockNetwork
8. TGNetwork
9. TGMessage + subtypes
10. TGBufferStream
11. TGPlayerList + TGNetPlayer
12. UtopiaModule

### Tier C - Game Flow
13. TGConfigMapping
14. VarManagerClass
15. Game / MultiplayerGame
16. Episode / Mission
17. TGTimer / TGTimerManager
18. SetManager / SetClass

### Tier D - Ship & Gameplay (NEW)
19. ShipClass + full object hierarchy (BaseObjectClass through ShipClass)
20. Property system (16 Create functions + generic Set* stub mechanism)
21. TGModelPropertyManager (RegisterLocalTemplate, ClearLocalTemplates)
22. ObjectGroup / ObjectGroupWithInfo
23. Ship lifecycle (Create, SetScript, SetNetType, SetupModel, SetupProperties, UpdateNodeOnly)
24. TGPoint3 / TGColorA (3D math types)
25. MultiplayerGame gameplay methods (GetShipFromPlayerID, DeletePlayerShipsAndTorps, etc.)
26. Mission group access (GetEnemyGroup, GetFriendlyGroup)

### Tier E - Stubs (NEW)
27. UI no-ops (TopWindow, MultiplayerWindow, SortedRegionMenu, all widget stubs)
28. Action/Sequence system (TGSequence, TGScriptAction -- needed for game flow)
29. Combat event stubs (WeaponHitEvent, ObjectExplodingEvent -- classes exist but events deferred)
30. Audio stubs (TGSoundAction, SubtitleAction, DynamicMusic, LoadDatabaseSoundInGroup)
31. Proximity/spatial stubs (ProximityManager, IsLocationEmptyTG)
32. Torpedo lifecycle stubs
33. Difficulty system (GetOffensiveDifficultyMultiplier, GetDefensiveDifficultyMultiplier)

---

## 6. Key Behavioral Insights

1. **UtopiaModule_InitializeNetwork takes 3 args** (self, wsn, TGString_name), not 2. Many references show only 2 args; the third is the server name string.

2. **TGNetwork_GetConnectStatus returns 2 for hosting** (counterintuitive -- 2=HOST, 3=CLIENT, 4=DISCONNECTED). Scripts check `GetConnectStatus() == 2` to verify hosting.

3. **TGMessage.SetGuaranteed(1)** is the primary way scripts mark messages as reliable. This maps to message+0x3A in the original.

4. **MultiplayerGame_Cast(game)** is called to downcast a Game pointer to MultiplayerGame. This is essentially a type check + reinterpret.

5. **VarManagerClass_MakeEpisodeEventType(offset)** generates unique event type IDs for mission scripts. Each mission gets a range of event types starting from a base offset.

6. **TGPlayerList is returned by reference from TGNetwork_GetPlayerList**. The list is owned by the network object and should not be deleted by Python code.

7. **App.py wraps Appc functions with shadow classes**. Python code calls `pMessage.SetGuaranteed(1)` which internally calls `Appc.TGMessage_SetGuaranteed(self.this, 1)`. Our reimplemented App.py must maintain this wrapping pattern.

8. **IsNull checks the SWIG pointer string format**. A null handle looks like `_ffffffff_p_void` or similar. The `IsNull()` function checks for the `ffffffff` prefix.

9. **Ship entities are lightweight data containers (~80 bytes), not simulation objects**. The relay server tracks identity (name, ObjID, NetType), ownership (NetPlayerID, IsPlayerShip), and death state (IsDying, IsDead). No physics, no position updates, no subsystem state.

10. **No torpedo entities needed on the server**. All scoring-relevant data (damage amount, firing player ID, hull hit flag) is embedded in network messages relayed between clients. The server does not need to track torpedo objects.

11. **Generic property base class handles ~200 Set* methods as no-ops**. The 16 property types each have 10-30 Set* methods called by hardpoint files during ship creation. A single base class with `__getattr__` returning a no-op callable handles all of them without individual method implementations.

12. **UI stubs must return dummy callable objects, NOT None**. Functions like `TopWindow_GetTopWindow()` and `SortedRegionMenu_GetWarpButton()` must return objects whose methods can be called without crashing. `RestartGame()`, `SetupEventHandlers()`, and other gameplay-critical functions access widget methods. Returning None causes AttributeError crashes that the original DedicatedServer.py mod catches with broad try/except blocks.

13. **Score tracking stays in Python dictionaries, not mirrored in ECS**. The scoring scripts maintain `g_kScoresDictionary`, `g_kDeathsDictionary`, etc. as plain Python dicts keyed by player ID. These do not need to be synchronized with ECS state -- they are purely Python-side bookkeeping that gets broadcast via SCORE_CHANGE_MESSAGE.

14. **The server IS the host**. `IsHost()` returns True, `IsClient()` returns False, `IsMultiplayer()` returns True. All host-side code paths in mission scripts (scoring handlers, message creation, game flow) execute on the dedicated server.
