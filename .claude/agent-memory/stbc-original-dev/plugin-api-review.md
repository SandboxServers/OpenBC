# Plugin API Review Notes (PRs #172, #173, #174)

## Event Bus (PR #172) - APPROVED
- Priority-ordered dispatch (0-255) fixes BC's registration-order-only limitation
- cancelled/suppress_relay are one-way latches (correct behavior, tested)
- Deferred subscribe/unsubscribe during fire: removals flush before additions
- Recursive depth guard: 8 levels, returns cancelled=true when exceeded
- Static singleton globals - acceptable for single-server model

## TOML Config (PR #173) - APPROVED
- Defaults -> TOML -> CLI layering matches intended dedicated server design
- Fixed-size string buffers, no heap alloc for config strings
- Range validation with warn-and-fallback (not crash)
- Per-module config namespaces via [[modules]] sections
- mode_file field reserved for future gamemode TOML system

## Module API (PR #174) - APPROVED with follow-up items

### Gaps identified (ordered by severity):
1. **No peer_set_team()** - Blocker for Mission2/3 (Team DM)
2. **No event data struct definitions** - kill_info_t, damage_info_t etc needed
3. **event_fire returns void** - Should return obc_event_result_t for module-to-module coordination
4. **config_bool type mismatch** - API uses int, config.h uses bool
5. **config_float narrows double to float** - precision loss (minor in practice)
6. **No peer_kick()** - Needed for admin modules
7. **No connection info** - peer_address(), peer_ping(), peer_connected_time()
8. **No team score accessors** - teamKills/teamScores are first-class in BC

### What maps correctly to original BC Python API:
- event_subscribe -> AddBroadcastPythonFuncHandler
- send_to_all -> SendTGMessage(0, msg)
- send_to_others -> SendTGMessageToGroup("NoMe", msg)
- send_reliable -> SendTGMessage(playerID, msg)
- score_add/kills/deaths/points -> g_kKillsDictionary etc
- ship_get -> App.ShipClass_Cast + GetNetPlayerID/GetObjID
- peer_team (read) -> g_kTeamDictionary[playerID]

### Damage ledger decision:
- Module-side state is acceptable (different modes may want different attribution)
- ship_damaged event data must include: source_slot, damage_amount, is_hull_hit, target_slot
- ship_killed event data must include: victim_slot, killer_slot, kill_method

### Timer design:
- Uses obc_event_handler_fn signature (faithful to BC's ET_TIMER approach)
- Slightly overloaded (cancelled/suppress meaningless for timers)
- Acceptable pragmatic reuse
