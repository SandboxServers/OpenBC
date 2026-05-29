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
      - Serialize a base **TGEvent (factory 0x0101)** with event type ADD_TO_REPAIR_LIST.
        Wire format is the 16-byte event payload (factory_id + event_type + source_obj_id
        + dest_obj_id), giving 17 bytes total on-wire with the 0x06 opcode prefix.
      - Preserve the IsA chain (0x0101 → 0x02 — a 2-level chain for the base class).
      - Send reliably to all other peers via the "NoMe" routing group.

   > **Note (2026-05-29)**: Earlier wording said "SubsystemEvent (factory 0x0101)" — that
   > class name was a fabrication. Factory 0x0101 IS plain TGEvent. See
   > `../tgobjptrevent-wire-format.md` for the canonical hierarchy.

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

