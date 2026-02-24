# Plugin API Review (PRs #172, #173, #174) -- 2026-02-23

## What the UI Layer Gets

### From Event Bus (#172)
- Subscribe to game events: `score_changed`, `chat`, `ship_killed`, `player_connected`, etc.
- Priority system (0-255) lets UI register as observer at 200+ priority
- `cancelled` flag can suppress events; `suppress_relay` can prevent network relay
- Safe re-entrant subscribe/unsubscribe during handler execution
- Recursion depth guard (max 8)

### From Config (#173)
- TOML parser (toml-c, MIT) -- reusable for client.toml
- `obc_config_mod_*` accessors -- pattern reusable for UI module config
- Layered: defaults -> TOML -> CLI args

### From Module API (#174)
- `peer_*` functions: player list for lobby/scoreboard
- `ship_get()`: full read-only ship state (shields, weapons, power, cloak, position)
- `ship_hull/hull_max/alive/species`: target info basics
- `subsystem_hp/hp_max/count`: damage display
- `score_kills/deaths/points`: scoreboard
- `game_time/map/in_progress`: HUD status
- `ship_class_by_species/ship_class_count`: registry lookup for ship silhouettes
- `send_reliable/unreliable/to_all/to_others`: raw wire messaging

## What the UI Layer Still Needs (follow-up work)

### P0 - Blockers
1. Event payload struct definitions (score_changed_data_t, chat_data_t, etc.)
2. local_player_slot() accessor
3. Client config struct with [ui] section (ui_scale, font_size, keybinds)

### P1 - Tactical HUD
4. ship_shield(slot, facing) convenience accessor
5. ship_speed(slot) accessor
6. Weapon state accessors (phaser charge, torpedo cooldown/type)
7. Power distribution read/write accessors
8. Cloak state accessor

### P2 - Lobby/Chat/Damage
9. chat_send(text) function
10. Lobby state (ship selection, ready, map list)
11. Repair queue read/write
12. Alert status accessor

### P3 - Architecture
13. Instance-based event bus (for client+server coexistence)
14. Client-local event catalog (ui_target_changed, ui_view_changed, etc.)

## Divergence Alert
- PR #172 final: `const void *event_data`, `OBC_EVENT_MAX_FIRE_DEPTH 8`
- PR #174 (older base): `void *event_data`, no depth guard constant in header
- Must reconcile when #174 rebases onto merged #172
