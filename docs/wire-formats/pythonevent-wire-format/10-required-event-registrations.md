# Required Event Registrations


For the collision → PythonEvent chain to function, three registration levels must be active:

### 1. Repair Subsystem Per-Instance Registration

Each ship's repair subsystem must register handlers during its initialization:
- SUBSYSTEM_HIT → adds damaged subsystem to repair queue
- REPAIR_COMPLETED → cleanup
- SUBSYSTEM_DAMAGED → tracks damage state
- REPAIR_CANCELLED → cleanup

Registered per ship instance (not globally). **Not gated on multiplayer** — always
registered.

### 2. Multiplayer Game Host Event Handler Registration

The multiplayer game object registers during construction:
- ADD_TO_REPAIR_LIST → serializes as opcode 0x06
- REPAIR_COMPLETED → serializes as opcode 0x06
- REPAIR_CANCELLED → serializes as opcode 0x06
- OBJECT_EXPLODING → serializes as opcode 0x06

**Gated on multiplayer mode** — only registered when a multiplayer game is active.

### 3. Ship Class Static Registration

Global (class-level) registration for collision processing:
- COLLISION_EFFECT → collision validation + damage application
- HOST_COLLISION_EFFECT → same, for client-reported collisions

Registered during class initialization (not per-instance).

If any registration level is missing, the chain breaks silently — damage may still
apply, but no PythonEvent messages are generated, and clients see no repair queue
updates.

---

