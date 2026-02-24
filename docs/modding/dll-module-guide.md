# DLL Module Development Guide

This guide covers writing C module DLLs for OpenBC. DLL modules have full access to the engine API and can implement any game behavior.

## Minimal Module

Every DLL exports exactly one function:

```c
#include "openbc/module_api.h"

static void on_player_join(const obc_engine_api_t *api, obc_event_ctx_t *ctx) {
    int slot = ctx->sender_slot;
    api->log_info("Player joined: slot %d, name %s", slot, api->peer_name(slot));
}

OBC_MODULE_EXPORT
int obc_module_load(const obc_engine_api_t *api, obc_module_t *self) {
    if (api->api_version < 1) {
        return -1;  // Incompatible engine
    }

    api->event_subscribe("new_player_in_game", on_player_join, 50);
    api->log_info("Welcome module loaded");
    return 0;
}
```

`OBC_MODULE_EXPORT` is a macro that expands to `__declspec(dllexport)` on Windows and `__attribute__((visibility("default")))` on Linux.

## Building

### Windows (MinGW)

```bash
i686-w64-mingw32-gcc -shared -o welcome.dll welcome.c \
    -I/path/to/openbc/include -Wall -Wextra
```

### Linux

```bash
gcc -shared -fPIC -o welcome.so welcome.c \
    -I/path/to/openbc/include -Wall -Wextra
```

The only build dependency is the `openbc/module_api.h` header. Module DLLs do not link against the engine -- they receive the API table at runtime.

## Module with State

Use `self->user_data` to store per-module state:

```c
typedef struct {
    int total_kills;
    int total_deaths;
    float round_start_time;
} my_state_t;

static void on_kill(const obc_engine_api_t *api, obc_event_ctx_t *ctx) {
    // Retrieve module state from the event -- modules store state in user_data
    // For simplicity, use a file-scope pointer set during load
}

static my_state_t g_state;

static void cleanup(obc_module_t *self) {
    // Called on server shutdown
    // Free any allocated resources
}

OBC_MODULE_EXPORT
int obc_module_load(const obc_engine_api_t *api, obc_module_t *self) {
    if (api->api_version < 1) return -1;

    memset(&g_state, 0, sizeof(g_state));
    self->user_data = &g_state;
    self->shutdown = cleanup;

    api->event_subscribe("ship_killed", on_kill, 50);
    return 0;
}
```

## Reading Configuration

Your module reads its TOML config section through the API:

```toml
# In server.toml
[[modules]]
name = "my_combat"
dll = "mods/my-mod/combat.dll"
[modules.config]
damage_multiplier = 2.5
friendly_fire = true
max_respawns = 3
```

```c
OBC_MODULE_EXPORT
int obc_module_load(const obc_engine_api_t *api, obc_module_t *self) {
    float mult = api->config_float(self, "damage_multiplier", 1.0f);
    int ff = api->config_bool(self, "friendly_fire", 0);
    int respawns = api->config_int(self, "max_respawns", -1);

    api->log_info("Config: damage=%.1fx, ff=%d, respawns=%d", mult, ff, respawns);
    // Store in module state...
    return 0;
}
```

## Replacing a Stock Module

To replace stock combat with your own:

1. Subscribe to the same events the stock combat module handles
2. Name your module with the same name in TOML
3. The engine loads whichever DLL is specified for that name

```toml
# Stock line (replace this):
# [[modules]]
# name = "combat"
# dll = "modules/combat.dll"

# Your replacement:
[[modules]]
name = "combat"
dll = "mods/my-mod/combat.dll"
[modules.config]
instagib = true
```

Your module receives all the events that stock combat would have received. You handle them however you want.

## Extending Stock Behavior

To add behavior without replacing stock modules, register at a higher priority number (runs after stock):

```c
// Priority 150 = runs AFTER stock scoring at 50
api->event_subscribe("ship_killed", on_bonus_kill, 150);
```

To override and cancel stock behavior:

```c
// Priority 25 = runs BEFORE stock at 50
static void on_custom_damage(const obc_engine_api_t *api, obc_event_ctx_t *ctx) {
    // Apply our custom damage logic
    // ...

    // Prevent stock combat handler from also applying damage
    ctx->cancelled = true;
}

api->event_subscribe("collision_effect", on_custom_damage, 25);
```

## Sending Custom Messages

Modules can send custom wire messages to clients:

```c
static void send_custom_event(const obc_engine_api_t *api, int slot) {
    uint8_t buf[16];
    buf[0] = 0x06;  // PythonEvent opcode
    // ... encode custom payload ...
    api->send_reliable(slot, buf, sizeof(buf));
}
```

## Using Timers

For periodic tasks:

```c
static void check_overtime(const obc_engine_api_t *api, obc_event_ctx_t *ctx) {
    if (api->game_time() > 600.0f) {
        api->log_info("Overtime! Sudden death mode.");
        // Modify game rules...
    }
}

// In obc_module_load:
api->timer_add(5.0f, check_overtime, NULL);  // Check every 5 seconds
```

## Common Patterns

### Kill Attribution

```c
static void on_kill(const obc_engine_api_t *api, obc_event_ctx_t *ctx) {
    kill_info_t *kill = (kill_info_t *)ctx->event_data;

    api->log_info("Kill: %s killed %s via %s",
        api->peer_name(kill->killer_slot),
        api->peer_name(kill->victim_slot),
        kill_method_name(kill->method));

    api->score_add(kill->killer_slot, 1, 0, 100);
    api->score_add(kill->victim_slot, 0, 1, 0);
}
```

### Conditional Relay Suppression

```c
static void on_chat(const obc_engine_api_t *api, obc_event_ctx_t *ctx) {
    chat_data_t *chat = (chat_data_t *)ctx->event_data;

    // Filter profanity
    if (contains_banned_word(chat->message)) {
        ctx->suppress_relay = true;  // Don't forward to other clients
        api->log_warn("Blocked chat from slot %d", ctx->sender_slot);
    }
}
```

### Game End Condition

```c
static void check_win(const obc_engine_api_t *api, obc_event_ctx_t *ctx) {
    int frag_limit = api->config_int(/* ... */);
    if (frag_limit <= 0) return;

    for (int i = 0; i < api->peer_max(); i++) {
        if (!api->peer_slot_active(i)) continue;
        if (api->score_kills(i) >= frag_limit) {
            api->event_fire("game_ended", -1, &(end_info_t){ .reason = END_FRAG_LIMIT });
            return;
        }
    }
}
```

## Debugging

Use `api->log_debug()` liberally during development. Debug output is suppressed unless the server runs with `--log-level debug` or `-v`.

## See Also

- [Module API Reference](../architecture/module-api-reference.md) -- Complete API table
- [Event System](../architecture/event-system.md) -- Event catalog and priority rules
- [Plugin System](../architecture/plugin-system.md) -- Module lifecycle
- [Total Conversion Guide](total-conversion-guide.md) -- Replacing all stock modules
