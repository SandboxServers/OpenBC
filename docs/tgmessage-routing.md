# TGMessage Routing — Clean Room Specification

Behavioral specification of the Bridge Commander TGMessage routing system, described purely
in terms of observable behavior. No binary addresses, decompiled code, or implementation
details. Suitable for clean-room reimplementation.

For the reverse engineering analysis with addresses and decompiled code, see
[tgmessage-routing.md](tgmessage-routing.md).

---

## Overview

Bridge Commander multiplayer uses a two-layer message system:

- **Transport layer**: Handles reliable delivery, fragmentation, connection management.
  Messages at this layer have a **transport type** byte.
- **Application layer**: Game-specific messages carried as opaque payloads inside transport
  messages. The first byte of the payload is the **game opcode**.

The server (host) acts as a hub in a star topology. All client-to-client communication
passes through the host.

---

## Transport Layer

### Transport Message Types

The transport layer supports up to 256 message types (one byte). Seven are defined:

| Type | Purpose |
|------|---------|
| 0x00 | Game data message (carries application-layer payload) |
| 0x01 | Acknowledgement (reliable delivery tracking) |
| 0x02 | Connection request |
| 0x03 | Connection acknowledgement |
| 0x04 | Boot / forced disconnect |
| 0x05 | Graceful disconnect |
| 0x32 | General-purpose data message (with fragment support) |

All other transport types are undefined. Packets with undefined transport types are
silently dropped — no error, no crash.

### Transport Type Registration

A registration function exists in the SWIG API (`TGNetwork_RegisterMessageType`) that
allows adding custom transport types at runtime. Stock code never calls it. All game
messages use the existing type 0x00 or 0x32 transports.

### Packet Format

Each UDP packet contains:
1. One byte: sender peer ID
2. One byte: count of sub-messages
3. N sub-messages, each starting with a transport type byte

The entire packet (after byte 0) is encrypted with a stream cipher. GameSpy protocol
packets (starting with `\` / 0x5C) are never encrypted.

---

## Application Layer — Game Opcodes

### Opcode Byte

The first byte of a transport message's payload is the **game opcode**. This determines
how the rest of the payload is interpreted.

### Three C++ Dispatchers

Game opcodes are processed by three independent C++ event handlers, all triggered by the
same network message event:

| Dispatcher | Opcodes Handled |
|------------|----------------|
| MultiplayerWindow | 0x00 (Settings), 0x01 (GameInit), 0x16 (UICollision) |
| MultiplayerGame | 0x02-0x2A (game objects, events, combat, players) |
| NetFile | 0x20-0x27 (checksums, file transfer) |

Each dispatcher reads the first payload byte, checks if it matches a known opcode, and
processes it. **Unknown opcodes are silently ignored** — no error, no rejection, no log.

### Python Event Handlers

Python scripts register handlers on the same network message event. They fire for ALL
incoming messages, read the opcode byte from the payload, and compare against their own
constants. This is how messages with opcodes > 0x2A are processed.

Stock Python handles these opcodes:

| Opcode | Decimal | Name | Handler |
|--------|---------|------|---------|
| 0x2C | 44 | CHAT_MESSAGE | MultiplayerMenus.ProcessMessageHandler |
| 0x2D | 45 | TEAM_CHAT_MESSAGE | MultiplayerMenus.ProcessMessageHandler |
| 0x35 | 53 | MISSION_INIT_MESSAGE | Mission1.ProcessMessageHandler |
| 0x36 | 54 | SCORE_CHANGE_MESSAGE | Mission1.ProcessMessageHandler |
| 0x37 | 55 | SCORE_MESSAGE | Mission1.ProcessMessageHandler |
| 0x38 | 56 | END_GAME_MESSAGE | MissionShared (via EndGame) |
| 0x39 | 57 | RESTART_GAME_MESSAGE | Mission1.ProcessMessageHandler |
| 0x3F | 63 | SCORE_INIT_MESSAGE | Mission2/3/5.ProcessMessageHandler |
| 0x40 | 64 | TEAM_SCORE_MESSAGE | Mission2/3/5.ProcessMessageHandler |
| 0x41 | 65 | TEAM_MESSAGE | Mission2/3/5.ProcessMessageHandler |

### MAX_MESSAGE_TYPES

The constant `App.MAX_MESSAGE_TYPES` equals **43 (0x2B)**. It represents the count of
C++-dispatched game opcodes. Python message types are defined as offsets from this value:
```
CHAT_MESSAGE         = MAX_MESSAGE_TYPES + 1   = 44
TEAM_CHAT_MESSAGE    = MAX_MESSAGE_TYPES + 2   = 45
MISSION_INIT_MESSAGE = MAX_MESSAGE_TYPES + 10  = 53
```

This is a convention, not a technical limit. Mods can define types at any value 0-255.

---

## Message Routing

### Network Topology: Star (Hub and Spoke)

```
Client A  ←→  HOST  ←→  Client B
                ↑
Client C  ←————┘
```

- Each client maintains a single connection: to the host.
- The host maintains connections to all clients.
- There are no direct client-to-client connections.

### Sending API

Two primary send functions are available via Python:

1. **SendTGMessage(target_id, message)**
   - `target_id = 0`: broadcast to all peers
   - `target_id = N`: unicast to specific peer

2. **SendTGMessageToGroup(group_name, message)**
   - Sends to all members of a named group
   - The `"NoMe"` group contains all peers except the local player

### Broadcast Behavior by Role

| Sender | SendTGMessage(0, msg) | SendTGMessageToGroup("NoMe", msg) |
|--------|----------------------|-----------------------------------|
| Client | Goes to host only (client has 1 peer) | Goes to host only |
| Host | Goes to all clients | Goes to all clients (host excluded) |

### Automatic Relay (C++ Layer)

When the host receives ANY game message from a client via the transport layer, the host
**automatically relays** it to all other connected clients. This relay is:

- **Unconditional**: Every message is relayed, regardless of opcode or content
- **Opaque**: The host does not read or interpret the message payload
- **Immediate**: Happens during the network update tick, before dispatch

This means a client sending a message effectively broadcasts to all other clients
(via the host relay), even if the client only intended to send to the host.

### Python-Level Relay (Selective)

Some messages receive additional relay treatment in Python:

- **CHAT_MESSAGE (0x2C)**: Host's Python handler explicitly forwards via
  `SendTGMessageToGroup("NoMe", copy)` — this results in the message being sent twice
  (once by C++ auto-relay, once by Python). In practice, clients handle the duplicate.

- **TEAM_CHAT_MESSAGE (0x2D)**: Host's Python handler selectively forwards only to
  teammates (not all clients). The C++ auto-relay still sends to everyone, but only
  teammates display it.

---

## Message Filtering

### What Gets Filtered

The server has **no message type whitelist**. The filtering that does exist is:

1. **Transport type**: Unknown transport types (unregistered factory entries) cause the
   packet to be silently dropped at the transport layer.

2. **Connection state**: Messages from disconnecting peers are not relayed.

3. **Python-level**: Individual Python handlers only process opcodes they recognize,
   ignoring all others.

### What Does NOT Get Filtered

- **Game opcode value**: No bounds check, no range validation, no whitelist.
- **Payload content**: Never examined during relay.
- **Message size**: Subject only to transport-layer length limits (13-bit or 14-bit
  depending on transport type, with fragmentation support for type 0x32).

---

## Mod Custom Message Types

### How Mods Define Custom Types

Mods write a custom opcode byte as the first byte of a TGMessage payload:

```python
# Example: Kobayashi Maru
KM_CUSTOM_MESSAGE = 205
kStream.WriteChar(chr(KM_CUSTOM_MESSAGE))
# ... write payload data ...
pMessage.SetDataFromStream(kStream)
pNetwork.SendTGMessage(0, pMessage)    # broadcast
```

### How Custom Types Survive the Server

1. Client creates a TGMessage with a custom opcode (e.g., 205) in the payload
2. Transport layer wraps it in a standard type-0x00 transport message
3. Host receives the transport message and deserializes the payload opaquely
4. Host's C++ auto-relay forwards the message to all other clients (no inspection)
5. On receiving clients, the C++ dispatchers see opcode 205, find no matching case,
   and silently ignore it
6. The mod's Python handler reads opcode 205 from the payload and processes it

### Available Opcode Ranges

| Range | Used By |
|-------|---------|
| 0x00-0x2A (0-42) | C++ dispatchers (stock game opcodes) |
| 0x2C-0x2D (44-45) | Stock Python: chat messages |
| 0x2E-0x34 (46-52) | **Unused** (available for mods) |
| 0x35-0x39 (53-57) | Stock Python: scoring/game flow |
| 0x3A-0x3E (58-62) | **Unused** (available for mods) |
| 0x3F-0x41 (63-65) | Stock Python: team mode scoring |
| 0x42-0xFF (66-255) | **Unused** (available for mods) |

Mods can also reuse stock Python opcodes by replacing the Python handlers.

### Known Mod Allocations

| Mod | Types | Decimal |
|-----|-------|---------|
| Stock team modes | MAX_MESSAGE_TYPES + 20-22 | 63-65 |
| Kobayashi Maru | hardcoded | 205, 211-214 |
| BC Remastered | MAX_MESSAGE_TYPES + 10-14 | 53-57 (replaces stock handlers) |

---

## Behavioral Guarantees

For a clean-room reimplementation, the following behaviors must be preserved:

1. **The host MUST relay all game messages to all other clients**, regardless of the
   game opcode value. Introducing filtering or whitelisting would break mod compatibility.

2. **The game opcode byte MUST NOT be examined during relay.** The message payload is
   an opaque blob at the transport/relay layer.

3. **Unknown game opcodes MUST be silently ignored** by C++ dispatchers. No error logging,
   no disconnection, no rejection.

4. **Python event handlers MUST fire for all incoming messages**, not just those with
   known opcodes. This allows mods to register handlers for custom types.

5. **The "NoMe" group MUST be routing-only** — it selects recipients, it does not
   filter or validate message content.

6. **SendTGMessage(0, msg) from a client MUST reach the host**, which then relays to
   all other clients. This is the standard mod broadcasting pattern.

7. **No maximum message type enforcement beyond byte width** (0-255).

---

## Implementation Considerations for Dedicated Server

A headless dedicated server reimplementation must:

1. **Relay all received game messages** to all connected clients (replicating the C++
   auto-relay behavior).

2. **Not add filtering** based on game opcode. Even if the server doesn't understand
   a custom mod message type, it must forward it.

3. **Handle Python-level messages** (chat, scoring) if the server needs to participate
   in game logic (e.g., computing scores, managing game state).

4. **Preserve the star topology** — clients expect to send only to the host, and expect
   the host to relay to all other clients.

5. **Support the "NoMe" group** for Python-level broadcasting that excludes the sender.

6. **Not crash or disconnect clients** for sending unrecognized message types. Silent
   ignore is the correct behavior.
