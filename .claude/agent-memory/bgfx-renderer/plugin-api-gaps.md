# Plugin API Gaps for Renderer (PRs #172-174)

Reviewed 2026-02-23. These are the gaps preventing the renderer from being a module.

## Event Bus (PR #172) Gaps
- No render lifecycle events: frame_begin, frame_end, render_tick, window_resize
- No entity lifecycle events: ship_spawned, ship_despawned
- No effect events: effect_spawn (explosion/phaser/torpedo/cloak), effect_despawn
- No camera_changed event
- Tick events (game_tick ~30Hz) do not match display refresh rate
- String-based dispatch via strcmp over 128 entries -- hot path concern at 60fps

## Config (PR #173) Gaps
- No [client] or [renderer] TOML section
- No fields for: resolution, fullscreen, vsync, backend selection, MSAA, texture quality, shadow quality, bloom, max particles, LOD distances, NIF search paths, shader cache
- Struct named obc_server_cfg_t -- needs parallel obc_client_cfg_t
- Per-module KV (32 entries max, string-only storage) could serve as stopgap but fragile

## Module API (PR #174) Gaps
- Visual state only via raw ship_get() struct pointer -- no stable accessors for:
  - ship_position(slot), ship_orientation(slot), ship_velocity(slot)
  - ship_cloak_state(slot), ship_shield_hp(slot, facing)
  - ship_phaser_charge(slot, bank), ship_tractor_target(slot)
  - ship_model_path(slot)
- No projectile API (torpedoes in flight for rendering)
- No camera API (position, orientation, FOV, projection)
- No delta_time() or render_time()
- No get_visible_entities() or frustum culling support
- event_fire wrapper returns void, cannot observe cancelled/suppress_relay
- No debug draw API

## Divergences Between PRs
- event_bus.h: PR #172 has `const void *event_data`, PR #174 has `void *event_data`
- event_bus.h: PR #172 has OBC_EVENT_MAX_FIRE_DEPTH recursion guard, PR #174 removes it
- Must merge #172 first, then rebase #174 to reconcile
