# Event System

The event bus is the central communication mechanism in OpenBC. All game logic executes in response to events. Modules subscribe to events during load and the engine dispatches events as they occur.

## Event Handler Signature

```c
typedef void (*obc_event_handler_fn)(const obc_engine_api_t *api, obc_event_ctx_t *ctx);
```

Every handler receives:
- `api` -- The engine API table (same one passed during module load)
- `ctx` -- Event context with event-specific data

## Event Context

```c
typedef struct obc_event_ctx {
    const char *event_name;     // Event identifier (e.g. "ship_killed")
    int         sender_slot;    // Player slot that triggered this event (-1 if engine)
    void       *event_data;     // Typed event data (cast based on event_name)
    bool        cancelled;      // Set to true to cancel further processing
    bool        suppress_relay; // Set to true to prevent network relay
} obc_event_ctx_t;
```

Handlers can:
- **Read** the event data to react (always safe)
- **Set `cancelled`** to prevent lower-priority handlers from running
- **Set `suppress_relay`** to prevent the event from being relayed to clients
- **Modify game state** through the engine API (e.g., apply damage, update scores)

## Subscribing to Events

During `obc_module_load`:

```c
api->event_subscribe("ship_killed", on_ship_killed, 50);
api->event_subscribe("game_tick_1s", check_frag_limit, 100);
```

Parameters:
- Event name (string)
- Handler function pointer
- Priority (lower = runs first; 0-255)

Multiple handlers can subscribe to the same event. They execute in priority order. If any handler sets `cancelled = true`, remaining handlers are skipped.

## Unsubscribing

```c
api->event_unsubscribe("ship_killed", on_ship_killed);
```

Typically called during module shutdown, but can be used at any time.

## Firing Events

Modules can fire custom events:

```c
my_custom_data_t data = { .bonus = 500 };
api->event_fire("my_mod_bonus", -1, &data);
```

Parameters: event name, sender slot (-1 for engine/module-initiated), event data pointer.

## Stock Event Catalog

### Protocol Events (fired by the protocol module)

These are fired when the server receives a game opcode from a client.

| Event | Data Type | Fired When |
|-------|-----------|------------|
| `raw_packet` | Raw packet bytes | Any incoming packet (before decode) |
| `obj_create` | Object create data | Client creates a ship (opcode 0x03) |
| `state_update` | Position/health data | Client sends state update (opcode 0x1C) |
| `start_firing` | Weapon fire data | Client starts firing (opcode 0x07) |
| `stop_firing` | Weapon stop data | Client stops firing (opcode 0x08) |
| `torpedo_fire` | Torpedo launch data | Torpedo fired (opcode 0x19) |
| `beam_fire` | Beam hit data | Beam weapon hit (opcode 0x1A) |
| `collision_effect` | Collision data | Collision event (opcode 0x15) |
| `explosion` | Explosion data | Explosion damage (opcode 0x29) |
| `python_event` | Event type + payload | Scripted event (opcode 0x06/0x0D) |
| `start_cloak` | Player slot | Cloak engaged (opcode 0x0E) |
| `stop_cloak` | Player slot | Cloak disengaged (opcode 0x0F) |
| `start_warp` | Player slot | Warp engaged (opcode 0x10) |
| `self_destruct_request` | Player slot | Client requests self-destruct (opcode 0x13) |
| `destroy_object` | Object ID | Object destruction (opcode 0x14) |
| `subsys_status` | Subsystem toggle data | Subsystem toggled (opcode 0x0A) |
| `repair_request` | Repair assignment data | Repair team assignment (opcode 0x0B) |
| `set_phaser_level` | Phaser config data | Phaser power changed (opcode 0x12) |
| `torp_type_change` | Torpedo type data | Torpedo type switched (opcode 0x1B) |
| `chat` | Message text + sender | Chat message (opcode 0x2C) |
| `team_chat` | Message text + sender + team | Team chat (opcode 0x2D) |
| `enter_set` | Set data | Player enters game set (opcode 0x1F) |
| `request_obj` | Object ID | Client requests object data (opcode 0x1E) |

### Lobby Events (fired by the lobby module)

| Event | Data Type | Fired When |
|-------|-----------|------------|
| `player_connected` | Peer info | New peer connects (transport level) |
| `checksums_complete` | Player slot | Player passes checksum validation |
| `new_player_in_game` | Player slot + ship | Player enters the game world |
| `player_disconnected` | Player slot + reason | Player disconnects or times out |
| `settings_sent` | Player slot | Settings packet delivered to new player |

### Game Logic Events (fired by combat, power, repair modules)

| Event | Data Type | Fired When |
|-------|-----------|------------|
| `ship_damaged` | Damage info | Ship takes damage (any source) |
| `ship_killed` | Kill info (victim, killer, method) | Ship hull reaches zero |
| `ship_respawned` | Player slot | Ship respawns after death |
| `subsystem_damaged` | Subsystem damage info | Individual subsystem takes damage |
| `subsystem_destroyed` | Subsystem destroy info | Subsystem HP reaches zero |
| `subsystem_repaired` | Subsystem repair info | Subsystem repair tick |
| `score_changed` | Score delta info | Player score modified |
| `game_ended` | End reason (frag/time/manual) | Game end condition met |
| `game_restarted` | None | Game restart triggered |

### Engine Tick Events

| Event | Interval | Purpose |
|-------|----------|---------|
| `game_tick` | Every frame (~30Hz) | Main simulation tick |
| `game_tick_sim` | 10Hz | Physics/power simulation tick |
| `game_tick_1s` | 1Hz | Periodic checks (frag limit, time limit) |
| `game_tick_5s` | 5s | Slow periodic tasks (GameSpy heartbeat) |

### Lifecycle Events

| Event | Fired When |
|-------|------------|
| `server_start` | Server initialization complete, before accepting connections |
| `server_shutdown` | Server shutting down |
| `gamespy_query` | GameSpy query received |

## Priority Ordering

Standard priority ranges:

| Range | Purpose |
|-------|---------|
| 0-24 | Engine internals (rarely used by modules) |
| 25-49 | Pre-processing (validation, anti-cheat) |
| **50** | Default priority (stock module handlers) |
| 51-99 | Post-processing (logging, telemetry) |
| 100-149 | Mod overrides (run after stock logic) |
| 150-199 | Mod additions (bonus scoring, custom effects) |
| 200-255 | Final observers (analytics, replay recording) |

Stock modules register at priority 50. A mod that wants to override stock behavior registers at priority 25 (runs first) and sets `cancelled = true`. A mod that wants to augment stock behavior registers at priority 150 (runs after).

## Example: Mod Bonus Scoring

```c
// Runs AFTER stock scoring (priority 50) has already counted the kill
static void on_bonus_kill(const obc_engine_api_t *api, obc_event_ctx_t *ctx) {
    kill_info_t *kill = (kill_info_t *)ctx->event_data;

    // Double points for self-destruct kills
    if (kill->method == KILL_SELF_DESTRUCT) {
        api->score_add(kill->killer_slot, 0, 0, 500);
    }
}

int obc_module_load(const obc_engine_api_t *api, obc_module_t *self) {
    if (api->api_version < 1) return -1;
    api->event_subscribe("ship_killed", on_bonus_kill, 150);
    return 0;
}
```

## See Also

- [Plugin System](plugin-system.md) -- Module lifecycle, DLL interface
- [Module API Reference](module-api-reference.md) -- Complete engine API
- [TOML Reference](../modding/toml-reference.md) -- Event binding in TOML
