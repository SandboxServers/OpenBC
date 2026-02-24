# Plugin System Architecture

OpenBC's game functionality is delivered through a modular DLL plugin system. The stock game's combat, scoring, movement, power, repair -- ALL game logic -- lives in DLLs loaded via TOML config. The stock DLLs are not special. They use the exact same API as mod DLLs. A total conversion mod replaces any stock module and the engine doesn't know the difference.

## Design Principle: Eat Our Own Dogfood

Stock game behavior is defined through the same extension mechanism that mods use. There is no "inner loop" that runs special stock code and an "outer loop" for mod hooks. Every game behavior -- collision damage, frag scoring, repair queues, power simulation -- is a handler registered by a module DLL via the public API.

This means:
- Mods can replace any stock behavior by replacing its module
- Mods can extend stock behavior by subscribing to the same events at different priorities
- All game behavior is testable through the public module API
- The stock game serves as a living example of how to write modules

## Three Tiers of Extensibility

| Tier | Who | Format | Power |
|------|-----|--------|-------|
| **C DLL** | Advanced modders | `.dll` + TOML | Full engine API, event handlers, custom game logic |
| **Lua script** | Intermediate modders | `.lua` + TOML | Sandboxed API subset, event handlers, custom rules |
| **TOML config** | All modders | `.toml` | Declarative game modes, mission definitions, handler bindings |

C DLLs have unrestricted engine API access. Lua scripts run in a sandbox with memory and instruction limits. TOML config is purely declarative -- it defines structure, not behavior.

## DLL Interface

Every module DLL exports exactly one function:

```c
// The only export a module DLL needs
// Called once when the engine loads the DLL
int obc_module_load(const obc_engine_api_t *api, obc_module_t *self);
```

During `obc_module_load`, the DLL:
1. Checks `api->api_version` for compatibility
2. Registers event handlers via `api->event_subscribe()`
3. Reads its config section via `api->config_string()`, `config_int()`, etc.
4. Allocates any per-module state and stores it in `self->user_data`
5. Returns 0 on success, nonzero on failure

The engine calls `obc_module_load` in dependency order. If a module fails to load, the engine logs the error and shuts down -- partial module sets are not supported.

### Module Lifecycle

```
Engine startup:
  1. Parse server.toml, collect [[modules]] entries
  2. Resolve DLL paths
  3. For each module (in config order):
     a. LoadLibrary / dlopen
     b. Lookup "obc_module_load" symbol
     c. Call obc_module_load(api, self)
     d. If it returns nonzero, log error and abort

Engine running:
  - Events fire, handlers execute
  - Modules interact only through the event bus and engine API

Engine shutdown:
  1. Fire "server_shutdown" event
  2. For each module (reverse order):
     a. Call self->shutdown(self) if set
     b. FreeLibrary / dlclose
```

### Module Struct

```c
typedef struct obc_module {
    const char *name;           // Set by engine from TOML config
    void       *user_data;      // Module's private state
    void      (*shutdown)(struct obc_module *self);  // Optional cleanup
} obc_module_t;
```

## Stock Module Split (9 Modules)

| Module | DLL | Responsibility |
|--------|-----|----------------|
| **protocol** | `modules/protocol.dll` | Wire format decode, opcode parsing, fires typed events |
| **lobby** | `modules/lobby.dll` | Join flow, checksums, settings, late-join replication |
| **combat** | `modules/combat.dll` | Damage calc, shields, subsystem damage, ship death, respawn |
| **scoring** | `modules/scoring.dll` | Kill tracking, damage ledger, frag/score/time limits, end/restart |
| **power** | `modules/power.dll` | Reactor simulation, battery/conduit model, efficiency computation |
| **repair** | `modules/repair.dll` | Repair queue, rate formula, auto-queue on damage |
| **movement** | `modules/movement.dll` | Position tracking, StateUpdate relay |
| **gamespy** | `modules/gamespy.dll` | LAN discovery, master server heartbeat, query responses |
| **chat** | `modules/chat.dll` | Chat relay, team filtering |

### What Stays in Engine Core

The engine core is **not** a module. It provides infrastructure that modules build on:

- Socket I/O, transport layer, reliability, cipher
- Peer management (connect, disconnect, slot allocation)
- Event bus, module loader
- TOML and JSON parsers
- Ship data registry (read-only access for modules)
- Ship state storage (mutated only through the engine API)
- Main loop and tick scheduling
- Logging subsystem

## API Versioning

The `obc_engine_api_t` struct has an `api_version` field. Modules check this on load:

```c
int obc_module_load(const obc_engine_api_t *api, obc_module_t *self) {
    if (api->api_version < 1) {
        return -1;  // Incompatible engine version
    }
    // ... register handlers ...
    return 0;
}
```

New API functions are always appended to the end of the struct. Existing function pointers never move or change signature. This provides forward compatibility: a module compiled against API v1 works with engine v2.

## Configuration

Each module receives its config through the engine API's config accessors. Config comes from the module's `[modules.config]` section in TOML:

```toml
[[modules]]
name = "combat"
dll = "modules/combat.dll"
[modules.config]
collision_damage = true
friendly_fire = false
collision_cooldown = 0.5
```

The module reads these at load time:

```c
bool collision = api->config_bool(self, "collision_damage", true);
bool ff = api->config_bool(self, "friendly_fire", false);
float cooldown = api->config_float(self, "collision_cooldown", 0.5f);
```

## See Also

- [Event System](event-system.md) -- Event names, handler signatures, priority ordering
- [Module API Reference](module-api-reference.md) -- Complete engine API table
- [Data Format Guide](data-format-guide.md) -- JSON vs TOML split
- [DLL Module Guide](../modding/dll-module-guide.md) -- How to write a module DLL
- [Lua Scripting Guide](../modding/lua-scripting-guide.md) -- Lua sandbox API
- [TOML Reference](../modding/toml-reference.md) -- All TOML config formats
