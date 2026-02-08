# Reversed Function Signatures

## Confidence Levels
- **VERIFIED**: Confirmed from decompiled code + runtime testing
- **HIGH**: Clear from decompiled code, consistent with usage
- **MEDIUM**: Inferred from patterns, some ambiguity
- **LOW**: Speculative based on context

---

## Network Layer (TGNetwork / TGWinsockNetwork)

| Address | Name | Signature | Confidence |
|---------|------|-----------|------------|
| 0x006b3ec0 | TGNetwork_HostOrJoin | `int __thiscall(WSN*, int addr_or_0, void* password)` | VERIFIED |
| 0x006b4560 | TGNetwork::Update | `void __fastcall(WSN*)` | VERIFIED |
| 0x006b4c10 | TGNetwork::Send | `int __thiscall(WSN*, int peerID, TGMessage* msg, int groupID)` | HIGH |
| 0x006b5080 | QueueMessageToPeer | `int __thiscall(WSN*, TGMessage* msg, TGPeer* peer)` | HIGH |
| 0x006b55b0 | SendOutgoingPackets | `void __fastcall(WSN*)` | HIGH |
| 0x006b5c90 | ProcessIncomingPackets | `void __fastcall(WSN*)` | HIGH |
| 0x006b5f70 | DispatchIncomingQueue | `void __fastcall(WSN*)` | HIGH |
| 0x006b61e0 | ReliableACKHandler | `void __thiscall(WSN*, TGMessage* msg, TGPeer* peer)` | MEDIUM |
| 0x006b6ad0 | DispatchToApplication | `void __thiscall(WSN*, TGMessage* msg, TGPeer* peer)` | MEDIUM |
| 0x006b7070 | SetAddressInfo | `void __thiscall(WSN*, TGString* addr)` | MEDIUM |
| 0x006b7410 | CreatePeer | `TGPeer* __thiscall(WSN*, int peerID, int addr)` | MEDIUM |
| 0x006b75b0 | DisconnectPeer | `void __thiscall(WSN*, int peerID)` | MEDIUM |
| 0x006b8670 | ResetRetryCounter | `void(TGMessage* msg, int retryCount)` | HIGH |
| 0x006b8700 | CheckRetryTimer | `short(TGMessage* msg)` | MEDIUM |
| 0x006b9b20 | CreateUDPSocket | `int __thiscall(WSN*)` | MEDIUM |
| 0x006b9bb0 | SetPortNumber | `void(WSN*, int port, int unk)` | HIGH |
| 0x006b52b0 | DequeueForApp | `TGMessage* __thiscall(WSN*, char flag)` | MEDIUM |
| 0x006b4930 | GetConnectStatus | `int __fastcall(WSN*)` -- returns state (2=host,3=client,4=disconnected) | HIGH |

## MultiplayerGame

| Address | Name | Signature | Confidence |
|---------|------|-----------|------------|
| 0x0069e590 | MultiplayerGame::Constructor | `void* __thiscall(this*, char* name, int maxPlayers)` | HIGH |
| 0x0069efe0 | RegisterMPGameHandlers | `void(void)` | VERIFIED |
| 0x0069f250 | RegisterTimerHandlers | `void(void)` | HIGH |
| 0x0069f620 | ProcessGameMessage | `void __thiscall(MPGame*, TGMessage* msg, char hasPlayerSlot)` | MEDIUM |
| 0x0069efc0 | InitAllPlayerSlots | `void __fastcall(MPGame*)` | HIGH |
| 0x0069edc0 | MultiplayerGame::Tick | `void __fastcall(MPGame*)` | MEDIUM |
| 0x006a0a30 | NewPlayerHandler | `void __thiscall(MPGame*, TGEvent* evt)` | VERIFIED |
| 0x006a1b10 | ChecksumCompleteHandler | `void __thiscall(MPGame*, TGEvent* evt)` | VERIFIED |
| 0x006a7770 | InitializePlayerSlot | `void(void* slotPtr, int index)` | HIGH |

## NetFile / Checksum Manager

| Address | Name | Signature | Confidence |
|---------|------|-----------|------------|
| 0x006a30c0 | NetFile_Constructor | `void* __thiscall(NetFile*)` | VERIFIED |
| 0x006a3560 | RegisterNetFileHandler | `void(void)` | VERIFIED |
| 0x006a3820 | ChecksumRequestSender | `void __thiscall(NetFile*, int peerID)` | VERIFIED |
| 0x006a39b0 | ChecksumRequestBuilder | `void __thiscall(NetFile*, int index, ...)` | HIGH |
| 0x006a3cd0 | NetFile::ReceiveMessageHandler | `void __thiscall(NetFile*, TGEvent* evt)` | VERIFIED |
| 0x006a4260 | ChecksumResponseEntry | `void(NetFile*, TGMessage* msg)` | HIGH |
| 0x006a4560 | ChecksumResponseVerifier | `void(NetFile*, TGMessage* msg, int peerID)` | HIGH |
| 0x006a4a00 | ChecksumFail | `void(NetFile*, int peerID, int failType)` | HIGH |
| 0x006a4bb0 | ChecksumAllPassed | `void(NetFile*, int peerID)` | HIGH |
| 0x006a5860 | FileTransferProcessor | `void(NetFile*, int peerID)` | HIGH |
| 0x006a5df0 | Client_ChecksumRequestHandler | `void(NetFile*, TGMessage* msg)` | HIGH |
| 0x006a6500 | CleanupPlayerState | `void(NetFile*, int peerID)` | MEDIUM |

## Event System

| Address | Name | Signature | Confidence |
|---------|------|-----------|------------|
| 0x006da130 | RegisterHandlerFunction | `void(void* funcPtr, char* name)` | VERIFIED |
| 0x006da2a0 | PostEvent | `void(EventManager*, TGEvent* evt)` | HIGH |
| 0x006da2c0 | EventManager::ProcessEvents | `void __fastcall(EventManager*)` | VERIFIED |
| 0x006da300 | DispatchSingleEvent | `void(EventManager*, TGEvent* evt)` | HIGH |
| 0x006db380 | RegisterEventHandler | `void(HandlerRegistry*, int eventType, void* target, char* handlerName, char p1, char p2, int unk)` | VERIFIED |
| 0x006db620 | DispatchToHandlerChain | `void(HandlerRegistry*, TGEvent* evt)` | HIGH |

## GameSpy

| Address | Name | Signature | Confidence |
|---------|------|-----------|------------|
| 0x006ac1e0 | qr_handle_query | `void __cdecl(qr_t* qr, char* queryBuf, sockaddr* from)` | HIGH |
| 0x006ac5f0 | SendBasicResponse | `void __cdecl(qr_t*, sockaddr*, char* buf)` | MEDIUM |
| 0x006ac7a0 | SendRulesResponse | `void __cdecl(qr_t*, sockaddr*, char* buf)` | MEDIUM |
| 0x006ac810 | SendPlayersResponse | `void __cdecl(qr_t*, sockaddr*, char* buf)` | MEDIUM |
| 0x006ac880 | SendExtraResponse | `void __cdecl(qr_t*, sockaddr*, char* buf)` | MEDIUM |
| 0x006ac550 | FlushResponse | `void __cdecl(qr_t*, sockaddr*, char* buf)` -- appends queryid, sends via sendto | HIGH |
| 0x006ac660 | AppendAndSend | `void __cdecl(qr_t*, sockaddr*, char* buf, char* kvPairs)` -- handles fragmentation | MEDIUM |
| 0x0069d720 | ProcessQueryHandler | `void __thiscall(GameSpy*, TGEvent* evt)` | HIGH |
| 0x0069cc40 | SetGameModeHandler | `void __thiscall(GameSpy*, TGEvent* evt)` | HIGH |

## Gameplay Relay (newly reversed)

| Address | Name | Signature | Confidence |
|---------|------|-----------|------------|
| 0x0069f620 | ProcessGameMessage | `void __thiscall(MPGame*, TGMessage* msg, char hasPlayerSlot)` - Core relay: deserialize, clone, forward to all other peers | VERIFIED |
| 0x0069f880 | ProcessEventMessage | `void(TGMessage* msg)` - Deserializes event from msg, posts to EventManager | HIGH |
| 0x0069f930 | ProcessShipUpdate | `void(TGMessage* msg)` - Position/state update handler, uses "Forward" group relay | HIGH |
| 0x0069fbb0 | ProcessTorpedoUpdate | `void(TGMessage* msg)` - Torpedo/weapon state, uses "Forward" group relay | HIGH |
| 0x0069fda0 | ProcessEventWithType | `void(TGMessage* msg, int eventType)` - Event deserialization with custom event type | HIGH |
| 0x0069ff50 | ProcessObjectLookup | `void(TGMessage* msg)` - Lookup object by ID, call handler on it | MEDIUM |
| 0x005a1f50 | DeserializeGameObject | `TGObject* __cdecl(char* data, int size)` - Reads typeID+classID, creates and deserializes object | HIGH |
| 0x0047dab0 | ShipObject_Create | `ShipObject* __thiscall(void* mem, TGObject* obj, char* label)` - Creates ShipObject wrapper for network sync | HIGH |
| 0x006a17c0 | SerializeAndSendObject | `void(TGObject* obj, byte opcode)` - Serializes game object into message, sends to host or group | HIGH |
| 0x006a19c0 | FindPlayerSlotByPeerID | `int __thiscall(MPGame*, int peerID)` - Returns slot index (0-15) for given peer ID | VERIFIED |
| 0x006a1aa0 | FindObjectByNetID | `int(int netID)` - Searches all sets for object with field 0x2E4 matching netID | HIGH |
| 0x006b4de0 | SendToGroup | `int __thiscall(WSN*, char* groupName, TGMessage* msg)` - Finds group by name, sends to all members | HIGH |
| 0x006b4ec0 | SendToGroupMembers | `int __thiscall(WSN*, int groupPtr, TGMessage* msg)` - Sends msg to all peers in group, cleanup disconnected | HIGH |
| 0x006b70d0 | AddGroup | `int __thiscall(WSN*, TGGroup* group)` - Adds named group to group list (sorted array at WSN+0xF4) | HIGH |
| 0x006b8530 | TGMessage_GetData | `char* __thiscall(TGMessage*, int* outSize)` - Returns payload ptr (this+4), size via outSize (this+8) | VERIFIED |

## Utility / Hash

| Address | Name | Signature | Confidence |
|---------|------|-----------|------------|
| 0x0071f270 | ComputeChecksum | `int(void* obj, char* dir, char* filter, int recursive)` | HIGH |
| 0x007202e0 | HashString | `int(char* data, int length)` | MEDIUM |
| 0x00718cb0 | malloc_wrapper | `void*(int size)` | HIGH |
| 0x00718cf0 | free_wrapper | `void(int ptr)` | HIGH |
