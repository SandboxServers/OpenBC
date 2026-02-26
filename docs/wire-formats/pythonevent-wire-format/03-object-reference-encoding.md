# Object Reference Encoding


Object reference fields use a three-way encoding:

| Wire Value | Meaning |
|-----------|---------|
| `0x00000000` | NULL (no object) |
| `0xFFFFFFFF` | Sentinel (-1, "self" reference) |
| Any other value | Network object ID |

### Ship Object IDs

Ship object IDs follow a player-based allocation: Player N base = `0x3FFFFFFF + (N * 0x40000)`.

### Subsystem Object IDs

Subsystems do **not** derive their IDs from the ship's base ID. Each subsystem receives
a globally unique ID from a sequential auto-increment counter at construction time. The
assignment order depends on the ship's subsystem creation sequence, which varies by ship
class.

This means:
- Subsystem IDs are **not predictable** from the ship's ID alone
- The only way to resolve a subsystem ID is via hash table lookup on the receiving end
- All subsystem IDs are assigned before the ship enters the game, so they are stable
  for the lifetime of that ship instance
- When a ship is destroyed and respawned, its subsystems receive new IDs from the counter

A server implementation must maintain a mapping of subsystem IDs to subsystem objects,
populated when the ship's subsystem list is created during InitObject deserialization.

---

