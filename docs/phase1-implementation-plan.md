# OpenBC Phase 1: Implementation Plan

## Document Status
- **Generated**: 2026-02-07, **Revised**: 2026-02-15 (RE items resolved, opcode corrections)
- **Source**: Synthesized from 9 specialized agent analyses (gameplay expansion round)
- **Agents consulted**: openbc-architect, game-reverse-engineer, swig-api-compat, stbc-original-dev, network-protocol, python-migration, flecs-ecs-architect, physics-sim, mod-compat-tester
- **Prerequisite**: [phase1-requirements.md](phase1-requirements.md)

---

## 1. Architecture Overview

```
                       +-------------------+
                       |   UDP Socket      |
                       |   (port 22101)    |
                       +--------+----------+
                                |
                    +-----------+-----------+
                    |                       |
              +-----v-----+         +------v------+
              | GameSpy    |         | TGNetwork   |
              | Query      |         | Protocol    |
              | Responder  |         | Engine      |
              +-----+------+         +------+------+
                    |  (peek-router demux)  |
                    +-----------+-----------+
                                |
                       +--------v----------+
                       |   Event Manager   |
                       |   (dispatch)      |
                       +--------+----------+
                                |
        +-----------------------+---------------------------+
        |                       |                           |
+-------v--------+     +-------v--------+       +----------v-----------+
| NetFile /       |     | MultiplayerGame|       | Python Script        |
| Checksum Mgr    |     | State Machine  |       | Engine               |
+-------+--------+     +-------+--------+       +----------+-----------+
        |                       |                           |
        |          +------------+------------+              |
        |          |            |            |              |
        |   +------v-----+ +---v-----+ +---v--------+     |
        |   | Message    | | Ship    | | Game        |     |
        |   | Relay      | | Object  | | Lifecycle   |     |
        |   | Engine     | | Model   | | FSM         |     |
        |   +------+-----+ +---+-----+ +---+---------+    |
        |          |            |            |              |
        +----------+--------+--+------------+--------------+
                             |
                      +------v-------+
                      |   flecs ECS  |
                      |   World      |
                      +--------------+
```

### Target Dependency Chain
```
flecs_static + Python3::Python + ws2_32(Win)/pthread(Linux)
    |
openbc_engine -> openbc_compat -> openbc_scripting -> openbc_network
    |                |                |                    |
    +--------- openbc_core (static lib) ------------------+
                     |
              openbc_server (executable)
              openbc_tests (test executable)
```

---

## 2. Work Chunks

The implementation is organized into 13 chunks across 3 parallel tracks. Each chunk has clear inputs, outputs, and acceptance criteria.

### Track A: Core Infrastructure
### Track B: Protocol & Network
### Track C: Scripting & API

---

### Chunk 1: Project Skeleton & Build System
**Track**: A | **Duration**: ~3-4 days | **Dependencies**: None

**Deliverables**:
- Root `CMakeLists.txt` with `OPENBC_SERVER_ONLY` option
- `CMakePresets.json` (debug, release, ci presets)
- `cmake/OpenBCVersion.cmake`
- `cmake/CompilerWarnings.cmake`
- `cmake/FetchDependencies.cmake`
- `include/openbc/config.h.in` (generated config header)
- `include/openbc/types.h` (platform socket abstraction: `SOCKET`, `sockaddr_in`, etc.)
- `src/CMakeLists.txt` (orchestrates subdirectories)
- Subdirectory CMakeLists for: engine, compat, network, scripting
- `vendor/flecs/` (download flecs v4.1.4 single-header release)
- `tests/CMakeLists.txt`
- `.github/workflows/build.yml` (CI matrix: Ubuntu/Windows/macOS x Debug/Release)
- `docker/Dockerfile.server` + `.dockerignore`

**Acceptance**: `cmake -B build -DOPENBC_SERVER_ONLY=ON && cmake --build build` succeeds on all 3 platforms producing an `openbc_server` binary (even if it does nothing yet).

**Key patterns**:
- Use `Python3::Python` imported target, NOT raw variables
- flecs_static needs `wsock32 ws2_32` on Windows, `pthread` on Linux
- Suppress warnings on vendored code with `-w` (GCC/Clang) / `/W0` (MSVC)
- Default build type to Release for single-config generators
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON` for IDE support

---

### Chunk 2: flecs ECS World & Component Registration
**Track**: A | **Duration**: ~3-4 days | **Dependencies**: Chunk 1

**Deliverables**:
- `src/engine/engine.h` / `engine.c` - Core engine struct and lifecycle
- `src/engine/game_loop.c` - 30Hz fixed timestep main loop
- `src/engine/clock.c` - Monotonic clock, game time, frame time
- `src/engine/components.h` - All Phase 1 ECS component definitions
- `src/engine/modules.c` - Register 3 modules (ObcNetworkModule, ObcGameStateModule, ObcCompatModule)

**Components to define**:
```c
// Singletons
ObcServerConfig, ObcGameSession, ObcNetworkState, ObcConfigMapping, ObcVarManager

// Per-entity
ObcNetworkPeer, ObcPlayerSlot, ObcChecksumExchange, ObcSWIGHandle

// Tags
ObcChecksumPassed, ObcChecksumFailed, ObcPeerDisconnecting, ObcPeerTimedOut

// Ship components (definitions only, populated in Chunk 11)
ObcShipIdentity, ObcShipOwnership

// Ship tags
ObcShipAlive, ObcShipDying, ObcShipDead

// Game state singletons (definitions only, populated in Chunk 12)
ObcMatchConfig

// Physics data structures (definitions only, no simulation)
Transform, PhysicsBody, ShipFlightParams, ThrottleInput, WarpState, CloakState
ShieldState, HullState, SubsystemState, PowerState, CollisionShape, DamageEvent
```

**Pipeline phases**: `NetworkReceive -> Checksum -> Lobby -> GameplayRelay -> EventDispatch -> NetworkSend -> Cleanup`

**Acceptance**: `ecs_progress(world, dt)` runs at 30Hz. Components register. Observers fire on add/remove. Unit tests pass for component registration and pipeline ordering.

---

### Chunk 3: Handle Map & SWIG Type System
**Track**: A | **Duration**: ~3-4 days | **Dependencies**: Chunk 2

**Deliverables**:
- `src/compat/handle_map.h` / `handle_map.c` - Entity-handle bidirectional mapping
- `src/compat/swig_types.h` / `swig_types.c` - Handle type enum, inheritance table
- `src/compat/globals.h` / `globals.c` - Singleton entity creation and registration

**Handle format**: `_HEXID_p_TypeName` (e.g., `_00000001_p_TGNetwork`)

**Type hierarchy**:
- TGWinsockNetwork is-a TGNetwork
- MultiplayerGame is-a Game
- ShipClass is-a PhysicsObjectClass is-a ObjectClass is-a TGObject

**Singleton handles** (created at startup):
```c
#define OBC_ENTITY_UTOPIA_MODULE   ((ecs_entity_t)1)
#define OBC_ENTITY_EVENT_MANAGER   ((ecs_entity_t)2)
#define OBC_ENTITY_CONFIG_MAPPING  ((ecs_entity_t)3)
#define OBC_ENTITY_VAR_MANAGER     ((ecs_entity_t)4)
#define OBC_ENTITY_TOP_WINDOW      ((ecs_entity_t)5)
#define OBC_ENTITY_NETWORK         ((ecs_entity_t)6)
```

**Acceptance**: Create handle for entity, resolve it back, verify type checking works. Null handle detection works. Inheritance resolution works (ShipClass_Cast succeeds on ship handles, fails on non-ship handles). Unit tests pass.

---

### Chunk 4: Event System
**Track**: A | **Duration**: ~4-5 days | **Dependencies**: Chunk 3

**Deliverables**:
- `src/compat/event_system.h` / `event_system.c` - TGEventManager, TGEvent, handler dispatch
- `src/compat/event_constants.h` - All ET_* constants with correct numeric values

**Features**:
- Event queue (ring buffer)
- Handler registry (hash map of event_type -> handler chain)
- C handler dispatch (direct function call)
- Python handler dispatch (`PyObject_CallFunction`)
- Broadcast handlers and per-instance handlers
- `ProcessEvent` / `CallNextHandler` chain walking

**Event types to define** (with hex values):
```
ET_NETWORK_MESSAGE_EVENT = 0x60001
ET_NETWORK_CONNECT_EVENT = 0x60002
ET_NETWORK_NEW_PLAYER = 0x60004
ET_CHECKSUM_COMPLETE = 0x8000e8
ET_SYSTEM_CHECKSUM_FAILED = 0x8000e7
ET_KILL_GAME = 0x8000e9
ET_START = 0x800053
ET_CREATE_SERVER = 0x80004A
ET_OBJECT_CREATED_NOTIFY = (extract from Appc.pyd)
ET_OBJECT_EXPLODING = (extract from Appc.pyd)
ET_WEAPON_HIT = (extract from Appc.pyd)
ET_NEW_PLAYER_IN_GAME = 0x8000F1
ET_NETWORK_DELETE_PLAYER = (extract from Appc.pyd)
ET_RESTART_GAME = (extract from Appc.pyd)
// + remaining ET_* values extracted from Appc.pyd
```

**Acceptance**: Register C handler, post event, handler fires. Register Python handler, post event, Python function called. Chain walking works. All event constants defined.

---

### Chunk 5: UDP Transport Layer
**Track**: B | **Duration**: ~7-10 days | **Dependencies**: Chunk 1

This is the largest and highest-risk chunk.

**Deliverables**:
- `src/network/legacy/transport.h` / `transport.c` - TGWinsockNetwork equivalent
- `src/network/legacy/peer.h` / `peer.c` - Peer data structure and lifecycle
- `src/network/legacy/message.h` / `message.c` - Message types 0-5 serialization
- `src/network/legacy/reliable.h` / `reliable.c` - ACK, retry, sequence tracking
- `src/network/legacy/protocol_opcodes.h` - Opcode constants

**Implementation order**:
1. Socket creation + non-blocking UDP bind on port 22101
2. Packet framing: parse/build `[peerID][count][messages]`
3. Message type serialization for types 0, 1, 3, 4, 5
4. Peer array (sorted, binary search, creation/removal)
5. Connection handshake (accept connection from unknown peers, assign peer ID)
      - AlbyRules cipher: XOR encrypt/decrypt with "AlbyRules!" key (10 bytes)
6. Reliable delivery (sequence numbering, ACK generation, retry with timeout)
7. Three-tier send queues (priority, reliable, unreliable)
8. Timeout/disconnect detection

**Peer structure** (~0xC0 bytes, VERIFIED from STBC-Dedi runtime inspection):
```c
struct Peer {
    // Bytes 0x00-0x17: internal linked list / vtable fields
    int32_t  peerID;           // +0x18 (VERIFIED)
    uint32_t address;          // +0x1C (sockaddr_in, VERIFIED)
    uint16_t seqRecvUnrel;     // +0x24
    uint16_t seqSendReliable;  // +0x26
    uint16_t seqRecvReliable;  // +0x28
    uint16_t seqSendPriority;  // +0x2A
    float    lastRecvTime;     // +0x2C
    float    lastSendTime;     // +0x30
    // Bytes 0x34-0x63: internal state
    Queue    unreliable;       // +0x64..+0x7C (VERIFIED)
    uint32_t unreliableCount;  // +0x7C (VERIFIED)
    Queue    reliable;         // +0x80..+0x98 (VERIFIED)
    uint32_t reliableCount;    // +0x98 (VERIFIED)
    Queue    priority;         // +0x9C..+0xB4 (VERIFIED)
    uint32_t priorityCount;    // +0xB4 (VERIFIED)
    float    disconnectTime;   // +0xB8
    uint8_t  isDisconnecting;  // +0xBC (NOTE: UNRELIABLE flag, use peer-array detection instead)
};
```

See [phase1-verified-protocol.md](phase1-verified-protocol.md) Section 11 for complete data structures.

**Acceptance**: Raw UDP packet sent to port 22101 is received and parsed. Peer creation from connection message. ACK round-trip for reliable messages. Timeout detection after 45s of silence.

**Risks**: This is the single highest-risk chunk. The wire format must match the original exactly. Mitigation: Use Wireshark captures from vanilla BC sessions as ground truth for byte-level comparison.

---

### Chunk 6: GameSpy LAN Discovery
**Track**: B | **Duration**: ~2-3 days | **Dependencies**: Chunk 5 (shared socket)

**Deliverables**:
- `src/network/legacy/gamespy_qr.h` / `gamespy_qr.c` - Query/response handler

**Features**:
- Peek-based router: MSG_PEEK first byte, `\` -> GameSpy, binary -> game
- Parse `\basic\`, `\status\`, `\info\` queries
- Respond with backslash-delimited key-value pairs:
  ```
  \hostname\OpenBC Server\numplayers\2\maxplayers\16\mapname\DeepSpace9\gametype\Deathmatch\hostport\22101\
  ```
- Response fragmentation if > 0x545 bytes
- queryid appending

**Acceptance**: Vanilla BC client running on same LAN sees server in multiplayer browser with correct name, player count, and map name.

---

### Chunk 7: Checksum Exchange
**Track**: B | **Duration**: ~4-5 days | **Dependencies**: Chunks 4, 5

**Deliverables**:
- `src/network/legacy/checksum.h` / `checksum.c` - Checksum exchange protocol
- `src/network/legacy/hash_tables.h` - 4x256 lookup tables extracted from stbc.exe
- `src/network/legacy/hash.h` / `hash.c` - Hash function implementation

**Features**:
- 4-round sequential checksum exchange (opcodes 0x20-0x28)
- Hash function matching FUN_007202e0 exactly
- Reference string hash verification for index 0
- Checksum pass -> ET_CHECKSUM_COMPLETE
- Checksum fail -> ET_SYSTEM_CHECKSUM_FAILED + opcodes 0x22/0x23
- `skip_checksums` config flag
- Settings delivery after pass (opcode 0x00 + 0x01)

**Acceptance**: Vanilla BC client connects, receives 4 checksum requests sequentially, responds to each, server verifies hashes match, sends settings, client reaches ship selection screen.

---

### Chunk 8: Python Embedding & Compatibility Shim
**Track**: C | **Duration**: ~5-7 days | **Dependencies**: Chunk 1

**Deliverables**:
- `src/scripting/python_host.h` / `python_host.c` - Python 3.x embedding
- `src/scripting/compat_shim.py` - Runtime compatibility patches
- `src/scripting/source_transform.c` - Import hook with AST transformation
- `src/scripting/bc_import_hook.py` - Custom import finder/loader

**Python embedding setup**:
```c
PyImport_AppendInittab("App", PyInit_App);
PyImport_AppendInittab("Appc", PyInit_Appc);
Py_Initialize();
// Install compat shim
// Add script_path to sys.path
```

**Compatibility shim** (`compat_shim.py`):
- Restore `dict.has_key`, `apply()`, `cmp()`, `execfile()`, `reload()`
- Alias `cPickle` -> `pickle`, provide `new`, `strop`, `imp` modules
- Patch `string` module for 1.5.2 compatibility
- Handle `sys.setcheckinterval()` -> `sys.setswitchinterval()`
- Patch `list.sort()` for old-style comparison functions

**Import hook** (`bc_import_hook.py`):
- Intercept imports of game scripts
- Apply source transformations before compilation
- Cache compiled bytecode
- Handle BC's script directory structure

**Acceptance**: `import App` succeeds. A simple BC script using `apply()`, `dict.has_key()`, and `print "text"` executes without errors. Import hook resolves scripts from BC game data directory.

---

### Chunk 9: App/Appc C Extension Module
**Track**: C | **Duration**: ~10-14 days | **Dependencies**: Chunks 3, 4, 8

This is the largest chunk by function count (~595 functions + ~345 constants).

**Deliverables**:
- `src/compat/app_module.h` / `app_module.c` - Python "App" module
- `src/compat/appc_module.h` / `appc_module.c` - Python "Appc" module (low-level)
- `src/compat/network_api.c` - TGNetwork/TGWinsockNetwork functions
- `src/compat/event_api.c` - TGEvent/TGEventManager functions
- `src/compat/config_api.c` - TGConfigMapping/VarManager functions
- `src/compat/multiplayer_api.c` - MultiplayerGame/Game functions
- `src/compat/message_api.c` - TGMessage/TGBufferStream functions
- `src/compat/util_api.c` - TGString/TGObject/IsNull/utility functions
- `src/compat/timer_api.c` - TGTimer/TGTimerManager functions
- `src/compat/player_list_api.c` - TGPlayerList/TGNetPlayer functions

**Implementation priority tiers**:

**Tier A - Bootstrap** (must work first):
1. All constants (ET_*, CT_*, SPECIES_*, MWT_*, MAX_MESSAGE_TYPES, etc.)
2. TGString (new, delete, methods)
3. TGObject (GetObjType, IsTypeOf, GetObjID)
4. TGEvent + typed events (Create, Get/Set methods)
5. TGEventHandlerObject (AddPythonFuncHandlerForInstance, ProcessEvent, CallNextHandler)
6. TGEventManager (AddBroadcastPythonFuncHandler, RemoveBroadcastHandler)
7. Globals (g_kConfigMapping, g_kUtopiaModule, g_kVarManager, g_kEventManager, etc.)

**Tier B - Network**:
8. TGWinsockNetwork (new, SetPortNumber)
9. TGNetwork (Connect, Update, SendTGMessage, SendTGMessageToGroup, GetNextMessage, IsHost, GetHostID, GetLocalID, etc.)
10. TGMessage + subtypes (Create, Copy, Serialize, SetGuaranteed, SetDataFromStream, etc.)
11. TGBufferStream (OpenBuffer, Read/Write*, Close -- ReadChar, WriteChar, ReadInt, WriteInt, ReadLong, WriteLong, ReadShort, WriteShort, Read, Write)
12. TGPlayerList, TGNetPlayer (GetNumPlayers, GetPlayerAtIndex, GetPlayer, GetNetID, GetName)
13. UtopiaModule (InitializeNetwork, GetNetwork, IsHost, IsClient, IsMultiplayer, GetGameTime, GetRealTime, SetFriendlyFireWarningPoints)

**Tier C - Game Flow**:
14. TGConfigMapping (Get/Set values, LoadConfigFile)
15. VarManagerClass (Get/Set variables, MakeEpisodeEventType)
16. Game/MultiplayerGame (GetCurrentGame, Cast, Create, LoadEpisode, GetShipFromPlayerID, DeletePlayerShipsAndTorps, SetReadyForNewPlayers, IsPlayerInGame, GetPlayerName, GetPlayerNumberFromID, GetMaxPlayers)
17. Episode/Mission (GetCurrentMission, LoadMission, GetScript, GetEnemyGroup, GetFriendlyGroup)
18. TGTimer/TGTimerManager
19. SetManager/SetClass (GetSet, ClearRenderedSet, DeleteAllSets, AddObjectToSet)
20. ObjectGroup/NameGroup (AddName, RemoveName, IsNameInGroup, RemoveAllNames)

**Tier D - Stubs**:
21. TGLocalizationManager (Load, Unload, GetString -- return empty strings)
22. TGSystemWrapperClass
23. IsNull, Breakpoint, TGScriptAction_Create
24. CPyDebug (Print method)

**Each function follows this pattern**:
```c
static PyObject* wrap_TGEvent_SetEventType(PyObject* self, PyObject* args) {
    const char* handle_str;
    int event_type;
    if (!PyArg_ParseTuple(args, "si", &handle_str, &event_type)) return NULL;

    ecs_entity_t e = obc_handle_resolve(g_handle_map, handle_str, OBC_HANDLE_TG_EVENT);
    if (e == 0) { PyErr_SetString(PyExc_RuntimeError, "Invalid handle"); return NULL; }

    OBC_TGEvent* evt = ecs_get_mut(g_world, e, OBC_TGEvent);
    evt->type = (uint32_t)event_type;

    Py_RETURN_NONE;
}
```

**Acceptance**: Python script can create a TGEvent, set its type, register a handler, post the event, and the handler fires. TGMessage can be created, written to, and read from. TGMessage.Copy() works for relay. SendTGMessageToGroup("NoMe", msg) delivers to group members. UtopiaModule singleton is accessible. All constants are defined with correct values.

---

### Chunk 10: Server Main & Integration
**Track**: All tracks merge | **Duration**: ~5-7 days | **Dependencies**: Chunks 2-9, 11-13

**Deliverables**:
- `src/server/main_server.c` - Server entry point
- `src/server/server_console.c` - CLI admin interface (optional)
- `src/server/server_config.c` - Server-specific configuration
- `src/engine/config.h` / `config.c` - TGConfigMapping implementation
- `src/engine/timer.h` / `timer.c` - TGTimerManager implementation

**Initialization sequence** (12 steps):
```
1. Parse command-line arguments
2. Initialize platform (timers/clock only)
3. Create flecs world + register all components/systems
4. Initialize Python embedding + install compat shim
5. Create singleton entities + handles (g_kUtopiaModule, etc.)
6. Load configuration (stbc.cfg)
7. Set multiplayer/host flags (IsHost=1, IsClient=0, IsMultiplayer=1)
8. Initialize network (create TGWinsockNetwork, bind socket, HOST state)
9. Create MultiplayerGame + register 28 event handlers
10. Create "NoMe" peer group, initialize game lifecycle FSM
11. Load mission script (e.g. Multiplayer.Episode.Mission1.Mission1)
12. Enter main loop (30Hz tick)
```

**Acceptance**: Full end-to-end test with vanilla BC clients (see Section 10: Definition of Done).

---

### Chunk 11: Ship Object Model & Game Object API
**Track**: A | **Duration**: ~5-7 days | **Dependencies**: Chunks 3, 4, 9

**Deliverables**:
- `src/engine/ship_components.h` - Ship ECS component population and lifecycle functions
- `src/compat/ship_api.c` - ShipClass_*, PhysicsObjectClass_*, BaseObjectClass_* SWIG functions
- `src/compat/gameobject_api.c` - ObjectClass_*, SetClass_*, SetManager_* functions
- `src/compat/property_api.c` - 16 Property_Create functions + generic Set*/Get* handler
- `src/compat/math_types.c` - TGPoint3, TGColorA math type wrappers

**Ship entity components** (populated from network messages):
```c
typedef struct {
    uint32_t obj_id;        // GetObjID() -- unique game object ID (monotonic counter)
    uint16_t net_type;      // GetNetType() -- species index (SPECIES_GALAXY, etc.)
    uint16_t class_type;    // Derived from net_type via GetClassFromSpecies()
    char     name[64];      // GetName() -- ship name (e.g. "USS Enterprise")
} ObcShipIdentity;

typedef struct {
    uint32_t net_player_id; // GetNetPlayerID() -- network player ID (0 = AI/no player)
    bool     is_player_ship;// IsPlayerShip() -- true if controlled by a human player
} ObcShipOwnership;

// Tags (zero-size): ObcShipAlive, ObcShipDying, ObcShipDead
```

**Ship lifecycle**:
- **Create**: On ObjectCreateTeam opcode (**0x03**) from client. Allocate flecs entity, set ObcShipIdentity + ObcShipOwnership + ObcShipAlive tag, register SWIG handle, fire ET_OBJECT_CREATED_NOTIFY.
- **Death**: On DestroyObject opcode (**0x14**). Tag transition: remove ObcShipAlive, add ObcShipDying, then remove ObcShipDying, add ObcShipDead. Fire ET_OBJECT_EXPLODING.
- **Respawn**: Delete old entity + create new entity with new ObjID (clean slate -- scripts may hold references to old ObjID in damage dictionaries).
- **Game restart**: `DeletePlayerShipsAndTorps()` deletes all ship entities, clears player-ship index.

**GetShipFromPlayerID**: Direct array lookup in `ObcGameSession.player_ships[OBC_MAX_PLAYERS]` indexed by player slot. With max 16 players, linear scan is cache-friendly and fast enough for a hot path called every damage event.

**Game Object ID system**: Monotonic counter stored in `ObcGameSession.next_obj_id`, starts at 1, never recycled within a session. Scripts use ObjIDs as dictionary keys that persist across a ship's destruction.

**Property system** (16 `*Property_Create` functions):
```c
// Generic property base that accepts any Set*/Get* as a no-op
// Scripts write properties during ship creation (hardpoint file execution)
// but the relay server does not use them for damage calculation.
// PhaserProperty_Create, ShieldProperty_Create, HullProperty_Create,
// ImpulseEngineProperty_Create, WarpEngineProperty_Create,
// SensorProperty_Create, PowerProperty_Create, RepairProperty_Create,
// ClockProperty_Create, TorpedoSystemProperty_Create, etc.
// Each returns a valid handle; Set*(value) calls are no-ops.
```

**Math types**: TGPoint3 (x,y,z float constructor + SetXYZ/GetX/GetY/GetZ) and TGColorA (r,g,b,a float) -- lightweight wrappers for hardpoint scripts that set ship positions/colors.

**Acceptance**: Python script can create a ship entity, query its properties (GetNetPlayerID, IsPlayerShip, GetObjID, GetNetType, GetName, IsDying, IsDead), cast it (ShipClass_Cast), and look it up via MultiplayerGame_GetShipFromPlayerID. All 16 Property_Create functions return valid handles. Hardpoint file execution (SpeciesToShip.CreateShip) completes without errors.

---

### Chunk 12: Message Relay & Game Lifecycle FSM
**Track**: B | **Duration**: ~5-7 days | **Dependencies**: Chunks 5, 9, 11

**Deliverables**:
- `src/network/relay.h` / `relay.c` - Clone-and-forward relay engine
- `src/network/peer_groups.h` / `peer_groups.c` - Named peer groups ("NoMe", team groups)
- `src/engine/game_state.h` / `game_state.c` - Game lifecycle state machine
- `src/compat/mission_api.c` - Episode/Mission/NameGroup SWIG functions

**Message relay -- the core gameplay mechanism**:

The relay clones RAW message bytes and forwards -- no deserialization is needed for most opcodes. This makes the relay implementation straightforward.

```c
// In ReceiveMessageHandler, for each incoming data message:
// 1. Read byte[1] (sender player slot index)
// 2. Iterate all 16 player slots
// 3. For each active slot that is NOT the sender and NOT self:
//    -> Clone the raw message (msg->Clone() via vtable+0x18)
//    -> Send via TGNetwork::Send(wsn, slot.peerID, clone, 0)
```

This matches the decompiled relay loop in FUN_0069f620 exactly:
```c
for each of 16 player slots:
    if (slot.active && slot.peerID != msg.senderPeerID && slot.peerID != wsn.localPeerID):
        clone = msg->Clone()
        TGNetwork::Send(wsn, slot.peerID, clone, 0)
```

**Verified game opcodes** (0x00-0x2A with gaps, MAX_MESSAGE_TYPES = **0x2B**):

| Opcode | Name | Server Action |
|--------|------|---------------|
| 0x00 | Settings | Server generates (post-checksum) |
| 0x01 | GameInit | Server generates (post-checksum) |
| 0x02 | ObjectCreate | Relay + optionally create ship entity |
| 0x03 | ObjectCreateTeam | Relay + create ship entity |
| 0x04 | BootPlayer | Server generates (reject/kick) |
| 0x06 | PythonEvent | Pure relay (Python event forward) |
| 0x07-0x0C | Event forwards | Pure relay (firing, subsystem, cloak, etc.) |
| 0x0D | PythonEvent2 | Pure relay (alternate Python path) |
| 0x0E-0x12 | Event forwards | Pure relay (cloak, warp, repair, phaser) |
| 0x13 | HostMsg | Relay (host-specific dispatch) |
| 0x14 | DestroyObject | Relay + update ship death state |
| 0x15 | CollisionEffect | Pure relay (C->S collision damage) |
| 0x16-0x18 | UI / Player management | Pure relay |
| 0x19 | TorpedoFire | Pure relay |
| 0x1A | BeamFire | Pure relay |
| 0x1B | TorpTypeChange | Pure relay |
| 0x1C | StateUpdate | Pure relay (unreliable, position/state) |
| 0x1D-0x1F | Object queries / EnterSet | Pure relay |
| 0x29 | Explosion | Relay (S->C only) |
| 0x2A | NewPlayerInGame | Server generates (player join) |

**Note**: Opcodes 0x05, 0x20-0x27 are NOT in the game dispatcher. 0x20-0x27 go to the NetFile/Checksum dispatcher. See [phase1-verified-protocol.md](phase1-verified-protocol.md) for complete table with handler addresses.

**Python script message opcodes** (>= MAX_MESSAGE_TYPES = **0x2B**):

| Opcode | Offset | Name | Relay Pattern |
|--------|--------|------|---------------|
| 0x2C | MAX+1 | CHAT_MESSAGE | Client->Host, Host->Group("NoMe") |
| 0x2D | MAX+2 | TEAM_CHAT_MESSAGE | Client->Host, Host->teammates only |
| 0x35 | MAX+10 | MISSION_INIT_MESSAGE | Host->specific client (on join) |
| 0x36 | MAX+11 | SCORE_CHANGE_MESSAGE | Deferred to Phase 2 |
| 0x37 | MAX+12 | SCORE_MESSAGE | Deferred to Phase 2 |
| 0x38 | MAX+13 | END_GAME_MESSAGE | Host->all (broadcast via SendTGMessage(0, msg)) |
| 0x39 | MAX+14 | RESTART_GAME_MESSAGE | Host->all (broadcast) |
| 0x3F | MAX+20 | SCORE_INIT_MESSAGE | Team modes (Mission2/3) |
| 0x40 | MAX+21 | TEAM_SCORE_MESSAGE | Team modes (Mission2/3) |
| 0x41 | MAX+22 | TEAM_MESSAGE | Team modes (Mission2/3) |

**Peer groups**:
- "NoMe" group: All peers except the host/server. Automatically maintained -- add on connect, remove on disconnect. Used by `SendTGMessageToGroup("NoMe", msg)` for chat forwarding and score broadcasts.
- Team groups: Created by mission scripts via `pMission.GetFriendlyGroup()` / `GetEnemyGroup()`. These are NameGroup objects with AddName/RemoveName/IsNameInGroup/RemoveAllNames methods -- simple string-set containers.

**Chat relay** (Python-level, NOT native relay):
```python
# From MultiplayerMenus.py -- chat is sent point-to-point to host
# CHAT_MESSAGE = 0x2C (MAX_MESSAGE_TYPES + 1, where MAX = 0x2B)
if (cType == CHAT_MESSAGE):
    if (App.g_kUtopiaModule.IsHost()):
        pNewMessage = pMessage.Copy()
        pNetwork.SendTGMessageToGroup("NoMe", pNewMessage)
```
Chat messages (opcode 0x2C) are sent TO THE HOST ONLY (not broadcast), so the native C++ relay does NOT forward them. The Python scripts on the host explicitly copy and forward. Team chat (0x2D) follows the same pattern but filters by team membership.

Chat wire format: `[0x2C][senderSlot:1][padding:3 x 0x00][msgLen:2 LE][ASCII text]`

**Game lifecycle state machine**:
```c
typedef enum {
    OBC_STATE_LOBBY = 0,       // Waiting for players, checksum exchange
    OBC_STATE_SHIP_SELECT,     // Players choosing ships (mission loaded)
    OBC_STATE_PLAYING,         // Active gameplay, full message relay
    OBC_STATE_GAME_OVER,       // Match ended, scores displayed
    OBC_STATE_RESTARTING,      // Cleanup, transition back to ship select
} obc_game_state_t;
```

| State | Message Relay | Accept Players | Ship Creation |
|-------|---------------|---------------|---------------|
| LOBBY | No | Yes (checksum) | No |
| SHIP_SELECT | Chat only | Yes | Yes (per player) |
| PLAYING | Full relay | Yes (late join) | Yes (respawn) |
| GAME_OVER | Chat only | No | No |
| RESTARTING | No | No | No (clearing) |

**State transitions**:

| Transition | Trigger |
|------------|---------|
| LOBBY -> SHIP_SELECT | Mission script loaded, `SetReadyForNewPlayers(1)` |
| SHIP_SELECT -> PLAYING | Host sends MISSION_INIT_MESSAGE to all clients |
| PLAYING -> GAME_OVER | Host sends END_GAME_MESSAGE (time up, frag limit, etc.) |
| GAME_OVER -> RESTARTING | Host sends RESTART_GAME_MESSAGE |
| RESTARTING -> SHIP_SELECT | Cleanup complete, ships cleared, `SetReadyForNewPlayers(1)` |

**Late join support**: When a player joins mid-game, `InitNetwork(iToID)` sends current mission config to the joining player via MISSION_INIT_MESSAGE. The Python mission script handles this -- the server provides the infrastructure (TGMessage, SendTGMessage, game state queries).

**Acceptance**: Two vanilla BC clients can:
1. Connect to server and pass checksums
2. Select ships
3. Enter gameplay with full message relay
4. Chat messages relay correctly (regular + team)
5. Game ends when time/frag limit reached
6. Restart returns all clients to ship select
7. Late-joining client receives MISSION_INIT and can play

---

### Chunk 13: UI Stubs & Full Gameplay Integration
**Track**: C | **Duration**: ~3-5 days | **Dependencies**: Chunks 9, 11, 12

**Deliverables**:
- `src/compat/ui_stubs.c` - ~80+ UI stub functions returning dummy callable objects
- `src/compat/sequence_stubs.c` - TGSequence, TGSoundAction, SubtitleAction stubs
- `src/compat/extended_constants.h` - Gameplay-related constants

**UI stubs -- critical design requirement**: Headless mode stubs MUST return dummy callable objects (NOT None) so that method calls like `pButton.SetEnabled(0)` do not crash with `AttributeError`. The `RestartGame()` function and many other code paths access UI widget methods.

**Stub categories**:
- **TopWindow stubs**: `TopWindow_GetTopWindow`, `SetupMultiplayerGame`, UI menu methods. Return a dummy object whose attribute access returns another dummy (recursive proxy).
- **MultiplayerWindow stubs**: Ship select list, player list, chat window. Return dummy callable objects.
- **SortedRegionMenu stubs**: Menu construction, item addition, selection handling.
- **STButton, TGPane, TGParagraph, TGIcon stubs**: `SetEnabled`, `SetVisible`, `SetColor`, `SetText` -- all no-ops.
- **DynamicMusic stubs**: `LoadBridge`, `PlayFanfare`, `SetMode` -- all no-ops.
- **Sound action stubs**: `LoadDatabaseSoundInGroup`, `TGSoundAction_Create` -- return valid handles, playback is no-op.
- **Subtitle/Sequence stubs**: `SubtitleAction_Create`, `TGSequence_Create`, `TGSequence_AddAction`, `TGSequence_Play` -- return valid handles, actions are no-ops.

**Implementation approach**: A Python `DummyObject` class that returns itself on any attribute access and is callable (returns itself). This handles arbitrary chains like `pWindow.GetMenu().GetItem(0).SetEnabled(0)`:
```python
class DummyObject:
    def __getattr__(self, name): return self
    def __call__(self, *args, **kwargs): return self
    def __int__(self): return 0
    def __str__(self): return ""
    def __bool__(self): return False
```

**Full end-to-end gameplay integration testing**:
- All vanilla multiplayer mission scripts (Mission1 FFA, Mission2 Team DM, Mission3 Team Objectives) must Initialize() without Python errors
- Mission script event handlers register correctly
- Game lifecycle (start -> play -> end -> restart) completes
- All 50+ vanilla ship hardpoint scripts execute successfully via Property API

**Acceptance**: All 3 playable vanilla multiplayer mission scripts load, register handlers, and manage game lifecycle without Python errors on the headless server. Hardpoint files execute. UI-referencing code paths do not crash.

---

## 3. Critical Path

```
Chunk 1 (Build System)
    |
    +---> Chunk 2 (ECS World) ---> Chunk 3 (Handle Map) ---> Chunk 4 (Events)
    |                                                              |
    +---> Chunk 5 (UDP Transport) ---> Chunk 6 (GameSpy)          |
    |          |                                                   |
    |          +---> Chunk 7 (Checksums) <-------------------------+
    |
    +---> Chunk 8 (Python Embedding)
              |
              +---> Chunk 9 (App/Appc Module) <--- Chunks 3, 4
                         |
                         +---> Chunk 11 (Ship Objects) <--- Chunks 3, 4
                         |          |
                         +---> Chunk 12 (Relay + FSM) <--- Chunks 5, 11
                         |          |
                         +---> Chunk 13 (UI Stubs) <--- Chunks 11, 12
                                    |
                                    v
                              Chunk 10 (Integration) <--- All chunks
```

**Critical path**: Chunk 1 -> Chunk 8 -> Chunk 9 -> Chunk 11 -> Chunk 12 -> Chunk 13 -> Chunk 10
**Network critical path**: Chunk 1 -> Chunk 5 -> Chunk 7 -> Chunk 12 -> Chunk 10

---

## 4. Parallel Tracks

These chunks have no mutual dependencies and can be developed simultaneously:

| Track A (Infrastructure) | Track B (Network) | Track C (Scripting) |
|--------------------------|-------------------|---------------------|
| Chunk 2: ECS World | Chunk 5: UDP Transport | Chunk 8: Python Embedding |
| Chunk 3: Handle Map | Chunk 6: GameSpy | |
| Chunk 4: Event System | | |

After the parallel phase:
- Chunks 7 (Checksums) depends on Chunks 4, 5
- Chunk 9 (API) depends on Chunks 3, 4, 8
- Chunk 11 (Ship Objects) depends on Chunks 3, 4, 9
- Chunk 12 (Relay + FSM) depends on Chunks 5, 9, 11
- Chunk 13 (UI Stubs) depends on Chunks 9, 11, 12
- Chunk 10 (Integration) depends on all chunks

### Chunk Duration Summary

| Chunk | Description | Duration | Phase |
|-------|-------------|----------|-------|
| 1 | Build System | 3-4 days | Parallel |
| 2 | ECS World | 3-4 days | Parallel |
| 3 | Handle Map | 3-4 days | Sequential |
| 4 | Event System | 4-5 days | Sequential |
| 5 | UDP Transport | 7-10 days | Parallel |
| 6 | GameSpy | 2-3 days | Sequential |
| 7 | Checksums | 4-5 days | Sequential |
| 8 | Python Embedding | 5-7 days | Parallel |
| 9 | App/Appc Module | 10-14 days | Sequential |
| 10 | Integration | 5-7 days | Final |
| **11** | **Ship Object Model** | **5-7 days** | **Sequential** |
| **12** | **Relay + FSM** | **5-7 days** | **Sequential** |
| **13** | **UI Stubs + Integration** | **3-5 days** | **Parallel with 12** |

**Total estimated effort**: 62-87 developer-days
**Calendar time with parallelism**: 12-14 weeks with one developer

---

## 5. File Manifest

### Source Files (New)

```
src/
├── engine/
│   ├── CMakeLists.txt
│   ├── engine.h / engine.c          # Core engine struct, lifecycle
│   ├── game_loop.c                  # 30Hz fixed timestep
│   ├── clock.h / clock.c            # Monotonic time, game time
│   ├── components.h                 # All ECS component definitions
│   ├── modules.c                    # Register flecs modules
│   ├── config.h / config.c          # TGConfigMapping (INI parser)
│   ├── timer.h / timer.c            # TGTimerManager
│   ├── ship_components.h            # Ship ECS component population + lifecycle
│   └── game_state.h / game_state.c  # Game lifecycle FSM (LOBBY->PLAYING->etc.)
├── compat/
│   ├── CMakeLists.txt
│   ├── handle_map.h / handle_map.c  # Entity<->handle bidirectional map
│   ├── swig_types.h / swig_types.c  # Type enum, inheritance
│   ├── globals.h / globals.c        # Singleton entities
│   ├── event_system.h / event_system.c  # TGEventManager/TGEvent
│   ├── event_constants.h            # All ET_* constants
│   ├── app_module.h / app_module.c  # Python "App" module init
│   ├── appc_module.h / appc_module.c  # Python "Appc" module init
│   ├── network_api.c               # TGNetwork SWIG functions
│   ├── event_api.c                  # TGEvent SWIG functions
│   ├── config_api.c                 # TGConfigMapping SWIG functions
│   ├── multiplayer_api.c           # MultiplayerGame SWIG functions
│   ├── message_api.c               # TGMessage/TGBufferStream SWIG functions
│   ├── timer_api.c                  # TGTimer SWIG functions
│   ├── util_api.c                   # TGString/TGObject/IsNull SWIG functions
│   ├── player_list_api.c           # TGPlayerList/TGNetPlayer SWIG functions
│   ├── ship_api.c                   # ShipClass/PhysicsObjectClass/BaseObjectClass
│   ├── gameobject_api.c            # ObjectClass/SetClass/SetManager functions
│   ├── property_api.c              # 16 Property_Create + generic Set*/Get*
│   ├── math_types.c                # TGPoint3/TGColorA wrappers
│   ├── mission_api.c               # Episode/Mission/NameGroup functions
│   ├── localization_api.c          # TGLocalizationManager stubs
│   ├── combat_event_api.c          # WeaponHitEvent/ObjectExplodingEvent stubs
│   ├── ui_stubs.c                   # ~80+ UI function stubs (DummyObject pattern)
│   ├── sequence_stubs.c            # TGSequence/TGSoundAction/SubtitleAction stubs
│   └── extended_constants.h        # Gameplay-related constants
├── network/
│   ├── CMakeLists.txt
│   ├── relay.h / relay.c            # Message relay engine (clone-and-forward)
│   ├── peer_groups.h / peer_groups.c  # Named peer groups ("NoMe", teams)
│   └── legacy/
│       ├── transport.h / transport.c  # TGWinsockNetwork equivalent
│       ├── peer.h / peer.c            # Peer data structure
│       ├── message.h / message.c      # Message types serialization
│       ├── reliable.h / reliable.c    # ACK, retry, sequence tracking
│       ├── gamespy_qr.h / gamespy_qr.c  # LAN discovery
│       ├── checksum.h / checksum.c    # Checksum exchange protocol
│       ├── hash.h / hash.c            # Hash function implementation
│       ├── hash_tables.h             # 4x256 lookup tables
│       └── protocol_opcodes.h        # Opcode constants (44 native + script-level)
├── scripting/
│   ├── CMakeLists.txt
│   ├── python_host.h / python_host.c  # Python 3.x embedding
│   ├── source_transform.c            # AST transformations
│   ├── compat_shim.py                # Runtime compatibility patches
│   └── bc_import_hook.py             # Custom import finder/loader
└── server/
    ├── main_server.c                  # Server entry point
    ├── server_console.c               # CLI admin interface
    └── server_config.c                # Server-specific config
```

### Build/CI Files (New)

```
CMakeLists.txt                        # Root CMake
CMakePresets.json                     # Build presets
cmake/
├── OpenBCVersion.cmake
├── CompilerWarnings.cmake
└── FetchDependencies.cmake
include/openbc/
├── config.h.in                       # Generated config
└── types.h                           # Platform abstractions
vendor/flecs/
├── flecs.h                           # v4.1.4
└── flecs.c
tests/
├── CMakeLists.txt
├── test_handle_map.c
├── test_event_system.c
├── test_packet_format.c
├── test_hash_function.c
├── test_peer_array.c
├── test_relay.c                      # Message relay clone-and-forward
├── test_peer_groups.c                # NoMe group management
├── test_ship_lifecycle.c             # Ship create/death/respawn/restart
├── test_game_state.c                 # Game lifecycle FSM transitions
├── test_chat_relay.c                 # Chat message forwarding
└── test_property_stubs.c             # Property_Create + Set* no-ops
.github/workflows/
├── build.yml                         # CI matrix builds
└── release.yml                       # Release packaging
docker/
├── Dockerfile.server                 # Multi-stage build
└── .dockerignore
```

---

## 6. Testing Strategy

### Unit Tests (per chunk)
| Chunk | Tests |
|-------|-------|
| 2 (ECS) | Component registration, pipeline ordering, singleton access |
| 3 (Handles) | Create/resolve/invalidate handles, type checking, null detection, ShipClass inheritance chain |
| 4 (Events) | Handler registration, event dispatch, chain walking, Python handlers |
| 5 (Transport) | Packet serialization round-trip, peer array insert/search, message types |
| 7 (Checksum) | Hash function against known values from stbc.exe |
| 8 (Python) | Compat shim for apply/has_key/print/except, import hook resolution |
| 9 (API) | Each SWIG function with mock ECS state, TGMessage.Copy() fidelity |
| 11 (Ships) | Ship create with ObcShipIdentity, GetShipFromPlayerID lookup, death tag transitions, DeletePlayerShipsAndTorps, Property_Create + Set* no-ops |
| 12 (Relay) | Clone-and-forward for all 44 native opcodes, NoMe group add/remove, chat relay path, game state FSM transitions, late join InitNetwork |
| 13 (UI Stubs) | DummyObject attribute chain access, all 80+ stub functions callable, mission script Initialize() without errors |

### Integration Tests
| Test | Description |
|------|-------------|
| Packet replay | Replay Wireshark-captured vanilla BC packets against our server; compare responses byte-for-byte |
| Full connection | Vanilla BC client connects through to ship selection screen |
| Multi-client | 2+ BC clients connected simultaneously with separate slots |
| Disconnect/reconnect | Client kills connection; server detects and frees slot; client reconnects |
| Reject when full | 17th client receives correct rejection |
| **Ship selection** | Client selects ship; ObjectCreateTeam (0x03) relayed to all other clients; ship entity created on server |
| **Gameplay relay** | 2+ clients in active match; StateUpdate (position/orientation), weapons (StartFiring 0x07, TorpedoFire 0x19, BeamFire 0x1A), all opcodes relayed correctly |
| **Chat relay** | Client sends CHAT_MESSAGE to host; host forwards to NoMe group; message appears on all other clients |
| **Team chat** | Client sends TEAM_CHAT_MESSAGE; host forwards only to teammates |
| **Game end** | Time/frag limit triggers END_GAME_MESSAGE; all clients receive it; game state transitions to GAME_OVER |
| **Game restart** | Host sends RESTART_GAME_MESSAGE; all clients return to ship select; ship entities cleared |
| **Late join** | Client connects mid-game; receives MISSION_INIT_MESSAGE via InitNetwork; can play immediately |
| **Ship death + respawn** | Ship destroyed via DestroyObject (0x14); death tags set; player respawns with new ship; new ObjID assigned |
| **Mission script lifecycle** | Mission1 FFA, Mission2 Team DM, Mission3 Team Objectives all load and register handlers |

### Regression Tests
| Test | Description |
|------|-------------|
| ACK round-trip | Reliable message ACKed correctly (guards against priority queue stall) |
| Checksum timeout | Client that never responds to checksum eventually times out |
| Keepalive | Client idle for 60s remains connected |
| Script execution | BC multiplayer scripts import and register handlers without error |
| **Relay fidelity** | Raw bytes received by Client B match raw bytes sent by Client A (no mutation in relay) |
| **NoMe group consistency** | NoMe group membership matches connected peer set after connect/disconnect sequences |
| **Game state invariants** | FSM never skips states; PLAYING state always preceded by SHIP_SELECT |
| **ObjID monotonicity** | Object IDs never decrease; no duplicates within a session |
| **Hardpoint execution** | All 50+ vanilla ship hardpoint scripts execute via Property API without errors |

---

## 7. Key Integration Points

1. **Network -> Events**: TGNetwork posts `ET_NETWORK_MESSAGE_EVENT` (0x60001) into EventManager
2. **Events -> Relay**: ReceiveMessageHandler reads opcode, clones raw message, forwards to all other active player slots
3. **Events -> Ship Model**: ObjectCreateTeam (0x03) creates ship entity in ECS; DestroyObject (0x14) sets death tags
4. **Events -> Python Handlers**: EventManager dispatches to Python handlers via chain (scoring, chat, game lifecycle)
5. **Handlers -> ECS**: Handlers modify flecs components via handle resolution (GetShipFromPlayerID, SetReadyForNewPlayers)
6. **Scripts -> API -> ECS**: Python calls `App.Func(handle)` -> resolve handle -> query/mutate component
7. **Scripts -> Network**: Python calls `SendTGMessage(id, msg)` or `SendTGMessageToGroup("NoMe", msg)` for chat/score/game-end forwarding
8. **ECS -> Network**: Game state changes trigger outbound network messages via send queues
9. **FSM -> Scripts**: Game lifecycle state transitions trigger Python mission script Initialize/Terminate calls

---

## 8. Reverse Engineering Work Items (ALL SOLVED)

All RE tasks have been completed by the STBC-Dedicated-Server project. No blocking RE work remains.

| ID | Task | Blocks | Status | Solution |
|----|------|--------|--------|----------|
| WI-4 | Message type dispatch table | WI-1, WI-2 | **SOLVED** | Jump table at 0x0069F534 verified |
| WI-9 | Peer structure layout | WI-1, WI-2, WI-3 | **SOLVED** | Full layout with VERIFIED offsets |
| WI-2 | Packet wire format | WI-1 | **SOLVED** | AlbyRules cipher + complete wire format |
| WI-3 | Reliable ACK format and sequence | WI-1 | **SOLVED** | Three-tier queues, ACK format, retry logic |
| WI-1 | Connection handshake protocol | Chunk 5 | **SOLVED** | Complete flow documented in multiplayer-flow.md |
| WI-14 | Hash algorithm tables | Chunk 7 | **SOLVED** | 4x256 tables at 0x0095c888 extracted |
| WI-6 | GameSpy query response fields | Chunk 6 | **PARTIAL** | Basic structure working, specific keys TBD |
| WI-7 | Player slot full structure | Chunk 10 | **SOLVED** | 0x18 bytes: active, peerID, objectID + extra fields |
| WI-16 | Game object serialization format | Chunk 11 | **SOLVED** | ObjectCreateTeam (0x03) with vtable+0x10C serialization |
| WI-17 | Native opcode verification | Chunk 12 | **SOLVED** | 34 opcodes with gaps (NOT 44 sequential), MAX=0x2B |

### Key Findings from RE Resolution

**Opcode table correction**: The original enumeration assigned sequential values 0x00-0x2B based on SWIG constant names. The actual wire bytes come from the jump table at `0x0069F534` and have gaps:
- Ship creation uses opcode **0x03** (ObjectCreateTeam), NOT 0x05
- Ship destruction uses opcode **0x14** (DestroyObject), NOT 0x06
- MAX_MESSAGE_TYPES = **0x2B** (43), NOT 0x2C (44)
- All Python message opcodes shift by -1 (CHAT = 0x2C not 0x2D, etc.)

**Packet encryption**: All game packets use the "AlbyRules!" XOR stream cipher. This was not previously documented.

See [phase1-verified-protocol.md](phase1-verified-protocol.md) for complete protocol reference and [phase1-re-gaps.md](phase1-re-gaps.md) for full gap analysis update.

---

## 9. Risk Register with Mitigations

| Risk | Probability | Impact | Status | Mitigation |
|------|-------------|--------|--------|------------|
| Wire format mismatch | ~~HIGH~~ | ~~HIGH~~ | **RESOLVED** | Complete wire format verified from 90MB packet trace + jump table analysis. See `phase1-verified-protocol.md`. |
| ACK priority queue stall | ~~HIGH~~ | ~~HIGH~~ | **RESOLVED** | Three-tier queue system (unreliable/reliable/priority) fully documented. Retry timing, sequence numbering, ACK flow all verified. See `phase1-verified-protocol.md` Section 3. |
| Hash table extraction fails | ~~LOW~~ | ~~HIGH~~ | **RESOLVED** | Hash algorithm reversed: CRC32 base with XOR-fold to 16-bit. Checksum exchange protocol (4 rounds) fully traced. See `phase1-verified-protocol.md` Section 9. |
| Python compat shim edge cases | MEDIUM | MEDIUM | OPEN | Test against full vanilla script corpus + Foundation Technologies |
| ET_* values unknown | ~~HIGH~~ | ~~HIGH~~ | **RESOLVED** | ET_* event values extracted from SWIG bindings (3,990 wrappers cataloged). Key events: 0x008000E0 (phaser level), 0x008000E3 (cloak start), 0x008000E5 (cloak stop). |
| Message dispatch table unknown | ~~MEDIUM~~ | ~~HIGH~~ | **RESOLVED** | Three dispatchers fully mapped: NetFile (0x20-0x27), MultiplayerGame (0x00-0x2A via jump table at 0x0069F534), MultiplayerWindow (0x00, 0x01, 0x16). Python path (0x2C-0x39) also documented. |
| GameSpy query format edge cases | LOW | MEDIUM | OPEN | Standard QR SDK protocol; capture actual queries from BC client |
| Connection handshake details | ~~MEDIUM~~ | ~~HIGH~~ | **RESOLVED** | Complete handshake flow documented: connect → GameSpy peek → checksum exchange (4 rounds) → Settings (0x00) → GameInit (0x01) → EnterSet (0x1F) → NewPlayerInGame (0x2A). See `phase1-verified-protocol.md` Section 4. |
| Opcode dispatch table unknown | ~~HIGH~~ | ~~HIGH~~ | **RESOLVED** | All 28 active opcodes verified from jump table at 0x0069F534 with handler addresses and frequency counts from 90MB trace. See `phase1-verified-protocol.md` Section 5. |
| Ship creation protocol unknown | ~~MEDIUM~~ | ~~HIGH~~ | **RESOLVED** | ObjectCreateTeam (0x03) fully reversed: contains team byte, ship name, NIF path. Server creates ship via Python-driven DeferredInitObject with real NIF models and 33 subsystems. See `phase1-re-gaps.md` Section 1.19. |
| **UI stub coverage insufficient** | HIGH | MEDIUM | OPEN | Scripts call many UI functions. Missing stubs cause AttributeError. Mitigation: DummyObject proxy pattern handles arbitrary attribute chains. Add specific stubs reactively as crashes found. |
| **list.sort(cmp_func) breakage** | HIGH | MEDIUM | OPEN | Compat shim with functools.cmp_to_key; 11 occurrences in gameplay scripts |
| **Late-join state sync incomplete** | MEDIUM | MEDIUM | OPEN | InitNetwork sends config via Python; test with vanilla client joining mid-game. STBC-Dedi has working InitNetwork with peer-array detection (~1.4s timing). |
| **Scoring not authoritative (Phase 1)** | LOW | LOW | OPEN | STBC-Dedi has working headless scoring (ObjectKilledHandler + SCORE_CHANGE_MESSAGE). Consider promoting to Phase 1. |
| **Message ordering sensitivity** | LOW | MEDIUM | OPEN | Reliable channel preserves order. Unreliable out-of-order is handled by clients (they already do in vanilla). |

---

## 10. Definition of Done

Phase 1 is complete when ALL of the following are true:

### Lobby & Connection
1. A vanilla BC client discovers the OpenBC server in the LAN browser
2. Client connects and receives a player slot (0-15)
3. All 4 checksum rounds complete successfully (or are skipped via config)
4. Client receives game settings (opcode 0x00) and status (opcode 0x01)
5. Client reaches the ship selection screen

### Gameplay
6. **Client selects a ship and the selection is broadcast to all other connected clients**
7. **Two or more clients can play a complete multiplayer match** (movement, weapons, combat all functional via message relay)
8. **Chat works**: messages typed by one client appear on all other clients (regular chat and team chat)
9. **Game end works**: when time limit or frag limit is reached, END_GAME_MESSAGE is received by all clients

### Game Lifecycle
10. **Game restart works**: after game end, host restart returns all clients to ship select screen
11. **Late join works**: a client connecting mid-game receives MISSION_INIT_MESSAGE and can play immediately
12. **Ship death and respawn works**: destroyed ships transition through death states; players can select new ships

### Capacity & Stability
13. Up to 16 simultaneous clients handled correctly
14. Idle clients remain connected (keepalive works)
15. Disconnected clients are detected and slots freed; remaining players continue
16. Server builds from clean clone on Linux, Windows, and macOS
17. Server runs in Docker container
18. CI passes on all platforms

### Script Compatibility
19. BC multiplayer scripts (Multiplayer/*.py) import and register handlers without Python errors
20. **Mission scripts 1-3** (FFA Deathmatch, Team DM, Team Objectives) load and manage game lifecycle
21. **All 50+ vanilla ship hardpoint scripts** execute successfully via the Property API
22. **UI-referencing code paths** do not crash (DummyObject stubs handle all attribute access)

### Deferred (NOT required for Phase 1 completion)
- Server-side scoring (clients track scores locally; server-side scoring deferred to Phase 2)
- Server-authoritative physics (Phase 1 trusts clients)
- AI systems (Mission5 coop deferred to Phase 2)
- Mission5 (Coop vs AI) requires AI + ship simulation on server
