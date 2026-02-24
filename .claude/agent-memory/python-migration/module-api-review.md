# Module API Review -- PR #174 (obc_engine_api_t)

## BC Script Pattern to Module API Mapping

### Fully Covered
| BC Pattern | Module API |
|---|---|
| EventManager.AddBroadcastPythonFuncHandler | event_subscribe(name, fn, priority) |
| TimerManager.AddTimer/DeleteTimer | timer_add/timer_remove |
| TGNetwork.SendTGMessage | send_reliable/send_to_all/send_to_others |
| MultiplayerGame.GetShipFromPlayerID | ship_get(slot) |
| Hull/subsystem reads | ship_hull/subsystem_hp/subsystem_count |
| Damage/kill/respawn | ship_apply_damage/ship_kill/ship_respawn |
| Score dicts (kills/deaths/points) | score_add/score_kills/score_deaths/score_points |
| UtopiaModule.GetGameTime | game_time() |
| VarManager.GetStringVariable | config_string(self, key, default) |
| Species constants | ship_class_by_species(species_id) |

### Gaps Identified
| BC Pattern | Missing API | Priority |
|---|---|---|
| ship.SetTranslate + RandomOrientation | ship_set_rotation(slot,qw,qx,qy,qz) | P2 (spawn) |
| ObjectGroupWithInfo team management | peer_set_team(slot, team_id) | P2 (team modes) |
| TGNetwork.DisconnectPlayer | peer_kick(slot, reason) | P2 (admin) |
| RestartGame() atomic restart | game_restart() | P2 |
| loadspacehelper.CreateShip (starbases) | object_create/destroy | P3 (Mission5) |
| Ship velocity control | ship_set_velocity | P3 |
| event_fire return value | event_fire should return cancelled state | P1 (before merge) |

### Design Issues Found
1. event_fire void return prevents modules from checking cancellation
2. timer_add reuses obc_event_handler_fn -- works but misleading context fields
3. Variadic log functions in function-pointer table -- ABI assumption (OK for x86)
4. No log stubs in test_module_api.c (variadic makes stubbing harder)

### Intentionally Absent (different architecture)
- TGBufferStream (C modules use raw byte arrays)
- SWIG pointer/shadow class machinery
- Module-global game state (engine-managed scoring)
- func_code hot-patching (proper subscribe/unsubscribe)
- Dynamic __import__ (data registry + TOML config)
