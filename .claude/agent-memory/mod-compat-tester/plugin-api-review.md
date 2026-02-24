# Plugin API Review (PRs #172, #173, #174)

## Date: 2026-02-23

## Architecture

- Event bus: string-keyed, priority-ordered (0=highest, 255=lowest), 64 subs/event, 128 events max
- Config: TOML-based, layered (defaults -> file -> CLI), per-module namespaced config
- Module API: vtable struct (obc_engine_api_t), append-only with api_version, C DLL modules

## Must-Fix Issues

1. **event_fire return type mismatch**: event_bus.h returns obc_event_result_t, module_api.h returns void
   - Blocks mods from implementing pre-event hooks (cancel/suppress pattern)
2. **event_fire data const mismatch**: bus uses `const void*`, module API uses `void*`
3. **config_float type mismatch**: config.h returns double, module_api.h returns float

## API Gaps for BC Mods

### Cannot build without these (critical):
- Ship spawn/destroy (needed for game modes with NPCs like Mission5)
- Team assignment (peer_set_team) -- needed for team game modes
- Ship rotation setter -- needed for object placement
- Player kick/ban -- needed for admin tools

### Ergonomic gaps (important but not blocking):
- No shield-specific accessors (must cast through ship_get raw struct)
- No velocity/heading read accessors
- No power system accessors
- No cloak state accessors
- No entity query beyond ships (torpedoes, asteroids, starbases)
- No event payload type definitions (mod authors must guess struct layouts)
- No wildcard/prefix event subscription
- No unsubscribe-all-by-module helper

## What Mods CAN Build Today

- Custom scoring modules
- Combat modifiers (damage multipliers, invulnerability)
- Match management (time limits, round rotation)
- Logging/analytics modules

## Config Capacity Limits

- OBC_CFG_MODULES_MAX = 16 (should be 32 for heavily modded servers)
- OBC_CFG_MODCFG_MAX = 32 kv pairs per module
- Module config has no array/nested table support (flat kv only)

## Foundation Technologies Coexistence

Foundation operates at Python script layer; OpenBC modules are native C.
They can coexist: Foundation manages ship registry, modules handle combat/scoring.
Event bus could bridge them once Python<->C event dispatch exists.
