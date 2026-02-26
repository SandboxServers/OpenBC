# Server Implementation Notes


### Minimal Server (hull damage only)

A server that only tracks hull HP (no repair queue) can skip PythonEvent generation
entirely. Collision damage still applies through the damage pipeline; PythonEvent
messages only carry repair-queue notifications and explosion effects.

### Full Server (subsystem repair + explosions)

For a full implementation:

1. **Repair events**: When applying collision damage to a subsystem:
   a. Check if condition decreased below maximum
   b. If so, add to the ship's repair queue (reject duplicates)
   c. If the add succeeds and this is the host in multiplayer:
      - Serialize a SubsystemEvent (factory 0x101) with event type ADD_TO_REPAIR_LIST
      - Send reliably to all other peers via the "NoMe" routing group

2. **Explosion events**: When a ship is destroyed:
   a. Serialize an ObjectExplodingEvent (factory 0x8129) with the killer's player ID
      and explosion duration
   b. Send reliably to all other peers via "NoMe"

3. **Repair completion/cancellation**: When repair finishes or is cancelled:
   - Same pattern as repair events with the appropriate event type

### Serialization Pattern (all producers)

All three producers use the same message construction:
1. Write opcode byte `0x06`
2. Serialize the event object (factory_id + event_type + object refs + class extensions)
3. Wrap in a reliable message
4. Send to "NoMe" routing group

### Client Relay

If a client sends an opcode 0x06 to the host (script events), the host should:
1. Forward to all other peers (excluding sender)
2. Process locally

---

