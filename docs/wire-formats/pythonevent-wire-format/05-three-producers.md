# Three Producers


Not all PythonEvent messages come from the same source. The game has three distinct
event-to-network pathways:

### 1. Host Event Handler

**Registered for**: ADD_TO_REPAIR_LIST, REPAIR_COMPLETED, REPAIR_CANNOT_BE_COMPLETED

This is the collision damage pathway. When a subsystem is damaged and added to the
repair queue, the handler serializes the event and sends it reliably to the "NoMe"
routing group (all peers except the host itself).

**Gate condition**: Only registered when multiplayer is active.

**Event generation chain**:
1. Subsystem condition decreases below maximum → posts SUBSYSTEM_HIT (internal only)
2. Repair subsystem catches SUBSYSTEM_HIT, adds to repair queue (rejects duplicates)
3. If add succeeded AND host AND multiplayer → posts ADD_TO_REPAIR_LIST
4. Host Event Handler catches ADD_TO_REPAIR_LIST → serializes as opcode 0x06

### 2. Object Exploding Handler

**Registered for**: OBJECT_EXPLODING (0x0080004E)

When a ship is destroyed, this handler serializes the explosion event and sends it
reliably to the "NoMe" group. In single-player, it directly triggers the explosion
visual effect instead.

**Gate condition**: Always registered, but internally checks multiplayer flag. In
multiplayer: serialize + send. In single-player: apply locally.

### 3. Generic Event Forward (NOT opcode 0x06)

Used by many other handlers (StartFiring, StopFiring, SubsystemStatus, StartCloak,
etc.) that forward events over the network. This pathway writes the **specific opcode**
for each event type (0x07, 0x08, 0x0A, etc.), **NOT** 0x06. Included here only to
clarify that it is NOT a source of PythonEvent messages despite sharing the same
serialization mechanism.

---

