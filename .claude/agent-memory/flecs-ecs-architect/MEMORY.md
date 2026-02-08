# Flecs ECS Architect - Memory

## Architecture Decisions (Phase 1)

- **Single world, multiple modules** (obc_network, obc_game_state, obc_compat). No multi-world.
- **Custom pipeline phases** via EcsDependsOn chains. Phase 2 inserts new phases between Lobby and EventDispatch.
- **Handle table (Option B)**: 32-bit handles encoding type (8 bits) + index (24 bits). Type safety for SWIG pointer validation. NOT raw entity IDs.
- **Observer-driven event bridge**: OnAdd/OnRemove for lifecycle events, ecs_emit for state transitions, deferred Python dispatch via ObcPendingScriptEvent tickets.
- **Opaque pointers** for config map and var store (not ECS components internally -- just pointers in singleton components).

## Architecture Decisions (Phase 1 Gameplay)

- **Ship entities are lightweight**: Only identity (ObcShipIdentity, 72B) + ownership (ObcShipOwnership, 8B) + state tags. No physics/rendering.
- **Score/team state stays in Python**: Scripts own their dicts (kills, deaths, scores, damage, teams). No ECS mirror.
- **Events are C structs, NOT entities**: ObcWeaponHitEvent, ObcObjectExplodingEvent dispatched to Python synchronously. Transient, stack-allocated.
- **No torpedo entities in Phase 1**: All weapon hit data comes from network messages. Phase 2 adds projectile entities.
- **Object IDs are monotonic, never recycled**: Scripts use ObjID as dict keys that persist across ship lifetimes.
- **Ship respawn = delete + create**: New entity, new ObjID, clean slate. Old entity deleted.
- **GetShipFromPlayerID via direct index**: Array in ObcGameSession, O(1) lookup. Max 16 players.
- **ShipClass_Cast = ecs_has(ObcShipIdentity)**: IsTypeOf(CT_SHIP) same check.
- **Ship death is tag transition**: ObcShipAlive -> ObcShipDying -> ObcShipDead

## Phase 1 Gameplay Pipeline
```
NetworkReceive -> Checksum -> Lobby -> GameplayMessage -> ScriptEventDispatch -> Timer -> NetworkSend -> Cleanup
```
GameplayMessage parses damage/kill/spawn messages, creates/destroys ships, constructs events.
ScriptEventDispatch calls registered Python handlers synchronously.

## Key Files
- [phase1-architecture.md](phase1-architecture.md) - Complete Phase 1 ECS design (components, systems, events, handles)
- [gameplay-ecs-design.md](gameplay-ecs-design.md) - Phase 1 gameplay server ECS (ships, events, scoring, lifecycle)

## flecs API Notes (C API, v4)
- Field indices start at 0 (changed from v3 where they started at 1)
- Singletons: `ecs_singleton_set(world, Type, {...})` / `ecs_singleton_get(world, Type)`
- Custom phases: `ecs_new_w_id(world, EcsPhase)` + `ecs_add_pair(world, phase, EcsDependsOn, prev_phase)`
- Custom events: `ecs_emit(world, &(ecs_event_desc_t){...})` with `.ids` for component matching
- Observers: `ecs_observer(world, {...})` with `.events = { EcsOnSet | EcsOnAdd | EcsOnRemove | custom }`
- Systems: `ecs_system(world, {...})` with `.entity.add = ecs_ids(ecs_dependson(Phase))`
- Queries (v4): `ecs_query(world, {...})` + `ecs_query_iter` + `ecs_query_next` + `ecs_query_fini`

## Original BC Architecture Reference
- Player slots: 0-15, each 0x18 bytes at MultiplayerGame+0x74
- Checksum exchange: 4 rounds (scripts/App.pyc, scripts/Autoexec.pyc, scripts/ships/*.pyc, scripts/mainmenu/*.pyc)
- Connection states: 2=hosting, 3=connected (counterintuitive)
- Event types: 0x60001=message, 0x60002=host_start, 0x8000e7=checksum_fail, 0x8000e8=checksum_complete
- SWIG pointer format: "_{hex}_p_{TypeName}" e.g. "_8097fa78_p_TGNetwork"

## Phase 1 Tick Pipeline
```
EcsOnUpdate -> NetworkReceive -> Checksum -> Lobby -> EventDispatch -> NetworkSend -> Cleanup
```

## Phase 2 Extension Pattern
Insert new phases between Lobby and EventDispatch:
```
... -> Lobby -> AI -> Weapons -> Physics -> Damage -> Shields -> Power -> EventDispatch -> ...
```
Uses ecs_remove_pair + ecs_add_pair to repoint DependsOn chain.

## Component Size Considerations
- Keep ObcNetworkPeer small (~80 bytes). Separate ObcChecksumExchange (~48 bytes) removed after exchange completes.
- ObcPlayerSlot (~80 bytes) stays for session duration.
- Event tickets (ObcPendingScriptEvent) are transient -- created and deleted within the same tick.
