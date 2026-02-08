# OpenBC Phase 1: Reverse Engineering Gap Analysis

## Document Status
- **Generated**: 2026-02-07, **Revised**: 2026-02-08
- **Source**: game-reverse-engineer agent analysis of STBC-Dedicated-Server decompiled code, expanded with gameplay relay findings
- **Reference repo**: `../STBC-Dedicated-Server/`

---

## 1. Already Reversed (High Confidence)

These systems are fully traced through decompiled code with verified behavior.

### 1.1 Network Initialization Pipeline
- **FUN_00445d90 (UtopiaModule::InitMultiplayer)** - VERIFIED
  - Creates TGWinsockNetwork (0x34C bytes) at UtopiaModule+0x78
  - Creates NetFile (0x48 bytes) at UtopiaModule+0x80
  - Creates GameSpy (0xF4 bytes) at UtopiaModule+0x7C
  - Calls TGNetwork_HostOrJoin for socket setup

- **FUN_006b3ec0 (TGNetwork_HostOrJoin)** - VERIFIED
  - Requires connState==4 (disconnected)
  - param_1==0: HOST mode (state=2, fires 0x60002)
  - param_1!=0: JOIN mode (state=3)
  - Creates UDP socket via vtable+0x60

### 1.2 Checksum Exchange Protocol - FULLY REVERSED
| Function | Address | Purpose |
|----------|---------|---------|
| FUN_006a0a30 | NewPlayerHandler | Assigns slot, starts checksums |
| FUN_006a3820 | ChecksumRequestSender | Queues 4 requests, sends #0 |
| FUN_006a39b0 | ChecksumRequestBuilder | Builds individual request |
| FUN_006a3cd0 | NetFile::ReceiveMessageHandler | Opcode dispatcher |
| FUN_006a4260 | ChecksumResponseEntry | Routes to verifier |
| FUN_006a4560 | ChecksumResponseVerifier | Hash compare, sends next |
| FUN_006a4a00 | ChecksumFail | Fires event + sends 0x22/0x23 |
| FUN_006a4bb0 | ChecksumAllPassed | Fires ET_CHECKSUM_COMPLETE |
| FUN_006a1b10 | ChecksumCompleteHandler | Sends settings + map |

### 1.3 Checksum Packet Formats - VERIFIED
- 0x20 (request): `[opcode][index:u8][dir_len:u16][dir][filter_len:u16][filter][recursive:u8]`
- 0x21 (response): `[opcode][index:u8][hashes...]`
- 0x00 (settings): `[opcode][gameTime:f32][setting1:u8][setting2:u8][playerSlot:u8][mapNameLen:u16][mapName][passFail:u8]`
- 0x01 (status): `[opcode]` (1 byte)
- 0x22/0x23 (fail): checksum mismatch notifications

### 1.4 Event System Core - VERIFIED
| Function | Address | Purpose |
|----------|---------|---------|
| FUN_006da2c0 | EventManager::ProcessEvents | Dequeue + dispatch |
| FUN_006db380 | RegisterHandler | Register for event type |
| FUN_006da130 | RegisterNamedHandler | Register by function name |
| FUN_006db620 | DispatchToChain | Walk handler chain |

### 1.5 MultiplayerGame Handler Registration - VERIFIED
- **FUN_0069efe0**: All 28 handler names and addresses documented
- **FUN_0069e590**: Event type bindings documented
- 28 handlers including: NewPlayer, Disconnect, ChecksumComplete, ReceiveMessage, KillGame, ObjectCreated, StartFiring, StopFiring, SubsystemStatus, StartWarp, etc.

### 1.6 Key Global Memory Layout - VERIFIED (Runtime Inspection)
| Address | Offset | Field |
|---------|--------|-------|
| 0x0097FA00 | Base | UtopiaModule |
| 0x0097FA78 | +0x78 | WSN pointer |
| 0x0097FA7C | +0x7C | GameSpy pointer |
| 0x0097FA80 | +0x80 | NetFile/ChecksumMgr |
| 0x0097FA88 | +0x88 | IsHost |
| 0x0097FA8A | +0x8A | IsMultiplayer |
| 0x0097F838 | -- | EventManager |
| 0x0097F864 | -- | Handler Registry |
| 0x009a09d0 | -- | Clock object |

### 1.7 Peek-Based UDP Router - VERIFIED
- GameSpy and TGNetwork share same UDP socket (WSN+0x194)
- MSG_PEEK checks first byte: `\` = GameSpy, binary = game
- qr_t+0xE4 set to 0 to disable GameSpy's own recvfrom

### 1.8 Confirmed Event Type Values
| Event | Hex Value | Confirmed |
|-------|-----------|-----------|
| ET_NETWORK_MESSAGE_EVENT | 0x60001 | Decompiled code |
| ET_NETWORK_CONNECT_EVENT | 0x60002 | Decompiled code |
| ET_CHECKSUM_COMPLETE | 0x8000e8 | Decompiled code |
| ET_SYSTEM_CHECKSUM_FAILED | 0x8000e7 | Decompiled code |
| ET_KILL_GAME | 0x8000e9 | Decompiled code |
| ET_START | 0x800053 | App.py reference |
| ET_CREATE_SERVER | 0x80004A | App.py reference |

### 1.9 Gameplay Relay Pattern (FUN_0069f620) - VERIFIED

The core relay function in `FUN_0069f620` (ProcessGameMessage) has been fully traced. The host/server performs a **clone-and-forward relay** of raw message bytes:

```
for each of 16 player slots:
    if (slot.active && slot.peerID != msg.senderPeerID && slot.peerID != wsn.localPeerID):
        clone = msg->Clone()   // vtable+0x18
        TGNetwork::Send(wsn, slot.peerID, clone, 0)
```

Key findings:
- The host clones the ORIGINAL raw network message (not the deserialized object) and sends it to every other active peer
- The server does NOT need to understand payload content for relay purposes
- The server DOES deserialize the object locally for bookkeeping (tracking objectIDs in player slots), but a dedicated server can skip this for most message types
- For a dedicated server (`DAT_0097fa88 == '\0'`, IsHost=true, IsClient=false), the host itself does not create ships or process game objects locally

### 1.10 Native Message Type Enumeration (44 opcodes) - VERIFIED

Complete native message type table enumerated from SWIG API constants, handler registrations in FUN_0069e590 and FUN_0069efe0:

```
Opcode  Name                             Handler
------  ---------------------------------  ---------------------------
0x00    (verification/settings)            (inline in ChecksumCompleteHandler)
0x01    (status byte)                      (inline in ChecksumCompleteHandler)
0x02    GAME_INITIALIZE_MESSAGE            (game init setup)
0x03    GAME_INITIALIZE_DONE_MESSAGE       (game init complete / reject)
0x04    CREATE_OBJECT_MESSAGE              ObjectCreatedHandler
0x05    CREATE_PLAYER_OBJECT_MESSAGE       ObjectCreatedHandler
0x06    DESTROY_OBJECT_MESSAGE             DeleteObjectHandler
0x07    TORPEDO_POSITION_MESSAGE           (position update)
0x08    HOST_EVENT_MESSAGE                 HostEventHandler
0x09    START_FIRING_MESSAGE               StartFiringHandler
0x0A    STOP_FIRING_MESSAGE                StopFiringHandler
0x0B    STOP_FIRING_AT_TARGET_MESSAGE      StopFiringAtTargetHandler
0x0C    SUBSYSTEM_STATE_CHANGED_MESSAGE    SubsystemStateChangedHandler
0x0D    ADD_TO_REPAIR_LIST_MESSAGE         AddToRepairListHandler
0x0E    CLIENT_EVENT_MESSAGE               ClientEventHandler
0x0F    CHANGED_TARGET_MESSAGE             ChangedTargetHandler
0x10    START_CLOAKING_MESSAGE             StartCloakingHandler
0x11    STOP_CLOAKING_MESSAGE              StopCloakingHandler
0x12    START_WARP_MESSAGE                 StartWarpHandler
0x13    REPAIR_LIST_PRIORITY_MESSAGE       RepairListPriorityHandler
0x14    SET_PHASER_LEVEL_MESSAGE           SetPhaserLevelHandler
0x15    SELF_DESTRUCT_REQUEST_MESSAGE      (self-destruct)
0x16    DELETE_OBJECT_FROM_GAME_MESSAGE    DeleteObjectFromGameHandler
0x17    CLIENT_COLLISION_MESSAGE           (collision)
0x18    COLLISION_ENABLED_MESSAGE          (collision toggle)
0x19    NEW_PLAYER_IN_GAME_MESSAGE         NewPlayerInGameHandler
0x1A    DELETE_PLAYER_FROM_GAME_MESSAGE    (player removal)
0x1B    CREATE_TORP_MESSAGE                (torpedo creation)
0x1C    CREATE_PULSE_MESSAGE               (pulse weapon)
0x1D    TORPEDO_TYPE_CHANGED_MESSAGE       TorpedoTypeChangedHandler
0x1E    SHIP_UPDATE_MESSAGE                (position/state update)
0x1F    VERIFY_ENTER_SET_MESSAGE           EnterSetHandler
0x20    DO_CHECKSUM_MESSAGE                NetFile::ReceiveMessageHandler
0x21    CHECKSUM_MESSAGE                   NetFile::ReceiveMessageHandler
0x22-27 (NetFile transfer opcodes)         NetFile::ReceiveMessageHandler
0x28    SEND_OBJECT_MESSAGE                (object sync)
0x29    VERIFY_EXITED_WARP_MESSAGE         ExitedWarpHandler
0x2A    DAMAGE_VOLUME_MESSAGE              (damage)
0x2B    CLIENT_READY_MESSAGE               (client ready)
0x2C    MAX_MESSAGE_TYPES                  -- (sentinel, not an opcode)
```

**MAX_MESSAGE_TYPES = 0x2C (44 decimal)**

### 1.11 Python Script Message Types - VERIFIED (from script source)

Python script messages start at `MAX_MESSAGE_TYPES + N`:
```
Opcode  Name                    Script
------  ----------------------  ---------------------------
0x2D    CHAT_MESSAGE            MultiplayerMenus.py
0x2E    TEAM_CHAT_MESSAGE       MultiplayerMenus.py
0x36    MISSION_INIT_MESSAGE    MissionShared.py
0x37    SCORE_CHANGE_MESSAGE    MissionShared.py
0x38    SCORE_MESSAGE           MissionShared.py
0x39    END_GAME_MESSAGE        MissionShared.py
0x3A    RESTART_GAME_MESSAGE    MissionShared.py
0x40    SCORE_INIT_MESSAGE      Mission2/3 (team modes)
0x41    TEAM_SCORE_MESSAGE      Mission2/3 (team modes)
0x42    TEAM_MESSAGE            Mission2/3 (team modes)
```

### 1.12 Chat Message Format - VERIFIED (from script source)

```
CHAT_MESSAGE:
[opcode:1]          -- 0x2D (chr(CHAT_MESSAGE))
[senderPeerID:4]    -- long, who sent it
[stringLen:2]       -- short, message length
[string:N]          -- N bytes, the chat text
Delivery: reliable (SetGuaranteed(1))
```

TEAM_CHAT_MESSAGE uses the same format with opcode 0x2E.

### 1.13 SendTGMessage Routing Patterns - VERIFIED (from script source)

| Call | Behavior |
|------|----------|
| `SendTGMessage(0, msg)` | Broadcast to ALL peers |
| `SendTGMessage(id, msg)` | Unicast to specific peer |
| `SendTGMessageToGroup("NoMe", msg)` | Send to all peers except self |

Usage in scripts:
- `SendTGMessage(0, msg)` -- EndGame, RestartGame (broadcast)
- `SendTGMessage(iToID, msg)` -- InitNetwork (send to newly joined player)
- `SendTGMessage(hostID, msg)` -- Chat from client, ship selection from client
- `SendTGMessageToGroup("NoMe", msg)` -- Chat forwarding, score updates

### 1.14 Game Start/End/Restart Protocol - VERIFIED (from script source)

Game start, end, and restart are ALL Python-level messages, not native engine transitions:

**Game Start:**
1. Host's Python script sends MISSION_INIT_MESSAGE (0x36) to all clients
2. Format: `[opcode:1][playerLimit:1][systemSpecies:1][timeLimit:1][endTime:4?][fragLimit:1]`
3. Each client receives and sets up local game state

**Game End:**
1. Host's Python script sends END_GAME_MESSAGE (0x39) broadcast
2. Format: `[opcode:1][reason:4]` (reason: 1=time up, 2=frag limit, etc.)
3. Host calls `SetReadyForNewPlayers(0)`

**Game Restart:**
1. Host sends RESTART_GAME_MESSAGE (0x3A) broadcast
2. Clients reset scores, clear ships, return to ship select
3. Host calls `SetReadyForNewPlayers(1)`

### 1.15 Ship Selection Protocol - VERIFIED (No Protocol Exists)

No ship selection network protocol exists. Ship selection is entirely local:
1. After checksum completion, client transitions to ship selection UI
2. Client selects species/ship type locally
3. Client creates ship locally via Python (`SpeciesToShip.CreateShip(iType)`)
4. Engine fires `ET_OBJECT_CREATED` internally
5. `ObjectCreatedHandler` serializes the ship and sends `CREATE_PLAYER_OBJECT_MESSAGE (0x05)` to the host
6. Host relays `CREATE_PLAYER_OBJECT (0x05)` to all other clients automatically

The server never validates or approves ship selection. It just relays the creation message.

### 1.16 Game Object State Sync - VERIFIED (Architecture)

Game object state sync (ship positions, velocity, etc.) is automatic C++ engine serialization:
- Unreliable delivery (latest update wins)
- No opcode byte -- the deserialization function `FUN_005a1f50` reads the object type ID from the data stream itself
- The server relays these as opaque blobs via the standard clone-and-forward relay
- Format: `[objectTypeID:4][objectClassID:4][object-specific serialized state:var]`

---

## 2. Partially Reversed (Medium Confidence)

Structure known but specific details need clarification.

### 2.1 TGNetwork::Send (FUN_006b4c10)
**Known**: Binary searches peer array by peer ID. Calls FUN_006b5080 to queue. Handles broadcast (param_1==0), specific peer, group (-1). Returns error codes (0=success, 4=wrong state, 0xb=peer not found, 10=queue full).

**Gaps**:
- Message object internal layout (vtable, size at +0x00, reliable flag at +0x3A, priority at +0x3B)
- Clone operation via vtable+0x18 for broadcast sends
- Message reference counting (vtable+0x04 with param=1 for release)

### 2.2 SendOutgoingPackets (FUN_006b55b0)
**Known**: Checks WSN+0x10C (send-enabled). Round-robin peer iteration. Three queue drain loops. Serializes via vtable+0x08. Sends via vtable+0x70 (sendto). Packet header: `[senderID:u8][messageCount:u8][serialized messages...]`. Max 0xFE messages. Priority: 3 retries before promoting. Reliable: sent once then moved to priority. After 8 retries: message dropped + peer disconnected.

**Gaps**:
- Exact per-message header format in the serialized stream
- Sequence number assignment during serialization
- How vtable+0x08 writes per-message headers
- Timeout calculation formula (uses param_1[0x2D] as base)
- Whether vtable+0x58 is an encrypt step (returns packet header size)

### 2.3 ProcessIncomingPackets (FUN_006b5c90)
**Known**: Calls vtable+0x6C (recvfrom). Parses sender ID + message count. Iterates messages using dispatch table at DAT_009962d4. Sets sender peer ID. Creates peer entries for unknown senders.

**Gaps**:
- Dispatch table contents (message type -> constructor mapping)
- Connection handshake flow (type 3 = request, type 5 = response?)
- Peer creation initial field values (FUN_006b7410)
- Password validation during connection

### 2.4 ReliableACKHandler (FUN_006b61e0)
**Known**: Scans priority queue for matching sequence/flags. Match found: resets retry counter. No match: creates ACK entry in priority queue.

**Gaps**:
- Exact ACK packet format (what bytes constitute an ACK?)
- Sequence number matching logic (uses ushort at message+0x14, peer+0x26/+0x2A)
- Sequence wrap handling (0xFFFF -> 0x0000)
- "Small" messages (type < 0x32) vs "large" (type >= 0x32) for sequence tracking

### 2.5 DispatchIncomingQueue (FUN_006b5f70 / FUN_006b6ad0)
**Known**: Iterates queued messages. Validates sequence against expected (peer+0x24/+0x28). Discards out-of-window (0x4000 range). FUN_006b6ad0 handles sequence validation.

**Gaps**:
- Exact sliding window logic
- Out-of-order message buffering vs dropping
- Application delivery queue structure
- How reliable incoming messages generate ACKs back to sender

### 2.6 GameSpy Query Handler (FUN_006ac1e0)
**Known**: Standard GameSpy QR SDK pattern. Query type table with 8 entries (basic, info, rules, players, combined, full, specific_key, echo). Response builders at FUN_006ac5f0/006ac7a0/006ac810/006ac880. Responses are backslash-delimited. Fragmentation if > 0x545 bytes.

**Gaps**:
- Specific key-value pairs returned by each callback
- Callback function pointers at qr_t+0x32-0x35
- qr_t struct layout beyond callbacks
- Heartbeat format/timing (FUN_006ab4e0, FUN_006aa2f0)

### 2.7 Player Slot Management
**Known**: 16 slots at MultiplayerGame+0x74, stride 0x18. Slot+0x00: active. Slot+0x04: peer ID. Slot+0x08: player object ID. FUN_006a7770 initializes. MaxPlayers at +0x1FC.

**Gaps**:
- Complete slot structure (all 0x18 bytes)
- How slot assignment interacts with checksum state
- Player name storage location
- Slot cleanup on disconnect (FUN_006a0ca0)
- Boot player mechanics (FUN_006a2640)

### 2.8 MultiplayerGame Opcode Dispatch (0x00-0x2B) - PARTIALLY REVERSED

**Known** (newly elevated from Section 3):
- The relay pattern in FUN_0069f620 is VERIFIED: clone raw message bytes, iterate 16 player slots, send to all except sender and self
- 44 native message types enumerated (0x00-0x2B) with handler names (see Section 1.10)
- The server acts as a SMART RELAY for native opcodes: receive, optionally deserialize for bookkeeping, clone and forward to all other peers
- Messages the server CONSTRUCTS (never receives): 0x00 (settings), 0x01 (status), 0x03 (reject when full)
- Opcode byte[1] is the sender's player slot index; byte[2] is additional data when `hasPlayerSlot` flag is set

**Gaps**:
- Ship creation/destruction opcode formats (0x04, 0x05, 0x06) -- see Section 3.1
- Game object serialization format (FUN_005a1f50 deserializer) -- see Section 3.2
- SHIP_UPDATE_MESSAGE (0x1E) exact payload format (position, rotation, velocity fields)
- Which opcodes use `hasPlayerSlot=true` vs `hasPlayerSlot=false`
- What game state changes each non-relay opcode triggers on the host

---

## 3. Not Yet Reversed (Critical for Phase 1)

### 3.1 Connection Handshake Protocol - CRITICAL, BLOCKING
**What**: The complete UDP connection handshake from first packet to ET_NETWORK_NEW_PLAYER event.
**Why blocking**: Without this, no client can connect to the server.
**Files**: `11_tgnetwork.c` FUN_006b5c90 (ProcessIncoming, peerID=-1 path), FUN_006b7410 (peer creation)
**Approach**: Trace the `-1` sender path in ProcessIncomingPackets. Follow message type 3 and type 5 dispatch. Map peer creation and ID assignment.

### 3.2 Packet Wire Format Details - CRITICAL, BLOCKING
**What**: Exact byte layout of per-message headers within UDP packets.
**Why blocking**: Need exact format to parse/build any packet.
**Files**: FUN_006b55b0 (SendOutgoing), FUN_006b5c90 (ProcessIncoming)
**Approach**: Focus on serialization/deserialization loops. Cross-reference with packet captures.

### 3.3 Reliable Delivery Layer - CRITICAL, BLOCKING
**What**: ACK packet format, sequence numbering details, retry logic.
**Why blocking**: Required for checksum exchange and all game commands.
**Files**: FUN_006b61e0 (ACK handler), FUN_006b5080 (queue), FUN_006b8670/FUN_006b8700 (retry)
**Approach**: Trace sequence assignment in Send, ACK generation in Recv, retry in SendOutgoing.

### 3.4 Message Type Dispatch Table (DAT_009962d4) - CRITICAL, BLOCKING
**What**: Map of first-byte values to message constructors for deserialization.
**Why blocking**: Must know message types to parse any packet.
**Files**: DAT_009962d4 data, `18_data_tables.c`
**Approach**: Read data at 0x009962d4, identify function pointers, trace each.

### 3.5 Ship Creation/Destruction Opcode Formats (0x04, 0x05, 0x06) - CRITICAL, BLOCKING
**What**: The exact wire format of CREATE_OBJECT_MESSAGE (0x04), CREATE_PLAYER_OBJECT_MESSAGE (0x05), and DESTROY_OBJECT_MESSAGE (0x06).
**Why blocking**: The server must parse CREATE_PLAYER_OBJECT (0x05) to create lightweight ship entities for script reference (REQ-SHIP-01). The server must parse DESTROY_OBJECT (0x06) to update ship death state. Without this, `MultiplayerGame.GetShipFromPlayerID()` returns nothing and mission scripts crash.
**Files**: `09_multiplayer_game.c` ObjectCreatedHandler (LAB_006a0f90), DeleteObjectHandler, FUN_005a1f50 (deserializer)
**Approach**: Capture a vanilla multiplayer session and extract 0x04/0x05/0x06 messages. Cross-reference with FUN_005a1f50 to understand the object type/class ID fields at the start of the payload.

### 3.6 Game Object Serialization Format (FUN_005a1f50) - IMPORTANT, BLOCKING
**What**: The deserializer that reads `[objectTypeID:4][objectClassID:4][object-specific state]` from network messages. Needed to extract ship type and player ID from CREATE_PLAYER_OBJECT messages.
**Why blocking**: The relay server must parse at minimum the ship type (species index) and owning player ID from creation messages to populate the lightweight ship entity. Without this, `GetNetType()`, `GetNetPlayerID()`, and `IsPlayerShip()` return garbage and mission scripts malfunction.
**Files**: `09_multiplayer_game.c` FUN_005a1f50, FUN_005a2060 (serializer), related vtable calls
**Approach**: Trace FUN_005a1f50 with a focus on what fields it reads from the stream before dispatching to the object-specific unserialize method. Identify the objectTypeID and objectClassID values for ShipClass objects. Determine where player ID and ship species are stored in the serialized data.

### 3.7 Team Assignment Mechanism for Mission2/3 - IMPORTANT
**What**: How team assignment works in Team Deathmatch (Mission2) and Team Objectives (Mission3).
**Why important**: Phase 1 includes Missions 1-3. Team chat forwarding requires knowing which players are on which team.
**Files**: `Multiplayer/Episode/Mission2/Mission2.py`, `Multiplayer/Episode/Mission3/Mission3.py`, `MissionShared.py`
**Approach**: Analyze Python scripts for group management (`GetFriendlyGroup()`, `GetEnemyGroup()`). Determine if team assignment is communicated via a network message or derived from ship creation order/position.

### 3.8 GameSpy Query Response Fields - IMPORTANT
**What**: Exact key-value pairs returned in GameSpy responses.
**Files**: Callback functions at qr_t+0x32-0x35
**Approach**: Trace qr_t creation to find callbacks. Follow to see strings written.

### 3.9 Lobby State Synchronization - IMPORTANT
**What**: How settings propagate to clients. Ready state management.

### 3.10 Message Serialization System - IMPORTANT
**What**: Message class hierarchy. Self-serialization via vtable+0x08. TGStream class.

---

## 4. Prioritized RE Work Items

### BLOCKING (Must complete before implementation)

| ID | Task | Complexity | Depends On |
|----|------|------------|------------|
| WI-4 | Message type dispatch table (DAT_009962d4) | MEDIUM | None |
| WI-9 | Peer structure layout (all ~0xC0 bytes) | MEDIUM | None |
| WI-2 | Packet wire format (exact byte layout) | LARGE | WI-4, WI-9 |
| WI-3 | Reliable ACK format and retry logic | LARGE | WI-4, WI-9 |
| WI-1 | Connection handshake protocol | LARGE | WI-2, WI-3, WI-4 |
| WI-5 | Game opcodes -- ship entity management (0x04, 0x05, 0x06) | LARGE | WI-1 |
| WI-16 | Game object serialization format (FUN_005a1f50) -- extract ship type, player ID from creation messages | LARGE | WI-5 |

### IMPORTANT (Required for functional server)

| ID | Task | Complexity | Depends On |
|----|------|------------|------------|
| WI-6 | GameSpy query response builder | MEDIUM | None |
| WI-7 | Player slot full structure (0x18 bytes) | SMALL | WI-1 |
| WI-8 | Lobby settings propagation | MEDIUM | WI-5 |
| WI-10 | GameSpy heartbeat and initialization | MEDIUM | WI-6 |
| WI-14 | Hash algorithm lookup tables extraction | SMALL | None |
| WI-15 | Chat message protocol -- forwarding and team routing | SMALL | WI-5 |
| WI-17 | Verify 44 native opcode enumeration against actual packet captures | MEDIUM | WI-1 |

### NICE-TO-HAVE (Can be deferred or approximated)

| ID | Task | Complexity |
|----|------|------------|
| WI-11 | TGWinsockNetwork full 0x34C object layout | LARGE |
| WI-12 | Event object structures (TGEvent variants) | MEDIUM |
| WI-13 | TGStream serialization class | MEDIUM |

---

## 5. Dependency Graph

```
WI-4 (Message Types) ----+
                          |
WI-2 (Packet Format) ----+--> WI-1 (Connection Handshake)
                          |         |
WI-3 (Reliable ACK) -----+         v
                                WI-7 (Player Slots)
WI-9 (Peer Structure) supports      |
    WI-1, WI-2, WI-3               v
                                WI-5 (Game Opcodes: 0x04/0x05/0x06)
                                    |
                                    +--> WI-16 (Object Serialization Format)
                                    |         |
                                    |         v
                                    |     Ship entity creation
                                    |
                                    +--> WI-8 (Settings Sync)
                                    |
                                    +--> WI-15 (Chat)
                                    |
                                    +--> WI-17 (Opcode Verification)

WI-6 (GameSpy Query) ---> WI-10 (GameSpy Init)

WI-14 (Hash Tables) ---> independent
```

---

## 6. Recommended RE Order

1. **WI-4** (Message Type Dispatch Table) - Small effort, unlocks everything
2. **WI-9** (Peer Structure) - Needed to understand all network code
3. **WI-14** (Hash Tables) - Small, independent, can run in parallel
4. **WI-2** (Packet Wire Format) - Foundation for all network I/O
5. **WI-3** (Reliable ACK) - Required for reliable message delivery
6. **WI-1** (Connection Handshake) - Depends on 2, 3, 4
7. **WI-6** (GameSpy Query) - Independent, can parallel with above
8. **WI-7** (Player Slots) - Small effort, after WI-1
9. **WI-5** (Game Opcodes 0x04/0x05/0x06) - Ship entity creation/destruction
10. **WI-16** (Object Serialization) - Extract ship type + player ID from 0x05
11. **WI-15** (Chat) - Small, after WI-5
12. **WI-17** (Opcode Verification) - Validate enumeration against captures
13. **WI-8** (Settings Sync) - After WI-5

---

## 7. Known Data Structures

### TGWinsockNetwork (0x34C bytes)
| Offset | Size | Field | Confidence |
|--------|------|-------|------------|
| +0x14 | 4 | connState (2=HOST, 3=CLIENT, 4=DISCONNECTED) | VERIFIED |
| +0x18 | 4 | localPeerID | VERIFIED |
| +0x2C | ptr | peerArray (sorted) | VERIFIED |
| +0x30 | 4 | peerCount | VERIFIED |
| +0x44 | 4 | statsEnabled | HIGH |
| +0x2B | 2 | packetSize (default 0x200) | HIGH |
| +0x2D | 4 | reliableTimeout (360.0f) | HIGH |
| +0x2E | 4 | disconnectTimeout (45.0f) | HIGH |
| +0xA8 | 4 | maxPendingBytes (0x8000) | HIGH |
| +0x10C | 1 | sendEnabled flag | VERIFIED |
| +0x10E | 1 | isHost flag | VERIFIED |
| +0x10F | 1 | isConnecting flag | HIGH |
| +0x194 | 4 | socket (SOCKET) | VERIFIED |

### Peer (~0xC0 bytes)
| Offset | Size | Field | Confidence |
|--------|------|-------|------------|
| +0x18 | 4 | peerID | VERIFIED |
| +0x1C | 4 | address | HIGH |
| +0x24 | 2 | seqRecvUnreliable | HIGH |
| +0x26 | 2 | seqSendReliable | HIGH |
| +0x28 | 2 | seqRecvReliable | HIGH |
| +0x2A | 2 | seqSendPriority | HIGH |
| +0x2C | 4 | lastRecvTime (float) | HIGH |
| +0x30 | 4 | lastSendTime (float) | HIGH |
| +0x64-0x7C | - | unreliable queue | HIGH |
| +0x7C | 4 | unreliable count | HIGH |
| +0x80-0x98 | - | reliable queue | HIGH |
| +0x98 | 4 | reliable count | HIGH |
| +0x9C-0xB4 | - | priority queue | HIGH |
| +0xB4 | 4 | priority count | HIGH |
| +0xB8 | 4 | disconnectTime (float) | HIGH |
| +0xBC | 1 | isDisconnecting | HIGH |

### MultiplayerGame
| Offset | Size | Field | Confidence |
|--------|------|-------|------------|
| +0x74 | 16*0x18 | playerSlots[16] | VERIFIED |
| +0x1F8 | 4 | readyForNewPlayers | VERIFIED |
| +0x1FC | 4 | maxPlayers | VERIFIED |

### Player Slot (0x18 bytes per slot)
| Offset | Size | Field | Confidence |
|--------|------|-------|------------|
| +0x00 | 1 | active flag | VERIFIED |
| +0x04 | 4 | peer network ID | VERIFIED |
| +0x08 | 4 | player object ID | VERIFIED |
| +0x0C-0x17 | - | UNKNOWN (needs WI-7) | - |

### NetFile (0x48 bytes)
| Offset | Size | Field | Confidence |
|--------|------|-------|------------|
| vtable+0x18 | ptr | hash table A (tracking) | HIGH |
| vtable+0x28 | ptr | hash table B (queued requests) | HIGH |
| vtable+0x38 | ptr | hash table C (file transfers) | HIGH |

### Event Types (30+ confirmed)
| Value | Name | Source |
|-------|------|--------|
| 0x60001 | ET_NETWORK_MESSAGE_EVENT | Decompiled |
| 0x60002 | ET_NETWORK_CONNECT_EVENT | Decompiled |
| 0x60007 | ET_NETWORK_NEW_PLAYER (tentative) | Decompiled |
| 0x8000e7 | ET_SYSTEM_CHECKSUM_FAILED | Decompiled |
| 0x8000e8 | ET_CHECKSUM_COMPLETE | Decompiled |
| 0x8000e9 | ET_KILL_GAME | Decompiled |
| 0x800053 | ET_START | App.py |
| 0x80004A | ET_CREATE_SERVER | App.py |
| 0x8000C8 | ET_OBJECT_CREATED | Decompiled (ObjectCreatedHandler) |
| 0x8000F1 | ET_NEW_PLAYER_IN_GAME | Decompiled (NewPlayerInGameHandler) |

### Native Message Payload Structure (opcodes 0x02-0x1F)
```
Offset  Size  Field
+0      1     opcode (message type byte)
+1      1     senderPlayerSlot (0-15)
+2      1     extraData (optional, only when hasPlayerSlot=true)
+2/+3   var   serialized game object data (type-specific)
```

The serialized game object data starts with:
```
+0      4     objectTypeID (read by FUN_005a1f50 via FUN_006cf670)
+4      4     objectClassID (read by FUN_005a1f50 via FUN_006cf670)
+8      var   object-specific serialized state
```

### Hash Function (FUN_007202e0)
```c
// 4-table byte-XOR substitution hash
// Tables at 0x0095c888, 0x0095c988, 0x0095ca88, 0x0095cb88
// Each table: 256 bytes
// Input: filename or file content as byte string
// Output: 32-bit hash (a<<24 | b<<16 | c<<8 | d)
// NOT MD5, NOT CRC32
```

### Stream Serializer Functions
| Function | Address | Purpose |
|----------|---------|---------|
| FUN_006cefe0 | Create | Create stream |
| FUN_006cf730 | WriteByte | Write 1 byte |
| FUN_006cf770 | WriteByte2 | Write 1 byte (different calling convention) |
| FUN_006cf7f0 | WriteShort | Write u16 little-endian |
| FUN_006cf8b0 | WriteFloat | Write f32 little-endian |
| FUN_006cf2b0 | WriteBytes | Write N raw bytes |

### Gameplay Relay Function (FUN_0069f620) - Reconstructed Logic
```c
void MultiplayerGame::ProcessGameMessage(TGMessage* msg, bool hasPlayerSlot) {
    // 1. Get message payload
    char* payload = msg->GetData(&payloadSize);

    // 2. Save/restore current player slot context
    int savedSlot = g_currentPlayerSlot;  // _DAT_0097fa84

    // 3. Read sender's player slot from byte[1] of payload
    char senderSlot = payload[1];
    int headerSize = 2;

    // 4. If hasPlayerSlot flag, also read byte[2] as extra data
    int extraData;
    if (hasPlayerSlot) {
        extraData = payload[2];
        headerSize = 3;
    }

    // 5. Swap player slot context
    this->playerSlots[g_currentPlayerSlot].objectID = g_currentObjectID;
    g_currentObjectID = this->playerSlots[senderSlot].objectID;
    g_currentPlayerSlot = senderSlot;

    // 6. DESERIALIZE the remaining payload into a game object
    TGObject* gameObj = DeserializeGameObject(payload + headerSize,
                                               payloadSize - headerSize);

    // 7. Restore slot context
    this->playerSlots[senderSlot].objectID = g_currentObjectID;
    g_currentObjectID = savedObjectID;
    g_currentPlayerSlot = savedSlot;

    if (gameObj == NULL) return;

    // 8. HOST PATH: RELAY TO ALL OTHER PEERS
    if (g_isMultiplayer) {
        for (int i = 0; i < 16; i++) {
            int* slot = &this->playerSlots[i];
            if (slot[-1] == 0) continue;  // slot not active

            if (slot[0] == msg->senderPeerID) {
                // SENDER's slot -- update objectID if hasPlayerSlot
                if (hasPlayerSlot) {
                    slot[1] = gameObj->field_0x04;
                }
            }
            else if (slot[0] != wsn->localPeerID) {
                // ANOTHER PEER -- clone and send
                TGMessage* clone = msg->Clone();
                TGNetwork::Send(wsn, slot[0], clone, 0);
            }
        }

        // Dedicated server (IsHost=true, IsClient=false): skip local processing
        if (!g_isHost) return;
    }
}
```

---

## 8. Message Flow Diagram

```
Client A                    Server (Host)                    Client B
   |                            |                                |
   |--- CREATE_PLAYER_OBJ ---->|                                |
   |   (0x05, ship creation)   |--- Clone + Forward ----------->|
   |                            |   (create ship entity)         |
   |                            |                                |
   |--- SHIP_UPDATE ---------->|                                |
   |   (0x1E, unreliable)      |--- Clone + Forward ----------->|
   |                            |                                |
   |                            |<--- START_FIRING --- Client B --|
   |<--- Clone + Forward ------|   (0x09)                       |
   |                            |                                |
   |--- CHAT_MESSAGE --------->|   (to host only, reliable)      |
   |   (0x2D)                  |--- Python forwards to NoMe --->|
   |                            |                                |
   |                            |--- MISSION_INIT (Python) ---->|
   |<--- MISSION_INIT ---------|   (0x36, reliable)             |
   |                            |                                |
   |                            |--- END_GAME (Python) -------->|
   |<--- END_GAME -------------|   (0x39, reliable)             |
```

---

## 9. Open Questions

1. **What is 0x8009?** -- The type check `gameObj->GetType() == 0x8009` causes the host to skip local processing. This might be a "network-only" object type that shouldn't be instantiated on the host.

2. **What does param_2 (hasPlayerSlot) indicate?** -- Some message types include a player slot byte at byte[2], others do not. Need to trace the ReceiveMessageHandler to see what determines this flag per opcode.

3. **How does FUN_0069f930 differ from FUN_0069f620?** -- FUN_0069f930 appears to handle position/state update messages (SHIP_UPDATE_MESSAGE 0x1E) and calls the "Forward" group rather than the raw relay. It reads position, rotation, velocity fields.

4. **What is DAT_008e5528?** -- This is the name of a network group. Based on the Python API, one of the two groups created in the MultiplayerGame constructor is "NoMe" (DAT_008e5528), the other is "Forward" (s_Forward_008d94a0).

5. **Exact value of MAX_MESSAGE_TYPES** -- Inferred 0x2C (44) based on the enumeration count. Needs verification from the Appc.pyd SWIG module or runtime testing. If wrong, all Python-level opcode numbers shift.

6. **Ship creation serialized fields** -- What specific fields does FUN_005a1f50 extract from a CREATE_PLAYER_OBJECT_MESSAGE (0x05)? At minimum we need: ship type (species index), owning player's network ID, and object ID. The object-specific serialized state format is unknown.

7. **Team assignment wire format** -- Is team assignment communicated via a network message, or is it derived from ship creation order / group membership in the Python scripts?
