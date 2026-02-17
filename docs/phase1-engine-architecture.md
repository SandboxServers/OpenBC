# OpenBC Phase 1: Bridge Commander Protocol Architecture Reference

## Document Status
- **Created**: 2026-02-15, **Updated**: 2026-02-17 (clean room audit: all binary addresses and struct offsets removed)
- **Source**: Observable protocol behavior, packet captures, readable Python scripts
- **Purpose**: Provide implementors with a clear understanding of BC's original engine layers, so OpenBC can reimplement the protocol-relevant parts. OpenBC is a standalone C server with data-driven configuration -- it does not embed Python, use SWIG, or replicate the original engine's class hierarchy. This document describes the behavioral architecture that OpenBC must be wire-compatible with.
- **Cross-reference**: [phase1-verified-protocol.md](phase1-verified-protocol.md) for wire format, [phase1-implementation-plan.md](phase1-implementation-plan.md) for OpenBC's architecture

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

**For the standalone OpenBC server, only the top two layers matter.** The NetImmerse scene graph and renderer are not needed -- the server has no 3D visualization. However, understanding NI 3.1 is useful for debugging object serialization formats and future rendering phases.

---

## 2. NetImmerse 3.1 Layer

### Overview

BC uses NetImmerse 3.1 (predecessor to Gamebryo). The engine's RTTI catalog shows 129 NI classes, 124 TG framework classes, and ~420 game-specific classes (670 total).

### NiRTTI and Type System

Every NI object has runtime type information. The type hierarchy is used for safe casting and object identification. NI 3.1's vtable layout differs from later Gamebryo versions -- slot names cannot be copied blindly from Gamebryo source.

### Key NI Types Used by Game

| Type | Purpose | Phase 1 Relevance |
|------|---------|-------------------|
| NiNode | Scene graph node (ships, subsystems) | Ship identity, referenced in wire format |
| NiAVObject | Visual object base | Bounding sphere, collision mesh source |
| NiStream | File I/O (NIF loading) | Not needed for standalone server |

### Not Needed for Standalone Server

The entire NI scene graph, renderer, texture system, and geometry pipeline are irrelevant for the standalone OpenBC server. The server uses precomputed collision mesh data (extracted from NIF files by a build-time tool) rather than loading NIF files at runtime.

---

## 3. TG Framework Layer

The TG (Totally Games) framework sits between NI and game logic. OpenBC reimplements the protocol-facing behavior of this layer (message framing, event dispatch, serialization) but does not replicate the original class hierarchy.

### TGObject

Base class for all TG framework objects. Provides:
- Type identification (`GetObjType()`, `IsTypeOf()`)
- Object ID allocation (`GetObjID()`)
- Serialization interface

### TGEventManager

Central event dispatch system. The event manager processes events from a ring buffer and dispatches them to registered handler chains. Key behaviors:
- **ProcessEvents**: Dequeue from ring buffer and dispatch to registered handlers
- **RegisterHandler**: Register a handler callback for a specific event type
- **DispatchToChain**: Walk the handler chain for an event, calling each handler in order
- **PostEvent**: Add an event to the queue for deferred processing

Events are 32-bit type codes. The handler registry is a hash map of `event_type -> handler chain`.

Key event types (these are protocol constants, not binary addresses):
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

Network subsystem. TGWinsockNetwork is the concrete UDP implementation.

Key methods:
| Method | Description |
|--------|-------------|
| HostOrJoin | Create socket, set HOST/CLIENT mode |
| Update | Call SendOutgoing + ProcessIncoming + DispatchQueue |
| Send | Queue message for a specific peer (or broadcast to all) |
| SendToGroup | Queue message for a named group |
| GetNextMessage | Dequeue next delivered message |

Internal behavior:
- **Send**: Binary-search the peer array, queue message to the appropriate peer
- **SendOutgoingPackets**: Drain 3 queues per peer (priority, reliable, unreliable), serialize, sendto
- **ProcessIncomingPackets**: recvfrom, parse transport framing, create peers for new connections
- **DispatchIncomingQueue**: Validate sequence numbers, deliver messages to the application layer

### TGMessage

Network message container. See [phase1-verified-protocol.md](phase1-verified-protocol.md) Section 2 for wire format.

Important methods exposed to Python:
| Python Method | Purpose |
|---------------|---------|
| `TGMessage_Create()` | Allocate new message |
| `TGMessage_Copy(msg)` | Clone message (for relay) |
| `SetGuaranteed(flag)` | Mark as reliable delivery |
| `SetDataFromStream(stream)` | Fill from TGBufferStream |

### TGBufferStream

Binary serialization stream with position-tracked byte buffer and bit-packing state.

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

Root game object. Contains pointers to all major subsystems:

| Field | Description |
|-------|-------------|
| TGWinsockNetwork* | Network subsystem |
| GameSpy* | LAN discovery |
| NetFile* | Checksum/file transfer manager |
| IsClient (u8) | 0=host, 1=client |
| IsHost (u8) | 1=host, 0=client |
| IsMultiplayer (u8) | 1=multiplayer active |

For a dedicated server: `IsClient=0, IsHost=1, IsMultiplayer=1`.

### MultiplayerGame

Game session manager. Contains player slots and game state:

| Field | Description |
|-------|-------------|
| playerSlots[16] | 16 player slots |
| readyForNewPlayers | Accept connections flag |
| maxPlayers | Maximum player count |

Registers 28 event handlers for all game events (see [phase1-verified-protocol.md](phase1-verified-protocol.md) Section 12).

### Player Slots

16 player slots, each containing:

| Field | Description |
|-------|-------------|
| active flag | Whether slot is in use |
| peer network ID | Network peer identifier |
| player object ID | Game object ID for this player's ship |

Slot assignment: first empty slot on connection. Slot freed on disconnect.

### Ship Hierarchy

Ships are full game objects with subsystems. On the stock game, a Sovereign-class ship has 33 subsystems. Key subsystem types:

| Name | Type |
|------|------|
| Powered | PoweredSubsystem |
| Shield | ShieldGenerator |
| Phaser | PhaserController |
| Repair | RepairSubsystem |
| Power | PowerReactor |
| Cloak | CloakingDevice |
| LifeSupport | LifeSupport |
| Sensor | SensorArray |
| Pulse | PulseWeapon |
| Warp | WarpDrive |
| ShipRef | ShipRefNiNode |

Plus multiple instances of: ImpulseEngine (4), PhaserEmitter (8), TorpedoTube (6), TractorBeam (4).

**For the standalone OpenBC server**: The server tracks ship state in lightweight data structures defined in the data registry (ships.toml). It does not replicate the original C++ class hierarchy. Subsystem health, weapon state, and other ship properties are tracked as flat data for protocol compatibility and damage simulation.

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
ET_NETWORK_MESSAGE_EVENT posted to EventManager
    |
    +----> NetFile Dispatcher
    |      Handles opcodes 0x20-0x27 (checksums, file transfer)
    |
    +----> MultiplayerGame Dispatcher
    |      Handles opcodes 0x00-0x2A (game messages)
    |      41-entry jump table indexed by opcode-2
    |      Relay pattern: clone + forward to all other peers
    |
    +----> MultiplayerWindow Dispatcher
           Handles opcodes 0x00, 0x01, 0x16 (UI-level)
           Only on client

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

## 6. Python / SWIG Integration (Original Engine Reference)

> **Note**: OpenBC does NOT use Python or SWIG. The standalone server is pure C with data-driven configuration (TOML/JSON). This section is preserved as reference for understanding how the original mission scripts drive game flow and for decoding Python-level opcodes (0x2C-0x39) that the OpenBC server must generate natively.

### Shadow Class System

BC uses SWIG 1.x to generate Python bindings for C++ classes. Python objects hold string handles in the format `_HEXID_p_TypeName`:

```python
pShip = App.ShipClass_Cast(pObj)
# pShip is a Python shadow class wrapping "_00000042_p_ShipClass"
pShip.GetObjID()  # Calls C++ via SWIG thunk
```

The SWIG binding layer contains ~3,990 wrapper functions.

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

## 7. Bootstrap Sequence (Original Engine Reference)

> **Note**: OpenBC's bootstrap is fundamentally different -- it is a standalone C server that reads TOML/JSON configuration, initializes networking directly, and enters a game loop with no Python or SWIG involvement. This section documents the original engine's bootstrap for reference (useful for understanding the order of operations that clients expect).

The original game bootstraps in phases:

### Phase 0: Flag Setting
Configure multiplayer mode flags: IsClient=0, IsHost=1, IsMultiplayer=1.

### Phase 1: Network Initialization
Initialize the multiplayer subsystem:
1. Create TGWinsockNetwork
2. Set port to 22101
3. Call HostOrJoin for HOST mode
4. Create NetFile (checksum/file transfer manager)
5. Create GameSpy (LAN discovery)

### Phase 2: MultiplayerGame Creation
Set up the game session:
1. Create MultiplayerGame session object
2. Register it globally
3. Set readyForNewPlayers = 1

### Phase 3: Python Automation (original only -- OpenBC reads server.toml instead)
Execute `DedicatedServer.TopWindowInitialized()`:
1. Set server name, captain name, max players
2. Create GameSpy for LAN discovery
3. Enable new player handling
4. Start game session

### Phase 4: Game Loop (30Hz)
Periodic timer fires at ~33ms intervals:
1. Main tick -- event processing, simulation
2. Network update -- send/receive packets
3. GameSpy query router -- handle LAN discovery
4. Peer detection -- scan peer array for new connections
5. InitNetwork scheduling -- call InitNetwork for new peers (~30 ticks after appearance)
6. Ship object polling -- detect new ship objects, initialize subsystems

---

## 8. Behavioral Reference Summary

The following behavioral patterns are what OpenBC must replicate for wire compatibility:

### Network Layer
| Behavior | Description |
|----------|-------------|
| HostOrJoin | Socket creation, HOST/CLIENT mode selection |
| Send | Binary-search peer array, queue message for peer |
| Update | Main network tick: send outgoing + process incoming + dispatch queue |
| SendOutgoing | Drain 3 queues per peer (priority, reliable, unreliable), serialize, sendto |
| ProcessIncoming | recvfrom, parse transport framing, create peers for new connections |
| DispatchQueue | Validate sequence numbers, deliver messages to application layer |
| ReliableACK | Process ACK messages, clear retransmit queue |
| CreatePeer | Allocate new peer in sorted array |

### Game Logic
| Behavior | Description |
|----------|-------------|
| InitMultiplayer | Create network + checksum + discovery subsystems |
| SetupMultiplayerGame | Create game session, set readyForNewPlayers |
| ReceiveMessage | Main opcode dispatcher (41-entry jump table) |
| ProcessGameMessage | Clone-and-forward relay for game opcodes |
| NewPlayerHandler | Assign slot, start checksum exchange |
| ChecksumCompleteHandler | Send Settings + GameInit after checksums pass |
| NewPlayerInGameHandler | Trigger InitNetwork + state replication |
| GetShipFromPlayerID | Look up ship object by connection ID |

### Checksum System
| Behavior | Description |
|----------|-------------|
| ChecksumRequestSender | Queue 4 directory requests, send first |
| NetFile ReceiveMessage | Checksum opcode dispatcher (0x20-0x27) |
| ChecksumResponseVerifier | Compare hashes against manifest, send next round |
| ChecksumAllPassed | Fire ET_CHECKSUM_COMPLETE event |
| StringHash | 4-lane Pearson hash for name matching |
| FileHash | Rotate-XOR hash for content integrity (skips bytes 4-7) |

### Damage System (reimplemented natively in OpenBC)
| Behavior | Description |
|----------|-------------|
| DoDamage | Central damage dispatcher |
| ProcessDamage | Subsystem damage distribution |
| CollisionDamageWrapper | Collision entry point |
| DoDamage_FromPosition | Single-point collision damage |
| DoDamage_CollisionContacts | Multi-contact collision damage |
| DestroyObject_Net | Opcode 0x14 handler (object destruction) |
| Explosion_Net | Opcode 0x29 handler (explosion effect) |

### Serialization
| Behavior | Description |
|----------|-------------|
| TGBufferStream::Create | Create serialization stream |
| WriteByte / ReadByte | Write/read u8 |
| WriteBit / ReadBit | Pack/unpack boolean into shared byte |
| WriteShort / ReadShort | Write/read u16 LE |
| WriteInt32 / ReadInt32 | Write/read i32 |
| WriteFloat / ReadFloat | Write/read f32 |
