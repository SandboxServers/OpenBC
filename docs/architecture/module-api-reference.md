# Module API Reference

Complete reference for the `obc_engine_api_t` struct passed to every module during load and available in every event handler.

## API Table Structure

```c
typedef struct obc_engine_api {
    int api_version;  // Currently 1; always check this first

    // --- Event System ---
    int  (*event_subscribe)(const char *event, obc_event_handler_fn handler, int priority);
    void (*event_unsubscribe)(const char *event, obc_event_handler_fn handler);
    void (*event_fire)(const char *event, int sender_slot, void *data);

    // --- Peer / Player ---
    int  (*peer_count)(void);
    int  (*peer_max)(void);
    int  (*peer_slot_active)(int slot);
    const char *(*peer_name)(int slot);
    int  (*peer_team)(int slot);

    // --- Ship State (Read) ---
    const obc_ship_state_t *(*ship_get)(int slot);
    float (*ship_hull)(int slot);
    float (*ship_hull_max)(int slot);
    int   (*ship_alive)(int slot);
    int   (*ship_species)(int slot);
    float (*subsystem_hp)(int slot, int subsys_index);
    float (*subsystem_hp_max)(int slot, int subsys_index);
    int   (*subsystem_count)(int slot);

    // --- Ship State (Write) ---
    void (*ship_apply_damage)(int slot, float amount, int source_slot);
    void (*ship_apply_subsystem_damage)(int slot, int subsys_index, float amount);
    void (*ship_kill)(int slot, int killer_slot, int method);
    void (*ship_respawn)(int slot);
    void (*ship_set_position)(int slot, float x, float y, float z);

    // --- Scoring ---
    void (*score_add)(int slot, int kills, int deaths, int points);
    int  (*score_kills)(int slot);
    int  (*score_deaths)(int slot);
    int  (*score_points)(int slot);
    void (*score_reset_all)(void);

    // --- Messaging ---
    void (*send_reliable)(int to_slot, const void *data, int len);
    void (*send_unreliable)(int to_slot, const void *data, int len);
    void (*send_to_all)(const void *data, int len, int reliable);
    void (*send_to_others)(int except_slot, const void *data, int len, int reliable);
    void (*relay_to_others)(int except_slot, const void *original_data, int len);

    // --- Config (per-module TOML values) ---
    const char *(*config_string)(const obc_module_t *self, const char *key, const char *def);
    int         (*config_int)(const obc_module_t *self, const char *key, int def);
    float       (*config_float)(const obc_module_t *self, const char *key, float def);
    int         (*config_bool)(const obc_module_t *self, const char *key, int def);

    // --- Timers ---
    int  (*timer_add)(float interval_sec, obc_event_handler_fn callback, void *user_data);
    void (*timer_remove)(int timer_id);

    // --- Logging ---
    void (*log_info)(const char *fmt, ...);
    void (*log_warn)(const char *fmt, ...);
    void (*log_debug)(const char *fmt, ...);
    void (*log_error)(const char *fmt, ...);

    // --- Game State ---
    float (*game_time)(void);
    const char *(*game_map)(void);
    int   (*game_mode_id)(void);
    int   (*game_in_progress)(void);

    // --- Data Registry (read-only) ---
    const obc_ship_class_t *(*ship_class_by_species)(int species_id);
    int (*ship_class_count)(void);
} obc_engine_api_t;
```

## Function Reference

### Event System

#### `event_subscribe`

```c
int event_subscribe(const char *event, obc_event_handler_fn handler, int priority);
```

Subscribe to an event. Returns 0 on success, -1 on failure (e.g., too many handlers).

- `event` -- Event name (see [Event System](event-system.md) for the full catalog)
- `handler` -- Function pointer to call when the event fires
- `priority` -- Execution order (0-255, lower runs first; stock handlers use 50)

Multiple handlers on the same event run in priority order. Equal priorities run in registration order.

#### `event_unsubscribe`

```c
void event_unsubscribe(const char *event, obc_event_handler_fn handler);
```

Remove a previously registered handler. Safe to call from within a handler.

#### `event_fire`

```c
void event_fire(const char *event, int sender_slot, void *data);
```

Fire a custom event. All subscribers are notified in priority order.

- `event` -- Event name (can be any string; mod events should use a unique prefix)
- `sender_slot` -- Player slot that caused the event, or -1 for engine/module-initiated
- `data` -- Typed event data pointer (subscribers cast based on event name)

### Peer / Player

#### `peer_count`

```c
int peer_count(void);
```

Returns the number of currently connected players (not including empty slots).

#### `peer_max`

```c
int peer_max(void);
```

Returns the server's maximum player capacity.

#### `peer_slot_active`

```c
int peer_slot_active(int slot);
```

Returns 1 if the given slot has an active player, 0 otherwise.

#### `peer_name`

```c
const char *peer_name(int slot);
```

Returns the player's name string. Returns NULL if slot is inactive.

#### `peer_team`

```c
int peer_team(int slot);
```

Returns the player's team index (0 = no team / FFA).

### Ship State (Read)

#### `ship_get`

```c
const obc_ship_state_t *ship_get(int slot);
```

Returns a read-only pointer to the full ship state for the given slot. Returns NULL if slot has no ship. Do not store this pointer across event handler calls -- it may be invalidated.

#### `ship_hull` / `ship_hull_max`

```c
float ship_hull(int slot);
float ship_hull_max(int slot);
```

Current and maximum hull HP for the ship at the given slot. Returns 0.0 if no ship.

#### `ship_alive`

```c
int ship_alive(int slot);
```

Returns 1 if the ship exists and has hull HP > 0.

#### `ship_species`

```c
int ship_species(int slot);
```

Returns the species ID of the ship at the given slot. Returns -1 if no ship.

#### `subsystem_hp` / `subsystem_hp_max`

```c
float subsystem_hp(int slot, int subsys_index);
float subsystem_hp_max(int slot, int subsys_index);
```

Current and maximum HP for a specific subsystem on the ship at the given slot.

#### `subsystem_count`

```c
int subsystem_count(int slot);
```

Returns the number of subsystems on the ship at the given slot.

### Ship State (Write)

#### `ship_apply_damage`

```c
void ship_apply_damage(int slot, float amount, int source_slot);
```

Apply damage to a ship's hull. The source_slot identifies who caused the damage (for kill attribution). Pass -1 for environmental damage.

#### `ship_apply_subsystem_damage`

```c
void ship_apply_subsystem_damage(int slot, int subsys_index, float amount);
```

Apply damage to a specific subsystem. Subsystem HP cannot go below 0.

#### `ship_kill`

```c
void ship_kill(int slot, int killer_slot, int method);
```

Immediately kill a ship. Fires the `ship_killed` event. Method values: `KILL_WEAPON`, `KILL_COLLISION`, `KILL_SELF_DESTRUCT`, `KILL_EXPLOSION`, `KILL_ENVIRONMENT`.

#### `ship_respawn`

```c
void ship_respawn(int slot);
```

Respawn a dead ship. Restores hull and subsystem HP to maximum. Fires `ship_respawned`.

### Scoring

#### `score_add`

```c
void score_add(int slot, int kills, int deaths, int points);
```

Add to a player's score. Values are deltas (can be negative). Fires `score_changed`.

#### `score_kills` / `score_deaths` / `score_points`

```c
int score_kills(int slot);
int score_deaths(int slot);
int score_points(int slot);
```

Read current score values for a player.

#### `score_reset_all`

```c
void score_reset_all(void);
```

Reset all player scores to zero. Used on game restart.

### Messaging

#### `send_reliable` / `send_unreliable`

```c
void send_reliable(int to_slot, const void *data, int len);
void send_unreliable(int to_slot, const void *data, int len);
```

Send a message to a specific player. Reliable messages are retransmitted until acknowledged.

#### `send_to_all`

```c
void send_to_all(const void *data, int len, int reliable);
```

Send a message to all connected players. `reliable` = 1 for reliable delivery.

#### `send_to_others`

```c
void send_to_others(int except_slot, const void *data, int len, int reliable);
```

Send to all players except one (typically the sender).

#### `relay_to_others`

```c
void relay_to_others(int except_slot, const void *original_data, int len);
```

Relay an incoming message as-is to all other players. Used when the server acts as a dumb relay for certain message types.

### Config

#### `config_string` / `config_int` / `config_float` / `config_bool`

```c
const char *config_string(const obc_module_t *self, const char *key, const char *def);
int         config_int(const obc_module_t *self, const char *key, int def);
float       config_float(const obc_module_t *self, const char *key, float def);
int         config_bool(const obc_module_t *self, const char *key, int def);
```

Read values from the module's `[modules.config]` section in TOML. Returns the default value if the key is not present.

### Timers

#### `timer_add`

```c
int timer_add(float interval_sec, obc_event_handler_fn callback, void *user_data);
```

Register a periodic callback. Returns a timer ID for later removal. The callback fires approximately every `interval_sec` seconds. `user_data` is passed as `ctx->event_data`.

#### `timer_remove`

```c
void timer_remove(int timer_id);
```

Cancel a previously registered timer.

### Logging

#### `log_info` / `log_warn` / `log_debug` / `log_error`

```c
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_debug(const char *fmt, ...);
void log_error(const char *fmt, ...);
```

Printf-style logging at different severity levels. Output goes to the server log. Module name is automatically prepended.

### Game State

#### `game_time`

```c
float game_time(void);
```

Returns elapsed game time in seconds since the current round started.

#### `game_map`

```c
const char *game_map(void);
```

Returns the current map name string.

#### `game_in_progress`

```c
int game_in_progress(void);
```

Returns 1 if a game round is active, 0 if in lobby/settings.

### Data Registry

#### `ship_class_by_species`

```c
const obc_ship_class_t *ship_class_by_species(int species_id);
```

Look up ship class data (hull HP, subsystem definitions, weapon loadout) by species ID. Returns NULL if species is not in the registry.

#### `ship_class_count`

```c
int ship_class_count(void);
```

Returns the total number of ship classes loaded in the registry.

## See Also

- [Plugin System](plugin-system.md) -- Module lifecycle, loading
- [Event System](event-system.md) -- Event names and context data
- [DLL Module Guide](../modding/dll-module-guide.md) -- How to write a module
