# Four Event Classes


### 1. SubsystemEvent (factory_id = 0x00000101)

The most common event in collision traffic. No extension fields beyond the base event —
factory_id + event_type + two object references is the complete payload.

```
Offset  Size  Type    Field            Notes
------  ----  ----    -----            -----
0       1     u8      opcode           0x06
1       4     i32     factory_id       0x00000101
5       4     i32     event_type       See table below
9       4     i32     source_obj_id    Damaged subsystem (subsystem's own object ID)
13      4     i32     dest_obj_id      Repair subsystem that queued it (repair sub's object ID)
```

**Total**: 17 bytes (fixed).

Both object references are **subsystem-level IDs**, not ship IDs. See "Subsystem Object
IDs" above for how these are allocated.

**Event types**:

| Event Type | Name | Meaning |
|-----------|------|---------|
| `0x008000DF` | ADD_TO_REPAIR_LIST | Subsystem damaged, added to repair queue |
| `0x00800074` | REPAIR_COMPLETED | Subsystem condition reached max (repair finished) |
| `0x00800075` | REPAIR_CANNOT_BE_COMPLETED | Subsystem destroyed while in repair queue (condition reached 0.0) |

### 2. CharEvent (factory_id = 0x00000105)

Extends SubsystemEvent with a single byte payload. This class is primarily used by
opcodes 0x07-0x12 and 0x1B (weapon, cloak, and warp events) rather than opcode 0x06.
Documented here because the polymorphic deserializer handles any registered factory
type.

```
Offset  Size  Type    Field            Notes
------  ----  ----    -----            -----
0       1     u8      opcode           0x06
1       4     i32     factory_id       0x00000105
5       4     i32     event_type       Depends on specific event
9       4     i32     source_obj_id    Source object
13      4     i32     dest_obj_id      Related object
17      1     u8      char_value       Single-byte payload
```

**Total**: 18 bytes (fixed).

See [set-phaser-level-wire-format.md](../set-phaser-level-wire-format.md) for detailed
analysis of CharEvent usage.

### 3. ObjPtrEvent (factory_id = 0x0000010C)

Extends SubsystemEvent with a single int32 third object reference — a network object ID
for a third party (e.g. the weapon subsystem that fired). This is the highest-volume
event class during combat, accounting for ~45% of all PythonEvent messages.

```
Offset  Size  Type    Field            Notes
------  ----  ----    -----            -----
0       1     u8      opcode           0x06
1       4     i32     factory_id       0x0000010C
5       4     i32     event_type       See table below
9       4     i32     source_obj_id    Source object
13      4     i32     dest_obj_id      Related object
17      4     i32     obj_ptr          Third network object reference
```

**Total**: 21 bytes (fixed).

The `obj_ptr` field carries a network object ID resolved via the same hash table as
`source_obj_id` and `dest_obj_id`. It is NOT a char byte like CharEvent (+1 byte) —
it is a full 4-byte int32.

**Network-forwarded event types** (cross the wire via opcode 0x06/0x0D or 0x09):

| Event Type | Name | obj_ptr Contains | Opcode |
|-----------|------|-----------------|--------|
| `0x0080007C` | WEAPON_FIRED | Target ID or 0 | 0x06 |
| `0x00800081` | PHASER_STARTED_FIRING | Target ID | 0x06 |
| `0x00800083` | PHASER_STOPPED_FIRING | Target ID | 0x06 |
| `0x0080007D` | TRACTOR_BEAM_STARTED_FIRING | Target ID | 0x06 |
| `0x0080007F` | TRACTOR_BEAM_STOPPED_FIRING | Target ID | 0x06 |
| `0x00800058` | TARGET_WAS_CHANGED | **Previous** target ID (not the new one) | 0x0D |
| `0x00800076` | REPAIR_INCREASE_PRIORITY | Subsystem ID | 0x11 |
| `0x008000DC` | STOP_FIRING_AT_TARGET_NOTIFY | Target ID or 0 | 0x09 (host-only) |

**Correction (Feb 2026)**: TARGET_WAS_CHANGED was previously listed as "local-only, never
sent over the network." A 2-player client-side trace captured 15 instances of 0x0D carrying
ObjPtrEvent relayed from the other player, confirming this event IS sent over the network
via opcode 0x0D. Weapon events (WEAPON_FIRED, PHASER_*, TRACTOR_*) are observed exclusively
on opcode 0x06, not 0x0D.

**Local-only event types** (never sent over the network, documented for completeness):

| Event Type | Name | obj_ptr Contains |
|-----------|------|-----------------|
| `0x0080000E` | SET_PLAYER | New player ship ID |
| `0x0080006B` | SUBSYSTEM_HIT | Subsystem's own ID |
| `0x00800085` | TRACTOR_TARGET_DOCKED | Docked ship ID |
| `0x00800088` | SENSORS_SHIP_IDENTIFIED | Identified ship ID |

### Dual-Fire Pattern

Weapon fire functions create **two** ObjPtrEvent messages simultaneously:

- **Phaser fire**: PHASER_STARTED_FIRING (0x81) + WEAPON_FIRED (0x7C)
- **Tractor fire**: TRACTOR_BEAM_STARTED_FIRING (0x7D) + WEAPON_FIRED (0x7C)
- **Torpedo fire**: WEAPON_FIRED (0x7C) only

A complete phaser or tractor cycle (fire → stop) therefore generates **4 ObjPtrEvent
messages**: start-specific + WEAPON_FIRED on fire, stopped-specific + STOP_AT_TARGET
on stop. This dual-fire pattern explains why ObjPtrEvent accounts for ~45% of all
combat PythonEvent traffic.

### Class Hierarchy

```
Event (base, factory 0x02)
  └── SubsystemEvent (factory 0x101)
        ├── CharEvent (factory 0x105)
        └── ObjPtrEvent (factory 0x10C)
```

The `IsA` check reports true for all IDs in the ancestry chain.

### 4. ObjectExplodingEvent (factory_id = 0x00008129)

Carries ship destruction notifications. Extends the base event with a firing player
ID (who killed the ship) and an explosion lifetime (visual effect duration).

```
Offset  Size  Type    Field              Notes
------  ----  ----    -----              -----
0       1     u8      opcode             0x06
1       4     i32     factory_id         0x00008129
5       4     i32     event_type         Always 0x0080004E (OBJECT_EXPLODING)
9       4     i32     source_obj_id      Object that is exploding
13      4     i32     dest_obj_id        Target (typically NULL or sentinel)
17      4     i32     firing_player_id   Connection ID of the killer
21      4     f32     lifetime           Explosion effect duration (seconds)
```

**Total**: 25 bytes (fixed).

---

