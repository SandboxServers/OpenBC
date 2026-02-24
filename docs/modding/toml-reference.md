# TOML Configuration Reference

Complete reference for all TOML configuration files used by OpenBC.

## Server Configuration: `server.toml`

The main server configuration file.

```toml
[server]
port = 22101                # UDP listen port
max_players = 6             # Maximum concurrent players (1-8)
name = "OpenBC Server"      # Server name shown in browser
log_level = "info"          # quiet|error|warn|info|debug|trace
log_file = ""               # Optional log file path (empty = stdout only)

[game]
map = "Multiplayer.Episode.Mission1.Mission1"   # Map mission string
system = 1                  # Star system index (1-9)
time_limit = -1             # Time limit in minutes (-1 = none)
frag_limit = -1             # Kill limit (-1 = none)
collision_damage = true     # Enable collision damage
friendly_fire = false       # Enable team damage
difficulty = 1              # 0=Easy, 1=Normal, 2=Hard
respawn_time = 10           # Seconds before respawn
mode_file = ""              # Optional game mode TOML file

[data]
registry = "data/vanilla-1.1/"    # Base ship data directory
manifest = "manifests/vanilla-1.1.json"   # Checksum manifest
mod_packs = []              # Additional data pack directories

[gamespy]
enabled = true              # Enable GameSpy protocol
lan_discovery = true        # Respond to LAN browser queries
master = []                 # Master server addresses (host:port)

[master]
heartbeat_interval = 60     # Seconds between master server heartbeats

# Module definitions (see Module Config section below)
```

## Module Configuration

Modules are defined as `[[modules]]` array entries in `server.toml`:

### C DLL Module

```toml
[[modules]]
name = "combat"                 # Module name (unique identifier)
dll = "modules/combat.dll"      # Path to DLL (relative to server dir)
[modules.config]                # Module-specific config (read via config_* API)
collision_damage = true
friendly_fire = false
collision_cooldown = 0.5
```

### Lua Script Module

```toml
[[modules]]
name = "bonus_scoring"
lua = "mods/my-mod/scoring.lua"   # Lua scripts use "lua" key instead of "dll"
[modules.config]
bonus_points = 500
```

### Required Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Unique module identifier |
| `dll` | string | One of dll/lua | Path to C DLL |
| `lua` | string | One of dll/lua | Path to Lua script |

### Optional Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `[modules.config]` | table | empty | Key-value config passed to the module |

## Game Mode Definitions

Game modes are TOML files that define match rules and event handler bindings. Referenced from `server.toml` via `game.mode_file`.

### File Location

```
modes/deathmatch.toml
modes/team-deathmatch.toml
modes/faction-deathmatch.toml
mods/my-mod/modes/instagib.toml
```

### Format

```toml
[mode]
name = "Deathmatch"                 # Display name
description = "Free-for-all combat" # Description
teams = false                       # Enable team system
team_count = 0                      # Number of teams (0 = FFA)
default_frag_limit = 10             # Default frag limit
default_time_limit = 15             # Default time limit (minutes)
respawn_time = 10                   # Default respawn time (seconds)

# Override server config values for this mode
[mode.overrides]
collision_damage = true
friendly_fire = false

# Event handler bindings for this mode
[[handlers]]
event = "ship_killed"               # Event name
module = "scoring"                  # Which module handles it
handler = "on_ffa_kill"             # Handler function name
priority = 50                       # Execution priority

[[handlers]]
event = "game_tick_1s"
module = "scoring"
handler = "check_frag_limit"
priority = 100
```

### Mode Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Display name |
| `description` | string | No | Mode description |
| `teams` | bool | No | Enable teams (default: false) |
| `team_count` | int | No | Number of teams (0 = FFA) |
| `default_frag_limit` | int | No | Default frag limit |
| `default_time_limit` | int | No | Default time limit (minutes) |
| `respawn_time` | int | No | Respawn delay in seconds |

### Handler Binding Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `event` | string | Yes | Event name to handle |
| `module` | string | Yes | Module name |
| `handler` | string | Yes | Handler function name within the module |
| `priority` | int | No | Priority (default: 50; lower = runs first) |

## Mission Definitions

Missions define specific match configurations. They reference a game mode and can override settings.

### File Location

```
missions/asteroid-field.toml
missions/deep-space.toml
mods/my-mod/missions/arena.toml
```

### Format

```toml
[mission]
name = "Asteroid Field FFA"         # Mission display name
mode = "deathmatch"                 # Game mode to use
system = 1                          # Star system index
map = "Multiplayer.Episode.Mission1.Mission1"   # Optional map override

# Override mode defaults for this mission
[mission.overrides]
frag_limit = 15
time_limit_minutes = 10
collision_damage = true
respawn_time = 5

# Mission-specific event handlers (in addition to mode handlers)
[[handlers]]
event = "server_start"
module = "my_mission"
handler = "setup_asteroids"
priority = 50
```

### Mission Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Mission display name |
| `mode` | string | Yes | Game mode filename (without .toml) |
| `system` | int | No | Star system index (1-9) |
| `map` | string | No | Override map string |

## Mod Manifest

Every mod has a `manifest.toml` in its root directory.

### Format

```toml
[mod]
name = "Instagib Mode"             # Mod display name
version = "1.0.0"                  # Semantic version
author = "ModderName"              # Author name
description = "One-hit kill combat mode"
url = ""                           # Optional homepage URL
compatible_api = 1                 # Minimum API version required

# Optional: additional data packs this mod provides
[mod.data]
packs = ["data/"]                  # Relative paths within the mod

# Optional: hash manifest for this mod's assets
[mod.manifest]
file = "manifests/mod-manifest.json"
```

### Manifest Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Mod display name |
| `version` | string | Yes | Version string |
| `author` | string | No | Author name |
| `description` | string | No | Description |
| `compatible_api` | int | Yes | Minimum engine API version |

## Event Handler TOML Binding

Both game modes and missions can bind event handlers in TOML:

```toml
[[handlers]]
event = "ship_killed"       # Event name (from event catalog)
module = "scoring"           # Module that owns the handler
handler = "on_ffa_kill"      # Function name in the module
priority = 50                # Execution priority (0-255)
```

These bindings tell the engine to call the named function in the named module when the event fires. The module must export the handler function.

## Stock Module Configuration

### Combat Module

```toml
[[modules]]
name = "combat"
dll = "modules/combat.dll"
[modules.config]
collision_damage = true      # Enable collision damage
friendly_fire = false        # Enable team damage
collision_cooldown = 0.5     # Seconds between collision events for same pair
shield_absorption = true     # Shields absorb directional damage
```

### Scoring Module

```toml
[[modules]]
name = "scoring"
dll = "modules/scoring.dll"
[modules.config]
kill_points = 100            # Points per kill
death_penalty = 0            # Points deducted on death
team_kill_penalty = -50      # Points for team kills
```

### Power Module

```toml
[[modules]]
name = "power"
dll = "modules/power.dll"
[modules.config]
simulation_rate = 10         # Hz (ticks per second)
battery_recharge = true      # Enable battery recharge
```

### Repair Module

```toml
[[modules]]
name = "repair"
dll = "modules/repair.dll"
[modules.config]
auto_queue = true            # Auto-queue damaged subsystems
repair_rate_base = 1.0       # Base repair rate multiplier
```

### Chat Module

```toml
[[modules]]
name = "chat"
dll = "modules/chat.dll"
[modules.config]
team_chat_enabled = true     # Allow team chat
max_message_length = 256     # Max chat message length
```

## See Also

- [Getting Started](getting-started.md) -- Modding overview
- [Data Format Guide](../architecture/data-format-guide.md) -- JSON vs TOML decision guide
- [Event System](../architecture/event-system.md) -- Event names and priorities
