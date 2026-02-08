# OpenBC Phase 1: Standalone Server -- Architectural Design Document

## Status: RESEARCH COMPLETE -- Design Finalized 2026-02-07

---

## 1. System Architecture Overview

Phase 1 delivers a headless dedicated server that speaks the legacy Bridge Commander
multiplayer protocol. A vanilla BC client (from GOG) can discover the server on LAN,
connect, pass checksums, receive game settings, and enter the ship selection lobby.

### High-Level Architecture

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
                    |                       |
                    |  (peek-router demux)  |
                    +-----------+-----------+
                                |
                       +--------v----------+
                       |   Event Manager   |
                       |   (dispatch)      |
                       +--------+----------+
                                |
              +-----------------+------------------+
              |                 |                   |
     +--------v------+  +------v-------+  +-------v--------+
     | NetFile /      |  | Multiplayer  |  | Python Script  |
     | Checksum Mgr   |  | Game State   |  | Engine         |
     +--------+------+  +------+-------+  +-------+--------+
              |                 |                   |
              +--------+--------+-------------------+
                       |
                +------v-------+
                |   flecs ECS  |
                |   World      |
                +--------------+
```

### Server-Only Boundaries

Phase 1 explicitly EXCLUDES:
- Rendering (bgfx, NIF loading)
- Audio (miniaudio)
- UI (RmlUi)
- Input (SDL3 input events -- SDL3 is used only for platform/timer)
- Scene graph (NiNode hierarchy)
- Single-player game logic (bridge crew, cutscenes)

Phase 1 INCLUDES:
- Network protocol (TGNetwork reimplementation via ENet)
- GameSpy LAN discovery responder
- Event system (TGEventManager reimplementation)
- Checksum exchange protocol
- Player slot management (16 slots)
- MultiplayerGame state machine
- Python 3.x embedding with 1.5.2 compatibility shim
- App/Appc module with ~297 server-relevant functions
- Config system (TGConfigMapping)
- Timer system (TGTimerManager)
- flecs ECS world (game state backbone)

---

## 2. Module Breakdown

### 2.1 `src/engine/` -- Core Engine

**Files**: `engine.h`, `engine.c`, `game_loop.c`, `clock.c`

Responsibilities:
- `ecs_world_t` creation and lifecycle
- Main tick function (30 Hz, matching original's 33ms timer)
- Clock management (game time, frame time, real time)
- Phase-aware initialization (server skips render/audio init)

```c
// engine.h
typedef struct obc_engine_t {
    ecs_world_t *world;
    double       game_time;      // Seconds since game start
    double       frame_time;     // Delta since last tick
    double       real_time;      // Wall-clock time
    bool         is_running;
    bool         is_server_only; // true for Phase 1
} obc_engine_t;

obc_engine_t *obc_engine_create(bool server_only);
void          obc_engine_destroy(obc_engine_t *engine);
void          obc_engine_tick(obc_engine_t *engine);
void          obc_engine_run(obc_engine_t *engine); // blocking main loop
```

### 2.2 `src/network/` -- Network Protocol

**Files**: `tg_network.h`, `tg_network.c`, `tg_message.h`, `tg_message.c`,
           `packet_format.h`, `packet_format.c`, `peer.h`, `peer.c`,
           `gamespy_responder.h`, `gamespy_responder.c`

This is the largest and most critical Phase 1 module. It reimplements the
TGWinsockNetwork (0x34C bytes) behavior using ENet as the UDP transport.

**Why ENet instead of raw sockets**: The original TGNetwork implements its own
reliable delivery, sequencing, ACK tracking, and priority queues over raw UDP.
ENet provides equivalent functionality (reliable/unreliable channels, sequencing,
peer management) with a proven, portable implementation. This avoids reimplementing
~225 functions of low-level UDP plumbing.

```c
// tg_network.h
typedef struct tg_network_t {
    ENetHost    *host;           // ENet host (server side)
    uint16_t     port;           // Default 22101 (0x5655)
    uint8_t      conn_state;     // 2=HOST, 3=CLIENT, 4=DISCONNECTED
    bool         is_host;        // true for server
    bool         send_enabled;   // maps to WSN+0x10C
    uint32_t     local_id;       // 1 for host
    uint32_t     host_id;        // 1 for host
    // Peer tracking (up to 16)
    tg_peer_t    peers[16];
    int          num_peers;
    // Corresponding flecs entity
    ecs_entity_t entity;
} tg_network_t;

// Peer structure (maps to original's peer array at WSN+0x2C)
typedef struct tg_peer_t {
    uint32_t     peer_id;        // Unique ID assigned on connect
    ENetPeer    *enet_peer;      // ENet peer handle
    uint8_t      player_slot;    // 0-15, or 0xFF if unassigned
    bool         active;
    // Queue statistics (for diagnostics)
    int          unreliable_pending;  // peer+0x7C equiv
    int          reliable_pending;    // peer+0x98 equiv
    int          priority_pending;    // peer+0xB4 equiv
} tg_peer_t;
```

**Critical compatibility requirement**: The wire format must match the original
TGNetwork packet format exactly, because vanilla BC clients will connect. This
means ENet is used ONLY for connection/peer management -- actual packet payloads
must be serialized in the original format and sent as raw data through ENet's
reliable/unreliable channels.

**Alternative approach**: If ENet's framing proves incompatible with vanilla clients,
we fall back to raw UDP sockets and reimplement the TGNetwork reliable delivery
protocol ourselves. The original protocol is documented:
- Packet header with sequence numbers
- 3 priority queues per peer (unreliable, reliable, priority reliable)
- ACK tracking with retry and timeout
- Max 15-byte retry packets every ~5 seconds

**Decision point**: This is the single highest-risk technical question in Phase 1.
We need to capture actual wire packets from a vanilla BC session and compare them
against ENet's wire format. If they differ (highly likely), we must use raw sockets.

#### 2.2.1 GameSpy LAN Discovery

The original game shares a single UDP socket between GameSpy queries and game
traffic. Packets are demuxed by peeking at the first byte:
- `\` prefix -> GameSpy query
- Binary data -> TGNetwork game packet

For Phase 1, we implement the GameSpy query responder as a simple UDP handler
that responds to the standard GameSpy query protocol (backslash-delimited
key-value pairs). This allows the server to appear in vanilla BC's LAN browser.

```c
// gamespy_responder.h
typedef struct gamespy_responder_t {
    int          socket_fd;     // Shared with tg_network if using raw sockets
    char         game_name[64];
    char         map_name[128];
    int          num_players;
    int          max_players;
    uint16_t     port;
} gamespy_responder_t;
```

### 2.3 `src/compat/` -- SWIG API Compatibility Layer

**Files**: `app_module.h`, `app_module.c`, `appc_module.h`, `appc_module.c`,
           `handle_map.h`, `handle_map.c`, `swig_types.h`, `swig_types.c`,
           `event_constants.h`, `network_api.c`, `event_api.c`,
           `config_api.c`, `multiplayer_api.c`, `util_api.c`

This is the SWIG API compatibility layer -- the bridge between Python scripts
and the engine. For Phase 1, we implement ~297 functions covering:

| Category | Count | Examples |
|----------|-------|---------|
| TGNetwork/TGWinsockNetwork | ~47 | Send, Update, GetNumPlayers, IsHost |
| TGEvent/TGEventManager | ~15 | Create, SetEventType, AddBroadcastHandler |
| MultiplayerGame | ~12 | Cast, Create, SetMaxPlayers, SetReadyForNewPlayers |
| TGConfigMapping | ~12 | GetIntValue, SetStringValue, LoadConfigFile |
| VarManagerClass | ~8 | SetStringVariable, GetStringVariable, MakeEpisodeEventType |
| TGString | ~3 | new_TGString, delete_TGString |
| UtopiaModule | ~15 | InitializeNetwork, GetNetwork, CreateGameSpy, GetCaptainName |
| TopWindow | ~5 | GetTopWindow, FindMainWindow, SetupMultiplayerGame |
| Game | ~5 | GetCurrentGame, LoadEpisode |
| TGMessage/TGBufferStream | ~20 | Create, GetBufferStream, ReadChar, ReadInt, WriteChar |
| Misc (Timer, Object) | ~10 | CreateTimer, DeleteTimer, IsNull |
| Constants (ET_*, CT_*, etc.) | ~145 | ET_NETWORK_MESSAGE_EVENT, CT_SHIP, MWT_MULTIPLAYER |

### 2.4 `src/compat/handle_map.c` -- Entity-Handle Mapping (CRITICAL)

The original SWIG API uses opaque pointer strings as handles. In Python 1.5,
these look like `_97fa00_p_UtopiaModule` -- a hex address with a type suffix.

Our reimplementation maps these to flecs entities.

#### Handle Format

We preserve the SWIG pointer string format for compatibility:
```
_HEXADDR_p_TypeName
```

But instead of actual memory addresses, we use the flecs entity ID encoded in hex:
```
_00000001_p_TGNetwork        (entity 1)
_00000002_p_MultiplayerGame  (entity 2)
_00000003_p_TGEvent          (entity 3)
```

#### Handle Registry

```c
// handle_map.h

// Type tag for handle validation
typedef enum obc_handle_type_t {
    OBC_HANDLE_NONE = 0,
    OBC_HANDLE_TG_NETWORK,
    OBC_HANDLE_TG_WINSOCK_NETWORK,
    OBC_HANDLE_MULTIPLAYER_GAME,
    OBC_HANDLE_TG_EVENT,
    OBC_HANDLE_TG_EVENT_MANAGER,
    OBC_HANDLE_TG_CONFIG_MAPPING,
    OBC_HANDLE_UTOPIA_MODULE,
    OBC_HANDLE_VAR_MANAGER,
    OBC_HANDLE_TOP_WINDOW,
    OBC_HANDLE_GAME,
    OBC_HANDLE_TG_MESSAGE,
    OBC_HANDLE_TG_BUFFER_STREAM,
    OBC_HANDLE_TG_TIMER,
    OBC_HANDLE_TG_STRING,
    OBC_HANDLE_TG_PLAYER_LIST,
    // Phase 2+ types:
    OBC_HANDLE_SHIP,
    OBC_HANDLE_SUBSYSTEM,
    OBC_HANDLE_WEAPON,
    OBC_HANDLE_MISSION,
    OBC_HANDLE_EPISODE,
    OBC_HANDLE_SET,
    OBC_HANDLE_MAX
} obc_handle_type_t;

typedef struct obc_handle_t {
    ecs_entity_t entity;       // flecs entity ID
    obc_handle_type_t type;    // Type tag for validation
    uint32_t generation;       // Monotonic counter for use-after-free detection
} obc_handle_t;

// Registry: maps handle string <-> flecs entity
// Uses a hash map internally (string -> obc_handle_t)
typedef struct obc_handle_map_t {
    // Forward map: string -> handle
    // Reverse map: entity -> string
    ecs_world_t *world;
    uint32_t next_generation;
} obc_handle_map_t;

// API
obc_handle_map_t *obc_handle_map_create(ecs_world_t *world);
void              obc_handle_map_destroy(obc_handle_map_t *map);

// Create a new handle for an entity with a given type
const char *obc_handle_create(obc_handle_map_t *map, ecs_entity_t entity,
                              obc_handle_type_t type);

// Resolve a handle string to an entity, with type checking
ecs_entity_t obc_handle_resolve(obc_handle_map_t *map, const char *handle_str,
                                obc_handle_type_t expected_type);

// Resolve without type checking (for generic functions)
ecs_entity_t obc_handle_resolve_any(obc_handle_map_t *map, const char *handle_str);

// Get the handle string for an entity
const char *obc_handle_get_string(obc_handle_map_t *map, ecs_entity_t entity);

// Invalidate a handle (entity destroyed)
void obc_handle_invalidate(obc_handle_map_t *map, ecs_entity_t entity);
```

#### SWIG Type Compatibility

The original SWIG code checks types strictly. `_p_TGNetwork` and `_p_TGWinsockNetwork`
are different types, and functions that expect one reject the other. We replicate this:

```c
// Type hierarchy for inheritance-like behavior:
// TGWinsockNetwork IS-A TGNetwork
// MultiplayerGame IS-A Game
// These are encoded in the handle_type enum and checked during resolution.

ecs_entity_t obc_handle_resolve(map, "_00000001_p_TGNetwork", OBC_HANDLE_TG_NETWORK) {
    // Also accepts OBC_HANDLE_TG_WINSOCK_NETWORK (subtype)
}
```

#### Singleton Handles

The original game has global singletons accessed via `App.g_kUtopiaModule`, etc.
These map to fixed flecs entities:

```c
// Singleton entities created at startup:
#define OBC_ENTITY_UTOPIA_MODULE   ((ecs_entity_t)1)
#define OBC_ENTITY_EVENT_MANAGER   ((ecs_entity_t)2)
#define OBC_ENTITY_CONFIG_MAPPING  ((ecs_entity_t)3)
#define OBC_ENTITY_VAR_MANAGER     ((ecs_entity_t)4)
#define OBC_ENTITY_TOP_WINDOW      ((ecs_entity_t)5)
#define OBC_ENTITY_NETWORK         ((ecs_entity_t)6)
```

These are registered in the handle map during initialization and exposed to Python
as `App.g_kUtopiaModule`, etc.

### 2.5 `src/compat/event_system.c` -- Event System

Reimplements the TGEventManager / TGEvent / TGEventHandlerObject pattern.

```c
// Event type (matches original's 32-bit event type IDs)
typedef uint32_t obc_event_type_t;

// Event object (maps to TGEvent)
typedef struct obc_event_t {
    obc_event_type_t type;
    ecs_entity_t     source;       // Who posted the event
    ecs_entity_t     destination;  // Target entity (or 0 for broadcast)
    // Payload: union of event-specific data
    union {
        struct { uint32_t peer_id; } network;
        struct { uint8_t *data; size_t len; } message;
        struct { ecs_entity_t object; } object_event;
        // ... extensible for Phase 2+
    } data;
    // For Python handler compatibility:
    const char *handle_str;       // SWIG handle string for this event
} obc_event_t;

// Handler registration (maps to TGEventHandlerObject_AddPythonFuncHandler...)
typedef struct obc_event_handler_t {
    obc_event_type_t event_type;
    // Either a C function pointer or a Python function reference
    enum { HANDLER_C, HANDLER_PYTHON_FUNC, HANDLER_PYTHON_METHOD } kind;
    union {
        void (*c_handler)(obc_event_t *event, void *userdata);
        struct {
            char module_name[128];  // e.g., "Multiplayer.MissionShared"
            char func_name[128];    // e.g., "ProcessMessageHandler"
        } python;
    } callback;
    ecs_entity_t target;           // For instance handlers
    void *userdata;                // For C handlers
    struct obc_event_handler_t *next; // Handler chain
} obc_event_handler_t;

// Event manager
typedef struct obc_event_manager_t {
    // Event queue (ring buffer)
    obc_event_t *queue;
    int queue_head, queue_tail, queue_capacity;
    // Handler registry: hash map of event_type -> handler chain
    obc_event_handler_t **handler_buckets;
    int num_buckets;
    // Corresponding flecs entity
    ecs_entity_t entity;
} obc_event_manager_t;
```

**Key event types for Phase 1** (hex values from decompiled code):

| Constant | Value | Purpose |
|----------|-------|---------|
| ET_NETWORK_MESSAGE_EVENT | 0x60001 | Incoming network message dispatched |
| ET_NETWORK_CONNECT_EVENT | 0x60002 | Host session created |
| ET_NETWORK_DISCONNECT_EVENT | (TBD) | Peer disconnected |
| ET_NETWORK_NEW_PLAYER | (TBD) | New peer connected |
| ET_NETWORK_DELETE_PLAYER | (TBD) | Peer removed |
| ET_CHECKSUM_COMPLETE | 0x8000e8 | All checksums passed |
| ET_SYSTEM_CHECKSUM_FAILED | 0x8000e7 | Checksum mismatch |
| ET_KILL_GAME | 0x8000e9 | Game terminated |
| ET_CREATE_SERVER | (from App.py) | Server creation requested |
| ET_START | 0x800053 | Game start |
| ET_MISSION_START | (from App.py) | Mission loading |
| ET_LOAD_EPISODE | (from App.py) | Episode loading |

### 2.6 `src/network/netfile.c` -- Checksum Manager

Reimplements the NetFile object (0x48 bytes) that handles checksum exchange,
file transfer, and message opcode dispatch for opcodes 0x20-0x27.

```c
// Checksum request entry
typedef struct checksum_request_t {
    uint8_t  index;           // 0-3
    char     directory[256];  // e.g., "scripts/"
    char     filter[64];      // e.g., "App.pyc" or "*.pyc"
    bool     recursive;
    uint32_t server_hash;     // Server-computed hash
    uint32_t client_hash;     // Client-reported hash
    bool     verified;
} checksum_request_t;

// Per-player checksum state
typedef struct player_checksum_state_t {
    uint32_t           peer_id;
    checksum_request_t requests[4];
    int                current_index;    // Next to verify
    bool               all_passed;
} player_checksum_state_t;

typedef struct netfile_t {
    // Hash table A: tracking (matches original vtable+0x18)
    // Hash table B: queued checksum requests (matches original vtable+0x28)
    // Hash table C: pending file transfers (matches original vtable+0x38)
    player_checksum_state_t players[16];
    int num_active;
    // The 4 standard checksum definitions
    checksum_request_t definitions[4];
    // Config: skip checksums flag (DAT_0097f94c)
    bool skip_checksums;
    ecs_entity_t entity;
} netfile_t;
```

The four checksum requests are fixed:

| Index | Directory | Filter | Recursive |
|-------|-----------|--------|-----------|
| 0 | scripts/ | App.pyc | No |
| 1 | scripts/ | Autoexec.pyc | No |
| 2 | scripts/ships/ | *.pyc | Yes |
| 3 | scripts/mainmenu/ | *.pyc | No |

### 2.7 `src/scripting/` -- Python Embedding

**Files**: `python_host.h`, `python_host.c`, `compat_shim.py`

Embeds Python 3.x and provides the `App` and `Appc` modules that original scripts
import. Includes a compatibility shim for Python 1.5.2 idioms.

```c
// python_host.h
typedef struct python_host_t {
    bool initialized;
    // Module references
    PyObject *app_module;   // The "App" module
    PyObject *appc_module;  // The "Appc" module (low-level)
    // Nesting counter (matches original at 0x0099EE38)
    int nesting_count;
    // Script search paths
    char script_path[256];
} python_host_t;

python_host_t *python_host_create(const char *script_path);
void           python_host_destroy(python_host_t *host);
int            python_host_run_string(python_host_t *host, const char *code);
int            python_host_run_file(python_host_t *host, const char *filename);
PyObject      *python_host_call(python_host_t *host, const char *module,
                                const char *func, PyObject *args);
```

**Compatibility shim** (`compat_shim.py`): Installed as a site-customize or
injected before any game scripts run. Handles:
- `apply(func, args)` -> `func(*args)` (Python 1.5 idiom)
- `raise AttributeError, name` -> `raise AttributeError(name)`
- `except Exception, e:` -> `except Exception as e:`
- `import new` -> compatibility wrapper
- `string.xxx` -> `str.xxx` methods
- `strop` module -> `string` module bridge
- `print` statement -> `print()` function (via `__future__` or AST transform)
- `cPickle` -> `pickle`

**Note**: The original scripts use Python 1.5.2 syntax. The compatibility shim
cannot fix all syntax differences at runtime. A separate offline migration tool
(`tools/migrate_scripts.py`) performs AST-level transformations. The shim handles
the runtime-patchable differences.

### 2.8 `src/engine/config.c` -- Configuration System

Reimplements TGConfigMapping for reading BC's `.cfg` files.

```c
// TGConfigMapping reimplementation
typedef struct config_section_t {
    char name[64];
    struct config_entry_t *entries;
    int num_entries;
} config_section_t;

typedef struct config_mapping_t {
    config_section_t *sections;
    int num_sections;
    ecs_entity_t entity;
} config_mapping_t;
```

### 2.9 `src/engine/timer.c` -- Timer System

Reimplements TGTimerManager for time-based events.

```c
typedef struct obc_timer_t {
    uint32_t id;
    double   fire_time;     // When to fire next
    double   interval;      // 0 = one-shot, >0 = repeating
    double   duration;      // -1.0 = infinite
    obc_event_type_t event_type;
    char     handler_name[256]; // Python handler function
    bool     active;
} obc_timer_t;
```

---

## 3. Data Flow

### 3.1 Inbound Packet Processing (per tick)

```
UDP Socket
    |
    v
[1] Peek first byte
    |
    +-- '\' prefix --------> GameSpy Query Responder
    |                              |
    |                              v
    |                         Format response, sendto()
    |
    +-- Binary data -------> TGNetwork::Update()
                                   |
                             [2] ProcessIncomingPackets
                                   |  (recvfrom, deserialize,
                                   |   ACK reliable packets)
                                   |
                             [3] DispatchIncomingQueue
                                   |  (sequence validation,
                                   |   discard out-of-window)
                                   |
                             [4] For each valid message:
                                   |  Create obc_event_t with
                                   |  type = ET_NETWORK_MESSAGE_EVENT
                                   |  Post to EventManager queue
                                   |
                                   v
                             EventManager::ProcessEvents()
                                   |
                    +--------------+--------------+
                    |                             |
              [5a] NetFile handler          [5b] MultiplayerGame handler
              (opcodes 0x20-0x27)          (opcodes 0x00-0x1F)
                    |                             |
                    v                             v
              Checksum exchange           Game state updates
              File transfer               (Phase 2+)
                    |
              [6] On completion:
                    Post ET_CHECKSUM_COMPLETE
                    |
                    v
              ChecksumCompleteHandler
                    |
              [7] Send opcode 0x00
                    (settings packet)
                    Send opcode 0x01
                    (status byte)
```

### 3.2 Outbound Message Flow

```
Game logic / Event handler
    |
    v
TGNetwork::Send(peer_id, message, flags)
    |
    v
Queue message to peer's send queue
    (unreliable / reliable / priority reliable)
    |
    v
[Next tick] SendOutgoingPackets()
    |
    v
Serialize message to wire format
    |
    v
sendto() via UDP socket
```

### 3.3 Event System Flow

```
Event source (network, timer, script)
    |
    v
obc_event_manager_post(manager, event)
    |
    v
[Queued in ring buffer]
    |
    v
[During tick] obc_event_manager_process(manager)
    |
    v
For each event in queue:
    1. Look up handlers for event.type in hash table
    2. Walk handler chain:
       a. C handlers: call directly
       b. Python handlers: PyObject_CallFunction(module.func, args)
    3. If event.destination != 0:
       Also dispatch to per-instance handlers on destination entity
    4. Free event
```

---

## 4. Entity-Handle Mapping Design

### 4.1 Rationale

The original SWIG API represents every C++ object as an opaque pointer string.
Scripts manipulate these strings to call methods on objects. Our ECS-based engine
stores state in flecs components, not C++ objects. The handle map bridges this gap.

### 4.2 Handle Lifecycle

```
[Python calls App.TGEvent_Create()]
    |
    v
[appc_module.c: wrap_TGEvent_Create()]
    1. Create flecs entity: ecs_entity_t e = ecs_new(world);
    2. Add TGEvent component: ecs_set(world, e, OBC_TGEvent, {...});
    3. Create handle: obc_handle_create(map, e, OBC_HANDLE_TG_EVENT);
       -> Returns "_00000042_p_TGEvent"
    4. Return string to Python
    |
    v
[Python stores handle, later calls App.TGEvent_SetEventType(evt, type)]
    |
    v
[appc_module.c: wrap_TGEvent_SetEventType(handle_str, type)]
    1. Resolve handle: ecs_entity_t e = obc_handle_resolve(map, handle_str,
                                                           OBC_HANDLE_TG_EVENT);
    2. Validate: if (e == 0) { PyErr_SetString(...); return NULL; }
    3. Get component: OBC_TGEvent *evt = ecs_get_mut(world, e, OBC_TGEvent);
    4. Modify: evt->type = type;
    5. Return Py_None
```

### 4.3 Type Safety

The handle string encodes the type: `_00000042_p_TGEvent`. When a function
expects a specific type, the resolve step checks:

```c
ecs_entity_t obc_handle_resolve(map, str, expected_type) {
    obc_handle_t *h = lookup(map, str);
    if (!h) return 0;  // Invalid handle

    // Check type compatibility (with inheritance)
    if (h->type != expected_type) {
        if (!obc_type_is_subtype(h->type, expected_type)) {
            return 0;  // Type mismatch
        }
    }

    // Check generation (use-after-free detection)
    // Entity must still be alive in flecs
    if (!ecs_is_alive(world, h->entity)) {
        return 0;
    }

    return h->entity;
}
```

Type inheritance table (for Phase 1):

| Subtype | Supertype |
|---------|-----------|
| TGWinsockNetwork | TGNetwork |
| MultiplayerGame | Game |

### 4.4 Null Handles

The original API uses `_ffffffff_p_TypeName` or simply checks `IsNull()`.
We handle this with:

```c
#define OBC_NULL_HANDLE_STR "_ffffffff_p_void"

// App.IsNull() checks:
bool obc_handle_is_null(const char *handle_str) {
    return handle_str == NULL ||
           strncmp(handle_str, "_ffffffff_", 10) == 0;
}
```

---

## 5. Server Game Loop

### 5.1 Tick Structure (30 Hz / 33ms)

The original dedicated server runs at 30 Hz via a Windows timer callback.
Our server uses a platform-agnostic sleep loop (SDL3 or plain usleep).

```c
void obc_engine_tick(obc_engine_t *engine) {
    // 1. Update clock
    double now = get_monotonic_time();
    engine->frame_time = now - engine->last_tick_time;
    engine->last_tick_time = now;
    engine->game_time += engine->frame_time;

    // 2. Timer manager update
    //    Fires any expired timers as events
    obc_timer_manager_update(engine->timer_manager, engine->game_time);

    // 3. Event manager: process all queued events
    //    This dispatches to C and Python handlers
    obc_event_manager_process(engine->event_manager);

    // 4. Network update: TGNetwork::Update equivalent
    //    a. Send outgoing packets from peer queues
    //    b. Receive incoming packets from socket
    //    c. Dispatch incoming queue (sequence validation)
    //    d. Post ET_NETWORK_MESSAGE_EVENT for each valid message
    tg_network_update(engine->network);

    // 5. GameSpy: handle any pending queries
    //    (peek-based demux on shared socket)
    gamespy_tick(engine->gamespy);

    // 6. Process events AGAIN (network update may have posted new events)
    obc_event_manager_process(engine->event_manager);

    // 7. Run flecs systems (ECS tick)
    //    Phase 1: minimal systems (player state sync, timeout checks)
    //    Phase 2+: ship simulation, AI, combat
    ecs_progress(engine->world, (float)engine->frame_time);
}
```

**Note on double event processing**: The original game loop calls MainTick
(which includes ProcessEvents) and then TGNetwork::Update separately.
Network messages received during Update are not processed until the next
tick's ProcessEvents call. However, the host's dequeue loop fires events
directly into the EventManager during TGNetwork::Update. We replicate this
by calling ProcessEvents both before and after network update.

### 5.2 Tick Ordering Rationale

The order matches the original:
1. **Timers first**: Expired timers generate events (e.g., keepalive timeouts)
2. **Events from previous tick**: Process events queued last tick
3. **Network I/O**: Send queued messages, receive new ones
4. **Events from network**: Process newly-received messages
5. **ECS systems**: Update game state based on all inputs this tick

---

## 6. Initialization Sequence

### 6.1 Complete Startup: main() to Accepting Connections

```
main(argc, argv)
    |
    [1] Parse command-line arguments
    |   --port=22101, --max-players=16, --game-name="OpenBC Server"
    |   --map=Multiplayer.Episode.Mission1.Mission1
    |   --script-path=scripts/  (path to BC game data scripts)
    |   --config=stbc.cfg       (path to BC config file)
    |
    [2] Initialize platform (SDL3 for timers/clock only)
    |   SDL_Init(SDL_INIT_TIMER)
    |
    [3] Create flecs world
    |   ecs_world_t *world = ecs_init();
    |   Register all Phase 1 components (see section 7)
    |   Register all Phase 1 systems
    |
    [4] Initialize Python embedding
    |   PyImport_AppendInittab("App", PyInit_App);
    |   PyImport_AppendInittab("Appc", PyInit_Appc);
    |   Py_Initialize();
    |   Install compatibility shim
    |   Add script_path to sys.path
    |
    [5] Create singleton entities + handles
    |   Create UtopiaModule entity -> handle "_00000001_p_UtopiaModule"
    |   Create EventManager entity -> handle "_00000002_p_TGEventManager"
    |   Create ConfigMapping entity -> handle "_00000003_p_TGConfigMapping"
    |   Create VarManager entity -> handle "_00000004_p_VarManagerClass"
    |   Register as App.g_kUtopiaModule, g_kEventManager, etc.
    |
    [6] Load configuration
    |   config_load("stbc.cfg")
    |   Read "Multiplayer Options" section
    |
    [7] Set multiplayer/host flags
    |   UtopiaModule.is_host = true
    |   UtopiaModule.is_multiplayer = true
    |
    [8] Initialize network (equivalent to FUN_00445d90)
    |   a. Create TGWinsockNetwork -> handle "_00000006_p_TGWinsockNetwork"
    |   b. Set port (default 22101)
    |   c. tg_network_host_or_join(network, NULL, NULL)
    |      - Bind UDP socket
    |      - Set conn_state = 2 (HOST)
    |      - Post ET_NETWORK_CONNECT_EVENT (0x60002)
    |   d. Create NetFile/ChecksumManager
    |      - Register handler for ET_NETWORK_MESSAGE_EVENT (0x60001)
    |   e. Create GameSpy responder
    |
    [9] Create MultiplayerGame
    |   Create entity with MultiplayerGame component
    |   Register all event handlers (equivalent to FUN_0069efe0):
    |     - NewPlayerHandler (ET_NETWORK_NEW_PLAYER)
    |     - DisconnectHandler (ET_NETWORK_DISCONNECT_EVENT)
    |     - ChecksumCompleteHandler (ET_CHECKSUM_COMPLETE)
    |     - ReceiveMessageHandler (ET_NETWORK_MESSAGE_EVENT)
    |     - KillGameHandler (ET_KILL_GAME)
    |     - DeletePlayerHandler (ET_NETWORK_DELETE_PLAYER)
    |     - ... (all 28 handlers from FUN_0069efe0)
    |
    [10] Run server automation (equivalent to DedicatedServer.py Phase 3)
    |    Set game name, captain name
    |    Set ReadyForNewPlayers = 1
    |    Set MaxPlayers = 16 (or from config)
    |    Set mission/map name in VarManager
    |
    [11] Run Autoexec.py (if it exists and is compatible)
    |    This is optional for server -- most Autoexec content is UI-related
    |
    [12] Enter main loop
         obc_engine_run(engine);  // Blocks, runs at 30 Hz
```

### 6.2 Player Connection Flow (After Startup)

```
[Vanilla BC client clicks "Join" in multiplayer browser]
    |
    v
Client sends connection request to port 22101
    |
    v
[Server tick: tg_network_update]
    ProcessIncomingPackets detects new peer
    Assigns peer_id
    Posts ET_NETWORK_NEW_PLAYER event
    |
    v
[Event processing: NewPlayerHandler fires]
    1. Check ReadyForNewPlayers (must be 1)
    2. Find empty player slot (0-15)
    3. If slots full: send rejection message, return
    4. Assign slot, store peer_id in slot
    5. Start checksum exchange:
       netfile_start_checksums(netfile, peer_id)
       -> Queue 4 checksum requests in hash table B
       -> Send request #0 (opcode 0x20) immediately
    |
    v
[Client receives opcode 0x20, computes checksums, sends opcode 0x21]
    |
    v
[Server tick: receives opcode 0x21]
    NetFile::ReceiveMessageHandler dispatches
    -> ChecksumResponseVerifier:
       Compare server hash vs client hash
       Match: dequeue, send next request
       Mismatch: fire ET_SYSTEM_CHECKSUM_FAILED
    -> When all 4 pass: fire ET_CHECKSUM_COMPLETE
    |
    v
[ChecksumCompleteHandler fires]
    1. Build opcode 0x00 packet:
       [0x00][gameTime:f32][setting1:u8][setting2:u8][playerSlot:u8]
       [mapNameLen:u16][mapName:bytes][passFail:u8]
    2. Send via reliable channel
    3. Build opcode 0x01 packet: [0x01]
    4. Send via reliable channel
    |
    v
[Client receives settings, enters ship selection lobby]
```

---

## 7. flecs ECS Component Design

### 7.1 Phase 1 Components

```c
// Network identity
typedef struct OBC_NetworkPeer {
    uint32_t peer_id;
    uint8_t  player_slot;    // 0-15
    bool     checksums_passed;
    bool     in_game;        // false until ship selected
    char     player_name[64];
} OBC_NetworkPeer;

// Multiplayer game state (singleton)
typedef struct OBC_MultiplayerState {
    bool     ready_for_new_players;
    int      max_players;
    int      num_players;
    char     game_name[64];
    char     captain_name[64];
    char     map_name[256];
    float    game_time;
    uint8_t  setting1;       // DAT_008e5f59
    uint8_t  setting2;       // DAT_0097faa2
    bool     game_started;
    bool     friendly_fire;
} OBC_MultiplayerState;

// Configuration variable storage (singleton)
typedef struct OBC_VarStorage {
    // Dynamic key-value store, scoped by string
    // Implemented as hash map: "scope/key" -> value
} OBC_VarStorage;

// Timer component (on timer entities)
typedef struct OBC_Timer {
    double   fire_time;
    double   interval;
    double   duration;
    obc_event_type_t event_type;
    char     handler[256];
    bool     active;
} OBC_Timer;

// Player slot (on player entities)
typedef struct OBC_PlayerSlot {
    uint8_t  slot_index;     // 0-15
    uint32_t peer_id;
    bool     active;
    // Checksum state
    uint32_t checksums[4];   // Client-reported hashes
    bool     checksum_verified[4];
} OBC_PlayerSlot;

// Server statistics
typedef struct OBC_ServerStats {
    uint64_t total_packets_sent;
    uint64_t total_packets_recv;
    uint64_t total_bytes_sent;
    uint64_t total_bytes_recv;
    int      active_connections;
    double   uptime;
} OBC_ServerStats;
```

### 7.2 Component Registration

```c
void obc_register_phase1_components(ecs_world_t *world) {
    ECS_COMPONENT(world, OBC_NetworkPeer);
    ECS_COMPONENT(world, OBC_MultiplayerState);
    ECS_COMPONENT(world, OBC_VarStorage);
    ECS_COMPONENT(world, OBC_Timer);
    ECS_COMPONENT(world, OBC_PlayerSlot);
    ECS_COMPONENT(world, OBC_ServerStats);
}
```

### 7.3 Phase 1 Systems

```c
// Timeout checker: disconnect peers that haven't sent data in N seconds
ECS_SYSTEM(world, TimeoutCheckSystem, EcsOnUpdate, OBC_NetworkPeer);

// Statistics updater
ECS_SYSTEM(world, StatsUpdateSystem, EcsOnUpdate, OBC_ServerStats);

// Timer expiration checker (fires timer events)
ECS_SYSTEM(world, TimerTickSystem, EcsOnUpdate, OBC_Timer);
```

### 7.4 Observers

```c
// When a new NetworkPeer component is added -> log connection
ecs_observer(world, {
    .query.terms = {{ ecs_id(OBC_NetworkPeer) }},
    .events = { EcsOnAdd },
    .callback = on_peer_connected
});

// When a NetworkPeer is removed -> cleanup slot, notify scripts
ecs_observer(world, {
    .query.terms = {{ ecs_id(OBC_NetworkPeer) }},
    .events = { EcsOnRemove },
    .callback = on_peer_disconnected
});
```

---

## 8. Shared Code Strategy

### 8.1 What Phase 1 Code Is Shared With Future Phases

| Module | Phase 1 | Phase 2 | Phase 3 | Phase 4 | Notes |
|--------|---------|---------|---------|---------|-------|
| `src/engine/engine.c` | Shared | Shared | Shared | Shared | Core loop, clock |
| `src/engine/config.c` | Shared | Shared | Shared | Shared | Config system |
| `src/engine/timer.c` | Shared | Shared | Shared | Shared | Timer system |
| `src/network/tg_network.c` | Shared | Shared | Shared | Shared | Protocol engine |
| `src/network/netfile.c` | Shared | Shared | Shared | Shared | Checksums |
| `src/network/gamespy_responder.c` | Server | Server | Server | Both | LAN discovery |
| `src/compat/handle_map.c` | Shared | Shared | Shared | Shared | Handle system |
| `src/compat/event_system.c` | Shared | Shared | Shared | Shared | Event dispatch |
| `src/compat/app_module.c` | Shared | Extended | Extended | Extended | ~297 -> ~5711 funcs |
| `src/scripting/python_host.c` | Shared | Shared | Shared | Shared | Python embedding |
| `src/render/` | -- | -- | New | Shared | bgfx renderer |
| `src/audio/` | -- | -- | -- | New | miniaudio |
| `src/ui/` | -- | -- | -- | New | RmlUi |
| `src/physics/` | -- | New | Shared | Shared | Ship dynamics |
| `src/assets/` | -- | -- | New | Shared | NIF loader |

### 8.2 Server-Specific Code

```
src/server/
    main_server.c          # Server-specific main() (no SDL3 window)
    server_console.c       # CLI interface for admin commands
    server_config.c        # Server-specific config (max players, etc.)
```

### 8.3 Build Configuration

```cmake
# CMakeLists.txt (simplified)

# Shared library: all platform-independent game code
add_library(openbc_core STATIC
    src/engine/engine.c
    src/engine/config.c
    src/engine/timer.c
    src/network/tg_network.c
    src/network/netfile.c
    src/network/gamespy_responder.c
    src/compat/handle_map.c
    src/compat/event_system.c
    src/compat/app_module.c
    src/scripting/python_host.c
)
target_link_libraries(openbc_core
    flecs::flecs_static
    enet::enet
    Python3::Python
)

# Server executable
if(OPENBC_SERVER_ONLY OR NOT OPENBC_SERVER_ONLY)
    add_executable(openbc_server
        src/server/main_server.c
        src/server/server_console.c
        src/server/server_config.c
    )
    target_link_libraries(openbc_server openbc_core)
    # No bgfx, no SDL3 (or SDL3 timer-only), no miniaudio, no RmlUi
endif()

# Client executable (Phase 3+)
if(NOT OPENBC_SERVER_ONLY)
    add_executable(openbc_client
        src/client/main_client.c
        src/render/bgfx_renderer.c
        src/assets/nif_loader.c
        src/audio/audio_system.c
        src/ui/rmlui_integration.c
        src/platform/sdl3_platform.c
    )
    target_link_libraries(openbc_client openbc_core
        bgfx::bgfx
        SDL3::SDL3
        miniaudio
        RmlUi::RmlUi
    )
endif()
```

---

## 9. Key Design Decisions

### 9.1 Server-Authoritative Model

The original BC multiplayer is server-authoritative for game state but client-
authoritative for movement (clients send their position, server rebroadcasts).
We preserve this model for compatibility, but the architecture allows future
server-authoritative movement for the new GNS protocol.

### 9.2 Tick Rate

30 Hz (33ms), matching the original. The original uses a Windows timer callback
with 33ms interval. Our implementation:
- Server: `usleep(33000)` with drift compensation
- Client (Phase 4): SDL3 timer or vsync-driven

### 9.3 Raw Sockets vs ENet

**Decision: Start with raw UDP sockets, matching the original TGNetwork wire format.**

Rationale: Vanilla BC clients speak the TGNetwork protocol, not ENet. The protocols
are almost certainly incompatible at the wire level. ENet adds its own headers,
channel IDs, and reliability layer. The original TGNetwork has a different packet
format with its own sequence numbers and ACK scheme.

This means we must reimplement:
- Packet framing (original header format)
- Sequence numbering per peer
- 3-tier send queues (unreliable, reliable, priority reliable)
- ACK tracking with retry logic
- Connection handshake

The ~225 functions in category 12 (TGNetwork) provide the reference.
The key functions to reimplement:
1. `CreateUDPSocket` (0x006b9b20) - bind + non-blocking
2. `HostOrJoin` (0x006b3ec0) - state machine setup
3. `Send` (0x006b4c10) - queue message to peer
4. `Update` (0x006b4560) - send/recv/dispatch per tick
5. `SendOutgoingPackets` (0x006b55b0) - serialize + sendto
6. `ProcessIncomingPackets` (0x006b5c90) - recvfrom + deserialize
7. `DispatchIncomingQueue` (0x006b5f70) - sequence validation
8. `ReliableACKHandler` (0x006b61e0) - ACK tracking

### 9.4 Checksum Strategy

For Phase 1, the server needs .pyc files (or their checksums) to verify against
clients. Options:

**Option A: Pre-compute checksums from BC install**
- Server reads .pyc files from the BC scripts/ directory
- Computes hashes using the same algorithm as the original (FUN_007202e0)
- Compares with client-reported hashes
- Pro: True compatibility verification
- Con: Requires BC install on server

**Option B: Accept all checksums**
- Server always reports "pass" regardless of client hashes
- Skip the actual file scanning and comparison
- Pro: No BC install needed on server
- Con: No actual verification (but this is a server choice)

**Decision: Implement Option A with Option B as a config flag (`skip_checksums=true`)**

The hash algorithm (FUN_007202e0) must be reverse-engineered to match exactly.
The decompiled code at that address provides the implementation.

### 9.5 Python Nesting Counter

The original game tracks a nesting counter at 0x0099EE38. When Python code calls
into C which calls back into Python, this counter prevents reentrancy issues.
`PyRun_String` requires the counter to be 0. We replicate this:

```c
static int g_python_nesting = 0;

// Before calling into Python:
if (g_python_nesting > 0) {
    // Queue the call for later (after current Python frame returns)
    return;
}
g_python_nesting++;
PyRun_String(code, ...);
g_python_nesting--;
```

---

## 10. Dependencies Between Subsystems

### 10.1 Build Order (Critical Path)

```
[Week 1-2]  flecs integration + ECS component registration
    |
    v
[Week 2-3]  Handle map + SWIG type system
    |
    +-------> [Week 3-4] Event system
    |              |
    v              v
[Week 3-5]  TGNetwork protocol engine (raw UDP)
    |              |
    v              v
[Week 4-5]  NetFile / checksum manager
    |
    v
[Week 4-6]  Python embedding + App/Appc module (stub)
    |
    v
[Week 5-7]  App/Appc function implementations (~297)
    |              |
    v              v
[Week 6-7]  GameSpy responder
    |
    v
[Week 7-8]  Server main loop + config + CLI
    |
    v
[Week 8-9]  Integration testing with vanilla client
    |
    v
[Week 9-10] Bug fixing, protocol debugging, polish
```

### 10.2 Parallelizable Work

These modules have no mutual dependencies and can be developed simultaneously:

| Track A | Track B | Track C |
|---------|---------|---------|
| TGNetwork protocol | Event system | Python embedding |
| GameSpy responder | Handle map | Compat shim |
| Checksum manager | Timer system | Config system |

### 10.3 Key Integration Points

1. **Network -> Events**: TGNetwork posts events into EventManager
2. **Events -> Handlers**: EventManager dispatches to C and Python handlers
3. **Handlers -> ECS**: Handlers modify flecs components via handle resolution
4. **Scripts -> API -> ECS**: Python calls App.Func(handle) -> resolve -> query/mutate
5. **ECS -> Network**: Game state changes trigger outbound network messages

---

## 11. Phase 1 Deliverables

### 11.1 Definition of Done

Phase 1 is complete when:

1. **LAN Discovery**: A vanilla BC client can discover the OpenBC server in
   the multiplayer browser via GameSpy LAN queries.

2. **Connection**: Client can connect to the server. Server assigns a player slot.

3. **Checksum Exchange**: Server sends 4 checksum requests (opcodes 0x20),
   client responds (0x21), server verifies and either passes or fails.

4. **Settings Delivery**: After checksums pass, server sends game settings
   (opcode 0x00) and status (opcode 0x01). Client enters ship selection screen.

5. **Multiple Clients**: Up to 16 simultaneous connections handled correctly.

6. **Keepalive**: Connected clients remain connected (no spurious timeouts).

7. **Disconnect Handling**: Client disconnect is detected and slot is freed.

8. **Python Scripts**: Server can load and execute the Multiplayer/ scripts
   (DedicatedServer.py pattern or equivalent automation).

9. **Cross-Platform**: Builds and runs on Linux (primary), Windows, macOS.

10. **Docker**: Server can run in a Docker container.

### 11.2 Test Plan

| Test | Method | Pass Criteria |
|------|--------|---------------|
| Server starts | Run binary | Binds port 22101, no crash |
| LAN discovery | BC client scan | Server appears in list with correct name |
| Client connect | BC client join | Connection established, slot assigned |
| Checksum exchange | BC client join | All 4 checksums pass (or fail correctly) |
| Settings received | BC client join | Client shows ship selection screen |
| Multi-client | 2+ BC clients | Both connect, separate slots |
| Disconnect | Kill BC client | Server detects, frees slot |
| Reconnect | Client rejoins | New slot assigned, fresh checksums |
| Max players | 17 clients | 17th rejected with correct reason |
| Keepalive | Client idle 60s | Still connected |
| Config | Custom port/name | Server uses configured values |
| Docker | Container run | Server accessible from host network |

### 11.3 Non-Goals for Phase 1

- Ship creation or game start (that is Phase 2)
- Any rendering or display
- Sound or music
- Bridge crew or single-player content
- Combat, weapons, damage
- AI
- Save/load

---

## 12. Risk Register

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| TGNetwork wire format incompatible with any existing library | HIGH | HIGH | Plan for raw UDP from the start; use packet captures as ground truth |
| Hash algorithm (FUN_007202e0) not perfectly replicated | MEDIUM | HIGH | Capture actual hash values from running game; test bit-for-bit |
| Python 1.5.2 scripts have syntax incompatible with shim | MEDIUM | MEDIUM | Offline migration tool; test with actual BC script corpus |
| GameSpy query format undocumented edge cases | LOW | MEDIUM | Capture actual queries from BC client; simple protocol |
| Event type numeric values don't match original | HIGH | HIGH | Extract all ET_* values from App.py (they are Appc constants, need actual integer values) |
| Original uses Windows-specific behavior (timer, socket) | LOW | MEDIUM | SDL3 abstracts timers; BSD sockets are portable |

---

## Appendix A: Wire Format Reference (from packet captures)

### TGNetwork Packet Header (preliminary, needs verification)

Based on decompiled analysis of SendOutgoingPackets (0x006b55b0) and
ProcessIncomingPackets (0x006b5c90):

```
Offset  Size  Field
0x00    1     Packet type (0x01=unreliable, 0x02=reliable, ...)
0x01    4     Sequence number (uint32, little-endian)
0x05    2     Payload length (uint16, little-endian)
0x07    N     Payload data
```

The reliable delivery retransmit pattern observed: 15-byte packets every ~5s
with format `[01/02] [4-byte seq] [data...]`.

### GameSpy Query Format

Standard GameSpy QR protocol (backslash-delimited):
```
Query:   \status\
Response: \gamename\stbc\hostname\OpenBC Server\numplayers\1\maxplayers\16\...
```

### Checksum Packet Formats

Request (Server->Client, opcode 0x20):
```
[0x20][index:u8][dir_len:u16][dir:bytes][filter_len:u16][filter:bytes][recursive:u8]
```

Response (Client->Server, opcode 0x21):
```
[0x21][index:u8][reference_hash(if idx==0):varies][dir_hash:u32][file_checksums:...]
```

Settings (Server->Client, opcode 0x00):
```
[0x00][gameTime:f32][setting1:u8][setting2:u8][playerSlot:u8]
[mapNameLen:u16][mapName:bytes][passFail:u8]
```

Status (Server->Client, opcode 0x01):
```
[0x01]
```

---

## Appendix B: Event Type Value Extraction

The actual integer values of ET_* constants must be extracted from the compiled
Appc module. From the decompiled code:

| Event | Hex Value | Source |
|-------|-----------|--------|
| ET_NETWORK_MESSAGE_EVENT | 0x60001 | Confirmed in decompiled code |
| ET_NETWORK_CONNECT_EVENT | 0x60002 | Confirmed (host session event) |
| ET_CHECKSUM_COMPLETE | 0x8000e8 | Confirmed in decompiled code |
| ET_SYSTEM_CHECKSUM_FAILED | 0x8000e7 | Confirmed in decompiled code |
| ET_KILL_GAME | 0x8000e9 | Confirmed in decompiled code |
| ET_START | 0x800053 | From App.py docs |
| ET_CREATE_SERVER | 0x80004A | From App.py docs |

Remaining ET_* values (ET_NETWORK_NEW_PLAYER, ET_NETWORK_DELETE_PLAYER, etc.)
must be extracted from a running BC instance or from the Appc.pyd binary.

---

## Appendix C: Phase 1 SWIG API Function Categories

Detailed breakdown of the ~297 functions needed:

### Network (47 functions)
- new_TGWinsockNetwork, TGWinsockNetwork_SetPortNumber
- TGNetwork_Connect, Disconnect, Update, Send
- TGNetwork_IsHost, GetHostID, GetLocalID, GetNumPlayers
- TGNetwork_GetConnectStatus, GetPlayerList
- TGNetwork_SetConnectionTimeout, SetSendTimeout
- TGNetwork_GetBootReason, SetBootReason
- TGNetwork_GetName, SetName, GetPassword, SetPassword
- TGNetwork_AddClassHandlers, RegisterHandlers
- TGNetwork_SendTGMessage, GetNextMessage
- TGNetwork_ReceiveMessageHandler
- TGNetwork_RegisterMessageType
- etc.

### Events (15 functions)
- TGEvent_Create, SetEventType, SetDestination
- TGEventHandlerObject_Cast, ProcessEvent, CallNextHandler
- TGEventHandlerObject_AddPythonFuncHandlerForInstance
- TGEventHandlerObject_AddPythonMethodHandlerForInstance
- TGEventHandlerObject_RemoveAllInstanceHandlers
- TGEventManager_AddBroadcastPythonFuncHandler
- TGEventManager_AddBroadcastPythonMethodHandler
- TGEventManager_AddEvent
- TGEventManager_RemoveAllBroadcastHandlersForObject
- etc.

### Multiplayer (12 functions)
- Game_GetCurrentGame
- Game_LoadEpisode
- MultiplayerGame_Cast, Create
- MultiplayerGame_GetNumberPlayersInGame
- MultiplayerGame_IsReadyForNewPlayers
- MultiplayerGame_SetMaxPlayers
- MultiplayerGame_SetReadyForNewPlayers
- new_MultiplayerGame
- LoadEpisodeAction_Create, Play
- etc.

### Config/Var (20 functions)
- TGConfigMapping_GetIntValue, GetStringValue, GetFloatValue
- TGConfigMapping_SetIntValue, SetStringValue, SetFloatValue
- TGConfigMapping_HasValue, LoadConfigFile, SaveConfigFile
- VarManagerClass_SetStringVariable, GetStringVariable
- VarManagerClass_SetFloatVariable, GetFloatVariable
- VarManagerClass_DeleteAllScopedVariables
- VarManagerClass_MakeEpisodeEventType
- etc.

### Message/Stream (20 functions)
- TGMessage_Create, GetBufferStream
- TGBufferStream methods: ReadChar, ReadInt, ReadFloat, ReadString
- TGBufferStream methods: WriteChar, WriteInt, WriteFloat, WriteString
- TGBufferStream_Close
- etc.

### String (3 functions)
- new_TGString, delete_TGString
- TGString conversion helpers

### Module/Window (20 functions)
- UtopiaModule_InitializeNetwork, GetNetwork, CreateGameSpy
- UtopiaModule_GetCaptainName, SetGameName
- UtopiaModule_GetDataPath, GetCurrentFriendlyFire
- TopWindow_GetTopWindow, FindMainWindow
- TopWindow_SetupMultiplayerGame
- etc.

### Constants (~145)
- All ET_* event types used by multiplayer scripts
- CT_* class types (CT_SHIP, CT_OBJECT, etc.)
- MWT_* main window types (MWT_MULTIPLAYER = 8)
- TGNetwork boot reason constants
- MAX_MESSAGE_TYPES
- NULL_ID
- etc.
