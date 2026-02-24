# Total Conversion Guide

A total conversion mod replaces every stock game system with its own implementation. OpenBC's module architecture makes this straightforward -- you replace the stock DLLs, provide your own ship data, and the engine doesn't know the difference.

## What "Total Conversion" Means in OpenBC

The stock game is 9 modules (protocol, lobby, combat, scoring, power, repair, movement, gamespy, chat) loaded from TOML config. A total conversion replaces some or all of these with your own DLLs and data:

```toml
# server.toml for a total conversion

[data]
registry = "mods/my-tc/data/"           # Your ship data
manifest = "mods/my-tc/manifests/tc.json"  # Your checksum manifest

# Replace ALL stock modules
[[modules]]
name = "protocol"
dll = "mods/my-tc/modules/protocol.dll"

[[modules]]
name = "lobby"
dll = "mods/my-tc/modules/lobby.dll"

[[modules]]
name = "combat"
dll = "mods/my-tc/modules/combat.dll"
[modules.config]
weapon_types = 12
shield_facings = 8

[[modules]]
name = "scoring"
dll = "mods/my-tc/modules/scoring.dll"
[modules.config]
objective_scoring = true
ctf_enabled = true

[[modules]]
name = "power"
dll = "mods/my-tc/modules/power.dll"

[[modules]]
name = "repair"
dll = "mods/my-tc/modules/repair.dll"

[[modules]]
name = "movement"
dll = "mods/my-tc/modules/movement.dll"

[[modules]]
name = "gamespy"
dll = "modules/gamespy.dll"     # Keep stock GameSpy (probably fine)

[[modules]]
name = "chat"
dll = "modules/chat.dll"        # Keep stock chat (probably fine)

# Add custom modules
[[modules]]
name = "objectives"
dll = "mods/my-tc/modules/objectives.dll"

[[modules]]
name = "ctf"
lua = "mods/my-tc/scripts/ctf.lua"
```

## Approach 1: Replace Everything

Replace all 9 stock DLLs. You control every game behavior.

**Pros**: Complete freedom. No interaction with stock code.
**Cons**: You implement everything. No free updates from stock improvements.

**When to use**: Your mod has fundamentally different game mechanics (e.g., turn-based combat, racing, exploration).

## Approach 2: Selective Replacement

Replace the modules you need to change, keep the rest stock.

```toml
# Keep stock protocol, lobby, movement, gamespy, chat
[[modules]]
name = "protocol"
dll = "modules/protocol.dll"      # Stock

[[modules]]
name = "lobby"
dll = "modules/lobby.dll"         # Stock

# Replace combat and scoring with your own
[[modules]]
name = "combat"
dll = "mods/my-tc/modules/combat.dll"

[[modules]]
name = "scoring"
dll = "mods/my-tc/modules/scoring.dll"

# Stock everything else
[[modules]]
name = "power"
dll = "modules/power.dll"

[[modules]]
name = "repair"
dll = "modules/repair.dll"

[[modules]]
name = "movement"
dll = "modules/movement.dll"

[[modules]]
name = "gamespy"
dll = "modules/gamespy.dll"

[[modules]]
name = "chat"
dll = "modules/chat.dll"
```

**Pros**: Less work. Benefits from stock module updates.
**Cons**: Must respect the event interfaces of stock modules.

**When to use**: Your mod changes combat rules but keeps the same general game structure.

## Approach 3: Extend, Don't Replace

Add new modules that layer on top of stock behavior. Use event priorities to run before or after stock handlers.

```toml
# All stock modules, plus your additions
[[modules]]
name = "protocol"
dll = "modules/protocol.dll"

# ... all stock modules ...

# Your custom modules (added at the end)
[[modules]]
name = "ctf_objectives"
lua = "mods/my-tc/scripts/ctf.lua"

[[modules]]
name = "custom_weapons"
dll = "mods/my-tc/modules/weapons.dll"
```

**Pros**: Minimal effort. Full stock compatibility.
**Cons**: Limited to what event hooks allow.

**When to use**: Your mod adds new game modes or scoring rules but doesn't change core combat.

## Ship Data

Total conversions typically bring their own ship roster. Provide a JSON data pack:

```
mods/my-tc/
  data/
    ships/
      my_ship_01.json
      my_ship_02.json
    projectiles/
      my_torpedo.json
```

Use the same JSON schema as `data/vanilla-1.1/`. The scraper tool (`tools/scrape_bc.py`) can generate these from mod Python scripts if your TC was originally a BC Python mod.

Reference `data/vanilla-1.1/` for the JSON schema and field definitions.

## Checksum Manifests

If your TC requires modified client files, generate a manifest:

```bash
openbc-hash --dir /path/to/tc/install --output mods/my-tc/manifests/tc.json
```

Reference this in `server.toml`:

```toml
[data]
manifest = "mods/my-tc/manifests/tc.json"
```

Clients must have matching files to pass checksum validation.

## Directory Structure

```
mods/my-tc/
  manifest.toml             # Mod metadata
  data/                     # JSON data packs
    ships/
    projectiles/
  manifests/                # Checksum manifests
    tc.json
  modules/                  # C DLLs
    combat.dll
    scoring.dll
    objectives.dll
  scripts/                  # Lua scripts
    ctf.lua
    announcements.lua
  modes/                    # TOML game mode definitions
    ctf.toml
    conquest.toml
  missions/                 # TOML mission definitions
    arena_ctf.toml
```

## Testing

1. Start with Approach 3 (extend) to validate your event handlers work
2. Move to Approach 2 (selective replace) as you need more control
3. Use Approach 1 (replace all) only if fundamentally different mechanics require it

Always test with stock BC 1.1 clients unless your TC requires a modified client.

## See Also

- [Getting Started](getting-started.md) -- Modding basics
- [DLL Module Guide](dll-module-guide.md) -- Writing C modules
- [TOML Reference](toml-reference.md) -- Configuration formats
- [Plugin System](../architecture/plugin-system.md) -- Module lifecycle
- [Module API Reference](../architecture/module-api-reference.md) -- Engine API
