# RmlUi Specialist Memory

## Project State (as of 2026-02-23)
- Client bootstrap exists: `src/client/main.c` with `bc_client_backend_init/frame/shutdown` loop
- Client backend: SDL3+bgfx (PR #175 merged), noop fallback
- No RmlUi integration yet -- client is render-loop-only, no UI
- Phase 2 plugin architecture PRs in flight: event bus (#172), TOML config (#173), module API (#174)

## Key Files for UI Integration
- Client entry: `src/client/main.c` (simple frame loop)
- Client backend API: `src/client/client_backend.h` (`bc_client_config_t`: title, width, height)
- Event bus: `include/openbc/event_bus.h`, `src/server/event_bus.c`
- Module API: `include/openbc/module_api.h` (40 function pointers, versioned, append-only)
- Config: `include/openbc/config.h` (`obc_server_cfg_t`), `src/server/config.c`
- Ship state: `include/openbc/ship_state.h` (`bc_ship_state_t` -- shields, weapons, power, cloak, position)
- Ship data: `include/openbc/ship_data.h` (`bc_ship_class_t` -- subsystem defs, static class data)
- Event catalog: `docs/architecture/event-system.md`

## Architecture Notes
- See [plugin-api-review.md](plugin-api-review.md) for detailed review of PRs #172-174

## Key Constraints
- Event bus is SINGLETON (static globals) -- cannot have separate client/server instances
- No event payload structs defined yet -- `event_data` is void*, types undocumented
- Config system is server-only -- no `[client]` or `[ui]` section
- Module API has `ship_get()` returning full `bc_ship_state_t*` but lacks convenience accessors for shields/speed/weapons/power/cloak
- No `local_player_slot()`, `chat_send()`, or lobby state in the API
- `obc_engine_api_t.event_fire` signature diverges between PR #172 (const void*) and #174 (void*)
