# Lua Scripting Guide

Lua scripts provide a sandboxed scripting tier for OpenBC mods. They run in a Lua 5.4 environment with access to a subset of the engine API through the `obc` global table.

## Loading a Lua Module

Register a Lua script as a module in `server.toml`:

```toml
[[modules]]
name = "my_rules"
lua = "mods/my-mod/rules.lua"
[modules.config]
bonus_points = 500
overtime_enabled = true
```

The engine loads the script, executes it in a sandboxed Lua state, and registers any event handlers it defines.

## The `obc` API

All engine interaction goes through the `obc` global table:

### Event Handling

```lua
-- Subscribe to an event
obc.on("ship_killed", function(ctx)
    obc.log("Kill: slot " .. ctx.killer_slot .. " -> " .. ctx.victim_slot)
end, 150)  -- priority 150

-- Unsubscribe (save the function reference)
local my_handler = function(ctx) ... end
obc.on("ship_killed", my_handler, 100)
obc.off("ship_killed", my_handler)

-- Fire a custom event
obc.fire("my_mod_event", { bonus = 500 })
```

### Player / Peer

```lua
obc.peer_count()            -- Number of connected players
obc.peer_max()              -- Max player capacity
obc.peer_active(slot)       -- Is slot occupied? (boolean)
obc.peer_name(slot)         -- Player name (string)
obc.peer_team(slot)         -- Team index (0 = FFA)
```

### Ship State

```lua
obc.ship_hull(slot)         -- Current hull HP
obc.ship_hull_max(slot)     -- Max hull HP
obc.ship_alive(slot)        -- Is ship alive? (boolean)
obc.ship_species(slot)      -- Species ID
obc.subsystem_hp(slot, idx) -- Subsystem current HP
obc.subsystem_hp_max(slot, idx)  -- Subsystem max HP
obc.subsystem_count(slot)   -- Number of subsystems
```

### Ship Mutation

```lua
obc.ship_apply_damage(slot, amount, source_slot)
obc.ship_apply_subsystem_damage(slot, subsys_idx, amount)
obc.ship_kill(slot, killer_slot, "weapon")  -- method: "weapon", "collision", "self_destruct", "explosion"
obc.ship_respawn(slot)
```

### Scoring

```lua
obc.score_add(slot, kills, deaths, points)
obc.score_kills(slot)
obc.score_deaths(slot)
obc.score_points(slot)
obc.score_reset_all()
```

### Configuration

```lua
obc.config_string("key", "default")
obc.config_int("key", 0)
obc.config_float("key", 1.0)
obc.config_bool("key", false)
```

Reads from the module's `[modules.config]` section in TOML.

### Timers

```lua
local id = obc.timer_add(5.0, function()
    obc.log("5 seconds elapsed")
end)

obc.timer_remove(id)
```

### Messaging

```lua
obc.send_reliable(slot, data)     -- Send bytes to one player
obc.send_to_all(data, true)       -- Send to all (reliable)
obc.send_to_others(slot, data, true)  -- Send to all except one
```

### Logging

```lua
obc.log("Info message")
obc.log_warn("Warning message")
obc.log_debug("Debug message")
obc.log_error("Error message")
```

### Game State

```lua
obc.game_time()         -- Elapsed seconds (float)
obc.game_map()          -- Current map name
obc.game_in_progress()  -- Is round active? (boolean)
```

## Event Context

Event handlers receive a `ctx` table with event-specific fields:

```lua
obc.on("ship_killed", function(ctx)
    -- ctx.event_name = "ship_killed"
    -- ctx.sender_slot = player who triggered the event
    -- ctx.killer_slot = who made the kill
    -- ctx.victim_slot = who was killed
    -- ctx.method = "weapon", "collision", etc.

    -- Cancel further handlers:
    ctx.cancelled = true

    -- Suppress network relay:
    ctx.suppress_relay = true
end, 50)
```

Each event type has its own context fields. See [Event System](../architecture/event-system.md) for the complete catalog.

## Complete Example: Instagib Mode

```lua
-- mods/instagib/rules.lua
-- One-hit kills: any damage instantly kills the target

obc.on("collision_effect", function(ctx)
    -- Kill on any collision
    if obc.ship_alive(ctx.victim_slot) then
        obc.ship_kill(ctx.victim_slot, ctx.attacker_slot, "collision")
    end
    ctx.cancelled = true  -- Skip stock damage calculation
end, 25)  -- Priority 25: before stock combat (50)

obc.on("beam_fire", function(ctx)
    if obc.ship_alive(ctx.target_slot) then
        obc.ship_kill(ctx.target_slot, ctx.sender_slot, "weapon")
    end
    ctx.cancelled = true
end, 25)

obc.on("ship_killed", function(ctx)
    -- Double points in instagib
    obc.score_add(ctx.killer_slot, 0, 0, 200)
end, 150)  -- After stock scoring

obc.log("Instagib mode loaded!")
```

## Sandbox Restrictions

Lua scripts run in a sandbox. The following standard libraries are **not available**:

| Removed | Reason |
|---------|--------|
| `io` | No filesystem access |
| `os` | No system calls |
| `debug` | No runtime introspection |
| `loadfile` / `dofile` | No arbitrary file loading |
| `package` / `require` | No external module loading |

Available standard libraries: `string`, `table`, `math`, `coroutine` (limited), `utf8`.

### Resource Limits

| Limit | Default | Purpose |
|-------|---------|---------|
| Memory | 16 MB | Prevents runaway allocation |
| Instructions | 1,000,000 per handler call | Prevents infinite loops |

If a script exceeds either limit, the handler call is terminated and an error is logged. The script remains loaded -- only the offending handler invocation is killed.

## Multiple Scripts

A mod can register multiple Lua scripts as separate modules:

```toml
[[modules]]
name = "instagib_rules"
lua = "mods/instagib/rules.lua"

[[modules]]
name = "instagib_announcer"
lua = "mods/instagib/announcer.lua"
```

Each script runs in its own Lua state. They communicate through events, not shared variables.

## See Also

- [Getting Started](getting-started.md) -- Modding overview
- [TOML Reference](toml-reference.md) -- Configuration formats
- [Event System](../architecture/event-system.md) -- Full event catalog
- [DLL Module Guide](dll-module-guide.md) -- C module development (full API access)
