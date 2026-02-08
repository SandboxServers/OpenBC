# Phase 1 (Standalone Server) Complete API Surface Catalog

## Summary
- **Estimated Total**: ~310 Appc functions needed
- **Must Implement (full behavior)**: ~185 functions
- **Stub OK (return None/0/empty)**: ~80 functions
- **Not Needed (client-only)**: ~45 functions referenced but skippable

---

## 1. TGNetwork Class (28 methods + 6 class constants)

**Source**: App.py lines 2697-2777

### Must Implement
| Function | Signature (from App.py) | What It Does | Confidence |
|----------|-------------------------|-------------|------------|
| `TGNetwork_Update` | `(self)` | Main network tick - receive/process/dispatch | Verified |
| `TGNetwork_Connect` | `(self)` | Start hosting, returns 0=success | Verified |
| `TGNetwork_Disconnect` | `(self)` | Tear down connection | Verified |
| `TGNetwork_GetConnectStatus` | `(self)` -> int | Returns status (2=hosting, 3=host-active) | Verified |
| `TGNetwork_CreateLocalPlayer` | `(self, TGString_name)` -> int | Returns player_id | Verified |
| `TGNetwork_DeleteLocalPlayer` | `(self, ?)` | Delete local player | High |
| `TGNetwork_SetName` | `(self, TGString)` | Set server name | Verified |
| `TGNetwork_GetName` | `(self)` -> TGStringPtr | Get server name | Verified |
| `TGNetwork_GetCName` | `(self)` -> str | Get C string name | High |
| `TGNetwork_SendTGMessage` | `(self, toID, msg)` | Send message to player (0=all) | Verified |
| `TGNetwork_SendTGMessageToGroup` | `(self, group, msg)` | Send to group | High |
| `TGNetwork_GetNextMessage` | `(self)` -> TGMessagePtr or None | Poll next message | Verified |
| `TGNetwork_GetHostID` | `(self)` -> int | Returns 1 when hosting | Verified |
| `TGNetwork_GetLocalID` | `(self)` -> int | Returns local player ID | Verified |
| `TGNetwork_IsHost` | `(self)` -> int | Returns 1 if hosting | Verified |
| `TGNetwork_GetNumPlayers` | `(self)` -> int | Count of connected players | Verified |
| `TGNetwork_SetPassword` | `(self, TGString)` | Set server password | Verified |
| `TGNetwork_GetPassword` | `(self)` -> TGStringPtr | Get server password | High |
| `TGNetwork_GetPlayerList` | `(self)` -> TGPlayerListPtr | Returns player list object | Verified |
| `TGNetwork_SetSendTimeout` | `(self, seconds)` | Set send timeout (e.g., 30) | Verified |
| `TGNetwork_SetConnectionTimeout` | `(self, seconds)` | Set connection timeout | Verified |
| `TGNetwork_SetBootReason` | `(self, reason)` | Set boot reason code | High |
| `TGNetwork_GetBootReason` | `(self)` -> int | Get boot reason | High |
| `TGNetwork_ReceiveMessageHandler` | `(self, msg?)` | Internal message handler | High |
| `TGNetwork_GetHostName` | `(self)` -> TGStringPtr | Get host player name | High |
| `TGNetwork_GetLocalIPAddress` | `(self)` -> str | Get local IP | High |

### Stub OK
| Function | Notes |
|----------|-------|
| `TGNetwork_SetEncryptor` | Encryption not needed initially |
| `TGNetwork_GetEncryptor` | Encryption not needed initially |
| `TGNetwork_AddGroup` | Group management, can defer |
| `TGNetwork_DeleteGroup` | Group management, can defer |
| `TGNetwork_GetGroup` | Group management, can defer |
| `TGNetwork_EnableProfiling` | Debug only |
| `TGNetwork_GetIPPacketHeaderSize` | Debug/stats only |
| `TGNetwork_GetTimeElapsedSinceLastHostPing` | Can return 0.0 |

### Class Constants (Must Define)
```
TGNetwork_TGNETWORK_MAX_SENDS_PENDING
TGNetwork_TGNETWORK_MAX_LOG_ENTRIES
TGNetwork_TGNETWORK_MAX_SEQUENCE_DIFFERENCE
TGNetwork_TGNETWORK_INVALID_ID
TGNetwork_TGNETWORK_GAMESPY_PLAYER_ID
TGNetwork_TGNETWORK_NULL_ID
TGNetwork_DEFAULT_BOOT
TGNetwork_TIMED_OUT
TGNetwork_INCORRECT_PASSWORD
TGNetwork_TOO_MANY_PLAYERS
TGNetwork_SERVER_BOOTED_YOU
TGNetwork_YOU_ARE_BANNED
```

### Static Functions (Must Implement)
| Function | Notes |
|----------|-------|
| `TGNetwork_RegisterHandlers` | `()` - 0 args, registers class handlers |
| `TGNetwork_AddClassHandlers` | `()` - 0 args, registers class handlers |
| `TGNetwork_RegisterMessageType` | `(net, ?)` - registers message type |
| `TGNetwork_GetTGNetworkList` | `(net)` - returns network list |

---

## 2. TGWinsockNetwork Class (4 methods + 3 class constants)

**Source**: App.py lines 2779-2802

### Must Implement
| Function | Signature | What It Does | Confidence |
|----------|-----------|-------------|------------|
| `new_TGWinsockNetwork` | `()` -> TGWinsockNetwork | Create new network object | Verified |
| `delete_TGWinsockNetwork` | `(self)` | Destroy network object | Verified |
| `TGWinsockNetwork_SetPortNumber` | `(self, port)` | Set listen port (e.g., 22000) | Verified |
| `TGWinsockNetwork_GetPortNumber` | `(self)` -> int | Get port number | High |
| `TGWinsockNetwork_GetLocalIPAddress` | `(self)` -> str | Get local IP address | High |
| `TGWinsockNetwork_GetIPPacketHeaderSize` | `(self)` -> int | Get packet header size | Stub OK |

### Static Functions
| Function | Notes |
|----------|-------|
| `TGWinsockNetwork_BanPlayerByIP` | Ban by IP address |
| `TGWinsockNetwork_BanPlayerByName` | Ban by player name |

### Class Constants
```
TGWinsockNetwork_TGWINSOCK_MAX_PACKET_SIZE
TGWinsockNetwork_WINSOCK_VERSION_NUMBER
TGWinsockNetwork_WINSOCK_PORT
```

---

## 3. TGMessage Class (47 methods + 3 class constants)

**Source**: App.py lines 2584-2657

### Must Implement
| Function | Signature | What It Does | Confidence |
|----------|-----------|-------------|------------|
| `new_TGMessage` | `(*args)` | Create message | Verified |
| `delete_TGMessage` | `(self)` | Destroy message | Verified |
| `TGMessage_SetGuaranteed` | `(self, flag)` | Mark as reliable | Verified |
| `TGMessage_IsGuaranteed` | `(self)` -> int | Check if reliable | High |
| `TGMessage_SetDataFromStream` | `(self, stream)` | Set data from buffer | Verified |
| `TGMessage_GetData` | `(self)` | Get raw data | High |
| `TGMessage_GetDataLength` | `(self)` -> int | Get data length | High |
| `TGMessage_SetData` | `(self, data)` | Set raw data | High |
| `TGMessage_GetBufferStream` | `(self)` -> TGBufferStreamPtr | Get read stream | Verified |
| `TGMessage_Copy` | `(self)` -> TGMessagePtr | Copy message | High |
| `TGMessage_Serialize` | `(self, stream)` | Serialize to stream | High |
| `TGMessage_GetFromID` | `(self)` -> int | Get sender ID | High |
| `TGMessage_GetFromAddress` | `(self)` | Get sender address | High |
| `TGMessage_SetFromID` | `(self, id)` | Set sender ID | High |
| `TGMessage_SetToID` | `(self, id)` | Set recipient ID | High |
| `TGMessage_GetToID` | `(self)` -> int | Get recipient ID | High |
| `TGMessage_SetHighPriority` | `(self, flag)` | Mark high priority | High |
| `TGMessage_IsHighPriority` | `(self)` -> int | Check priority | High |
| `TGMessage_SetSequenceNumber` | `(self, seq)` | Set sequence | High |
| `TGMessage_GetSequenceNumber` | `(self)` -> int | Get sequence | High |
| `TGMessage_GetBufferSpaceRequired` | `(self)` -> int | Get buffer size needed | High |

### Stub OK (Retry/Backoff/Multipart - defer)
```
TGMessage_SetFirstResendTime, GetFirstResendTime
TGMessage_SetBackoffType, GetBackoffType, SetBackoffFactor, GetBackoffFactor
TGMessage_ReadyToResend, SetBackoffTime, GetBackoffTime
TGMessage_SetTimeStamp, GetTimeStamp, SetFirstSendTime, GetFirstSendTime
TGMessage_SetNumRetries, IncrementNumRetries, GetNumRetries
TGMessage_SetMultiPartSequenceNumber, GetMultiPartSequenceNumber
TGMessage_SetMultiPartCount, GetMultiPartCount, SetMultiPart, IsMultiPart
TGMessage_SetAggregate, IsAggregate, BreakUpMessage
TGMessage_Merge, OverrideOldPackets, SetDataNoCopy
TGMessage_SetFromAddress
```

### Static Functions
| Function | Notes |
|----------|-------|
| `TGMessage_Create` | `()` -> TGMessagePtr - Factory function | Must Implement |
| `TGMessage_UnSerialize` | `(stream)` -> TGMessagePtr | Must Implement |

### Class Constants
```
TGMessage_TGMESSAGE_SAME_BACKOFF
TGMessage_TGMESSAGE_LINEAR_BACKOFF
TGMessage_TGMESSAGE_DOUBLE_BACKOFF
```

---

## 4. TGMessage Subtypes (6 classes, ~20 methods)

### Must Implement (all create + serialize + key methods)
| Class | Key Methods | Notes |
|-------|-------------|-------|
| TGNameChangeMessage | new_, delete_, Copy, Serialize, GetBufferSpaceRequired, _UnSerialize | Name changes |
| TGAckMessage | new_, delete_, Copy, Serialize, GetBufferSpaceRequired, SetSystemMessage, IsSystemMessage, _UnSerialize | ACK packets |
| TGDoNothingMessage | new_, delete_, Copy, Serialize, GetBufferSpaceRequired, _UnSerialize | Keepalive/ping |
| TGBootPlayerMessage | new_, delete_, Copy, Serialize, SetBootReason, GetBootReason, GetBufferSpaceRequired, _UnSerialize | Boot players |
| TGConnectMessage | new_, delete_, Copy, Serialize, GetBufferSpaceRequired, _UnSerialize | Connection |
| TGDisconnectMessage | new_, delete_, Copy, Serialize, GetBufferSpaceRequired, _UnSerialize | Disconnection |

---

## 5. TGBufferStream Class (30 methods)

**Source**: App.py lines 453-500

### Must Implement
| Function | Signature | What It Does | Confidence |
|----------|-----------|-------------|------------|
| `new_TGBufferStream` | `(*args)` | Create buffer stream | Verified |
| `delete_TGBufferStream` | `(self)` | Destroy | Verified |
| `TGBufferStream_OpenBuffer` | `(self, size)` | Open with size (e.g., 256) | Verified |
| `TGBufferStream_Close` | `(self)` | Close buffer (alias: CloseBuffer) | Verified |
| `TGBufferStream_ReadChar` | `(self)` -> str | Read 1 char | Verified |
| `TGBufferStream_WriteChar` | `(self, ch)` | Write 1 char | Verified |
| `TGBufferStream_ReadInt` | `(self)` -> int | Read 4-byte int | Verified |
| `TGBufferStream_WriteInt` | `(self, val)` | Write 4-byte int | Verified |
| `TGBufferStream_ReadShort` | `(self)` -> int | Read 2-byte short | High |
| `TGBufferStream_WriteShort` | `(self, val)` | Write 2-byte short | High |
| `TGBufferStream_ReadFloat` | `(self)` -> float | Read 4-byte float | High |
| `TGBufferStream_WriteFloat` | `(self, val)` | Write 4-byte float | High |
| `TGBufferStream_ReadBool` | `(self)` -> int | Read bool | High |
| `TGBufferStream_WriteBool` | `(self, val)` | Write bool | High |
| `TGBufferStream_ReadCString` | `(self)` -> str | Read C string | High |
| `TGBufferStream_WriteCString` | `(self, str)` | Write C string | High |
| `TGBufferStream_ReadCLine` | `(self)` -> str | Read line | High |
| `TGBufferStream_WriteCLine` | `(self, str)` | Write line | High |
| `TGBufferStream_GetBuffer` | `(self)` | Get raw buffer | High |
| `TGBufferStream_SetWriteMode` | `(self, mode)` | Set read/write mode | High |
| `TGBufferStream_GetWriteMode` | `(self)` -> int | Get mode | High |

### Stub OK
```
TGBufferStream_ReadDouble, WriteDouble
TGBufferStream_ReadLong, WriteLong
TGBufferStream_ReadCWLine, WriteCWLine
TGBufferStream_ReadWChar, WriteWChar
TGBufferStream_ReadID, WriteID
TGBufferStream_Read, Write (raw byte read/write)
```

---

## 6. TGEvent System (6 classes, ~60 methods)

### TGEvent (17 methods) - Must Implement ALL
**Source**: App.py lines 673-717
```
new_TGEvent(*args), delete_TGEvent(self)
TGEvent_GetEventType(self) -> int
TGEvent_SetEventType(self, type)
TGEvent_GetSource(self) -> TGObjectPtr
TGEvent_SetSource(self, obj)
TGEvent_GetDestination(self) -> TGEventHandlerObjectPtr
TGEvent_SetDestination(self, obj)
TGEvent_GetTimestamp(self) -> float
TGEvent_SetTimestamp(self, time)
TGEvent_Copy(self, other)
TGEvent_Duplicate(self) -> TGEventPtr
TGEvent_SetLogged/IsLogged, SetPrivate/IsPrivate, SetNotSaved/IsNotSaved
TGEvent_IncRefCount/DecRefCount/GetRefCount
```

### TGEvent Static Functions
```
TGEvent_Create() -> TGEventPtr          # MUST IMPLEMENT
TGEvent_Cast(obj) -> TGEventPtr         # MUST IMPLEMENT
```

### Typed Events - Must Implement
| Class | Methods | Notes |
|-------|---------|-------|
| TGBoolEvent | new_, GetBool, SetBool, ToggleBool + _Create, _Cast | Used by menu toggles |
| TGIntEvent | new_, GetInt, SetInt + _Create, _Cast | Used by MissionShared |
| TGFloatEvent | new_, GetFloat, SetFloat + _Create, _Cast | Used by timers |
| TGStringEvent | new_, GetString, SetString + _Create, _Cast | Used by name changes |
| TGCharEvent | new_, GetChar, SetChar + _Create, _Cast | Used sparingly |
| TGShortEvent | new_, GetShort, SetShort + _Create, _Cast | Used sparingly |
| TGVoidPtrEvent | new_, GetVoidPtr, SetVoidPtr + _Create | Stub OK for phase 1 |
| TGObjPtrEvent | new_, GetObjPtr, SetObjPtr + _Create, _Cast | Stub OK for phase 1 |
| TGMessageEvent | new_, GetMessage, SetMessage + _Create | MUST IMPLEMENT |
| TGPlayerEvent | new_, GetPlayerID, SetPlayerID + _Create | MUST IMPLEMENT |

---

## 7. TGEventHandlerObject (6 methods)

**Source**: App.py lines 883-904

### Must Implement ALL
```
TGEventHandlerObject_ProcessEvent(self, event)
TGEventHandlerObject_CallNextHandler(self, event)
TGEventHandlerObject_AddPythonFuncHandlerForInstance(self, evt_type, 'Module.FuncName')
TGEventHandlerObject_AddPythonMethodHandlerForInstance(self, evt_type, 'Module.FuncName')
TGEventHandlerObject_RemoveHandlerForInstance(self, evt_type, ?)
TGEventHandlerObject_RemoveAllInstanceHandlers(self)
TGEventHandlerObject_Cast(obj) -> TGEventHandlerObjectPtr  # Static
delete_TGEventHandlerObject(self)
```

---

## 8. TGEventManager (4 methods)

**Source**: App.py lines 906-922

### Must Implement ALL
```
TGEventManager_AddBroadcastPythonFuncHandler(self, evt_type, target_obj, 'Module.FuncName')
TGEventManager_AddBroadcastPythonMethodHandler(self, evt_type, target_obj, 'Module.FuncName')
TGEventManager_RemoveBroadcastHandler(self, ?)
TGEventManager_RemoveAllBroadcastHandlersForObject(self, obj)
TGEventManager_AddEvent(self, event)  # NOT in App.py methods but referenced in docs
```

---

## 9. TGTimer / TGTimerManager (10 methods)

**Source**: App.py lines 925-969

### Must Implement
```
# TGTimer
new_TGTimer(*args), delete_TGTimer(self)
TGTimer_SetTimerStart(self, time), TGTimer_GetTimerStart(self)
TGTimer_SetDelay(self, delay), TGTimer_GetDelay(self)
TGTimer_SetDuration(self, duration), TGTimer_GetDuration(self)
TGTimer_SetEvent(self, event), TGTimer_GetEvent(self) -> TGEventPtr
TGTimer_Create() -> TGTimerPtr  # Static factory

# TGTimerManager
TGTimerManager_AddTimer(self, timer)
TGTimerManager_RemoveTimer(self, timer)
TGTimerManager_DeleteTimer(self, timer_id)
```

---

## 10. UtopiaModule Class (~67 methods, ~40 needed for server)

**Source**: App.py lines 3176-3271

### Must Implement
```
UtopiaModule_InitializeNetwork(self, wsn, TGString_name)  # 3 args!
UtopiaModule_TerminateNetwork(self)
UtopiaModule_GetNetwork(self) -> TGNetworkPtr
UtopiaModule_SetIsHost(self, flag)
UtopiaModule_IsHost(self) -> int
UtopiaModule_SetMultiplayer(self, flag)
UtopiaModule_IsMultiplayer(self) -> int
UtopiaModule_SetIsClient(self, flag)
UtopiaModule_IsClient(self) -> int
UtopiaModule_GetGameTime(self) -> float
UtopiaModule_SetGameTime(self, time)
UtopiaModule_GetRealTime(self) -> float
UtopiaModule_IsGamePaused(self) -> int
UtopiaModule_SetTimeRate(self, rate)
UtopiaModule_SetTimeScale(self, scale)
UtopiaModule_Pause(self)
UtopiaModule_ForceUnpause(self)
UtopiaModule_SetGameName(self, TGString)
UtopiaModule_GetGameName(self) -> str
UtopiaModule_SetGamePath(self, path)
UtopiaModule_GetGamePath(self) -> str
UtopiaModule_SetDataPath(self, path)
UtopiaModule_GetDataPath(self) -> str
UtopiaModule_GetCaptainName(self) -> TGStringPtr
UtopiaModule_SetCaptainName(self, TGString)
UtopiaModule_SetPlayerNumber(self, num)
UtopiaModule_GetPlayerNumber(self) -> int
UtopiaModule_SetProcessingPackets(self, flag)
UtopiaModule_IsProcessingPackets(self) -> int
UtopiaModule_SetUnusedClientID(self, id)
UtopiaModule_GetUnusedClientID(self) -> int
UtopiaModule_IncrementClientID(self)
UtopiaModule_SetIgnoreClientIDForObjectCreation(self, flag)
UtopiaModule_IsIgnoreClientIDForObjectCreation(self) -> int
UtopiaModule_SetFriendlyFireWarningPoints(self, pts)
UtopiaModule_GetFriendlyFireWarningPoints(self) -> int
UtopiaModule_GetCurrentFriendlyFire(self) -> float
UtopiaModule_SetCurrentFriendlyFire(self, val)
UtopiaModule_SetMaxFriendlyFire(self, val)
UtopiaModule_GetFriendlyFireTolerance(self) -> float
```

### Stub OK
```
UtopiaModule_GetCamera, RenderDefaultCamera (rendering)
UtopiaModule_IsDirectStart, CDCheck (client checks)
UtopiaModule_SaveToFile, LoadFromFile, etc. (save/load)
UtopiaModule_LoadEpisodeSounds (audio)
UtopiaModule_SetMaxTorpedoLoad, GetMaxTorpedoLoad (defer to Phase 2)
UtopiaModule_CreateGameSpy, TerminateGameSpy, GetGameSpy (GameSpy)
UtopiaModule_DetermineModemConnection, IsModemConnection (obsolete)
UtopiaModule_GetTestMenuState (debug)
UtopiaModule_SetFriendlyTractorTime/Warning/Max (Phase 2)
UtopiaModule_IsLoading
UtopiaModule_SetLoadFromFileName, SetInternalLoadFileName
UtopiaModule_GetSaveFilename, GetLoadFilename
UtopiaModule_SaveMissionState, LoadMissionState
```

### Static Functions (Must Implement)
```
UtopiaModule_GetNextEventType() -> int
UtopiaModule_GetGameVersion() -> str
UtopiaModule_ConvertGameUnitsToKilometers(val) -> float
UtopiaModule_ConvertKilometersToGameUnits(val) -> float
UtopiaModule_SetGameUnitConversionFactor(val)
UtopiaModule_CLIENT_RANGE  # constant
```

---

## 11. TGConfigMapping Class (10 methods)

**Source**: App.py lines 305-331

### Must Implement ALL
```
TGConfigMapping_GetIntValue(self, section, key) -> int
TGConfigMapping_GetFloatValue(self, section, key) -> float
TGConfigMapping_GetStringValue(self, section, key) -> str
TGConfigMapping_GetTGStringValue(self, section, key) -> TGStringPtr
TGConfigMapping_SetIntValue(self, section, key, value)
TGConfigMapping_SetFloatValue(self, section, key, value)
TGConfigMapping_SetStringValue(self, section, key, value)
TGConfigMapping_SetTGStringValue(self, section, key, TGString)
TGConfigMapping_HasValue(self, section, key) -> int
TGConfigMapping_LoadConfigFile(self, filename)
TGConfigMapping_SaveConfigFile(self, filename)
```

---

## 12. VarManagerClass (7 methods)

**Source**: App.py lines 4185-4208

### Must Implement ALL
```
VarManagerClass_SetFloatVariable(self, scope, key, float_val)
VarManagerClass_GetFloatVariable(self, scope, key) -> float
VarManagerClass_SetStringVariable(self, scope, key, TGString_val)
VarManagerClass_GetStringVariable(self, scope, key) -> str
VarManagerClass_DeleteAllVariables(self)
VarManagerClass_DeleteAllScopedVariables(self, scope)
VarManagerClass_MakeEpisodeEventType(self, offset) -> int
```

---

## 13. Game / ScriptObject Classes

### Game Class (App.py lines 3685-3746) - Must Implement
```
Game_GetCurrentGame() -> GamePtr      # Static - CRITICAL
Game_GetCurrentPlayer() -> ShipClassPtr   # Static
Game_Create(*args) -> GamePtr         # Static
Game_GetNextEventType() -> int        # Static
Game_GetDifficulty() -> int           # Static
Game_SetDifficulty(val)               # Static

# Instance methods
Game_LoadEpisode(self, episode_str)
Game_GetCurrentEpisode(self) -> EpisodePtr
Game_GetPlayer(self) -> ShipClassPtr
Game_GetPlayerGroup(self) -> ObjectGroupPtr
Game_GetPlayerSet(self) -> SetClassPtr
Game_SetPlayer(self, ship)
Game_GetScore(self), GetRating(self), GetKills(self)
Game_GetTorpsFired(self), GetTorpsHit(self)
Game_Terminate(self)
Game_SetGodMode(self, flag), InGodMode(self)
Game_SetUIShipID(self, id)
Game_LoadSoundInGroup(self, ...) -> TGSoundPtr  # Stub OK (audio)
Game_LoadDatabaseSoundInGroup(self, ...) -> TGSoundPtr  # Stub OK (audio)
```

### ScriptObject (App.py lines 3638-3675) - Must Implement
```
ScriptObject_GetScript(self) -> str
ScriptObject_GetInstanceNextEventType(self) -> int
ScriptObject_SetScript(self, script_module)  # NOT in App.py but inferred
ScriptObject_SetEventCount(self, count)
ScriptObject_SetOnIdleFunc(self, func)
ScriptObject_SetAssetsLoaded(self, flag)
ScriptObject_LoadSound(self, ...) -> TGSoundPtr  # Stub OK
ScriptObject_LoadDatabaseSound(self, ...) -> TGSoundPtr  # Stub OK
```

---

## 14. MultiplayerGame Class (11 methods + 1 constant)

**Source**: App.py lines 9994-10029

### Must Implement ALL
```
new_MultiplayerGame(*args)
delete_MultiplayerGame(self)
MultiplayerGame_SetReadyForNewPlayers(self, flag)
MultiplayerGame_IsReadyForNewPlayers(self) -> int
MultiplayerGame_SetMaxPlayers(self, n)
MultiplayerGame_GetMaxPlayers(self) -> int
MultiplayerGame_GetNumberPlayersInGame(self) -> int
MultiplayerGame_IsPlayerInGame(self, player_id) -> int
MultiplayerGame_GetPlayerNumberFromID(self, id) -> int
MultiplayerGame_GetPlayerName(self, player_id) -> TGStringPtr
MultiplayerGame_GetShipFromPlayerID(self, player_id) -> ShipClassPtr
MultiplayerGame_DeletePlayerShipsAndTorps(self)
MultiplayerGame_DeleteObjectFromGame(self, obj)
MultiplayerGame_IsPlayerUsingModem(self, player_id) -> int

# Static
MultiplayerGame_Cast(game) -> MultiplayerGamePtr
MultiplayerGame_Create(game) -> MultiplayerGamePtr
MultiplayerGame_MAX_PLAYERS  # constant
```

---

## 15. Episode / Mission Classes

### Episode (App.py lines 3748-3770) - Must Implement
```
Episode_GetCurrentMission(self) -> MissionPtr
Episode_LoadMission(self, mission_str)
Episode_RegisterGoal(self, ?)
Episode_RemoveGoal(self, ?)
```

### Mission (App.py lines 3772-3792) - Must Implement
```
Mission_GetPrecreatedShip(self, ?) -> ShipClassPtr
Mission_AddPrecreatedShip(self, ship)
```

### LoadEpisodeAction - Must Implement
```
LoadEpisodeAction_Create(game) -> ...
LoadEpisodeAction_Play(self)
```

---

## 16. TGPlayerList / TGNetPlayer / TGNetGroup

### TGPlayerList (App.py lines 2659-2695) - Must Implement
```
new_TGPlayerList(*args), delete_TGPlayerList(self)
TGPlayerList_AddPlayer(self, player)
TGPlayerList_DeletePlayer(self, player_id)
TGPlayerList_DeletePlayerAtIndex(self, idx)
TGPlayerList_GetNumPlayers(self) -> int
TGPlayerList_GetPlayerAtIndex(self, idx) -> TGNetPlayerPtr
TGPlayerList_GetPlayer(self, id) -> TGNetPlayerPtr
TGPlayerList_GetPlayerFromAddress(self, addr) -> TGNetPlayerPtr
TGPlayerList_GetPlayerList(self)
TGPlayerList_ChangePlayerNetID(self, old_id, new_id)
TGPlayerList_DeleteAllPlayers(self)
TGPlayerList_DeletePlayerByAddress(self, addr)
```

### TGNetPlayer (App.py lines 3053-3091) - Must Implement
```
new_TGNetPlayer(*args), delete_TGNetPlayer(self)
TGNetPlayer_SetName(self, TGString)
TGNetPlayer_GetName(self) -> TGStringPtr
TGNetPlayer_SetNetID(self, id)
TGNetPlayer_GetNetID(self) -> int
TGNetPlayer_SetNetAddress(self, addr)
TGNetPlayer_GetNetAddress(self) -> int
TGNetPlayer_SetDisconnected(self, flag)
TGNetPlayer_IsDisconnected(self) -> int
TGNetPlayer_SetConnectUID(self, uid)
TGNetPlayer_GetConnectUID(self) -> int
TGNetPlayer_SetConnectAddress(self, addr)
TGNetPlayer_GetConnectAddress(self) -> int
TGNetPlayer_GetBytesPerSecondTo(self) -> float
TGNetPlayer_GetBytesPerSecondFrom(self) -> float
TGNetPlayer_Copy(self) -> TGNetPlayerPtr
TGNetPlayer_Compare(self, other) -> int
```

### TGNetGroup (App.py lines 3024-3051) - Stub OK initially
```
new_TGNetGroup(*args), delete_TGNetGroup(self)
TGNetGroup_SetName, GetName, GetPlayerList, Validate
TGNetGroup_AddPlayerToGroup, DeletePlayerFromGroup, IsPlayerInGroup
TGNetGroup_Copy
```

---

## 17. TGString Class (7 methods)

**Source**: App.py lines 411-437

### Must Implement ALL
```
new_TGString(*args)       # Create from Python string
delete_TGString(self)
TGString_GetLength(self) -> int
TGString_SetString(self, str)
TGString_FindC(self, str) -> int
TGString_Find(self, TGString) -> int
TGString_CompareC(self, str) -> int
TGString_Compare(self, TGString) -> int
TGString_Append(self, str) -> TGStringPtr
```

---

## 18. TGObject Base Class (5 methods)

**Source**: App.py lines 356-373

### Must Implement
```
TGObject_GetObjType(self) -> int
TGObject_IsTypeOf(self, type_id) -> int
TGObject_GetObjID(self) -> int
TGObject_Destroy(self)
TGObject_Print(self)
```

---

## 19. TGAttrObject / TGTemplatedAttrObject (8 methods)

### Must Implement
```
TGAttrObject_LookupAttrValue(self, key) -> str
TGAttrObject_GetAttrValue(self, key) -> str
TGAttrObject_SetAttrValue(self, key, value)
TGAttrObject_RemoveAttr(self, key)
TGAttrObject_LookupAttrInt(self, key) -> int
TGAttrObject_GetAttrInt(self, key) -> int
TGAttrObject_SetAttrInt(self, key, value)
TGTemplatedAttrObject_LookupLocalAttrValue(self, key) -> str
```

---

## 20. SetManager / SetClass (Minimal for Server)

### SetManager - Must Implement
```
SetManager_ClearRenderedSet(self)
SetManager_DeleteAllSets(self)
SetManager_AddSet(self, set)
SetManager_DeleteSet(self, name)
SetManager_GetSet(self, name) -> SetClassPtr
SetManager_GetRenderedSet(self) -> SetClassPtr
SetManager_GetNumSets(self) -> int
SetManager_MakeRenderedSet(self, name)
SetManager_Terminate(self)
```

### SetClass (for mission set management) - Subset needed
```
SetClass_Create(name) -> SetClassPtr
SetClass_Cast(obj) -> SetClassPtr
SetClass_GetName(self) -> str
SetClass_SetName(self, name)
SetClass_AddObjectToSet(self, obj)
SetClass_RemoveObjectFromSet(self, obj) -> ObjectClassPtr
SetClass_DeleteObjectFromSet(self, obj)
SetClass_GetObject(self, name) -> ObjectClassPtr
SetClass_GetObjectByID(self, id) -> ObjectClassPtr
SetClass_GetFirstObject(self) -> ObjectClassPtr
SetClass_GetNextObject(self) -> ObjectClassPtr
SetClass_IsRendered(self) -> int
```

---

## 21. TGLocalizationManager (5 methods)

**Source**: App.py lines 526-550

### Must Implement (scripts load databases for string lookup)
```
TGLocalizationManager_Load(self, filename) -> TGLocalizationDatabasePtr
TGLocalizationManager_Unload(self, db)
TGLocalizationManager_GetIfRegistered(self, filename) -> TGLocalizationDatabasePtr
TGLocalizationManager_DeleteAll(self)
TGLocalizationManager_Purge(self)
TGLocalizationManager_RegisterDatabase(self, db)
```

---

## 22. TGSystemWrapperClass (10 methods)

**Source**: App.py lines 256-285

### Must Implement
```
TGSystemWrapperClass_GetTimeInSeconds(self) -> float
TGSystemWrapperClass_GetTimeInMilliseconds(self) -> int
TGSystemWrapperClass_GetTimeElapsedInSeconds(self) -> float
TGSystemWrapperClass_GetTimeElapsedInMilliseconds(self) -> int
TGSystemWrapperClass_GetTimeSinceFrameStart(self) -> float
TGSystemWrapperClass_GetUpdateNumber(self) -> int
TGSystemWrapperClass_GetRandomNumber(self, max) -> int
TGSystemWrapperClass_SetRandomSeed(self, seed)
TGSystemWrapperClass_GetIniInt(self, section, key) -> int
TGSystemWrapperClass_GetIniFloat(self, section, key) -> float
TGSystemWrapperClass_GetIniString(self, section, key) -> str
```

### Stub OK
```
TGSystemWrapperClass_GetVerticalAspectRatio -> 0.75
TGSystemWrapperClass_IsForeground -> 1
TGSystemWrapperClass_TakeScreenshot (client only)
TGSystemWrapperClass_SetDefaultScreenShotPath (client only)
```

---

## 23. Utility Functions

### Must Implement
```
IsNull(obj) -> int                    # Check if SWIG pointer is null
Breakpoint()                          # Debug breakpoint (stub: pass)
TGScriptAction_Create(module, func)   # Create script action - needed by missions
```

---

## 24. Required Constants (ET_*, CT_*, MWT_*, SPECIES_*, TEAM_*, etc.)

### Network Event Types (Must Define)
```
ET_NETWORK_MESSAGE_EVENT
ET_NETWORK_CONNECT_EVENT
ET_NETWORK_DISCONNECT_EVENT
ET_NETWORK_NEW_PLAYER
ET_NETWORK_DELETE_PLAYER
ET_NETWORK_GAMESPY_MESSAGE
ET_NETWORK_NAME_CHANGE_EVENT
FIRST_TGNETWORK_MODULE_EVENT_TYPE
```

### Server/Multiplayer Event Types (Must Define)
```
ET_CREATE_SERVER, ET_CREATE_CLIENT, ET_CREATE_CLIENT_AND_SERVER, ET_CREATE_DIRECT_CLIENT
ET_START, ET_KILL_GAME, ET_EXIT_GAME, ET_EXIT_PROGRAM
ET_CHECKSUM_COMPLETE, ET_SYSTEM_CHECKSUM_COMPLETE, ET_SYSTEM_CHECKSUM_FAILED
ET_LOAD_EPISODE, ET_LOAD_MISSION, ET_EPISODE_START, ET_MISSION_START
ET_SET_PLAYER, ET_NEW_MULTIPLAYER_GAME, ET_NEW_PLAYER_IN_GAME
ET_CANCEL, ET_OKAY, ET_CANCEL_CONNECT
ET_SET_MISSION_NAME, ET_SET_GAME_MODE
ET_LOCAL_INTERNET_HOST, ET_END_GAME_OKAY
ET_SELECT_MISSION, ET_START
ET_WARP_BUTTON_PRESSED, ET_SCAN
ET_FRIENDLY_FIRE_DAMAGE, ET_FRIENDLY_FIRE_REPORT, ET_FRIENDLY_FIRE_GAME_OVER
ET_OBJECT_DELETED, ET_OBJECT_CREATED, ET_OBJECT_CREATED_NOTIFY
ET_OBJECT_DESTROYED, ET_OBJECT_EXPLODING
```

### Game Object Event Types (Phase 2 but referenced)
```
ET_WEAPON_HIT, ET_SUBSYSTEM_DAMAGED/DESTROYED/DISABLED/OPERATIONAL/STATE_CHANGED
ET_TORPEDO_FIRED/RELOAD, ET_PHASER_*/ET_TRACTOR_*
ET_ENTERED_SET, ET_EXITED_SET, ET_TORPEDO_ENTERED_SET, ET_TORPEDO_EXITED_SET
```

### Input Events (Stub OK - only ET_INPUT_TOGGLE_SCORE_WINDOW and ET_INPUT_TOGGLE_CHAT_WINDOW referenced by MP)
```
ET_INPUT_TOGGLE_SCORE_WINDOW
ET_INPUT_TOGGLE_CHAT_WINDOW
ET_KEYBOARD, ET_MOUSE
```

### Class Type Constants (Must Define)
```
CT_MULTIPLAYER_GAME    # Used by IsTypeOf checks
CT_GAME, CT_EPISODE, CT_MISSION, CT_SET
CT_SHIP, CT_TORPEDO, CT_OBJECT, CT_PHYSICS_OBJECT, CT_DAMAGEABLE_OBJECT
CT_NETWORK, CT_MESSAGE_EVENT, CT_PLAYER_EVENT
CT_TGEVENT, CT_TGEVENTHANDLEROBJECT, CT_TGTIMER
CT_VAR_MANAGER
```

### Other Constants (Must Define)
```
NULL_ID
MAX_MESSAGE_TYPES
NULL_STRING_INDEX
PI, TWO_PI, HALF_PI, FOURTH_PI
SPECIES_* (all ~30 species constants)
TEAM_* (all 10 team constants)
GENUS_* (UNKNOWN, SHIP, STATION, ASTEROID)
MWT_MULTIPLAYER (and others if TopWindow is stubbed)
```

---

## 25. Server-Side Script Dependency Map

### Direct Multiplayer Scripts
1. **Multiplayer/MultiplayerGame.py** - Game init/terminate
   - Calls: `App.g_kSetManager.ClearRenderedSet()`, `pGame.LoadEpisode()`, `DynamicMusic.*` (stub OK)
   - Calls: `LoadTacticalSounds.LoadSounds()` (stub OK), `pGame.LoadSound()` (stub OK)

2. **Multiplayer/MissionShared.py** - Core game logic
   - Calls: `App.g_kVarManager.MakeEpisodeEventType()`, `App.g_kEventManager.AddBroadcastPythonFuncHandler()`
   - Calls: `App.TGMessage_Create()`, `App.TGBufferStream()`, `pNetwork.SendTGMessage()`
   - Calls: `App.MultiplayerGame_Cast()`, `App.Game_GetCurrentGame()`
   - Calls: `MissionLib.CreateTimer()`, `MissionLib.GetMission()`
   - Calls: `App.g_kLocalizationManager.Load/Unload()`
   - Calls: `App.g_kUtopiaModule.IsHost()`, `GetRealTime()`, `SetFriendlyFireWarningPoints()`

3. **Multiplayer/SpeciesToShip.py** - Ship creation over network (CRITICAL)
   - Calls: `App.ShipClass_Create()`, `pShip.SetScript()`, `pShip.SetNetType()`
   - Calls: `pShip.SetupModel()`, `pShip.GetPropertySet()`, `pShip.SetupProperties()`, `pShip.UpdateNodeOnly()`
   - Calls: `App.g_kModelPropertyManager.ClearLocalTemplates()`

4. **Multiplayer/Modifier.py** - Pure Python score modifiers (no App calls)

5. **Multiplayer/Episode/Episode.py** - Episode script loader

6. **Multiplayer/Episode/Mission1-5/** - Individual mission scripts
   - Call various ship creation, AI, event handler APIs

### Support Scripts
7. **MissionLib.py** - Timer creation, mission queries
   - `CreateTimer()` -> uses `App.TGEvent_Create()`, `App.TGTimer_Create()`, `App.g_kTimerManager.AddTimer()`
   - `GetMission()` -> uses `App.Game_GetCurrentGame().GetCurrentEpisode().GetCurrentMission()`

8. **MissionMenusShared.py** - Game config UI (mostly client but has shared state)

---

## 26. Implementation Priority Order

### Tier A - Bootstrap (must work first)
1. All constants (ET_*, CT_*, SPECIES_*, etc.)
2. TGString (new, delete, methods)
3. TGObject (GetObjType, IsTypeOf, GetObjID)
4. TGEvent + typed events (Create, Get/Set methods)
5. TGEventHandlerObject (AddPythonFuncHandlerForInstance, ProcessEvent, CallNextHandler)
6. TGEventManager (AddBroadcastPythonFuncHandler, RemoveBroadcastHandler)
7. Globals (g_kConfigMapping, g_kUtopiaModule, g_kVarManager, g_kEventManager, g_kTimerManager, etc.)

### Tier B - Network
8. TGWinsockNetwork (new, SetPortNumber)
9. TGNetwork (Connect, Update, SendTGMessage, GetNextMessage, IsHost, etc.)
10. TGMessage + subtypes (Create, Serialize, SetGuaranteed, SetDataFromStream, etc.)
11. TGBufferStream (OpenBuffer, Read/Write*, Close)
12. TGPlayerList, TGNetPlayer (player management)
13. UtopiaModule (InitializeNetwork, GetNetwork, IsHost, etc.)

### Tier C - Game Flow
14. TGConfigMapping (Get/Set values, LoadConfigFile)
15. VarManagerClass (Get/Set variables, MakeEpisodeEventType)
16. Game/MultiplayerGame (GetCurrentGame, Cast, Create, LoadEpisode, etc.)
17. Episode/Mission (GetCurrentMission, LoadMission)
18. TGTimer/TGTimerManager (CreateTimer flow)
19. SetManager/SetClass (basic set management)

### Tier D - Stubs for script compatibility
20. TGLocalizationManager (Load/Unload databases)
21. TGSystemWrapperClass (time functions, random)
22. IsNull, Breakpoint, TGScriptAction_Create
23. CPyDebug (Print method - logging)
