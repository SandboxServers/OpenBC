# Module API (PR #174) - ECS Compatibility Review

## Slot-Indexed Facade Pattern

The obc_engine_api_t uses `int slot` (player slot index 0-6) as the universal key for all
ship/peer/score operations. This is a clean facade: modules never see ecs_entity_t, SWIG
handles, or object IDs.

### ECS Migration Path (per accessor)

| API Function | Current Backing | ECS Backing |
|---|---|---|
| ship_hull(slot) | g_server.ships[slot].hull_hp | ecs_get(world, slot_map[slot], Hull)->current_hp |
| subsystem_hp(slot, idx) | ships[slot].subsystem_hp[idx] | ChildOf query or flat array in component |
| ship_alive(slot) | ships[slot].alive | ecs_has(world, entity, ObcShipAlive) tag |
| ship_species(slot) | ships[slot].class_index -> species | ecs_get(world, entity, ObcShipIdentity)->net_type |
| ship_class_by_species(id) | registry linear scan | Query prefab with matching species_id |

All function pointer implementations can be swapped independently. No ABI change needed.

### ship_get() Risk

Returns `const bc_ship_state_t *` -- the entire 500+ byte monolith. In an ECS world this
data splits across Hull, ShieldSystem, WeaponState, PowerState, CloakState, Transform, etc.
Options:
1. Shadow struct aggregated from ECS components each tick (wasteful)
2. Keep monolith as actual storage, ECS views into it (defeats ECS purpose)
3. Deprecate ship_get() in api_version 2

Recommendation: Option 3. All fields already covered by dedicated accessors.

### Missing Accessors for Future ECS

- No ship_by_objid() -- modules can't resolve wire object IDs
- No ship_set_rotation() -- teleport without facing control
- No ship iteration API -- must loop 0..peer_max() with ship_alive()
- No va_list logging variants -- can't wrap variadic function pointers

All appendable in future api_version without breaking existing modules.

### Subsystem Hierarchy in ECS

The `subsystem_hp(slot, subsys_index)` pattern with integer index abstracts over:
- Flat array in a ship component (current)
- Ordered ChildOf children with Subsystem component
- Prefab slot-based lookup (flecs SlotOf)
- Separate SubsystemRef component with entity ID array

Integer index is the right abstraction -- it survives all backing store changes.

### Ship Class as Prefab

`ship_class_by_species(int species_id)` returns `const obc_ship_class_t *`.
Future ECS: ship classes become prefab entities. New ship instances use `ecs_new_w_pair(world, EcsIsA, class_prefab)`.
The returned pointer must be stable -- either the ECS component pointer (stable while prefab exists) or a cached copy.
