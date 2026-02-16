# OpenBC Phase 1: Reverse Engineering Gap Analysis

## Document Status
- **Generated**: 2026-02-07, **Revised**: 2026-02-15 (major update with STBC-Dedi verified findings)
- **Source**: game-reverse-engineer agent analysis + STBC-Dedicated-Server verified results
- **Reference repo**: `../STBC-Dedicated-Server/`
- **Cross-reference**: [phase1-verified-protocol.md](phase1-verified-protocol.md) for complete wire format

---

## IMPORTANT: Corrections from Original Document

This revision corrects several critical errors in the 2026-02-08 version:

1. **Opcode table was wrong.** The original assigned sequential values (0x04=CREATE_OBJECT, 0x05=CREATE_PLAYER_OBJECT, etc.) based on SWIG constant names. The actual wire byte values come from the jump table at `0x0069F534` and differ for most entries. See Section 1.10.

2. **MAX_MESSAGE_TYPES was wrong.** Original said 0x2C (44). Actual: **0x2B (43)**. Stock scripts use `CHAT_MESSAGE = App.MAX_MESSAGE_TYPES + 1`, and traces confirm CHAT at byte 0x2C. Therefore MAX = 0x2B. All Python opcodes in the original were off by +1.

3. **IsHost/IsClient bytes were swapped.** Original: `0x0097FA88 = IsHost`. Actual: `0x0097FA88 = IsClient`, `0x0097FA89 = IsHost`.

---

## 1. Already Reversed (High Confidence)

These systems are fully traced through decompiled code with verified behavior from the STBC-Dedicated-Server project.

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
- 0x00 (settings): `[opcode][gameTime:f32][setting1:bit][setting2:bit][playerSlot:u8][mapNameLen:u16][mapName][passFail:bit]`
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
| 0x0097FA88 | +0x88 | **IsClient** (0=host, 1=client) |
| 0x0097FA89 | +0x89 | **IsHost** (1=host, 0=client) |
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
| ET_BOOT_PLAYER | 0x8000f6 | Decompiled (subsystem hash anti-cheat) |

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

### 1.10 Verified Game Opcode Table (from jump table at 0x0069F534)

**CORRECTED.** The wire byte values come from the MultiplayerGame dispatcher jump table at `0x0069F534` (41 entries, opcode - 2 = index) and verified against a 90MB stock-dedi packet trace (30,000+ packets over 15 minutes).

Note: The opcode space has **gaps** (no 0x04, 0x05, 0x20-0x27 in game dispatcher). The SWIG constant names do NOT map to sequential wire byte values.

| Opcode | Name | Handler | Direction | Stock 15-min Count |
|--------|------|---------|-----------|-------------------|
| 0x00 | Settings | FUN_00504d30 | S->C | at join |
| 0x01 | GameInit | FUN_00504f10 | S->C | at join |
| 0x02 | ObjectCreate | FUN_0069f620 | S->C | rare |
| 0x03 | ObjectCreateTeam | FUN_0069f620 | S->C | 11 (ship spawns) |
| 0x04 | BootPlayer | (inline) | S->C | rare |
| 0x06 | PythonEvent | FUN_0069f880 | any | **3432** |
| 0x07 | StartFiring | FUN_0069fda0 | any | **2282** |
| 0x08 | StopFiring | FUN_0069fda0 | any | common |
| 0x09 | StopFiringAtTarget | FUN_0069fda0 | any | common |
| 0x0A | SubsysStatus | FUN_0069fda0 | any | common |
| 0x0B | AddToRepairList | FUN_0069fda0 | any | occasional |
| 0x0C | ClientEvent | FUN_0069fda0 | any | occasional |
| 0x0D | PythonEvent2 | FUN_0069f880 | any | alternate path |
| 0x0E | StartCloaking | FUN_0069fda0 | any | occasional |
| 0x0F | StopCloaking | FUN_0069fda0 | any | occasional |
| 0x10 | StartWarp | FUN_0069fda0 | any | occasional |
| 0x11 | RepairListPriority | FUN_0069fda0 | any | occasional |
| 0x12 | SetPhaserLevel | FUN_0069fda0 | any | 33 |
| 0x13 | HostMsg | FUN_006a0d90 | C->S | rare |
| 0x14 | DestroyObject | FUN_006a01e0 | S->C | on death |
| 0x15 | CollisionEffect | FUN_006a2470 | C->S | 84 |
| 0x16 | UICollisionSetting | FUN_00504c70 | S->C | at join |
| 0x17 | DeletePlayerUI | FUN_006a1360 | S->C | on disconnect |
| 0x18 | DeletePlayerAnim | FUN_006a1420 | S->C | on disconnect |
| 0x19 | TorpedoFire | FUN_0069f930 | owner->all | **897** |
| 0x1A | BeamFire | FUN_0069fbb0 | owner->all | common |
| 0x1B | TorpTypeChange | FUN_0069fda0 | any | occasional |
| 0x1C | StateUpdate | FUN_005b21c0 | owner->all | **continuous** |
| 0x1D | ObjNotFound | FUN_006a0490 | S->C | rare |
| 0x1E | RequestObject | FUN_006a02a0 | C->S | rare |
| 0x1F | EnterSet | FUN_006a05e0 | S->C | at join |
| 0x28 | (no handler) | (default) | S->C | vestigial |
| 0x29 | Explosion | FUN_006a0080 | S->C | on hit |
| 0x2A | NewPlayerInGame | FUN_006a1e70 | S->C | at join |

**MAX_MESSAGE_TYPES = 0x2B (43 decimal).** Not 0x2C.

### 1.11 Python Script Message Types - VERIFIED (from packet traces)

Python script messages start at `MAX_MESSAGE_TYPES + N` where MAX = 0x2B:

| Opcode | Offset | Name | Script |
|--------|--------|------|--------|
| 0x2C | MAX+1 | CHAT_MESSAGE | MultiplayerMenus.py |
| 0x2D | MAX+2 | TEAM_CHAT_MESSAGE | MultiplayerMenus.py |
| 0x35 | MAX+10 | MISSION_INIT_MESSAGE | MissionShared.py |
| 0x36 | MAX+11 | SCORE_CHANGE_MESSAGE | MissionShared.py |
| 0x37 | MAX+12 | SCORE_MESSAGE | MissionShared.py |
| 0x38 | MAX+13 | END_GAME_MESSAGE | MissionShared.py |
| 0x39 | MAX+14 | RESTART_GAME_MESSAGE | MissionShared.py |

Team mode messages (Mission2/3):

| Opcode | Offset | Name | Script |
|--------|--------|------|--------|
| 0x3F | MAX+20 | SCORE_INIT_MESSAGE | Mission2/3 |
| 0x40 | MAX+21 | TEAM_SCORE_MESSAGE | Mission2/3 |
| 0x41 | MAX+22 | TEAM_MESSAGE | Mission2/3 |

### 1.12 Chat Message Format - VERIFIED (from packet traces)

```
CHAT_MESSAGE:
[opcode:1]          -- 0x2C (chr(CHAT_MESSAGE))
[senderSlot:1]      -- player slot index (byte)
[padding:3]         -- 0x00 0x00 0x00
[stringLen:2]       -- short (little-endian), message length
[string:N]          -- N bytes, the chat text (ASCII)
Delivery: reliable (SetGuaranteed(1))
```

TEAM_CHAT_MESSAGE uses the same format with opcode 0x2D.

### 1.13 SendTGMessage Routing Patterns - VERIFIED (from script source)

| Call | Behavior |
|------|----------|
| `SendTGMessage(0, msg)` | Broadcast to ALL peers |
| `SendTGMessage(id, msg)` | Unicast to specific peer |
| `SendTGMessageToGroup("NoMe", msg)` | Send to all peers except self |

### 1.14 Game Start/End/Restart Protocol - VERIFIED (from script source + packet traces)

**Game Start:**
1. Host's Python script sends MISSION_INIT_MESSAGE (0x35) to all clients
2. Format: `[opcode:1][playerLimit:1][systemSpecies:1][timeLimit:1][endTime:4?][fragLimit:1]`
3. Each client receives and sets up local game state

**Game End:**
1. Host's Python script sends END_GAME_MESSAGE (0x38) broadcast
2. Format: `[opcode:1][reason:4]` (reason: 1=time up, 2=frag limit, etc.)
3. Host calls `SetReadyForNewPlayers(0)`

**Game Restart:**
1. Host sends RESTART_GAME_MESSAGE (0x39) broadcast
2. Clients reset scores, clear ships, return to ship select
3. Host calls `SetReadyForNewPlayers(1)`

### 1.15 Ship Selection Protocol - VERIFIED (No Protocol Exists)

No ship selection network protocol exists. Ship selection is entirely local:
1. After checksum completion, client transitions to ship selection UI
2. Client selects species/ship type locally
3. Client creates ship locally via Python (`SpeciesToShip.CreateShip(iType)`)
4. Engine fires `ET_OBJECT_CREATED` internally
5. `ObjectCreatedHandler` serializes the ship and sends `ObjectCreateTeam (0x03)` to the host
6. Host relays `ObjectCreateTeam (0x03)` to all other clients automatically

The server never validates or approves ship selection. It just relays the creation message.

### 1.16 Game Object State Sync - VERIFIED (Architecture)

Game object state sync (ship positions, velocity, etc.) uses `StateUpdate (0x1C)`:
- Unreliable delivery (latest update wins)
- Dirty-flag-based: only changed fields are sent
- Format: `[0x1C][objectId:i32][gameTime:f32][dirtyFlags:u8][fields...]`
- Dirty flags: 0x01=abs pos, 0x02=delta pos, 0x04=fwd, 0x08=up, 0x10=speed, 0x20=subsystems, 0x40=cloak, 0x80=weapons
- Direction-based: clients send 0x80 (weapons), server sends 0x20 (subsystems)
- See [phase1-verified-protocol.md](phase1-verified-protocol.md) for full StateUpdate format

### 1.17 Connection Handshake Protocol - VERIFIED (STBC-Dedi)

**Formerly WI-1 (CRITICAL, BLOCKING). Now SOLVED.**

The complete UDP connection flow from first packet to gameplay:

1. **Client sends connection request** (message type in transport layer)
2. **Server creates peer entry** (FUN_006b7410), assigns peer ID
3. **Server fires ET_NETWORK_NEW_PLAYER** (0x60004)
4. **NewPlayerHandler** (FUN_006a0a30) assigns player slot, starts checksum exchange
5. **4-round checksum exchange** completes (opcodes 0x20/0x21)
6. **ET_CHECKSUM_COMPLETE** fires, server sends Settings (0x00) + GameInit (0x01)
7. **NewPlayerInGame** (0x2A) sent by server, triggers InitNetwork in Python
8. **Python sends MISSION_INIT_MESSAGE** (0x35) to the new client
9. Client selects ship, sends ObjectCreateTeam (0x03)
10. **DeferredInitObject** on server loads NIF model + creates 33 subsystems
11. StateUpdate (0x1C) with real subsystem data begins flowing

See `../STBC-Dedicated-Server/docs/multiplayer-flow.md` for complete timing data.

### 1.18 Reliable Delivery System - VERIFIED (STBC-Dedi)

**Formerly WI-3 (CRITICAL, BLOCKING). Now SOLVED.**

Three-tier send queues per peer:
- **Unreliable** (peer+0x64): fire-and-forget, used for StateUpdate (0x1C)
- **Reliable** (peer+0x80): guaranteed delivery with ACK, 360s timeout
- **Priority** (peer+0x9C): ACKs and retried reliable messages, max 8 retries

Transport message types in the wire format:
- **0x01**: ACK — `[0x01][seq:1][0x00][flags:1]` (4 bytes)
- **0x32**: Reliable data — `[0x32][totalLen:1][flags:1][seq_hi:1][seq_lo:1][payload]`
  - flags & 0x80 = reliable, flags & 0x20 = fragmented, flags & 0x01 = more fragments

Sequence numbering: u16 wrapping, separate for reliable and unreliable channels.
Sliding window: 0x4000 range for out-of-window rejection.

### 1.19 Ship Creation Protocol - VERIFIED (STBC-Dedi)

**Formerly WI-5/WI-16 (CRITICAL, BLOCKING). Now SOLVED.**

Ship creation uses opcode **0x03** (ObjectCreateTeam), NOT 0x05:

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      type_tag = 3 (object with team)
1       1     u8      owner_player_slot (0-15)
2       1     u8      team_id
3+      var   data    serialized_object (vtable+0x10C output)
```

The serialized_object data is produced by `object->vtable[0x10C](buffer, maxlen)` which includes object type ID, position, rotation, health, subsystem states, weapon loadouts.

The server creates a lightweight ship entity on receipt, then Python's `DeferredInitObject` loads the NIF model and creates 33 subsystems (enabling collision/subsystem damage).

Ship destruction uses opcode **0x14** (DestroyObject), NOT 0x06:
```
[0x14][objectId:i32]
```

### 1.20 Damage Pipeline - VERIFIED (STBC-Dedi)

**Complete damage system reverse-engineered from stock-dedi function traces.**

```
Collision path:
  CollisionDamageWrapper (0x005B0060) -> DoDamage_FromPosition (0x00593650)
  DoDamage_CollisionContacts (0x005952D0) -> DoDamage (multiple contacts)

Weapon path:
  WeaponHitHandler (0x005AF010) -> ApplyWeaponDamage (0x005AF420) -> DoDamage

ALL damage flows through:
  DoDamage (0x00594020) -> ProcessDamage (0x00593E50)
```

Gate checks in DoDamage:
- `ship+0x18` (NiNode) must be non-NULL
- `ship+0x140` (damage target ref) must be non-NULL

ProcessDamage subsystem distribution:
- Uses `ship+0x128` / `ship+0x130` (handler ARRAY), **NOT** `ship+0x284` (linked list)
- `ship+0x284` is for state serialization; `ship+0x128` is for damage distribution

Damage notification is **CLIENT-ONLY** (gated on `IsHost==0` at `0x0097FA89`).

See `../STBC-Dedicated-Server/docs/damage-system.md` for full analysis with stock-dedi trace data.

### 1.21 AlbyRules Stream Cipher - VERIFIED (STBC-Dedi)

All game packets (not GameSpy) are encrypted with the "AlbyRules!" stream cipher:
- XOR-based with a 10-byte key derived from the string "AlbyRules!"
- Applied after transport framing, before UDP send
- Removed on receive before parsing
- GameSpy packets (first byte `\`) are plaintext and exempt

The STBC-Dedi packet tracer implements full decrypt/encrypt for both directions.

---

## 2. Partially Reversed (Medium Confidence)

### 2.1 GameSpy Query Handler (FUN_006ac1e0)
**Known**: Standard GameSpy QR SDK pattern. Query type table with 8 entries (basic, info, rules, players, combined, full, specific_key, echo). Response builders at FUN_006ac5f0/006ac7a0/006ac810/006ac880. Responses are backslash-delimited. Fragmentation if > 0x545 bytes.

**Gaps**:
- Specific key-value pairs returned by each callback
- Callback function pointers at qr_t+0x32-0x35
- qr_t struct layout beyond callbacks
- Heartbeat format/timing (FUN_006ab4e0, FUN_006aa2f0)

### 2.2 Team Assignment Mechanism for Mission2/3

**Known**: Team modes use Python `GetFriendlyGroup()` / `GetEnemyGroup()` for group management. Teams are `NameGroup` objects with `AddName/RemoveName/IsNameInGroup` methods.

**Gaps**:
- Exact wire format for team assignment communication
- Whether team is communicated via network message or derived from ship creation order
- Team chat filtering logic (which peers are "teammates")

---

## 3. Not Yet Reversed (Critical for Phase 1)

**This section is now EMPTY.** All items that were previously listed here have been solved by the STBC-Dedicated-Server project and moved to Section 1.

Previously contained:
- ~~3.1 Connection Handshake Protocol~~ -> Now 1.17
- ~~3.2 Packet Wire Format Details~~ -> Now in [phase1-verified-protocol.md](phase1-verified-protocol.md)
- ~~3.3 Reliable Delivery Layer~~ -> Now 1.18
- ~~3.4 Message Type Dispatch Table~~ -> Solved (WI-4)
- ~~3.5 Ship Creation/Destruction Opcode Formats~~ -> Now 1.19
- ~~3.6 Game Object Serialization Format~~ -> Now 1.19
- ~~3.7 Team Assignment~~ -> Moved to 2.2 (partially reversed)
- ~~3.8 GameSpy Query Response Fields~~ -> Moved to 2.1
- ~~3.9 Lobby State Synchronization~~ -> Solved
- ~~3.10 Message Serialization System~~ -> Solved

---

## 4. RE Work Items - ALL SOLVED

All blocking RE work items from the original document have been solved by the STBC-Dedicated-Server project.

| ID | Task | Status | Solution Reference |
|----|------|--------|-------------------|
| WI-1 | Connection handshake protocol | **SOLVED** | STBC-Dedi multiplayer-flow.md |
| WI-2 | Packet wire format (exact byte layout) | **SOLVED** | STBC-Dedi wire-format-spec.md |
| WI-3 | Reliable ACK format and retry logic | **SOLVED** | STBC-Dedi wire-format-spec.md, Transport Layer section |
| WI-4 | Message type dispatch table (DAT_009962d4) | **SOLVED** | Jump table at 0x0069F534, verified against packet traces |
| WI-5 | Game opcodes -- ship entity management | **SOLVED** | 0x03=ObjCreateTeam, 0x14=DestroyObject (see 1.19) |
| WI-6 | GameSpy query response builder | **PARTIALLY SOLVED** | Basic structure known, specific keys TBD (see 2.1) |
| WI-7 | Player slot full structure (0x18 bytes) | **SOLVED** | See Section 7 data structures |
| WI-8 | Lobby settings propagation | **SOLVED** | Opcode 0x00 Settings, 0x16 UICollisionSetting |
| WI-9 | Peer structure layout (all ~0xC0 bytes) | **SOLVED** | See Section 7 data structures |
| WI-10 | GameSpy heartbeat and initialization | **PARTIALLY SOLVED** | Working in STBC-Dedi |
| WI-14 | Hash algorithm lookup tables extraction | **SOLVED** | 4x256-byte tables at 0x0095c888-0x0095cb87 |
| WI-15 | Chat message protocol | **SOLVED** | See 1.12, working relay in STBC-Dedi |
| WI-16 | Game object serialization format | **SOLVED** | See 1.19, DeferredInitObject in STBC-Dedi |
| WI-17 | 44 native opcode enumeration verification | **SOLVED** | See 1.10 (actually 34 game opcodes + gaps, not 44) |

---

## 5. Dependency Graph

All dependencies have been resolved. The graph is preserved for historical reference.

```
ALL SOLVED -- no blocking dependencies remain.

WI-4 (Message Types) ---- SOLVED
WI-2 (Packet Format) ---- SOLVED --> WI-1 (Connection Handshake) -- SOLVED
WI-3 (Reliable ACK) ----- SOLVED         |
WI-9 (Peer Structure) --- SOLVED         v
                                    WI-7 (Player Slots) -- SOLVED
                                          |
                                          v
                                    WI-5 (Game Opcodes) -- SOLVED
                                          |
                                          +--> WI-16 (Object Serialization) -- SOLVED
                                          +--> WI-8 (Settings Sync) -- SOLVED
                                          +--> WI-15 (Chat) -- SOLVED
                                          +--> WI-17 (Opcode Verification) -- SOLVED

WI-6 (GameSpy Query) -- PARTIALLY SOLVED
WI-14 (Hash Tables) ---- SOLVED
```

---

## 6. Recommended RE Order

No further RE work is required for Phase 1 implementation. The remaining partial items (GameSpy query specifics, team assignment details) can be resolved during implementation by testing against vanilla BC clients.

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
| +0x10C | 1 | sendEnabled flag | VERIFIED |
| +0x10E | 1 | isHost flag | VERIFIED |
| +0x10F | 1 | isConnecting flag | HIGH |
| +0x194 | 4 | socket (SOCKET) | VERIFIED |
| +0x338 | 4 | port number | VERIFIED |
| +0x348 | ptr | peer address list head | VERIFIED |

### Peer (~0xC0 bytes)
| Offset | Size | Field | Confidence |
|--------|------|-------|------------|
| +0x18 | 4 | peerID | VERIFIED |
| +0x1C | 4 | address (sockaddr_in) | VERIFIED |
| +0x24 | 2 | seqRecvUnreliable | HIGH |
| +0x26 | 2 | seqSendReliable | HIGH |
| +0x28 | 2 | seqRecvReliable | HIGH |
| +0x2A | 2 | seqSendPriority | HIGH |
| +0x2C | 4 | lastRecvTime (float) | HIGH |
| +0x30 | 4 | lastSendTime (float) | HIGH |
| +0x64-0x7C | - | unreliable queue | VERIFIED |
| +0x7C | 4 | unreliable count | VERIFIED |
| +0x80-0x98 | - | reliable queue | VERIFIED |
| +0x98 | 4 | reliable count | VERIFIED |
| +0x9C-0xB4 | - | priority queue | VERIFIED |
| +0xB4 | 4 | priority count | VERIFIED |
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
| +0x0C-0x17 | - | additional fields (team, name ptr) | HIGH |

### Ship Object (key offsets for damage/subsystems)
| Offset | Type | Field | Confidence |
|--------|------|-------|------------|
| +0x18 | NiNode* | Scene graph root (DoDamage gate) | VERIFIED |
| +0xD8 | float | Ship mass (collision damage formula) | VERIFIED |
| +0x128 | void** | Subsystem damage handler array (ProcessDamage) | VERIFIED |
| +0x130 | int | Subsystem damage handler count | VERIFIED |
| +0x13C | void* | Hull damage receiver | VERIFIED |
| +0x140 | NiNode* | Damage target reference (DoDamage gate) | VERIFIED |
| +0x1B8 | float | Damage resistance multiplier (1.0=normal) | VERIFIED |
| +0x1BC | float | Damage falloff multiplier (1.0=normal) | VERIFIED |
| +0x280 | int | Subsystem count (linked list) | VERIFIED |
| +0x284 | void* | Subsystem linked list HEAD (state updates) | VERIFIED |
| +0x2B0-0x2E4 | ptrs | Named subsystem slots (see subsystem catalog) | VERIFIED |

### NetFile (0x48 bytes)
| Offset | Size | Field | Confidence |
|--------|------|-------|------------|
| vtable+0x18 | ptr | hash table A (tracking) | HIGH |
| vtable+0x28 | ptr | hash table B (queued requests) | HIGH |
| vtable+0x38 | ptr | hash table C (file transfers) | HIGH |

### Network Object ID Allocation
| Formula | Description |
|---------|-------------|
| `0x3FFFFFFF + N * 0x40000` | Player N base object ID |
| `(objID - 0x3FFFFFFF) >> 18` | Extract player slot from object ID |
| 262,143 IDs per player | Maximum objects per player |

### Hash Function (FUN_007202e0)
```c
// 4-table byte-XOR substitution hash
// Tables at 0x0095c888, 0x0095c988, 0x0095ca88, 0x0095cb88
// Each table: 256 bytes
// Input: filename or file content as byte string
// Output: 32-bit hash (a<<24 | b<<16 | c<<8 | d)
// NOT MD5, NOT CRC32
```

---

## 8. Message Flow Diagram

```
Client A                    Server (Host)                    Client B
   |                            |                                |
   |--- ObjectCreateTeam ------>|                                |
   |   (0x03, ship creation)   |--- Clone + Forward ----------->|
   |                            |   (create ship entity)         |
   |                            |                                |
   |--- StateUpdate ----------->|                                |
   |   (0x1C, unreliable)      |--- Clone + Forward ----------->|
   |                            |                                |
   |                            |<--- StartFiring ----- Client B |
   |<--- Clone + Forward ------|   (0x07)                       |
   |                            |                                |
   |--- CHAT_MESSAGE --------->|   (to host only, reliable)      |
   |   (0x2C)                  |--- Python forwards to NoMe --->|
   |                            |                                |
   |                            |--- MISSION_INIT (Python) ---->|
   |<--- MISSION_INIT ---------|   (0x35, reliable)             |
   |                            |                                |
   |                            |--- END_GAME (Python) -------->|
   |<--- END_GAME -------------|   (0x38, reliable)             |
```

---

## 9. Open Questions - ALL ANSWERED

| # | Question | Answer |
|---|----------|--------|
| 1 | What is 0x8009? | Network-only object type that shouldn't be instantiated on the host. Confirmed from decompiled code. |
| 2 | What does hasPlayerSlot indicate? | Opcodes 0x02/0x03 include a player slot byte. Other event-forward opcodes (0x07-0x12) do not. Determined by the jump table entry. |
| 3 | How does FUN_0069f930 differ from FUN_0069f620? | FUN_0069f930 handles TorpedoFire (0x19) and other projectile creation. It does NOT relay via clone-forward; it creates the projectile locally and uses the "Forward" group. |
| 4 | What is DAT_008e5528? | Confirmed: "NoMe" network group name. The other group "Forward" is at `s_Forward_008d94a0`. |
| 5 | Exact value of MAX_MESSAGE_TYPES? | **0x2B (43).** Verified: `CHAT_MESSAGE = MAX + 1 = 0x2C`, confirmed by packet traces showing CHAT at byte 0x2C. |
| 6 | Ship creation serialized fields? | ObjectCreateTeam (0x03) carries full object state via `vtable[0x10C]` serialization. DeferredInitObject extracts ship type from Python-level species mapping. |
| 7 | Team assignment wire format? | Team ID is embedded in ObjectCreateTeam (0x03) as byte[2]. Python scripts manage group membership via NameGroup AddName/RemoveName. |
