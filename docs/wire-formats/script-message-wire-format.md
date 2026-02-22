# Script Message Wire Format

How Python-created network messages (TGMessage) are framed on the UDP wire. Covers the creation API, wire encoding, send/receive dispatch, and byte-level examples.

Derived from protocol analysis of stock BC packet captures and reference Python scripts (MissionShared.py, MultiplayerMenus.py, MultiplayerGame.py).

---

## 1. Overview

Bridge Commander has two categories of game-layer network messages:

1. **Engine opcodes (0x00-0x2A)**: Handled by C++ dispatch tables. Structured formats defined by the engine (object creation, weapon fire, state updates, etc.)

2. **Script messages (0x2C+)**: Created by Python scripts using `TGMessage_Create()` + `TGBufferStream`. The payload is entirely script-defined. The first byte is conventionally a message type identifier.

Both categories travel inside **the same type 0x32 transport envelope** (see [transport-layer.md](../protocol/transport-layer.md)). There is no separate transport type for script messages.

---

## 2. MAX_MESSAGE_TYPES Constant

`MAX_MESSAGE_TYPES = 43` (0x2B)

This constant defines the boundary between engine opcodes and script message types. Python scripts define custom types as `MAX_MESSAGE_TYPES + N` to avoid colliding with engine opcodes.

### Stock Message Types

| Constant | Value | Hex | Defined In | Purpose |
|----------|-------|-----|------------|---------|
| MAX_MESSAGE_TYPES | 43 | 0x2B | Engine (SWIG) | Boundary marker |
| CHAT_MESSAGE | 44 | 0x2C | MultiplayerMenus | Player chat |
| TEAM_CHAT_MESSAGE | 45 | 0x2D | MultiplayerMenus | Team-only chat |
| MISSION_INIT_MESSAGE | 53 | 0x35 | MissionShared | Game config at join |
| SCORE_CHANGE_MESSAGE | 54 | 0x36 | MissionShared | Score delta on kill |
| SCORE_MESSAGE | 55 | 0x37 | MissionShared | Full score sync |
| END_GAME_MESSAGE | 56 | 0x38 | MissionShared | Game over |
| RESTART_GAME_MESSAGE | 57 | 0x39 | MissionShared | Restart broadcast |
| SCORE_INIT_MESSAGE | 63 | 0x3F | Mission5 (team modes) | Team score init |
| TEAM_SCORE_MESSAGE | 64 | 0x40 | Mission5 (team modes) | Team score update |
| TEAM_MESSAGE | 65 | 0x41 | Mission5 (team modes) | Team assignment |

Mods can define custom types using any value from 43 to 255 (the type is a single byte written via `WriteChar`).

---

## 3. Creation and Sending Pattern

The canonical Python pattern for creating and sending a script message:

```python
# Create message object
pMessage = App.TGMessage_Create()
pMessage.SetGuaranteed(1)           # Reliable delivery (recommended)

# Create write stream and fill payload
kStream = App.TGBufferStream()
kStream.OpenBuffer(256)             # Allocate write buffer

kStream.WriteChar(chr(messageType)) # First byte: message type (e.g., 0x38)
kStream.WriteInt(someValue)         # Additional payload data
kStream.WriteShort(textLength)
kStream.Write(textString, textLength)

# Attach payload to message and send
pMessage.SetDataFromStream(kStream) # Copies stream bytes into message
pNetwork.SendTGMessage(0, pMessage) # 0 = broadcast to all peers
kStream.CloseBuffer()               # Clean up stream buffer
```

### Key Points

- `SetDataFromStream` copies the stream's written bytes directly into the message's data buffer. **No additional framing or header is added.** The stream content IS the message payload.
- `SetGuaranteed(1)` enables reliable delivery (ACK + retransmit). The default after `TGMessage_Create()` is unreliable. All stock scripts explicitly set guaranteed=1.
- The message type byte is the **first byte of the payload**, written by the script. The engine does not prepend anything.

### Stream Write Primitives

All writes are little-endian (native x86):

| Python Method | Size | Format |
|---------------|------|--------|
| `WriteChar(chr(N))` | 1 byte | uint8 |
| `WriteBool(N)` | 1 byte | uint8 (0 or 1) |
| `WriteShort(N)` | 2 bytes | uint16 LE |
| `WriteInt(N)` | 4 bytes | int32 LE |
| `WriteLong(N)` | 4 bytes | int32 LE (same as WriteInt on 32-bit) |
| `WriteFloat(N)` | 4 bytes | float32 LE (IEEE 754) |
| `Write(buf, len)` | N bytes | Raw memcpy |
| `WriteCString(s)` | 2+N bytes | [uint16 LE strlen][raw chars, no null] |

---

## 4. Wire Format

### No Game-Layer Header

Script messages have **zero additional framing** between the type 0x32 transport envelope and the script payload. The game-layer payload starts directly with the script's first `WriteChar` byte.

```
UDP packet (decrypted):
  [peer_id:1][msg_count:1]
    [0x32][flags_len:2 LE][seq:2 if reliable]  ← transport envelope
    [script_payload...]                          ← starts with message type byte
```

### Payload = Exactly What the Script Wrote

When a script writes:
```python
kStream.WriteChar(chr(0x38))  # END_GAME_MESSAGE
kStream.WriteInt(2)            # reason code
```

The TGMessage payload is exactly 5 bytes: `38 02 00 00 00`

The transport layer wraps this with its own header (type byte + flags_len + optional seq_num), but the game-layer payload is the raw stream content with no additions.

---

## 5. Byte-By-Byte Wire Examples

### Example 1: CHAT_MESSAGE (0x2C)

Python code:
```python
kStream.WriteChar(chr(CHAT_MESSAGE))     # 0x2C
kStream.WriteLong(pNetwork.GetLocalID()) # sender network ID (e.g., 2)
kStream.WriteShort(5)                    # text length
kStream.Write("hello", 5)               # raw text bytes
```

TGMessage payload (12 bytes):
```
2C 02 00 00 00 05 00 68 65 6C 6C 6F
^^                                      message type (CHAT_MESSAGE = 44)
   ^^ ^^ ^^ ^^                         sender ID (int32 LE = 2)
               ^^ ^^                    text length (uint16 LE = 5)
                     ^^ ^^ ^^ ^^ ^^    "hello" (raw bytes, no null)
```

Type 0x32 transport message (17 bytes):
```
32 11 80 01 00 2C 02 00 00 00 05 00 68 65 6C 6C 6F
^^                                                     type 0x32
   ^^ ^^                                               flags_len LE = 0x8011
                                                         bit 15 = reliable
                                                         bits 12-0 = 17 (total size)
         ^^ ^^                                          seq_num LE = 0x0001
               ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^  payload (12 bytes)
```

Full UDP packet (19 bytes, shown decrypted):
```
01 01 32 11 80 01 00 2C 02 00 00 00 05 00 68 65 6C 6C 6F
^^                                                           peer_id (server)
   ^^                                                        msg_count (1)
      ^^...                                                  transport message
```

### Example 2: Custom Mod Message (type 205)

Python code:
```python
MY_MSG = App.MAX_MESSAGE_TYPES + 162  # = 43 + 162 = 205 = 0xCD
kStream.WriteChar(chr(MY_MSG))        # 0xCD
kStream.WriteInt(42)                  # custom data
```

TGMessage payload (5 bytes):
```
CD 2A 00 00 00
^^              custom message type (205)
   ^^ ^^ ^^ ^^ int value 42 (int32 LE)
```

Type 0x32 transport message (10 bytes):
```
32 0A 80 01 00 CD 2A 00 00 00
^^                              type 0x32
   ^^ ^^                        flags_len LE = 0x800A (reliable, size=10)
         ^^ ^^                  seq_num LE = 0x0001
               ^^ ^^ ^^ ^^ ^^  payload (5 bytes)
```

### Example 3: END_GAME_MESSAGE (0x38)

Python code (from MissionShared.py):
```python
kStream.WriteChar(chr(END_GAME_MESSAGE))  # 0x38
kStream.WriteInt(iReason)                  # reason code (int32)
```

TGMessage payload (5 bytes):
```
38 03 00 00 00
^^              END_GAME_MESSAGE (56)
   ^^ ^^ ^^ ^^ reason code 3 (int32 LE)
```

---

## 6. Send Functions

### SendTGMessage(targetID, message)

Sends a message to a specific peer or broadcasts to all.

| targetID | Behavior |
|----------|----------|
| `0` | **Broadcast**: Send to ALL connected peers (message cloned for each) |
| `> 0` | **Unicast**: Send to the specific peer with that network ID |
| `pNetwork.GetHostID()` | Send to host only (common for client→server messages) |

### SendTGMessageToGroup(groupName, message)

Sends a message to all members of a named network group.

**Built-in groups** (created by the multiplayer game constructor):

| Group Name | Members | Used For |
|------------|---------|----------|
| `"NoMe"` | All connected peers EXCEPT the sender | Chat relay (host receives chat, forwards to "NoMe") |
| `"Forward"` | Same membership as "NoMe" | Engine event forwarding (weapon fire, cloak, etc.) |

Both groups contain the same peers. The naming convention distinguishes script-level relay ("NoMe") from engine-level event forwarding ("Forward").

### Guaranteed vs Unreliable

- `SetGuaranteed(1)`: Reliable delivery — transport sets bit 15 in flags_len, includes seq_num, ACKs and retransmits. **All stock scripts use this.**
- `SetGuaranteed(0)` (default): Fire-and-forget — no seq_num, no ACK, no retransmit. Supported but never used by stock scripts.

---

## 7. Receive-Side Dispatch

When a type 0x32 message arrives from the network:

1. **Transport layer** deserializes flags_len, seq_num, and payload from the UDP packet
2. **Reliable delivery** handles ACK generation and fragment reassembly if needed
3. **Event system** wraps the completed message as an `ET_NETWORK_MESSAGE_EVENT` and posts it to the event manager
4. **Multiple handlers** receive the event simultaneously:
   - **C++ game handler**: Reads first payload byte, dispatches opcodes 0x02-0x2A via switch table. Opcodes outside this range (including all script messages 0x2C+) are **ignored** by this handler.
   - **C++ UI handler**: Dispatches opcodes 0x00, 0x01, 0x16
   - **C++ checksum handler**: Dispatches opcodes 0x20-0x27
   - **Python handlers**: Registered via event system. Read first byte to identify message type, dispatch to appropriate handler function.

The C++ handlers and Python handlers are **independent** — they all receive the same event and each processes only the opcodes it recognizes. Script messages (0x2C+) pass through C++ handlers without effect and are handled exclusively by Python.

### Python Receive Pattern

```python
def ProcessMessageHandler(pObject, pEvent):
    pMessage = pEvent.Message()
    kStream = pMessage.GetBufferStream()

    cType = ord(kStream.ReadChar())  # Read message type byte

    if cType == CHAT_MESSAGE:
        HandleChat(kStream)
    elif cType == END_GAME_MESSAGE:
        HandleEndGame(kStream)
    # ... etc
```

---

## 8. Stock Message Formats

### CHAT_MESSAGE (0x2C)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x2C
1       4     i32     sender_network_id (from pNetwork.GetLocalID())
5       2     u16     text_length
7       var   bytes   message_text (ASCII, no null terminator)
```

Direction: Client → Host → "NoMe" group (relay pattern)

### TEAM_CHAT_MESSAGE (0x2D)

Same format as CHAT_MESSAGE but relayed only to team members.

### MISSION_INIT_MESSAGE (0x35)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x35
1       1     u8      current_player_count
2       1     u8      system_index
3       1     u8      time_limit (0xFF = no limit)
[if time_limit != 0xFF:]
4       4     i32     end_time
[end if]
+0      1     u8      frag_limit
```

> **Correction**: The first payload byte is `current_player_count` (dynamic, u8), not
> `player_limit` (i32). Stock servers send the number of currently connected players.
> The join-flow document has the authoritative format; this section is corrected to match.

Direction: Host → joining client (sent after client sends NewPlayerInGame 0x2A)

### SCORE_CHANGE_MESSAGE (0x36)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x36
1       4     i32     killer_player_id (0 if suicide/environment)
[if killer_player_id != 0:]
5       4     i32     killer_kills
9       4     i32     killer_score
[end if]
+0      4     i32     victim_player_id
+4      4     i32     victim_deaths
+8      1     u8      update_count (number of additional score entries)
[repeated update_count times:]
  +0    4     i32     player_id
  +4    4     i32     score
[end repeat]
```

Direction: Host → all clients (broadcast on kill)
All player IDs in this message use the network ID domain (`GetNetID()` / wire slot).

### SCORE_MESSAGE (0x37)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x37
1       4     i32     player_id
5       4     i32     kills
9       4     i32     deaths
13      4     i32     score
```

Direction: Host → joining client (full score roster sync, one message per player)
`player_id` uses the network ID domain (`GetNetID()` / wire slot).

### END_GAME_MESSAGE (0x38)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x38
1       4     i32     reason_code
```

Reason codes: 0-6 (game-specific end conditions)
Direction: Host → all clients (broadcast)

### RESTART_GAME_MESSAGE (0x39)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x39
```

Direction: Host → all clients (broadcast, 1 byte, opcode only)

---

## 9. Implementation Notes for Server

### Relay Behavior

The server must handle script messages in two ways:

1. **Messages it originates** (scoring, game flow): Create the payload, wrap in type 0x32, send to peers
2. **Messages it relays** (chat): Receive from sender, parse enough to identify type, forward to appropriate peers

For chat relay, the server receives CHAT_MESSAGE from a client (sent to host), then must create a new message with the same payload and send it to the "NoMe" group (all peers except the original sender).

### No Payload Modification

The server should never modify the script payload content when relaying. The payload bytes between the message type byte and end-of-message are opaque to the relay — just copy and forward.

### Custom Mod Messages

Mods may define message types in the 43-255 range. A generic server should relay any unrecognized message type byte to all peers (or the appropriate group) without parsing the payload. This enables mod compatibility without server-side mod knowledge.

---

## Related Documents

- [transport-layer.md](../protocol/transport-layer.md) — Transport framing (type 0x32 envelope, flags_len, fragmentation)
- [transport-cipher.md](../protocol/transport-cipher.md) — AlbyRules stream cipher
- [phase1-verified-protocol.md](phase1-verified-protocol.md) — Complete opcode table and protocol flow
- [join-flow.md](../network-flows/join-flow.md) — When MISSION_INIT_MESSAGE is sent during join
- [disconnect-flow.md](../network-flows/disconnect-flow.md) — Cleanup on player leave
