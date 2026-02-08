# Phase 1 Gameplay ECS Design - Relay Server with Scoring Scripts

## Overview

Phase 1 server is a **relay server** that:
- Forwards network messages between clients (clients do physics/rendering)
- Runs Python scoring scripts (Mission1.py through Mission7.py)
- Tracks ship entities for script queries (identity, ownership, state only)
- Does NOT simulate physics, AI, weapons, or rendering

The scoring scripts need to:
1. Cast event destinations to ships (`App.ShipClass_Cast(pEvent.GetDestination())`)
2. Query ship identity (`GetNetType()`, `GetObjID()`, `GetName()`)
3. Query ship ownership (`GetNetPlayerID()`, `IsPlayerShip()`)
4. Query ship death state (`IsDying()`, `IsDead()`)
5. Look up ships by player ID (`pGame.GetShipFromPlayerID(iPlayerID)`)
6. Process weapon hit events (`GetFiringPlayerID()`, `GetDamage()`, `IsHullHit()`)
7. Process death events (`ET_OBJECT_EXPLODING`)

---

## 1. Ship Entity Components

Ships on the relay server are lightweight identity/state containers.
No physics, no rendering, no subsystem simulation.

### ObcShipIdentity
Core identity that scripts query via `GetName()`, `GetObjID()`, `GetNetType()`.

```c
typedef struct {
    uint32_t obj_id;        // GetObjID() -- unique game object ID
    uint16_t net_type;      // GetNetType() -- species index (SpeciesToShip enum)
    uint16_t class_type;    // Derived from net_type via GetClassFromSpecies()
    char     name[64];      // GetName() -- ship name (e.g. "USS Enterprise")
} ObcShipIdentity;
```

**Size**: 72 bytes. Queried on every damage/kill event.

### ObcShipOwnership
Maps ship to controlling player. Scripts query via `GetNetPlayerID()`, `IsPlayerShip()`.

```c
typedef struct {
    uint32_t net_player_id; // GetNetPlayerID() -- network player ID (0 = AI/no player)
    bool     is_player_ship;// IsPlayerShip() -- true if controlled by a human player
} ObcShipOwnership;
```

**Size**: 8 bytes (padded). Split from identity for query efficiency -- some queries
only need ownership (team damage checks), not full identity.

### Ship State Tags (zero-size)
```c
ECS_TAG_DECLARE(ObcShipDying);   // IsDying() -- ship in death animation
ECS_TAG_DECLARE(ObcShipDead);    // IsDead() -- ship fully destroyed
ECS_TAG_DECLARE(ObcShipAlive);   // Neither dying nor dead
```

The lifecycle is: `ObcShipAlive` -> `ObcShipDying` -> `ObcShipDead` -> entity deleted or
recycled on respawn.

### Ship Archetype Summary
```
Ship entity = ObcShipIdentity + ObcShipOwnership + ObcShipAlive + ObcSWIGHandle
```

On death:
```
remove ObcShipAlive, add ObcShipDying
then: remove ObcShipDying, add ObcShipDead
```

On respawn:
```
remove ObcShipDead, add ObcShipAlive
reset ObcShipIdentity (new obj_id, possibly new net_type)
```

---

## 2. Game Object ID System

### The Three ID Spaces

1. **flecs entity ID** (`ecs_entity_t`, 64-bit): Internal ECS identifier. Stable for
   entity lifetime. Used for all internal lookups and relationships.

2. **Game Object ID** (`uint32_t`): Returned by `GetObjID()`. Used by scripts to index
   damage dictionaries (`g_kDamageDictionary[iShipID]`). Must be stable for ship
   lifetime but recyclable after destruction.

3. **SWIG handle** (`uint32_t`, formatted as `"_{hex}_p_{TypeName}"`): Used for Python
   API type checking. Encoded as type(8b)|index(24b) per Phase 1 design.

### Mapping Strategy

```
Python script call
    |
    v
SWIG handle string -> handle table -> ecs_entity_t
    |
    v
ecs_get(world, entity, ObcShipIdentity) -> obj_id, net_type, name
```

The handle table (from Phase 1) already maps handles to entities. We extend it with
reverse lookups:

```c
// Forward: handle -> entity (existing from Phase 1)
ecs_entity_t obc_handle_resolve(ObcHandleTable *ht, uint32_t handle);

// Reverse: game object ID -> entity (new for gameplay)
ecs_entity_t obc_objid_to_entity(ObcHandleTable *ht, uint32_t obj_id);

// Reverse: player ID -> ship entity (implements GetShipFromPlayerID)
ecs_entity_t obc_player_to_ship(ecs_world_t *world, uint32_t player_id);
```

### Object ID Generation

Use a monotonic counter stored in the `ObcGameSession` singleton:

```c
// In ObcGameSession (singleton, already exists from Phase 1):
typedef struct {
    // ... existing fields ...
    uint32_t next_obj_id;  // NEW: monotonic counter, starts at 1
} ObcGameSession;

// Allocate a new object ID
uint32_t obc_alloc_obj_id(ecs_world_t *world) {
    ObcGameSession *session = ecs_singleton_get_mut(world, ObcGameSession);
    return session->next_obj_id++;
}
```

Object IDs are NOT recycled within a game session. The scripts use them as dictionary
keys that persist across a ship's destruction (damage tracking).

### GetShipFromPlayerID Implementation

This is a hot path called frequently by scoring scripts. Two options:

**Option A: flecs query** (simple, correct)
```c
// Query: find ship where ObcShipOwnership.net_player_id == target
ecs_entity_t obc_player_to_ship(ecs_world_t *world, uint32_t player_id) {
    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { ecs_id(ObcShipIdentity) },
            { ecs_id(ObcShipOwnership) }
        }
    });

    ecs_iter_t it = ecs_query_iter(world, q);
    ecs_entity_t result = 0;
    while (ecs_query_next(&it)) {
        ObcShipOwnership *own = ecs_field(&it, ObcShipOwnership, 1);
        for (int i = 0; i < it.count; i++) {
            if (own[i].net_player_id == player_id) {
                result = it.entities[i];
                break;
            }
        }
        if (result) break;
    }
    ecs_query_fini(q);
    return result;
}
```

**Option B: Direct index** (fast, preferred)
```c
// Maintain a hash map: player_id -> ship_entity in ObcGameSession
// Updated on ship creation and destruction
// O(1) lookup vs O(n) query scan
```

**Decision**: Use Option B (direct index) because `GetShipFromPlayerID` is called in
every damage event handler and kill handler. With up to 16 players, a simple array
suffices:

```c
#define OBC_MAX_PLAYERS 16

typedef struct {
    // ... existing fields ...
    uint32_t       next_obj_id;
    ecs_entity_t   player_ships[OBC_MAX_PLAYERS]; // index by slot, not player_id
    // Need a separate map for player_id -> slot for O(1) lookup
} ObcGameSession;
```

Actually, since player IDs are not necessarily contiguous (they're network IDs assigned
by the ENet/GNS layer), use a small hash map or linear scan of the 16-element array.
With max 16 players, linear scan is cache-friendly and fast enough.

---

## 3. Event Architecture

### Which Events Do Scripts Handle?

From analysis of Mission1.py through Mission5.py:

| Event Type | Script Handler | Data Needed |
|---|---|---|
| `ET_WEAPON_HIT` | `DamageEventHandler` | firing_player_id, destination (ship), damage, is_hull_hit |
| `ET_OBJECT_EXPLODING` | `ObjectKilledHandler` | firing_player_id, destination (ship) |
| `ET_NEW_PLAYER_IN_GAME` | `NewPlayerHandler` | player_id |
| `ET_NETWORK_DELETE_PLAYER` | `DeletePlayerHandler` | player_id |
| `ET_OBJECT_CREATED_NOTIFY` | `ObjectCreatedHandler` | destination (ship) |
| `ET_NETWORK_MESSAGE_EVENT` | `ProcessMessageHandler` | message buffer |
| `ET_NETWORK_NAME_CHANGE_EVENT` | `ProcessNameChangeHandler` | (none used) |
| `ET_RESTART_GAME` | `RestartGameHandler` | (none) |

### Event Representation: C Structs, NOT Entities

Events are transient. They carry data from one system to Python handlers within the
same tick. Making them entities would be wasteful:
- Allocation/deletion overhead per event
- Archetype churn
- No need for querying events across ticks

**Design**: Events are C structs dispatched through the event manager (a C struct, not
an ECS singleton). The event manager calls registered Python handlers synchronously.

```c
// Base event structure matching original TGEvent
typedef struct {
    uint32_t     event_type;      // ET_WEAPON_HIT, ET_OBJECT_EXPLODING, etc.
    ecs_entity_t source;          // GetSource() -> entity that sent the event
    ecs_entity_t destination;     // GetDestination() -> target entity (e.g. hit ship)
} ObcEvent;

// Weapon hit event (extends ObcEvent)
typedef struct {
    ObcEvent     base;
    uint32_t     firing_player_id; // GetFiringPlayerID()
    float        damage;           // GetDamage()
    bool         is_hull_hit;      // IsHullHit()
} ObcWeaponHitEvent;

// Object exploding event
typedef struct {
    ObcEvent     base;
    uint32_t     firing_player_id; // GetFiringPlayerID() -- who dealt killing blow
} ObcObjectExplodingEvent;

// New player in game event
typedef struct {
    ObcEvent     base;
    uint32_t     player_id;       // GetPlayerID()
} ObcNewPlayerEvent;
```

### Event Flow: Network Message -> Event Dispatch -> Python

```
Client sends "ship X hit ship Y for Z damage"
    |
    v
NetworkReceiveSystem parses message
    |
    v
Creates ObcWeaponHitEvent on stack
    |
    v
obc_event_dispatch(world, &event)  -- calls registered Python handlers
    |
    v
Python DamageEventHandler(pObject, pEvent) runs
    |
    v
pEvent.GetDestination() -> resolves entity -> SWIG handle -> ShipClass_Cast
pEvent.GetFiringPlayerID() -> returns firing_player_id from event struct
pEvent.GetDamage() -> returns damage from event struct
```

### Event Entity Wrapping for SWIG

When Python calls `pEvent.GetDestination()`, it expects a SWIG pointer to a TGObject.
We need a thin wrapper:

```c
// SWIG wrapper for events (lives on stack or temp allocation, not ECS)
typedef struct {
    uint32_t     swig_handle;    // "_xxx_p_TGEvent" or subclass
    ObcEvent    *event;          // Points to the actual event data
    ecs_world_t *world;          // For resolving entity references
} ObcSWIGEventWrapper;

// GetDestination() implementation:
// 1. Get event->destination (ecs_entity_t)
// 2. Look up its SWIG handle from ObcSWIGHandle component
// 3. Return as SWIG pointer string
```

### ShipClass_Cast Implementation

`App.ShipClass_Cast(pEvent.GetDestination())` does a type check.
In the original engine, this is a C++ dynamic_cast. For us:

```c
// ShipClass_Cast: Check if entity has ObcShipIdentity component
// Returns: SWIG handle to ship, or NULL if not a ship
PyObject *obc_shipclass_cast(ecs_world_t *world, uint32_t swig_handle) {
    ecs_entity_t entity = obc_handle_resolve(&g_handle_table, swig_handle);
    if (!entity || !ecs_has(world, entity, ObcShipIdentity)) {
        Py_RETURN_NONE;  // Cast fails -- not a ship
    }
    // Return same handle (it's already a ship handle)
    return obc_make_swig_ptr(swig_handle, "ShipClass");
}
```

`IsTypeOf(App.CT_SHIP)` works the same way -- checks for `ObcShipIdentity` component.

---

## 4. Game State Singletons

### ObcGameSession (extended from Phase 1)

```c
typedef enum {
    OBC_GAME_STATE_LOBBY     = 0,
    OBC_GAME_STATE_LOADING   = 1,
    OBC_GAME_STATE_INGAME    = 2,
    OBC_GAME_STATE_ENDING    = 3,
} ObcGameState;

typedef struct {
    // --- From Phase 1 ---
    ObcGameState state;
    uint32_t     host_player_id;
    uint8_t      max_players;
    float        game_time;          // GetGameTime()

    // --- New for gameplay ---
    uint32_t     next_obj_id;        // Monotonic object ID counter
    bool         game_over;          // g_bGameOver flag
    bool         ready_for_new_players; // SetReadyForNewPlayers()
    char         mission_script[128]; // e.g. "Multiplayer.Episode.Mission1.Mission1"
} ObcGameSession;
```

### ObcMatchConfig (new singleton)

Game mode configuration set during lobby, read during gameplay.
Maps to `MissionMenusShared` globals.

```c
typedef struct {
    uint8_t  system_species;   // g_iSystem -- map/system index
    int16_t  time_limit;       // g_iTimeLimit -- minutes, -1 = unlimited
    int16_t  frag_limit;       // g_iFragLimit -- kill/score limit, -1 = unlimited
    uint8_t  player_limit;     // g_iPlayerLimit -- max players
    bool     use_score_limit;  // g_iUseScoreLimit -- frag limit vs score limit
    float    time_left;        // g_iTimeLeft -- seconds remaining (countdown)
} ObcMatchConfig;
```

### ObcScoreState (new singleton)

Score tracking is currently done in Python dictionaries. We have two options:

**Option A**: Let Python own the score state (as dictionaries). The ECS just provides
the event dispatch and ship entity queries. Python scripts are unchanged.

**Option B**: Store scores in ECS components and have Python read from them.

**Decision**: Option A. The scoring scripts already manage their own dictionaries
(`g_kKillsDictionary`, etc). Mirroring this in ECS would create synchronization
complexity with no benefit. The server's job is to:
1. Provide ship entities that scripts can query
2. Dispatch events to script handlers
3. Relay messages between clients

Score state lives in Python module globals, exactly as the original scripts expect.

The only ECS-side state we need is what the scripts query FROM the engine:
- Ship identity/ownership (ObcShipIdentity, ObcShipOwnership)
- Game state (ObcGameSession)
- Match config (ObcMatchConfig)

---

## 5. Ship Lifecycle

### Creation

When a client selects a ship and the server receives the create notification:

```c
void obc_create_player_ship(ecs_world_t *world, uint32_t player_id,
                            uint16_t net_type, const char *name) {
    // Allocate entity
    ecs_entity_t ship = ecs_new(world);

    // Allocate unique game object ID
    uint32_t obj_id = obc_alloc_obj_id(world);

    // Set identity
    ecs_set(world, ship, ObcShipIdentity, {
        .obj_id     = obj_id,
        .net_type   = net_type,
        .class_type = obc_class_from_species(net_type),
        .name       = {0}  // set below
    });
    ObcShipIdentity *ident = ecs_get_mut(world, ship, ObcShipIdentity);
    strncpy(ident->name, name, sizeof(ident->name) - 1);
    ecs_modified(world, ship, ObcShipIdentity);

    // Set ownership
    ecs_set(world, ship, ObcShipOwnership, {
        .net_player_id = player_id,
        .is_player_ship = (player_id != 0)
    });

    // Set initial state
    ecs_add(world, ship, ObcShipAlive);

    // Register SWIG handle
    uint32_t handle = obc_handle_alloc(&g_handle_table, OBC_HANDLE_SHIP, ship);
    ecs_set(world, ship, ObcSWIGHandle, { .handle = handle });

    // Update player->ship index
    obc_register_player_ship(world, player_id, ship);

    // Fire ET_OBJECT_CREATED_NOTIFY to scripts
    ObcEvent event = {
        .event_type  = OBC_ET_OBJECT_CREATED_NOTIFY,
        .source      = 0,
        .destination = ship
    };
    obc_event_dispatch(world, &event);
}
```

### Destruction

When the server receives a "ship destroyed" message from the authoritative client:

```c
void obc_ship_dying(ecs_world_t *world, ecs_entity_t ship,
                    uint32_t firing_player_id) {
    // Transition state: Alive -> Dying
    ecs_remove(world, ship, ObcShipAlive);
    ecs_add(world, ship, ObcShipDying);

    // Fire ET_OBJECT_EXPLODING to scoring scripts
    ObcObjectExplodingEvent event = {
        .base = {
            .event_type  = OBC_ET_OBJECT_EXPLODING,
            .source      = 0,
            .destination = ship
        },
        .firing_player_id = firing_player_id
    };
    obc_event_dispatch(world, (ObcEvent *)&event);
}

void obc_ship_dead(ecs_world_t *world, ecs_entity_t ship) {
    // Transition: Dying -> Dead
    ecs_remove(world, ship, ObcShipDying);
    ecs_add(world, ship, ObcShipDead);

    // Unregister from player->ship index
    const ObcShipOwnership *own = ecs_get(world, ship, ObcShipOwnership);
    if (own) {
        obc_unregister_player_ship(world, own->net_player_id);
    }
}
```

### Respawn

When a player selects a new ship after death:

```c
void obc_respawn_player_ship(ecs_world_t *world, uint32_t player_id,
                             uint16_t new_net_type, const char *new_name) {
    // Find the old dead ship entity (if it still exists)
    ecs_entity_t old_ship = obc_find_player_ship(world, player_id);
    if (old_ship && ecs_has(world, old_ship, ObcShipDead)) {
        // Option 1: Delete old entity, create fresh (simpler)
        obc_handle_free(&g_handle_table, ecs_get(world, old_ship, ObcSWIGHandle)->handle);
        ecs_delete(world, old_ship);
    }

    // Create a brand new ship entity
    obc_create_player_ship(world, player_id, new_net_type, new_name);
}
```

**Why delete + recreate instead of reuse?**
- Scripts may hold references to the old ObjID in damage dictionaries
- The new ship may be a different type (player can pick different species on respawn)
- Clean slate avoids stale component data

### DeletePlayerShipsAndTorps (game restart)

```c
void obc_clear_all_ships(ecs_world_t *world) {
    // Query all ship entities
    ecs_query_t *q = ecs_query(world, {
        .terms = {{ ecs_id(ObcShipIdentity) }}
    });

    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            const ObcSWIGHandle *h = ecs_get(world, it.entities[i], ObcSWIGHandle);
            if (h) obc_handle_free(&g_handle_table, h->handle);
            ecs_delete(world, it.entities[i]);
        }
    }
    ecs_query_fini(q);

    // Clear player->ship index
    ObcGameSession *session = ecs_singleton_get_mut(world, ObcGameSession);
    memset(session->player_ships, 0, sizeof(session->player_ships));
}
```

---

## 6. Torpedo/Projectile Entities

### Do They Need to Exist on the Server?

The scoring scripts access these fields from events:
- `pEvent.GetFiringPlayerID()` -- who fired the weapon
- `pEvent.GetDamage()` -- how much damage
- `pEvent.IsHullHit()` -- shield or hull hit
- `pEvent.GetDestination()` -- the ship that was hit (not the torpedo)

None of these require a torpedo entity to exist. All the needed data comes from
the network message: "player X's weapon hit ship Y for Z damage (hull/shield)".

### Decision: NO torpedo entities in Phase 1

The relay server receives weapon hit messages from clients that include:
- Firing player ID (embedded in the network message)
- Target ship (identified by game object ID or player ID)
- Damage amount
- Hit type (hull vs shield)

The server constructs an `ObcWeaponHitEvent` from this data and dispatches it to
Python handlers. No projectile entity lifecycle needed.

### Phase 2 Note

When we add server-authoritative physics, we WILL need torpedo entities with:
```
ObcProjectile { weapon_type, damage, firing_player_id }
ObcTransform  { position, rotation }
ObcPhysics    { velocity, lifetime }
ObcHoming     { target_entity, turn_rate }  // optional
```
But that's Phase 2. The component definitions don't need to exist yet.

---

## 7. Team Support

Mission2.py introduces team-based gameplay with additional dictionaries:
- `g_kTeamDictionary[player_id] = team_number`
- `g_kTeamScoreDictionary[team] = score`
- `g_kTeamKillsDictionary[team] = kills`

### Team Assignment

Team data lives in Python (following the same principle as score data). The server
does NOT need to track teams in ECS because:
1. Team assignments come from network messages, not engine state
2. The `IsSameTeam()` check is pure Python comparing dictionary values
3. No engine system needs to query team membership

If Phase 2 requires team-aware AI or friendly fire rules in the engine, we can add:
```c
typedef struct {
    uint8_t team_id;  // 0-based team number, 255 = unassigned
} ObcTeamAssignment;
```
But for Phase 1, team logic stays in Python.

---

## 8. Phase 2 Evolution Path

### What Changes When We Add Server-Authoritative Physics

The key architectural decision is that Phase 1 components are a SUBSET of Phase 2.
We never need to redesign -- only add.

#### Phase 1 (relay server) ship archetype:
```
ObcShipIdentity + ObcShipOwnership + ObcShipAlive/Dying/Dead + ObcSWIGHandle
```

#### Phase 2 (authoritative server) ship archetype:
```
ObcShipIdentity + ObcShipOwnership + ObcShipAlive/Dying/Dead + ObcSWIGHandle
+ ObcTransform + ObcPhysics + ObcHull + ObcShieldSystem + ObcWeaponSystem
+ ObcPowerSystem + ObcEngineSystem + ObcSensorSystem + ObcAI
```

#### New systems inserted into pipeline:
```
Phase 1 pipeline:
  NetworkReceive -> Checksum -> Lobby -> GameplayEventDispatch -> NetworkSend -> Cleanup

Phase 2 pipeline (extend between Lobby and GameplayEventDispatch):
  NetworkReceive -> Checksum -> Lobby
    -> InputProcessing -> AI -> Weapons -> Physics -> Damage -> Shields -> Power
    -> GameplayEventDispatch -> NetworkSend -> Cleanup
```

#### Migration checklist:
- Ship entities gain new components (additive, no existing component changes)
- Events gain new sources (physics system fires ET_WEAPON_HIT instead of network relay)
- `ObcWeaponHitEvent.damage` computed by server instead of trusted from client
- Torpedo entities created (new archetype, new handle type OBC_HANDLE_PROJECTILE)
- `ObcMatchConfig` gains physics parameters (tick rate, etc.)

### Component Forward-Compatibility Rules

1. **Never rename Phase 1 components** -- SWIG API functions depend on these names
2. **Never change Phase 1 component fields** -- only add new components
3. **Phase 1 tags (ObcShipAlive/Dying/Dead) persist into Phase 2** -- their semantics
   don't change
4. **ObcShipIdentity.obj_id semantics are permanent** -- scripts store them in dicts
5. **Event struct base layout is permanent** -- Python wrapper ABI depends on it

---

## 9. Complete Component Registry

### Phase 1 Components (this document)

| Component | Size (bytes) | Entity Types | Purpose |
|---|---|---|---|
| ObcShipIdentity | 72 | Ship | Name, obj_id, net_type |
| ObcShipOwnership | 8 | Ship | Player ID, is_player flag |
| ObcSWIGHandle | 4 | Ship, Peer, Globals | SWIG handle for Python API |
| ObcNetworkPeer | ~80 | Peer | Network connection state |
| ObcPlayerSlot | ~80 | Peer | Player session info |
| ObcChecksumExchange | ~48 | Peer (transient) | Checksum validation |
| ObcGameSession | ~160 | Singleton | Game state machine |
| ObcMatchConfig | 12 | Singleton | Time/frag/player limits |

### Phase 1 Tags

| Tag | Entity Types | Purpose |
|---|---|---|
| ObcShipAlive | Ship | Ship is active |
| ObcShipDying | Ship | Ship in death state |
| ObcShipDead | Ship | Ship fully destroyed |
| ObcChecksumPassed | Peer | Checksum validated |
| ObcChecksumFailed | Peer | Checksum rejected |
| ObcPeerDisconnecting | Peer | Connection teardown |
| ObcPeerTimedOut | Peer | Timeout exceeded |

### Phase 1 Custom Events (ecs_emit style, for flecs observers)

| Event | Purpose | When |
|---|---|---|
| OBC_ET_OBJECT_CREATED_NOTIFY | Ship created | obc_create_player_ship() |
| OBC_ET_OBJECT_EXPLODING | Ship dying | obc_ship_dying() |
| OBC_ET_WEAPON_HIT | Weapon damage | Network relay parse |
| OBC_ET_NEW_PLAYER_IN_GAME | Player joined game | Player creates ship |
| OBC_ET_NETWORK_DELETE_PLAYER | Player left | Disconnect handler |

### Phase 1 Event Structs (C, dispatched to Python, NOT entities)

| Struct | Fields | Script Method |
|---|---|---|
| ObcEvent | event_type, source, destination | GetSource(), GetDestination() |
| ObcWeaponHitEvent | +firing_player_id, damage, is_hull_hit | GetFiringPlayerID(), GetDamage(), IsHullHit() |
| ObcObjectExplodingEvent | +firing_player_id | GetFiringPlayerID() |
| ObcNewPlayerEvent | +player_id | GetPlayerID() |

---

## 10. System Pipeline (Phase 1 Gameplay)

```
EcsOnUpdate
  |
  v
NetworkReceiveSystem        -- Parse incoming packets from all peers
  |                            Produces: raw message buffers, relay commands
  v
ChecksumSystem              -- Validate script checksums (lobby phase only)
  |
  v
LobbySystem                 -- Ship selection, team assignment, game start
  |
  v
GameplayMessageSystem       -- NEW: Parse gameplay messages (damage, kills, spawns)
  |                            Produces: ObcWeaponHitEvent, ObcObjectExplodingEvent
  |                            Creates/destroys ship entities
  v
ScriptEventDispatchSystem   -- NEW: Dispatch events to Python handlers
  |                            Calls registered Python callbacks synchronously
  v
TimerSystem                 -- NEW: Update countdown timers, check time limit
  |
  v
NetworkSendSystem           -- Relay/forward messages to other peers
  |
  v
CleanupSystem               -- Delete fully dead ship entities after grace period
                               Remove disconnected peers
```

### GameplayMessageSystem Detail

This is the core new system for Phase 1 gameplay. It processes gameplay-relevant
network messages and translates them into events + entity state changes.

```c
void GameplayMessageSystem(ecs_iter_t *it) {
    // Process queued gameplay messages (from NetworkReceiveSystem)
    // For each message:
    //   - SHIP_CREATE: call obc_create_player_ship()
    //   - SHIP_DESTROYED: call obc_ship_dying() + obc_ship_dead()
    //   - WEAPON_HIT: construct ObcWeaponHitEvent, dispatch to scripts
    //   - SCORE_CHANGE: relay to other clients (scores live in Python)
}
```

### ScriptEventDispatchSystem Detail

Drains the pending event queue and calls Python handlers.

```c
void ScriptEventDispatchSystem(ecs_iter_t *it) {
    // For each pending event:
    //   1. Wrap event in SWIG-compatible Python object
    //   2. Look up registered handlers for event type
    //   3. Call each handler: handler(pObject, pEvent)
    //   4. Handlers may modify Python-side state (score dicts, etc.)
    //   5. Handlers may call SWIG API (GetShipFromPlayerID, etc.)
    //      which queries the ECS world synchronously
}
```

---

## 11. SWIG API Functions Needed for Gameplay

### Ship Functions (on ship entity via handle)
| Function | Implementation |
|---|---|
| `ShipClass_Cast(obj)` | Check entity has ObcShipIdentity |
| `pShip.GetName()` | Return ObcShipIdentity.name |
| `pShip.GetObjID()` | Return ObcShipIdentity.obj_id |
| `pShip.GetNetType()` | Return ObcShipIdentity.net_type |
| `pShip.GetNetPlayerID()` | Return ObcShipOwnership.net_player_id |
| `pShip.IsPlayerShip()` | Return ObcShipOwnership.is_player_ship |
| `pShip.IsDying()` | Return ecs_has(world, entity, ObcShipDying) |
| `pShip.IsDead()` | Return ecs_has(world, entity, ObcShipDead) |
| `pShip.IsTypeOf(CT_SHIP)` | Return ecs_has(world, entity, ObcShipIdentity) |

### Game Functions (on MultiplayerGame singleton)
| Function | Implementation |
|---|---|
| `MultiplayerGame_Cast(game)` | Return game singleton handle |
| `pGame.GetShipFromPlayerID(id)` | Lookup player->ship index |
| `pGame.DeletePlayerShipsAndTorps()` | obc_clear_all_ships() |
| `pGame.SetReadyForNewPlayers(flag)` | Set ObcGameSession field |

### Event Functions (on event wrapper)
| Function | Implementation |
|---|---|
| `pEvent.GetDestination()` | Return event.destination entity as SWIG ptr |
| `pEvent.GetSource()` | Return event.source entity as SWIG ptr |
| `pEvent.GetFiringPlayerID()` | Return from ObcWeaponHitEvent or ObcObjectExplodingEvent |
| `pEvent.GetDamage()` | Return ObcWeaponHitEvent.damage |
| `pEvent.IsHullHit()` | Return ObcWeaponHitEvent.is_hull_hit |
| `pEvent.GetPlayerID()` | Return ObcNewPlayerEvent.player_id |
| `pEvent.GetMessage()` | Return raw message buffer wrapper |

---

## 12. Key Design Decisions Summary

| Decision | Choice | Rationale |
|---|---|---|
| Score storage | Python dicts (not ECS) | Scripts already own this; no sync overhead |
| Team storage | Python dicts (not ECS) | Same as scores; no engine queries needed |
| Events | C structs (not entities) | Transient, no cross-tick persistence needed |
| Torpedo entities | None in Phase 1 | All data comes from network messages |
| Ship death | Tag transition (Alive->Dying->Dead) | Clean, queryable, Phase 2 compatible |
| GetShipFromPlayerID | Direct array index | O(1) vs O(n), called every damage event |
| Object IDs | Monotonic counter, never recycled | Scripts use as dict keys across lifetimes |
| Ship respawn | Delete + recreate entity | Clean slate, new ObjID, possibly new type |
