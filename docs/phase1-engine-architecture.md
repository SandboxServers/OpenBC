# OpenBC Phase 1: Bridge Commander Engine Architecture Reference

## Document Status
- **Created**: 2026-02-15
- **Source**: STBC-Dedicated-Server reverse engineering (Ghidra decompilation, RTTI catalog, function tracing)
- **Purpose**: Provide implementors with a clear understanding of BC's engine layers, so OpenBC can reimplement the relevant parts
- **Cross-reference**: [phase1-verified-protocol.md](phase1-verified-protocol.md) for wire format, [phase1-re-gaps.md](phase1-re-gaps.md) for gap analysis

---

## 1. Three-Layer Architecture

Bridge Commander's engine has three distinct layers:

```
+-----------------------------------------------------+
|  Game Logic Layer                                    |
|  UtopiaModule, MultiplayerGame, Player Slots,        |
|  Ship Objects, Mission Scripts, Scoring               |
+-----------------------------------------------------+
|  TG Framework Layer                                  |
|  TGObject, TGEventManager, TGNetwork, TGMessage,     |
|  TGBufferStream, TGTimerManager, TGConfigMapping      |
+-----------------------------------------------------+
|  NetImmerse 3.1 Engine                               |
|  NiObject, NiNode, NiAVObject, NiRTTI, NiStream,     |
|  Scene Graph, Renderer (D3D7/DDraw7)                  |
+-----------------------------------------------------+
```

**For Phase 1 (headless relay server), only the top two layers matter.** The NetImmerse scene graph and renderer are not needed -- the server has no 3D visualization. However, understanding NI 3.1 is useful for debugging and future phases.

---

## 2. NetImmerse 3.1 Layer

### Overview

BC uses NetImmerse 3.1 (predecessor to Gamebryo). The RTTI catalog shows 129 NI classes, 124 TG framework classes, and ~420 game-specific classes (670 total).

### NiRTTI and Type System

Every NI object has runtime type information:
- **Slot 0 = GetRTTI** (NOT destructor -- opposite of Gamebryo 1.2!)
- **Slot 10 = scalar_deleting_dtor** (+0x28)
- NiObject: 12 vtable slots
- NiObjectNET: 12 (adds ZERO new slots)
- NiAVObject: 39 (+27 new)
- NiNode: 43 (+4 new)

**Warning**: NI 3.1 has MORE virtuals than Gamebryo 1.2. Slot names cannot be copied blindly from Gamebryo source.

### Key NI Types Used by Game

| Type | Purpose | Phase 1 Relevance |
|------|---------|-------------------|
| NiNode | Scene graph node (ships, subsystems) | Ship identity (ship+0x18) |
| NiAVObject | Visual object base | Bounding sphere at node+0x94 |
| NiStream | File I/O (NIF loading) | Not needed for relay |

### Not Needed for Phase 1

The entire NI scene graph, renderer, texture system, and geometry pipeline are irrelevant for a headless relay server. OpenBC Phase 1 should NOT attempt to reimplement these.

---

## 3. TG Framework Layer

The TG (Totally Games) framework sits between NI and game logic. This is the layer OpenBC must reimplement.

### TGObject

Base class for all TG framework objects. Provides:
- Type identification (`GetObjType()`, `IsTypeOf()`)
- Object ID allocation (`GetObjID()`)
- Serialization interface

### TGEventManager

Central event dispatch system:

| Function | Address | Purpose |
|----------|---------|---------|
| ProcessEvents | FUN_006da2c0 | Dequeue from ring buffer + dispatch |
| RegisterHandler | FUN_006db380 | Register handler for event type |
| RegisterNamedHandler | FUN_006da130 | Register by function name string |
| DispatchToChain | FUN_006db620 | Walk handler chain for an event |
| PostEvent | FUN_006da2a0 | Add event to queue |

**Global instance**: `0x0097F838` (EventManager), `0x0097F864` (handler registry).

Events are 32-bit type codes. The handler registry is a hash map of `event_type -> handler chain`.

Key event types:
| Type | Name | Description |
|------|------|-------------|
| 0x60001 | ET_NETWORK_MESSAGE_EVENT | Incoming network message (dispatches to both NetFile and MultiplayerGame) |
| 0x60002 | ET_NETWORK_CONNECT_EVENT | Host session created |
| 0x60004 | ET_NETWORK_NEW_PLAYER | New peer connected |
| 0x8000e7 | ET_SYSTEM_CHECKSUM_FAILED | Checksum mismatch |
| 0x8000e8 | ET_CHECKSUM_COMPLETE | All checksums passed for a player |
| 0x8000e9 | ET_KILL_GAME | Game killed |
| 0x8000f6 | ET_BOOT_PLAYER | Anti-cheat kick |
| 0x800053 | ET_START | Application start |
| 0x80004A | ET_CREATE_SERVER | Server creation |
| 0x8000C8 | ET_OBJECT_CREATED_NOTIFY | Game object created |
| 0x8000F1 | ET_NEW_PLAYER_IN_GAME | Player fully joined |

### TGNetwork / TGWinsockNetwork

Network subsystem. TGWinsockNetwork (0x34C bytes) is the concrete implementation.

Key methods:
| Method | Description |
|--------|-------------|
| HostOrJoin | Create socket, set HOST/CLIENT mode |
| Update | Call SendOutgoing + ProcessIncoming + DispatchQueue |
| Send | Queue message for a specific peer (or broadcast to all) |
| SendToGroup | Queue message for a named group |
| GetNextMessage | Dequeue next delivered message |

Internal functions:
| Function | Address | Purpose |
|----------|---------|---------|
| FUN_006b4c10 | TGNetwork::Send | Binary-search peer array, queue message |
| FUN_006b55b0 | SendOutgoingPackets | Drain 3 queues per peer, serialize, sendto |
| FUN_006b5c90 | ProcessIncomingPackets | recvfrom, parse, create peers |
| FUN_006b5f70 | DispatchIncomingQueue | Validate sequences, deliver to application |

### TGMessage

Network message container. Key fields at known offsets (see [phase1-verified-protocol.md](phase1-verified-protocol.md) Section 2).

Important methods exposed to Python:
| Python Method | Purpose |
|---------------|---------|
| `TGMessage_Create()` | Allocate new message |
| `TGMessage_Copy(msg)` | Clone message (for relay) |
| `SetGuaranteed(flag)` | Mark as reliable delivery |
| `SetDataFromStream(stream)` | Fill from TGBufferStream |

### TGBufferStream

Binary serialization stream. Layout at offsets +0x1C (buffer), +0x24 (position), +0x2C (bit-pack state).

Read/Write functions documented in [phase1-verified-protocol.md](phase1-verified-protocol.md) Section 11.

### TGTimerManager

Timer management for scheduled callbacks. Used by the game loop for periodic tasks.

### TGConfigMapping

INI-style configuration file reader. Loads `stbc.cfg` and provides Get/Set methods for key-value pairs. Used for server name, player name, password, and other settings.

### VarManager

Global variable store. Provides `Get/SetVariable` for shared state between C++ and Python. Used for episode event type construction (`MakeEpisodeEventType`).

---

## 4. Game Logic Layer

### UtopiaModule

Root game object at `0x0097FA00`. Contains pointers to all major subsystems:

| Offset | Field | Description |
|--------|-------|-------------|
| +0x78 | TGWinsockNetwork* | Network subsystem |
| +0x7C | GameSpy* | LAN discovery |
| +0x80 | NetFile* | Checksum/file transfer manager |
| +0x88 | IsClient (u8) | 0=host, 1=client |
| +0x89 | IsHost (u8) | 1=host, 0=client |
| +0x8A | IsMultiplayer (u8) | 1=multiplayer active |

For a dedicated server: `IsClient=0, IsHost=1, IsMultiplayer=1`.

### MultiplayerGame

Game session manager at `0x0097E238`. Contains player slots and game state:

| Offset | Field | Description |
|--------|-------|-------------|
| +0x74 | playerSlots[16] | 16 player slots, 0x18 bytes each |
| +0x1F8 | readyForNewPlayers | Accept connections flag |
| +0x1FC | maxPlayers | Maximum player count |

Registers 28 event handlers for all game events (see [phase1-verified-protocol.md](phase1-verified-protocol.md) Section 12).

### Player Slots

16 slots at MultiplayerGame+0x74, stride 0x18:

| Offset | Field |
|--------|-------|
| +0x00 | active flag |
| +0x04 | peer network ID |
| +0x08 | player object ID |

Slot assignment: first empty slot on connection. Slot freed on disconnect.

### Ship Hierarchy

Ships are full game objects with subsystems. On the stock game, a Sovereign-class ship has 33 subsystems. Key subsystem types (15 named slots from ship+0x2B0 to ship+0x2E4):

| Offset | Name | Type |
|--------|------|------|
| +0x2B0 | Powered | PoweredSubsystem |
| +0x2B4 | Shield | ShieldGenerator |
| +0x2B8 | Phaser | PhaserController |
| +0x2C0 | Repair | RepairSubsystem |
| +0x2C4 | Power | PowerReactor |
| +0x2C8 | Cloak | CloakingDevice |
| +0x2CC | LifeSupport | LifeSupport |
| +0x2D0 | Sensor | SensorArray |
| +0x2D4 | Pulse | PulseWeapon |
| +0x2D8 | Warp | WarpDrive |
| +0x2E0 | ShipRef | ShipRefNiNode |

Plus multiple instances of: ImpulseEngine (4), PhaserEmitter (8), TorpedoTube (6), TractorBeam (4).

**For Phase 1 relay server**: Ship entities are lightweight data containers. Full subsystem simulation is not needed -- the relay server forwards StateUpdate packets without interpreting subsystem data.

### Object ID System

Network object IDs are allocated per-player:
- Player N base = `0x3FFFFFFF + N * 0x40000` (262,143 IDs each)
- Extract player slot from ID: `(objID - 0x3FFFFFFF) >> 18`
- IDs are monotonic within a session (never recycled)

---

## 5. Message Flow (Three Dispatchers + Python Path)

BC has three C++ message dispatchers plus a Python-level path:

```
Incoming UDP packet
    |
    v
TGNetwork::ProcessIncomingPackets
    |
    v
TGNetwork::DispatchIncomingQueue
    |
    v
ET_NETWORK_MESSAGE_EVENT (0x60001) posted to EventManager
    |
    +----> NetFile Dispatcher (FUN_006a3cd0)
    |      Handles opcodes 0x20-0x27 (checksums, file transfer)
    |      Registered on UtopiaModule+0x80 (NetFile object)
    |
    +----> MultiplayerGame Dispatcher (0x0069f2a0)
    |      Handles opcodes 0x00-0x2A (game messages)
    |      Jump table at 0x0069F534 (41 entries)
    |      Relay pattern: clone + forward to all other peers
    |
    +----> MultiplayerWindow Dispatcher (FUN_00504c10)
           Handles opcodes 0x00, 0x01, 0x16 (UI-level)
           Only on client (gated by this+0xb0)

Python Path (opcodes 0x2C-0x39):
    These bypass ALL C++ dispatchers.
    Sent via SendTGMessage(), received by Python ReceiveMessage handlers.
    The Python script decides whether to relay (e.g., chat forwarding).
```

### Relay Pattern

For most game opcodes, the server performs clone-and-forward:
1. Receive message from client
2. Read byte[1] (sender player slot)
3. For each of 16 player slots:
   - If active AND not sender AND not self: clone message, send to peer
4. Optionally deserialize locally for bookkeeping

The server does NOT need to understand most message payloads. It operates on raw bytes.

### Messages the Server Constructs (Never Receives)

| Opcode | Name | When |
|--------|------|------|
| 0x00 | Settings | After checksum pass |
| 0x01 | GameInit | After checksum pass |
| 0x04 | BootPlayer | When rejecting (server full, kicked) |
| 0x14 | DestroyObject | When a ship is destroyed |
| 0x29 | Explosion | When damage creates an explosion |
| 0x2A | NewPlayerInGame | When a player fully joins |

---

## 6. Python / SWIG Integration

### Shadow Class System

BC uses SWIG 1.x to generate Python bindings for C++ classes. Python objects hold string handles in the format `_HEXID_p_TypeName`:

```python
pShip = App.ShipClass_Cast(pObj)
# pShip is a Python shadow class wrapping "_00000042_p_ShipClass"
pShip.GetObjID()  # Calls C++ via SWIG thunk
```

The SWIG table at `0x008e6438` contains 3,990 wrapper functions.

### Handle Format

`_HEXID_p_TypeName` where:
- HEXID = hex string of the C++ pointer or object ID
- TypeName = C++ class name (ShipClass, TGNetwork, etc.)

Type hierarchy checking via SWIG: `ShipClass` is-a `PhysicsObjectClass` is-a `ObjectClass` is-a `TGObject`.

### Python 1.5.2 Gotchas

BC embeds Python 1.5.2. Key incompatibilities with modern Python:

| Construct | Problem | Workaround |
|-----------|---------|------------|
| `x in dict` | Not supported | `dict.has_key(x)` |
| `'sub' in 'string'` | Not supported | `strop.find(string, sub) >= 0` |
| `import X as Y` | `as` keyword doesn't exist | Use `X = __import__('X')` |
| `continue` in try/except | SyntaxError | Restructure loop |
| List comprehensions | Not supported | Use `map`/`filter` |
| Ternary `x if c else y` | Not supported | `(y, x)[c]` or if/else |
| `print(x)` | Print is a statement | `print x` |
| `except E as e:` | Wrong syntax | `except E, e:` |

### Key SWIG Modules

- **App** (high-level): Shadow classes, handle-based API
- **Appc** (low-level): Direct C++ access, constants, type IDs

Constants include all ET_* event types, SPECIES_* ship types, CT_* damage types, and MAX_MESSAGE_TYPES (0x2B).

---

## 7. Bootstrap Sequence

The original game (and our proxy) bootstraps in phases:

### Phase 0: Flag Setting
Direct memory writes to configure multiplayer mode:
```
0x0097FA88 (IsClient) = 0
0x0097FA89 (IsHost) = 1
0x0097FA8A (IsMultiplayer) = 1
```

### Phase 1: Network Initialization
Call `FUN_00445d90` (UtopiaModule::InitMultiplayer):
1. Create TGWinsockNetwork (0x34C bytes) -> UtopiaModule+0x78
2. Set port to 22101 (0x5655)
3. Call TGNetwork_HostOrJoin(0, password) for HOST mode
4. Create NetFile (0x48 bytes) -> UtopiaModule+0x80
5. Create GameSpy (0xF4 bytes) -> UtopiaModule+0x7C

### Phase 2: MultiplayerGame Creation
Call `FUN_00504f10` (TopWindow_SetupMultiplayerGame):
1. Create MultiplayerGame session object
2. Register at `0x0097E238`
3. Set `readyForNewPlayers = 1` (at +0x1F8)

### Phase 3: Python Automation
Execute `DedicatedServer.TopWindowInitialized()`:
1. Set server name, captain name, max players
2. Create GameSpy for LAN discovery
3. Enable new player handling
4. Start game session

### Phase 4: Game Loop (30Hz)
Periodic timer fires `GameLoopTimerProc` at ~33ms intervals:
1. `UtopiaApp_MainTick` -- event processing, simulation
2. `TGNetwork::Update` -- send/receive packets
3. GameSpy query router -- handle LAN discovery
4. Peer detection -- scan WSN peer array for new connections
5. InitNetwork scheduling -- call `Mission1.InitNetwork(peerID)` 30 ticks after peer appears
6. DeferredInitObject -- poll for new ship objects, load NIF + create subsystems

---

## 8. Key Function Address Table

Functions that OpenBC must reimplement or understand:

### Network Layer
| Address | Name | Purpose |
|---------|------|---------|
| 0x006b3ec0 | TGNetwork_HostOrJoin | Socket creation, HOST/CLIENT mode |
| 0x006b4c10 | TGNetwork::Send | Queue message for peer |
| 0x006b4560 | TGNetwork::Update | Main network tick |
| 0x006b55b0 | SendOutgoingPackets | Drain queues, serialize, sendto |
| 0x006b5c90 | ProcessIncomingPackets | recvfrom, parse, create peers |
| 0x006b5f70 | DispatchIncomingQueue | Validate sequences, deliver |
| 0x006b61e0 | ReliableACKHandler | Process ACK messages |
| 0x006b7410 | CreatePeerEntry | Allocate new peer in sorted array |

### Game Logic
| Address | Name | Purpose |
|---------|------|---------|
| 0x00445d90 | UtopiaModule::InitMultiplayer | Create WSN + NetFile + GameSpy |
| 0x00504f10 | TopWindow_SetupMultiplayerGame | Create MultiplayerGame session |
| 0x0069f2a0 | ReceiveMessageHandler | Main opcode dispatcher (jump table) |
| 0x0069f620 | ProcessGameMessage | Clone-and-forward relay |
| 0x006a0a30 | NewPlayerHandler | Assign slot, start checksums |
| 0x006a1b10 | ChecksumCompleteHandler | Send settings + GameInit |
| 0x006a1e70 | NewPlayerInGameHandler | Trigger InitNetwork + replication |
| 0x006a1aa0 | GetShipFromPlayerID | __cdecl(int connID) -> ship* |

### Checksum System
| Address | Name | Purpose |
|---------|------|---------|
| 0x006a3820 | ChecksumRequestSender | Queue 4 requests, send #0 |
| 0x006a3cd0 | NetFile::ReceiveMessageHandler | Checksum opcode dispatcher |
| 0x006a4560 | ChecksumResponseVerifier | Hash compare, send next |
| 0x006a4bb0 | ChecksumAllPassed | Fire ET_CHECKSUM_COMPLETE |
| 0x007202e0 | HashString | 4-table XOR substitution hash |

### Event System
| Address | Name | Purpose |
|---------|------|---------|
| 0x006da2c0 | EventManager::ProcessEvents | Dequeue + dispatch |
| 0x006db380 | RegisterHandler | Register for event type |
| 0x006db620 | DispatchToChain | Walk handler chain |
| 0x006da2a0 | PostEvent | Add event to queue |

### Damage System (reference only -- not needed for Phase 1 relay)
| Address | Name | Purpose |
|---------|------|---------|
| 0x00594020 | DoDamage | Central damage dispatcher |
| 0x00593e50 | ProcessDamage | Subsystem damage distribution |
| 0x005b0060 | CollisionDamageWrapper | Collision entry point |
| 0x00593650 | DoDamage_FromPosition | Single-point collision |
| 0x005952d0 | DoDamage_CollisionContacts | Multi-contact collision |
| 0x006a01e0 | DestroyObject_Net | Opcode 0x14 handler |
| 0x006a0080 | Explosion_Net | Opcode 0x29 handler |

### Serialization
| Address | Name | Purpose |
|---------|------|---------|
| 0x006cefe0 | TGBufferStream::Create | Create stream |
| 0x006cf730 | WriteByte | Write u8 |
| 0x006cf770 | WriteBit | Pack boolean into shared byte |
| 0x006cf7f0 | WriteShort | Write u16 LE |
| 0x006cf870 | WriteInt32 | Write i32 |
| 0x006cf8b0 | WriteFloat | Write f32 |
| 0x006cf540 | ReadByte | Read u8 |
| 0x006cf580 | ReadBit | Read packed boolean |
| 0x006cf600 | ReadShort | Read u16 LE |
| 0x006cf670 | ReadInt32 | Read i32 |
| 0x006cf6b0 | ReadFloat | Read f32 |
