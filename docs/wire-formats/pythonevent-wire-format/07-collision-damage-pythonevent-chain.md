# Collision Damage → PythonEvent Chain


When two ships collide in multiplayer, the host generates approximately **14 PythonEvent
(opcode 0x06) messages**:

```
Collision Detection
    │
    ▼
Host Validates Collision (ownership, proximity, dedup)
    │
    ▼
Per-Contact Damage Application
    │
    ▼
Per-Subsystem Condition Update (SetCondition)
    │
    ├──→ Posts SUBSYSTEM_HIT event (internal)
    │        │
    │        ▼
    │    Repair Subsystem catches SUBSYSTEM_HIT
    │        │
    │        ▼
    │    Adds subsystem to repair queue (rejects duplicates)
    │        │
    │        ▼
    │    Posts ADD_TO_REPAIR_LIST [host + MP only]
    │        │
    │        ▼
    │    Host Event Handler serializes as PythonEvent (0x06)
    │        │
    │        ▼
    │    Sent reliably to "NoMe" group
    │
    ▼
(next subsystem...)
```

### Why ~14 Messages

- Two ships collide → each takes damage
- Each ship has ~7 top-level subsystems in the damage volume (shields, hull sections,
  weapons, etc.)
- Each damaged subsystem → one ADD_TO_REPAIR_LIST → one PythonEvent
- 7 subsystems × 2 ships = ~14 PythonEvent messages

The exact count varies with collision geometry and whether subsystems are already in the
repair queue (duplicates are rejected).

### Key Behavioral Invariant

ADD_TO_REPAIR_LIST is ONLY posted when **all three conditions** are true:
- The subsystem was successfully added to the queue (not a duplicate)
- The game is running as host
- Multiplayer is active

This prevents clients from generating spurious repair events and ensures only the host's
authoritative damage decisions produce network traffic.

---

