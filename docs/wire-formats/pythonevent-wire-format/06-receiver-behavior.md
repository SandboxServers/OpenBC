# Receiver Behavior


### Opcode 0x06 / 0x0D Receiver

When a client receives a PythonEvent:

1. **Skip** the opcode byte (0x06)
2. **Read** the factory type ID (first 4 bytes of payload)
3. **Construct** the correct event class using the factory registry
4. **Deserialize** remaining fields via the class-specific reader
5. **Resolve** object references (network IDs → local objects)
6. **Dispatch** the event through the local event system

Local event handlers then process the event — for example, updating the repair queue
UI, playing explosion sound effects, or triggering visual feedback.

### Relay Behavior

**Opcode 0x06**: The host generates PythonEvent messages (repair queue updates, explosions,
script events) and sends them directly to all clients via the "NoMe" routing group. Clients
receive and dispatch locally. Clients do NOT re-relay received 0x06 messages.

If a client sends a 0x06 message to the host (script-initiated events), the host will:
1. Relay the message to all other connected clients (excluding sender)
2. Also process the event locally

**Opcode 0x0D**: Clients send targeting updates to the host. The host dispatches locally
(updates internal state) but does NOT relay to other clients. This is confirmed by stock
server traces showing 0 outbound 0x0D messages despite receiving 31 from clients.

### Opcodes 0x07-0x12, 0x1B (Generic Event Forward)

These opcodes use a **different receiver** that performs both relay and dispatch. The
receiver applies an event type override after deserialization — implementing the
sender/receiver event code pairing system.

#### Event Type Override Table

| Opcode | Name | Sender Code | Receiver Override |
|--------|------|-------------|-------------------|
| 0x07 | StartFiring | 0x008000D8 | 0x008000D7 |
| 0x08 | StopFiring | 0x008000DA | 0x008000D9 |
| 0x09 | StopFiringAtTarget | 0x008000DC | 0x008000DB |
| 0x0A | SubsystemStatusChanged | 0x0080006C | 0x0080006C (no change) |
| 0x0B | AddToRepairList | 0x008000DF | Preserve original |
| 0x0C | ClientEvent | (varies) | Preserve original |
| 0x0E | StartCloaking | 0x008000E2 | 0x008000E3 |
| 0x0F | StopCloaking | 0x008000E4 | 0x008000E5 |
| 0x10 | StartWarp | 0x008000EC | 0x008000ED |
| 0x11 | RepairListPriority | 0x00800076 | Preserve original |
| 0x12 | SetPhaserLevel | 0x008000E0 | Preserve original |
| 0x1B | TorpedoTypeChange | 0x008000FE | 0x008000FD |

"Preserve original" means the event arrives with its wire event code unchanged. Opcodes
with an override replace the event code after deserialization — this implements
sender/receiver asymmetry (e.g., a sender locally posts "start firing notify" but
receivers post "start firing command").

---

