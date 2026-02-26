# Overview


Opcode 0x06 (PythonEvent) is a **polymorphic serialized-event transport**. The host
broadcasts game events to all clients using this opcode. The payload after the opcode
byte is a serialized event object. The **first 4 bytes** of the payload are a factory
type ID that determines which event class follows — different event types carry
different fields.

This is the primary mechanism for synchronizing repair-list changes, explosion
notifications, and forwarded script events in multiplayer.

**Direction**: Host → All Clients (via "NoMe" routing group)
**Reliability**: Sent reliably (ACK required)
**Frequency**: ~251 per 15-minute 3-player combat session (~3,432 total in a 34-minute
session — the most frequently sent game opcode)

### Opcode 0x0D (PythonEvent2)

Opcode 0x0D shares the same wire format and deserialization logic as 0x06. However, the
two opcodes have **different routing semantics**:

| Property | 0x06 (PythonEvent) | 0x0D (PythonEvent2) |
|----------|-------------------|---------------------|
| Primary origin | Host (repair events, explosions) | Clients (targeting, script events) |
| Direction | Host → All clients | Client → Host only |
| Relay behavior | Host sends directly to all clients | **NOT relayed** — server absorbs and dispatches locally |

**Verification (Feb 2026)**: A stock dedi server-side trace captured 31 instances of 0x0D
received from clients (C→S), and 0 instances sent to clients (S→C). The server processes
0x0D locally but does NOT relay it to other peers. This is distinct from 0x06, which the
server does broadcast to clients. All 31 instances carried ObjPtrEvent (factory 0x010C)
with event type TARGET_WAS_CHANGED (0x00800058) — clients informing the server of targeting
changes as server-side bookkeeping.

---

