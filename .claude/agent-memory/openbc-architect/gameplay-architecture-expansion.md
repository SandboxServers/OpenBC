# Phase 1 Expansion: Playable Dedicated Server -- Architectural Assessment

## Status: DESIGN COMPLETE -- 2026-02-08

---

## 1. Revised System Diagram

### Previous (Lobby-Only) Architecture

The lobby-only design terminated at the ship selection screen. The server's
responsibility ended after sending opcode 0x00 (settings) and opcode 0x01
(status). No game messages were relayed, no ship objects existed, no scoring
ran.

### New (Playable Server) Architecture

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
        |   | Engine     | | Model   | | State Mach. |     |
        |   +------+-----+ +---+-----+ +---+---------+    |
        |          |            |            |              |
        +----------+--------+--+------------+--------------+
                             |
                      +------v-------+
                      |   flecs ECS  |
                      |   World      |
                      +--------------+
```

### New Modules Beyond Lobby Design

| New Module | Purpose | Files |
|------------|---------|-------|
| **Message Relay Engine** | Forward game messages between peers; host copies received messages to other players | `src/network/relay.h`, `relay.c` |
| **Ship Object Model** | Lightweight ship entities with queryable components (no physics/rendering) | `src/compat/ship_api.c`, `src/engine/ship_components.h` |
| **Game Lifecycle FSM** | State machine: LOBBY -> SHIP_SELECT -> LOADING -> PLAYING -> GAME_OVER -> RESTARTING | `src/engine/game_state.h`, `game_state.c` |
| **Game Object Manager** | Create/destroy game objects, manage sets/systems | `src/compat/gameobject_api.c`, `src/compat/set_api.c` |
| **Score System Support** | API surface for Python scoring scripts (damage events, kill tracking) | `src/compat/combat_event_api.c` |
| **Peer Groups** | "NoMe" and team groups for targeted message delivery | `src/network/peer_groups.h`, `peer_groups.c` |
| **Localization Stubs** | TGLocalizationManager for .tgl database loading (stub: return empty strings) | `src/compat/localization_api.c` |
| **Episode/Mission System** | LoadEpisode, GetCurrentMission, mission script lifecycle | `src/compat/mission_api.c` |

---

## 2. Message Relay Engine

### How BC Multiplayer Messaging Works

Bridge Commander multiplayer is **NOT peer-to-peer**. It is **host-relayed
peer-to-peer**. Every game message passes through the host:

1. Client A sends a message to the host (via `SendTGMessage(hostID, msg)`)
2. Host's `ReceiveMessageHandler` receives it via `ET_NETWORK_MESSAGE_EVENT`
3. Python scripts on the host decide what to do:
   - Chat messages: host copies the message and sends to all other peers
     via `SendTGMessageToGroup("NoMe", copy)`
   - Team chat: host checks group membership, forwards selectively
   - Scoring: host processes the event, computes scores, broadcasts updates
   - Ship selection: host validates, creates ship object, broadcasts
4. The engine-level `ReceiveMessageHandler` also handles
   certain opcodes (0x00-0x1F) at the C++ level, forwarding position
   updates, firing events, etc.

### Key Evidence From Scripts

From `MultiplayerMenus.py` line 2276-2279:
```python
if (App.g_kUtopiaModule.IsHost()):
    # I'm the host, forward this message to everybody else.
    pNewMessage = pMessage.Copy()
    pNetwork.SendTGMessageToGroup("NoMe", pNewMessage)
```

This pattern repeats across all multiplayer scripts. The host is always
the relay point.

### Architectural Placement

The message relay is NOT a new standalone subsystem. It operates at two levels:

**Level 1: C++ Engine (ReceiveMessageHandler)**
- Handles opcodes 0x00-0x1F (game state messages)
- Position updates, firing events, cloak/warp state changes
- These are handled in the engine C++ code and rebroadcast to peers
- For Phase 1: we must implement this handler's relay logic

**Level 2: Python Scripts**
- Chat messages (CHAT_MESSAGE, TEAM_CHAT_MESSAGE)
- Score messages (SCORE_CHANGE_MESSAGE, SCORE_MESSAGE)
- Mission init messages (MISSION_INIT_MESSAGE)
- Game end/restart messages (END_GAME_MESSAGE, RESTART_GAME_MESSAGE)
- These are handled by Python event handlers, which call SendTGMessage/SendTGMessageToGroup

### Relay Architecture

```
Incoming ET_NETWORK_MESSAGE_EVENT (0x60001)
    |
    v
[Peek opcode byte]
    |
    +-- 0x20-0x27 --------> NetFile handler (checksum/file xfer) [EXISTING]
    |
    +-- 0x00-0x1F --------> MultiplayerGame::ReceiveMessageHandler [NEW]
    |                              |
    |                    [Engine opcode dispatch table]
    |                              |
    |                    +--- Position/orientation updates: rebroadcast
    |                    +--- Firing start/stop: rebroadcast + fire event
    |                    +--- Ship creation ack: update ship object model
    |                    +--- Cloak/warp: rebroadcast + fire event
    |                    +--- Settings (0x00): process locally
    |                    +--- Status (0x01): process locally
    |
    +-- Custom (>= MAX_MESSAGE_TYPES + 10) --> Python handlers [NEW]
                                                    |
                                         [Script ProcessMessageHandler]
                                                    |
                                         Host relay: Copy + SendToGroup("NoMe")
```

### Implementation: Message Relay in C

```c
// src/network/relay.h

typedef struct obc_relay_t {
    tg_network_t *network;
    ecs_world_t  *world;
    // Peer group registry
    struct {
        char name[32];
        uint32_t peer_ids[16];
        int count;
    } groups[8];
    int num_groups;
} obc_relay_t;

// Forward a message to all peers except the sender
void obc_relay_to_group(obc_relay_t *relay, const char *group_name,
                        uint32_t sender_id, tg_message_t *msg);

// Forward a message to a specific peer
void obc_relay_to_peer(obc_relay_t *relay, uint32_t peer_id,
                       tg_message_t *msg);

// Register/unregister peer groups
void obc_relay_add_group(obc_relay_t *relay, const char *name);
void obc_relay_add_to_group(obc_relay_t *relay, const char *name,
                            uint32_t peer_id);
void obc_relay_remove_from_group(obc_relay_t *relay, const char *name,
                                 uint32_t peer_id);
```

### The "NoMe" Group

The "NoMe" group is automatically maintained by the engine. It contains all
peers except the host. When a new player connects, they are added. When they
disconnect, they are removed. `SendTGMessageToGroup("NoMe", msg)` sends to
all connected clients.

This maps to `TGNetwork_SendTGMessageToGroup(net, "NoMe", msg)` in the SWIG API.

### Engine-Level Relay (ReceiveMessageHandler Opcodes 0x00-0x1F)

The C++ ReceiveMessageHandler uses a dispatch table to handle
game opcodes. For a dedicated server, the critical opcodes are:

| Opcode | Name | Server Action |
|--------|------|---------------|
| 0x00 | Settings | Process locally (already implemented in lobby) |
| 0x01 | Status | Process locally (already implemented in lobby) |
| 0x02 | Ship selection | Client chose ship; server creates ship object, rebroadcasts |
| 0x03 | Position update | Rebroadcast to all other peers |
| 0x04 | Orientation update | Rebroadcast to all other peers |
| 0x05 | Fire weapon | Rebroadcast + fire ET_START_FIRING on ship |
| 0x06 | Stop firing | Rebroadcast + fire ET_STOP_FIRING |
| 0x07 | Torpedo type change | Rebroadcast |
| 0x08 | Cloak start | Rebroadcast + fire ET_START_CLOAKING |
| 0x09 | Cloak stop | Rebroadcast + fire ET_STOP_CLOAKING |
| 0x0A | Warp start | Rebroadcast |
| 0x0B-0x1F | Various | Rebroadcast (many are simple relay) |

**Key insight**: Most of these opcodes are pure relay on the server. The
server does not simulate physics or validate positions. It trusts the
clients (client-authoritative movement). The server's role is:

1. Receive the message from one client
2. Copy it
3. Send the copy to all other clients
4. Optionally fire an ECS event (for scoring scripts to observe)

This makes Phase 1 gameplay significantly simpler than it first appears.

### Critical Risk: Opcode Dispatch Table

The opcode dispatch table determines which function handles each
opcode. We need to understand this table to know:
- Which opcodes are just relay (copy + rebroadcast)
- Which opcodes trigger events (fire ET_OBJECT_EXPLODING, etc.)
- Which opcodes modify server state (ship creation, etc.)

**Mitigation**: Start by treating ALL opcodes 0x02-0x1F as pure relay
(copy + rebroadcast to "NoMe"). Then incrementally add event firing for
opcodes that scoring scripts need to observe (explosions, weapon hits).

---

## 3. Ship Object Model

### What Scripts Need

From analysis of Multiplayer/Episode/Mission1/Mission1.py:

```python
pShip = App.ShipClass_Cast(pEvent.GetDestination())
pShip.IsPlayerShip()       # Is this a player-controlled ship?
pShip.GetNetPlayerID()     # Which player owns this ship?
pShip.GetObjID()           # Unique object ID
pShip.GetNetType()         # Ship type (species index)
pShip.GetName()            # Ship display name
pShip.IsDying()            # Is ship being destroyed?
pShip.IsDead()             # Has ship been destroyed?

pGame = App.MultiplayerGame_Cast(App.Game_GetCurrentGame())
pGame.GetShipFromPlayerID(playerID)  # Look up ship by owner
pGame.DeletePlayerShipsAndTorps()    # Clear all player ships
```

### ECS Component Design

Ships do NOT need physics, rendering, or AI in Phase 1. They need:

```c
// Ship identity component (on ship entities)
typedef struct OBC_ShipIdentity {
    uint32_t obj_id;          // Unique object ID
    uint32_t net_player_id;   // Owning player's network ID
    uint32_t net_type;        // Ship species/type index
    char     name[128];       // Ship display name
    bool     is_player_ship;  // Always true for MP ships
} OBC_ShipIdentity;

// Ship state component (minimal for relay server)
typedef struct OBC_ShipState {
    bool     is_dying;        // Ship exploding animation
    bool     is_dead;         // Ship fully destroyed
    bool     is_cloaked;      // Cloak state
    bool     is_warping;      // Warp state
} OBC_ShipState;

// Player-ship link (on player entities, references ship entity)
typedef struct OBC_PlayerShipLink {
    ecs_entity_t ship_entity; // The ship this player controls
    uint32_t     ship_obj_id; // For quick lookup
} OBC_PlayerShipLink;
```

### Handle Map Integration

Ship entities get handles like other objects:

```c
OBC_HANDLE_SHIP,            // ShipClass
OBC_HANDLE_PHYSICS_OBJECT,  // PhysicsObjectClass (parent of ShipClass)
```

Type hierarchy for ships:
```
ShipClass IS-A PhysicsObjectClass IS-A ObjectClass IS-A TGObject
```

When a Python script calls `App.ShipClass_Cast(obj)`, the handle map checks:
- Is the handle type OBC_HANDLE_SHIP? Accept.
- Is it a supertype? Accept with cast.
- Otherwise reject.

### Ship Lifecycle

```
[Client sends ship selection message (opcode 0x02)]
    |
    v
[ReceiveMessageHandler processes opcode 0x02]
    1. Parse: species index, player ID
    2. Create flecs entity with OBC_ShipIdentity + OBC_ShipState
    3. Create handle: "_HEXID_p_ShipClass"
    4. Link player entity to ship entity (OBC_PlayerShipLink)
    5. Fire ET_OBJECT_CREATED_NOTIFY event
    6. Fire ET_NEW_PLAYER_IN_GAME event
    7. Rebroadcast ship creation to all other peers
    |
    v
[Ship exists in ECS, queryable by Python scripts]
    |
    v
[Client ship explodes (ET_OBJECT_EXPLODING from relay)]
    1. Set ship.is_dying = true
    2. Fire ET_OBJECT_EXPLODING event (scoring scripts observe this)
    3. Scoring Python handler (ObjectKilledHandler) processes scores
    |
    v
[Ship destroyed]
    1. Set ship.is_dead = true
    2. Fire ET_DELETE_OBJECT event
    3. Remove handle
```

### MultiplayerGame Ship Tracking

```c
// In the MultiplayerGame component:
typedef struct OBC_MultiplayerState {
    // ... existing fields ...

    // Ship tracking (new for gameplay)
    struct {
        ecs_entity_t entity;
        uint32_t     player_id;
        uint32_t     obj_id;
        bool         active;
    } ships[16];             // One per player slot
    int num_ships;
} OBC_MultiplayerState;
```

`MultiplayerGame_GetShipFromPlayerID(mg, playerID)` iterates this array
and returns the ship handle.

---

## 4. Game Lifecycle State Machine

### State Diagram

```
                    Server Start
                         |
                         v
                   +-----------+
                   |   LOBBY   |<---------------------------+
                   | (waiting  |                            |
                   |  for join)|                            |
                   +-----+-----+                            |
                         |                                  |
                   [Checksum pass]                          |
                         |                                  |
                   +-----v--------+                         |
                   | SHIP_SELECT  |                         |
                   | (players     |                         |
                   |  choosing)   |                         |
                   +-----+--------+                         |
                         |                                  |
                  [Host clicks Start]                       |
                         |                                  |
                   +-----v--------+                         |
                   |   LOADING    |                         |
                   | (creating    |                         |
                   |  system/ships)|                        |
                   +-----+--------+                         |
                         |                                  |
                   [All created]                            |
                         |                                  |
                   +-----v--------+                         |
                   |   PLAYING    |                         |
                   | (relay,      |                         |
                   |  scoring)    |                         |
                   +-----+--------+                         |
                         |                                  |
              [Time/frag limit OR host ends]                |
                         |                                  |
                   +-----v--------+                         |
                   |  GAME_OVER   |                         |
                   | (scores      |                         |
                   |  displayed)  |                         |
                   +-----+--------+                         |
                         |                                  |
                  [Host clicks Restart]                     |
                         |                                  |
                   +-----v--------+                         |
                   |  RESTARTING  |                         |
                   | (cleanup,    |-------->  SHIP_SELECT --+
                   |  reset)      |    OR     LOBBY
                   +--------------+
```

### State Implementation

```c
// src/engine/game_state.h

typedef enum obc_game_state_t {
    OBC_STATE_LOBBY = 0,       // Waiting for players, checksum exchange
    OBC_STATE_SHIP_SELECT,     // Players choosing ships (mission loaded)
    OBC_STATE_LOADING,         // Creating system, spawning ships
    OBC_STATE_PLAYING,         // Active gameplay, message relay, scoring
    OBC_STATE_GAME_OVER,       // Match ended, scores displayed
    OBC_STATE_RESTARTING,      // Cleanup, transition back
} obc_game_state_t;

typedef struct obc_game_lifecycle_t {
    obc_game_state_t state;
    obc_game_state_t previous_state;
    double           state_enter_time;   // When we entered this state
    double           time_limit;         // 0 = no limit
    int              frag_limit;         // -1 = no limit
    int              score_limit;        // -1 = no limit
    bool             game_over;          // Global flag scripts check
} obc_game_lifecycle_t;
```

### State Transitions (who triggers them)

| Transition | Trigger | Source |
|------------|---------|--------|
| LOBBY -> SHIP_SELECT | `TopWindow_SetupMultiplayerGame()` + `LoadEpisode()` called by automation | Server init / Python |
| SHIP_SELECT -> LOADING | Host clicks "Start" button (dedicated: automation or API call) | Python script |
| LOADING -> PLAYING | All ships created, system built | C engine |
| PLAYING -> GAME_OVER | Time limit expired, frag limit reached, or host calls `EndGame()` | Python script |
| GAME_OVER -> RESTARTING | Host clicks "Restart" or `RestartGameHandler` fires | Python script |
| RESTARTING -> SHIP_SELECT | Cleanup complete, ships cleared | C engine |

### State-Dependent Behavior

| State | Message Relay | Accept New Players | Run Scoring | Ship Creation |
|-------|---------------|-------------------|-------------|---------------|
| LOBBY | No | Yes (checksum) | No | No |
| SHIP_SELECT | Partial (chat) | Yes | No | Yes (per player) |
| LOADING | Partial (chat) | No | No | Yes (batch) |
| PLAYING | Full | Yes (late join) | Yes | Yes (respawn) |
| GAME_OVER | Partial (chat) | No | No | No |
| RESTARTING | No | No | No | No (clearing) |

### Dedicated Server Automation

The original DedicatedServer.py handles automation for states that normally
require UI interaction. For a dedicated server (IsHost=1, IsClient=0):

- **LOBBY**: Automatic. Players connect via network.
- **SHIP_SELECT -> LOADING**: The original has a "Start" button the host
  clicks. For dedicated, scripts can auto-start when enough players are
  ready, or a timer triggers game start.
- **PLAYING**: Automatic. Python scoring scripts run on events.
- **GAME_OVER**: Automatic after time/frag limit.
- **RESTARTING**: Automatic. `RestartGameHandler` clears ships, returns
  to ship select.

---

## 5. Revised API Surface Estimate

### Previous Estimate: ~297 functions (lobby-only)

### New Estimate: ~512 functions (playable server)

| Category | Lobby-Only | Gameplay Added | New Total | Notes |
|----------|-----------|----------------|-----------|-------|
| TGNetwork/WSN | 47 | 5 | 52 | +SendTGMessageToGroup, AddGroup, DeleteGroup, GetGroup, CreateLocalPlayer |
| TGEvent/EventManager | 15 | 5 | 20 | +More typed event classes (WeaponHitEvent, ObjectExplodingEvent, NewPlayerInGameEvent) |
| MultiplayerGame | 12 | 10 | 22 | +GetShipFromPlayerID, DeletePlayerShipsAndTorps, DeleteObjectFromGame, IsPlayerInGame, GetPlayerName, GetPlayerNumberFromID, GetMaxPlayers, IsPlayerUsingModem |
| TGConfigMapping | 12 | 0 | 12 | Unchanged |
| VarManager | 8 | 0 | 8 | Unchanged |
| TGString | 3 | 0 | 3 | Unchanged |
| UtopiaModule | 15 | 8 | 23 | +GetGameTime, GetRealTime, SetFriendlyFireWarningPoints, IsClient |
| TopWindow | 5 | 0 | 5 | Unchanged |
| Game | 5 | 3 | 8 | +GetPlayer, LoadDatabaseSoundInGroup (stub) |
| TGMessage/Stream | 20 | 5 | 25 | +TGMessage_Copy, SetDataFromStream, WriteLong, ReadLong, ReadShort |
| Misc (Timer, Object) | 10 | 0 | 10 | Unchanged |
| **NEW: Ship/Physics** | 0 | 35 | 35 | ShipClass_Cast, GetNetPlayerID, IsPlayerShip, GetObjID, GetNetType, GetName, IsDying, IsDead, PhysicsObjectClass_* (stubs) |
| **NEW: Game Objects** | 0 | 15 | 15 | ObjectClass_GetName, IsTypeOf, SetClass_*, SetManager_* |
| **NEW: Episode/Mission** | 0 | 20 | 20 | MissionClass_GetScript, GetMission, GetEnemyGroup, GetFriendlyGroup, NameGroup_* |
| **NEW: Set/System** | 0 | 15 | 15 | SetManager_GetSet, ClearRenderedSet, DeleteAllSets, SetClass_AddObjectToSet, etc. |
| **NEW: Localization** | 0 | 8 | 8 | TGLocalizationManager_Load, Unload, GetString |
| **NEW: Combat Events** | 0 | 15 | 15 | WeaponHitEvent_GetFiringPlayerID, GetDamage, IsHullHit, ObjectExplodingEvent_* |
| **NEW: Sequence/Action** | 0 | 10 | 10 | TGSequence_Create, AddAction, Play, Completed (stubs for server) |
| **NEW: UI stubs** | 0 | 80 | 80 | STButton_*, TGPane_*, TGParagraph_*, SubtitleAction_*, TGIcon_* (all no-ops) |
| **NEW: Player List ext.** | 0 | 8 | 8 | TGPlayerList_GetNumPlayers, GetPlayerAtIndex, GetPlayer, TGNetPlayer_GetNetID, GetName |
| Constants | 145 | 50 | 195 | +ET_OBJECT_EXPLODING, ET_WEAPON_HIT, ET_NEW_PLAYER_IN_GAME, ET_DELETE_OBJECT_PUBLIC, ET_OBJECT_CREATED_NOTIFY, CT_SHIP, SPECIES_*, MAX_FLYABLE_SHIPS, etc. |
| **TOTAL** | **~297** | **~282** | **~579** | Approximately doubled |

### Function Classification

Of the ~282 new functions:
- **~80 need real behavior** (ship queries, message relay, scoring events, episode/mission lifecycle)
- **~50 are simple stubs** (return None/0, or store a value in ECS without side effects)
- **~80 are UI no-ops** (return None, do nothing -- server has no display)
- **~72 are event/constant definitions** (just numeric values)

The actual implementation work is concentrated in ~80 functions with real
behavior, which is comparable to the original ~185 fully-implemented
functions in the lobby scope.

---

## 6. Revised Implementation Timeline

### Original Timeline: 10 chunks, ~8-10 weeks

### Revised Timeline: 13 chunks, ~12-14 weeks

The original 10 chunks remain with minor modifications. Three new chunks
are added:

### NEW Chunk 11: Ship Object Model & Game Object API
**Track**: A | **Duration**: ~5-7 days | **Dependencies**: Chunks 3, 4, 9

**Deliverables**:
- `src/engine/ship_components.h` - Ship ECS components
- `src/compat/ship_api.c` - ShipClass_*, PhysicsObjectClass_* SWIG functions
- `src/compat/gameobject_api.c` - ObjectClass_*, SetClass_*, SetManager_* functions
- `src/compat/combat_event_api.c` - WeaponHitEvent, ObjectExplodingEvent typed events

**Implementation**:
- Register OBC_ShipIdentity, OBC_ShipState, OBC_PlayerShipLink components
- Add OBC_HANDLE_SHIP, OBC_HANDLE_PHYSICS_OBJECT to handle type enum
- Implement type hierarchy: ShipClass -> PhysicsObjectClass -> ObjectClass -> TGObject
- ~35 ship query functions (real behavior)
- ~15 game object management functions
- ~15 combat event functions

**Acceptance**: Python script can create a ship entity, query its properties
(GetNetPlayerID, IsPlayerShip, GetObjID), cast it (ShipClass_Cast), and look
it up via MultiplayerGame_GetShipFromPlayerID.

---

### NEW Chunk 12: Message Relay & Game Lifecycle
**Track**: B | **Duration**: ~5-7 days | **Dependencies**: Chunks 5, 9, 11

**Deliverables**:
- `src/network/relay.h` / `relay.c` - Message relay engine
- `src/network/peer_groups.h` / `peer_groups.c` - Named peer groups ("NoMe", teams)
- `src/engine/game_state.h` / `game_state.c` - Game lifecycle state machine
- `src/compat/mission_api.c` - Episode/Mission/NameGroup SWIG functions
- `src/compat/localization_api.c` - TGLocalizationManager stubs

**Implementation**:
- ReceiveMessageHandler opcode dispatch (0x00-0x1F)
- Default relay: copy + rebroadcast to "NoMe" group
- Event-triggering opcodes: fire ET_OBJECT_EXPLODING, ET_WEAPON_HIT, etc.
- Ship creation from opcode 0x02 (create entity + rebroadcast)
- State machine: LOBBY -> SHIP_SELECT -> LOADING -> PLAYING -> GAME_OVER
- Episode loading (call Python Initialize/Terminate on mission scripts)
- "NoMe" group auto-management (add on connect, remove on disconnect)

**Acceptance**: Two vanilla BC clients can:
1. Connect to server
2. Select ships
3. Enter gameplay
4. Chat messages relay correctly
5. Ship explosions fire scoring events
6. Game ends when time/frag limit reached

---

### NEW Chunk 13: UI Stubs & Full Integration
**Track**: C | **Duration**: ~3-5 days | **Dependencies**: Chunks 9, 11, 12

**Deliverables**:
- `src/compat/ui_stubs.c` - ~80 UI function stubs (all no-ops)
- `src/compat/sequence_stubs.c` - TGSequence, TGSoundAction, SubtitleAction stubs
- `src/compat/extended_constants.h` - Gameplay-related constants

**Implementation**:
- All UI functions return None/NULL (server has no display)
- TGSequence_Create returns a valid handle but Play() is a no-op
- SubtitleAction_Create, TGSoundAction_Create return valid handles
- These stubs prevent Python script crashes when UI code paths execute

**Acceptance**: All 5 vanilla multiplayer mission scripts (Mission1-Mission5)
can Initialize() without Python errors on the server.

---

### Revised Critical Path

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
                         +---> Chunk 11 (Ship Objects) <--- Chunk 3
                         |          |
                         +---> Chunk 12 (Relay + FSM) <--- Chunks 5, 11
                         |          |
                         +---> Chunk 13 (UI Stubs)
                                    |
                                    v
                              Chunk 10 (Integration) <--- All chunks
```

**New critical path**: Chunk 1 -> 8 -> 9 -> 11 -> 12 -> 10
**New longest path**: ~12-14 weeks (vs 8-10 weeks for lobby-only)

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
| 10 | Integration | 7-10 days | Final |
| **11** | **Ship Objects** | **5-7 days** | **Sequential** |
| **12** | **Relay + FSM** | **5-7 days** | **Sequential** |
| **13** | **UI Stubs** | **3-5 days** | **Parallel with 12** |

**Total estimated effort**: 62-87 developer-days (vs 44-63 for lobby-only)
**Calendar time with parallelism**: 12-14 weeks with one developer

---

## 7. Risk Assessment

### New Risks from Gameplay Expansion

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **Opcode dispatch table unknown** | HIGH | HIGH | Start with pure relay for all opcodes 0x02-0x1F; incrementally add event firing as needed. Vanilla BC will work with just relay. |
| **Ship creation protocol unknown** | MEDIUM | HIGH | Capture wire packets from vanilla game during ship selection. The ship creation message format must be reverse-engineered. |
| **Python scoring scripts fail** | MEDIUM | MEDIUM | Test incrementally: first get mission scripts to load without errors (UI stubs), then get event handlers to fire, then validate scoring logic. |
| **UI stub coverage insufficient** | HIGH | MEDIUM | Scripts call many UI functions. Missing stubs cause AttributeError. Mitigation: build comprehensive UI function list from App.py (14K lines) and stub all of them. A missing stub is easy to add when hit. |
| **Message ordering sensitivity** | LOW | MEDIUM | Some messages may need to arrive in order (ship create before position update). The reliable channel preserves order. If unreliable messages arrive out of order, the client handles this (they already do in vanilla). |
| **Late-join state sync** | MEDIUM | HIGH | When a player joins mid-game, they need the current game state. The original handles this via `InitNetwork(iToID)` in the Python scripts, which sends mission init + scores. We must ensure this Python path works. |
| **Type hierarchy complexity** | MEDIUM | MEDIUM | ShipClass -> PhysicsObjectClass -> ObjectClass -> TGObject is a 4-level inheritance chain. Every level needs correct is-a checking in the handle map. |
| **Dedicated server flag differences** | LOW | HIGH | Scripts check `IsHost() and not IsClient()` for dedicated server paths. These paths skip UI construction. We must ensure these branches execute correctly. |

### Risks Carried Forward (unchanged)

| Risk | Probability | Impact |
|------|-------------|--------|
| Wire format mismatch | HIGH | HIGH |
| Hash algorithm mismatch | MEDIUM | HIGH |
| Python 1.5.2 compat gaps | MEDIUM | MEDIUM |
| ET_* values unknown | HIGH | HIGH |
| Priority queue ACK stall | HIGH | HIGH |

---

## 8. Phase 2 Boundary Redefinition

### Old Phase Boundaries

| Phase | Scope |
|-------|-------|
| Phase 1 | Lobby-only server (connect, checksum, ship select screen) |
| Phase 2 | Full game logic (ships, combat, AI, scoring) |
| Phase 3 | Rendering client (bgfx, NIF) |
| Phase 4 | Full client (UI, audio, input) |

### New Phase Boundaries

| Phase | Scope | Key Difference |
|-------|-------|----------------|
| **Phase 1** | **Playable relay server** (connect, checksum, ship select, message relay, scoring, game lifecycle) | Server is a functional game server; clients can play matches |
| **Phase 2** | **Server-authoritative simulation** (physics, collision, damage model, AI, authoritative position) | Server validates/computes game state instead of trusting clients |
| Phase 3 | Rendering client (bgfx, NIF) | Unchanged |
| Phase 4 | Full client (UI, audio, input) | Unchanged |

### What is LEFT for Phase 2

Phase 2 becomes the "server-authoritative" upgrade. Phase 1 trusts clients
(client-authoritative, just relay). Phase 2 adds server-side validation:

| Feature | Phase 1 (Relay) | Phase 2 (Authoritative) |
|---------|----------------|------------------------|
| Position/movement | Client sends, server relays blindly | Server simulates physics, validates positions |
| Weapon fire | Client says "I fired", server relays | Server validates firing angles, ammo, cooldowns |
| Damage | Client reports damage via events | Server computes damage from physics collision |
| Collision | Not checked server-side | Server runs collision detection |
| Ship state | Stored as simple flags (is_dying) | Full ship systems (shields, hull, subsystems, power) |
| AI | None | Server runs AI for NPC ships |
| Anti-cheat | None (client-authoritative) | Server validates all state changes |
| Mod support | Vanilla scripts only | Custom ship definitions, balanced gameplay |

### Phase 2 New API Functions (~400-600 additional)

| Category | Count | Examples |
|----------|-------|---------|
| Physics simulation | ~80 | GetVelocity, SetVelocity, ApplyForce, GetMass |
| Ship systems | ~100 | GetShieldStrength, GetHullCondition, GetSubsystemHealth |
| Weapon systems | ~60 | GetPhaserDamage, GetTorpedoSpeed, FireWeapon |
| AI | ~80 | SetAITarget, GetAIState, CreateAIController |
| Collision | ~30 | GetCollisionShape, CheckCollision |
| Scene graph | ~50 | GetPosition, GetOrientation, SetPosition |
| Ship properties | ~50 | LoadHardpoints, GetPropertyManager |
| Subsystems | ~50 | GetSubsystem, RepairSubsystem, DisableSubsystem |

### Phase 2 ECS Additions

```c
// Full physics components (Phase 2)
Transform, PhysicsBody, ShipFlightParams, ThrottleInput
WarpState, CloakState, ShieldState, HullState
SubsystemState, PowerState, CollisionShape, DamageEvent

// AI components (Phase 2)
AIController, AITarget, AIBehavior, AIFormation

// Weapon components (Phase 2)
WeaponMount, PhaserState, TorpedoLauncher, TorpedoInFlight
```

### Phase 2 Library Additions

- **JoltC or custom physics**: 6DOF ship dynamics, collision detection
- Ship flight model with thrust, rotation, mass, inertia
- Damage model with shield/hull/subsystem degradation

### Clear Phase Boundary Test

**Phase 1 is complete when**: Two vanilla BC clients can connect to the
OpenBC server, complete checksums, select ships, play a deathmatch, see
kills/deaths tracked, and the game ends when the frag limit is reached.
The server relays all messages; clients do all simulation locally.

**Phase 2 is complete when**: The server independently simulates ship
physics, computes damage from weapon hits, and clients receive
authoritative position/state updates. A hacked client that sends false
position data is corrected by the server.

---

## 9. Compatibility Notes

### IsHost/IsClient for Dedicated Server

Scripts use this pattern to detect dedicated server mode:
```python
if App.g_kUtopiaModule.IsHost() and not App.g_kUtopiaModule.IsClient():
    # This is a dedicated server, skip UI construction
```

Our server must return:
- `IsHost()` = True (1)
- `IsClient()` = False (0)
- `IsMultiplayer()` = True (1)

This is critical: many script branches create UI only when `IsClient()` is
true. On the dedicated server, these branches are skipped, which is correct
behavior and also means many UI functions will never be called on the server.

### SendTGMessage Destination 0

When `pNetwork.SendTGMessage(0, msg)` is called with destination 0, the
original engine broadcasts to ALL connected peers (including self on a
listen server, excluding self on dedicated). This is the "broadcast"
semantic.

### MAX_MESSAGE_TYPES Constant

Scripts define custom message types as offsets from `App.MAX_MESSAGE_TYPES`:
```python
MISSION_INIT_MESSAGE = App.MAX_MESSAGE_TYPES + 10
SCORE_CHANGE_MESSAGE = App.MAX_MESSAGE_TYPES + 11
```

The value of `MAX_MESSAGE_TYPES` must match the original (extracted from
Appc.pyd). These custom message types are carried in the first byte of
the TGMessage payload and are interpreted entirely by Python scripts.

### NameGroup for Team Chat

Team chat requires the mission's FriendlyGroup and EnemyGroup (NameGroup
objects). On the server, these are created by mission scripts:
```python
pMission = MissionLib.GetMission()
pEnemyGroup = pMission.GetEnemyGroup()
pFriendlyGroup = pMission.GetFriendlyGroup()
```

We need NameGroup objects with AddName/RemoveName/IsNameInGroup/RemoveAllNames
methods. These are simple string-set containers.

---

## 10. Summary of Architectural Impact

### Scope Change Magnitude

The expansion from lobby-only to playable server approximately **doubles**
the API surface (~297 -> ~579 functions) and adds **4 new subsystems**
(relay, ship model, game FSM, scoring support).

However, the *complexity* increase is moderate, not exponential:
- Most new functions are either simple stubs (UI no-ops) or thin queries
  (read a component field from the ECS)
- The relay engine is architecturally simple (copy + rebroadcast)
- The game FSM has only 6 states with well-defined transitions
- Ship objects are data-only (no simulation)

### What Does NOT Change

- Network protocol (TGNetwork wire format) -- identical
- Event system -- identical architecture, more event types
- Handle map -- identical mechanism, more handle types
- Python embedding -- identical
- Build system -- identical structure, more source files
- All lobby functionality -- unchanged, extended

### Architectural Soundness

The existing architecture cleanly accommodates this expansion because:
1. **ECS flexibility**: Adding ship components and new entity types is trivial in flecs
2. **Handle map extensibility**: The type enum was designed to be extended
3. **Event system generality**: New event types are just new integer constants
4. **Module boundaries**: The relay engine fits naturally into `src/network/`
5. **Stub pattern**: UI no-ops are a proven pattern for headless servers

The expansion validates the original architectural decision to use an ECS
as the game state backbone. Every new gameplay concept (ship, score, game
state) maps cleanly to components on entities.
