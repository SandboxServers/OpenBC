# Legacy BC Network Protocol - Phase 1 Implementation Plan

## Decision: Raw UDP, NOT ENet

ENet uses its own wire format (ENet protocol headers, channel system, connection handshake).
A vanilla BC client speaks TGWinsockNetwork's custom binary protocol. The two are incompatible
at the wire level. There is no way to make ENet speak TGWinsockNetwork's protocol without
essentially reimplementing the transport layer anyway.

**We must reimplement TGWinsockNetwork from scratch using raw UDP sockets.**

ENet may still be useful as a reference for reliable UDP patterns, but the actual bytes on
the wire must match what stbc.exe expects.

---

## 1. Transport Layer (TGWinsockNetwork Reimplementation)

### 1.1 Socket Management

Create a single non-blocking UDP socket on port 22101 (0x5655):
```c
SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
bind(sock, INADDR_ANY:22101);
ioctlsocket(sock, FIONBIO, &one);  // non-blocking
```

This socket is shared between the game protocol and GameSpy LAN discovery.
A peek-based router (Section 2) demultiplexes them.

### 1.2 Connection State Machine

```
DISCONNECTED (4)
      |
      | HostOrJoin(addr=0) for server
      v
    HOST (2) <--- server stays here permanently
      |
      | client sends connection message (type 3)
      v
  peer created, ET_NETWORK_NEW_PLAYER fired
```

States (stored at WSN+0x14):
- **4 (DISCONNECTED)**: Initial state. No socket. No processing.
- **2 (HOST)**: Listening for connections. Processes all 3 sub-functions in Update.
  Fires ET_NETWORK_NEW_PLAYER when new peers connect.
  Dequeues received messages and posts them as ET_NETWORK_MESSAGE_EVENT (0x60001).
- **3 (CLIENT)**: Connected to host. Sends initial connection packet.
  Processes incoming, dispatches to event system.

For Phase 1 (standalone server), we only implement HOST state.

### 1.3 Peer Tracking

**Peer Array**: Sorted array of peer pointers, stored at WSN+0x2C.
Binary-searched by peer ID (peer+0x18). Peer count at WSN+0x30.

Each peer tracks:
```c
struct Peer {
    int32_t  peerID;           // +0x18, unique identifier
    uint32_t address;          // +0x1C, sockaddr_in equivalent
    uint16_t seqRecvUnrel;     // +0x24, expected unreliable seq from peer
    uint16_t seqSendReliable;  // +0x26, next reliable seq to assign
    uint16_t seqRecvReliable;  // +0x28, expected reliable seq from peer
    uint16_t seqSendPriority;  // +0x2A, next priority seq to assign
    float    lastRecvTime;     // +0x2C
    float    lastSendTime;     // +0x30

    // 3 outbound queues (linked lists)
    Queue    unreliable;       // +0x64..+0x7C (count at +0x7C)
    Queue    reliable;         // +0x80..+0x98 (count at +0x98)
    Queue    priority;         // +0x9C..+0xB4 (count at +0xB4)

    // Stats
    int32_t  bytesSentPayload; // +0x48
    int32_t  bytesSentTotal;   // +0x54
    int32_t  bytesRecvPayload; // +0x38 (mirror: +0x0E for self)
    int32_t  bytesRecvTotal;   // +0x40

    float    disconnectTime;   // +0xB8
    uint8_t  isDisconnecting;  // +0xBC
};
```

**Peer creation** (FUN_006b7410): Allocates peer, inserts into sorted array,
assigns peer ID. For incoming connections with peerID=-1, assigns new ID.

**Self-peer**: The host creates a peer for itself (peerID = WSN+0x18 = host's own ID).
This is used for loopback messages and stats tracking.

### 1.4 Wire Packet Format

Every UDP packet sent/received has this structure:
```
Offset  Size   Field
0x00    1      senderPeerID (low byte)
0x01    1      messageCount (1-254, number of serialized messages in this packet)
0x02    ...    message[0] serialized data
...     ...    message[1] serialized data
...     ...    message[N-1] serialized data
```

The first byte is the **low byte of the sender's peer ID**. This is how the receiver
identifies who sent the packet (combined with the source sockaddr).

Message count capped at 0xFE (254).

### 1.5 Message Serialization

Each message in the packet is serialized by its vtable+0x08 method (Serialize).
The exact format depends on message type. Each message also has a vtable+0x14 method
that returns the serialized size.

Messages have a type code returned by vtable+0x00 (GetType):
```
Type 0: Connection request (includes peer info, password)
Type 1: Reliable ACK
Type 2: Internal/padding (discarded on receive)
Type 3: Data message (game payload - this carries opcodes 0x00-0x27)
Type 4: Disconnect
Type 5: Keepalive/ping
```

### 1.6 Data Message (Type 3) Serialization

The data message (type 3) is the primary carrier. Its serialized form includes:
```
[message_type_byte]     - identifies this as type 3 via dispatch table
[sequence_number: u16]  - for reliable ordering
[flags: u8]             - includes reliable flag
[payload_length: varies]
[payload_data]          - the actual game/checksum opcode data
```

The payload starts with the opcode byte (0x00 for settings, 0x20 for checksum request, etc.).

### 1.7 Message Queuing (TGNetwork::Send = FUN_006b4c10)

When application code calls Send(WSN, peerID, message, flags):
1. Binary search peer array for peerID
2. Call FUN_006b5080 to queue the message
3. FUN_006b5080 checks message type and reliable flag:
   - type < 0x32 (50) with reliable flag -> sequence from peer+0x26, queue to reliable
   - type >= 0x32 with reliable flag -> sequence from peer+0x2A, queue to priority
   - unreliable -> sequence from peer+0x28 (if applicable), queue to unreliable
4. After queuing, increment the appropriate sequence counter
5. Check buffer limit (WSN+0xA8 = 0x8000); reject if peer queue too full (return 10)

### 1.8 Outbound Processing (FUN_006b55b0 - SendOutgoingPackets)

Called every tick from TGNetwork::Update. For each peer (round-robin starting from WSN+0x2C):

1. Check WSN+0x10C (sendEnabled flag) - skip if 0
2. Allocate packet buffer of packetSize bytes (WSN+0x2B, default 0x200=512 for TGWinsockNetwork)
3. Write sender peerID low byte at offset 0
4. Reserve offset 1 for message count
5. Start writing messages from offset 2:

**Priority queue first** (peer+0x9C):
   - Iterate linked list via cursor
   - For each message: check FUN_006b8700 (ready-to-send timing check)
   - If ready AND retryCount < 3: serialize, increment retryCount, record send time
   - If retryCount >= 8: drop message (timeout)
   - Cap at 254 total messages

**Reliable queue second** (peer+0x80):
   - Similar iteration
   - If message exceeds reliableTimeout (WSN+0x2D = 360.0f): drop it
   - Otherwise serialize and bump retry count

**Unreliable queue last** (peer+0x64):
   - First unreliable message always serialized (flag-based skip logic)
   - After serialize: for reliable messages, move to reliable retry queue
   - For unreliable: free after sending

6. Write final message count at offset 1
7. If messageCount > 0 AND statsEnabled (WSN+0x44):
   - Update peer stats (bytesSent)
   - Update self-peer stats
8. Call vtable+0x70 (sendto) with the buffer

### 1.9 Inbound Processing (FUN_006b5c90 - ProcessIncomingPackets)

Called every tick. Loops on recvfrom:

1. Call vtable+0x6C (recvfrom wrapper) to get packet + source address
2. Parse first byte as senderPeerID
3. Parse second byte as messageCount
4. For each message in packet:
   - Deserialize using dispatch table at DAT_009962d4 (indexed by first byte of message)
   - Set message's senderPeerID and source address
   - Look up peer by senderPeerID in peer array

   **If peerID == -1 (new connection)**:
   - Check message type 3 (connection) or type 5 (keepalive)
   - For type 3: extract proposed peer info, create peer via FUN_006b7410
   - For type 5: similar new-peer handling

   **If known peer**:
   - Update peer's lastRecvTime
   - Update stats if enabled

5. If message is reliable (flag at +0x3A != 0):
   - Call FUN_006b61e0 (ACK handler)
   - Search priority queue for matching sequence/flags
   - If found: reset retry counter (message was ACKed)
   - If not found: create ACK entry and add to priority queue (will be sent back)

6. Call FUN_006b6ad0 to validate sequence and queue for application delivery

### 1.10 Dispatch to Application (FUN_006b5f70 + FUN_006b6ad0)

FUN_006b6ad0 performs sequence validation:
- Compare message sequence (at msg+5, u16) against peer's expected sequence (peer+0x24 or +0x28)
- If sequence matches expected: accept, increment expected
- If behind expected (within window of 0x4000): discard as duplicate
- If ahead: buffer for later delivery (out-of-order handling)

FUN_006b5f70 dispatches accepted messages:
- Switch on message type (vtable+0x00):
  - Type 0: Connection established (FUN_006b63a0) - fires ET_NETWORK_NEW_PLAYER (0x60007)
  - Type 1: ACK processing (FUN_006b64d0) - removes from reliable queue
  - Type 3: Data delivery (FUN_006b6640) - queues for app
  - Type 4: Disconnect (FUN_006b6a70)
  - Type 5: Keepalive (FUN_006b6a20)

For HOST state, dequeued messages become ET_NETWORK_MESSAGE_EVENT (0x60001) events
posted to EventManager.

### 1.11 ACK System Details

When a reliable message arrives:
1. Receiver creates an ACK message (type 1) containing the sequence number
2. ACK is queued in the receiver's priority queue for the sender
3. ACK is sent in the next outbound packet

When sender receives ACK back:
1. FUN_006b61e0 matches ACK against priority queue entries
2. Matching criteria: sequence number + type flag + fragment flag
3. On match: reset retry counter to 0, mark as acknowledged
4. Acknowledged messages are removed from the retry queue

**Retry timing**: Checked by FUN_006b8700. Messages have a send timestamp and a
retry interval. If current_time - send_time > retry_interval, message is ready for resend.

### 1.12 Timeout and Disconnect

- **Reliable timeout** (WSN+0x2D = 360.0f seconds): If a reliable message has been
  retransmitted for this long without ACK, it's dropped
- **Disconnect timeout** (WSN+0x2E = 45.0f seconds): If no activity from a peer
  for this long, the peer is disconnected
- **Priority max retries**: After 8 retries, priority messages are dropped
- **Disconnect detection**: In SendOutgoingPackets, after processing all queues,
  check each peer's lastActivity time. If exceeded, call vtable+0x74 (disconnect callback)

---

## 2. GameSpy LAN Discovery

### 2.1 Query Format

GameSpy queries are text-based UDP packets starting with backslash '\' (0x5C):
```
\basic\        - Request basic server info
\status\       - Request detailed status
\info\         - Request game info
```

These are part of the GameSpy QR (Query/Report) SDK from ~2001.

### 2.2 Response Format

Responses are also backslash-delimited key-value pairs:
```
\hostname\MyServer\numplayers\2\maxplayers\16\mapname\DeepSpace9\gametype\Deathmatch\
```

Standard GameSpy QR fields used by BC:
- `hostname` - Server name
- `numplayers` - Current player count
- `maxplayers` - Maximum players
- `mapname` - Current map name
- `gametype` - Game mode
- `gamemode` - Additional mode info
- `hostport` - Game port (22101)

The GameSpy object is 0xF4 bytes. The qr_t (query/report struct) is at GameSpy+0xDC.
The qr_t socket is at qr_t+0x00 (same socket as WSN+0x194 -- they share it).

### 2.3 Peek-Based Router

The shared socket problem: both GameSpy queries and game packets arrive on port 22101.

Solution (implemented in the STBC proxy, to be reimplemented cleanly):
```
loop:
    select(sock, timeout=0)  // check if data available
    if no data: break

    recvfrom(sock, &peekByte, 1, MSG_PEEK)  // peek without consuming

    if peekByte == '\\':
        // GameSpy query
        recvfrom(sock, buf, 256, 0)  // consume packet
        handle_gamespy_query(buf, sender_addr)
    else:
        // Binary game packet - leave in socket buffer
        break  // TGNetwork::Update will consume it via its own recvfrom
```

**Critical**: qr_t+0xE4 must be set to 0 to disable GameSpy's own recvfrom loop.
GameSpy_Tick is still called for internal state management but doesn't read the socket.

### 2.4 Implementation for OpenBC

For the standalone server, we implement GameSpy QR response directly:
- Parse incoming `\basic\`, `\status\`, `\info\` queries
- Respond with appropriate key-value pairs
- No need to link the actual GameSpy SDK; it's a simple text protocol

---

## 3. Checksum Exchange Protocol

### 3.1 Overview

After a client connects, the server verifies that the client has matching script files.
This is a 4-round sequential exchange where the server sends one request at a time
and waits for the client's response before sending the next.

### 3.2 Opcode 0x20: Checksum Request (Server -> Client)

```
Offset  Size    Field
0x00    1       opcode = 0x20
0x01    1       index (0-3)
0x02    2       directory string length (u16, little-endian)
0x04    N       directory string (NOT null-terminated)
0x04+N  2       filter string length (u16, little-endian)
0x06+N  M       filter string (NOT null-terminated)
0x06+N+M 1      recursive flag (0 or 1)
```

Built by FUN_006a39b0 using a stream serializer:
- FUN_006cf730(stream, 0x20) - write opcode byte
- FUN_006cf730(stream, index) - write index byte
   Wait, looking more carefully at FUN_006a35b0 (the actual builder called by 006a39b0):
- FUN_006cf730(stream, 0x20) - opcode
- FUN_006cf730(stream, 0xFF) - special marker (index comes from the hash table key)
- FUN_006cf7f0(stream, dirLen) - directory length as u16
- FUN_006cf2b0(stream, dir, dirLen) - directory bytes
- FUN_006cf7f0(stream, filterLen) - filter length as u16
- FUN_006cf2b0(stream, filter, filterLen) - filter bytes
- FUN_006cf770(stream, recursive) - recursive flag byte

**Correction**: Looking at the actual FUN_006a39b0 call vs FUN_006a35b0, the builder
writes 0x20 then 0xFF for the queue entry. But the index byte comes from the hash table
lookup. The queued message stores the directory/filter info for server-side re-verification.

For the FIRST request (index 0), the message is sent immediately via TGNetwork::Send.
For indices 1-3, messages are queued in NetFile hash table B but NOT sent yet.

### 3.3 The 4 Checksum Requests

| Index | Directory          | Filter          | Recursive |
|-------|--------------------|-----------------|-----------|
| 0     | scripts/           | App.pyc         | 0         |
| 1     | scripts/           | Autoexec.pyc    | 0         |
| 2     | scripts/ships/     | *.pyc           | 1         |
| 3     | scripts/mainmenu/  | *.pyc           | 0         |

### 3.4 Opcode 0x21: Checksum Response (Client -> Server)

```
Offset  Size    Field
0x00    1       opcode = 0x21
0x01    1       index (0-3, or 0xFF for file transfer)
0x02    ...     hash data (variable, depends on index)
```

For index 0: includes a reference string hash (PTR_DAT_008d9af4) followed by
directory hash and per-file checksums.

For indices 1-3: directory hash + per-file checksums.

The hash data format from FUN_0071f270:
- Scans directory with FindFirstFile/FindNextFile
- For each matching file: computes hash via FUN_007202e0
- Builds a sorted list of (filename_hash, file_content_hash) pairs
- The response contains these pairs

### 3.5 Hash Function (FUN_007202e0)

**This is NOT MD5 or CRC32.** It is a custom 4-table byte-XOR hash:

```c
uint32_t hash_string(const char* str) {
    uint8_t a = 0, b = 0, c = 0, d = 0;
    while (*str) {
        uint8_t ch = (uint8_t)*str;
        a = table_A[ch ^ a];  // table at 0x0095c888
        b = table_B[ch ^ b];  // table at 0x0095c988
        c = table_C[ch ^ c];  // table at 0x0095ca88
        d = table_D[ch ^ d];  // table at 0x0095cb88
        str++;
    }
    return (a << 24) | (b << 16) | (c << 8) | d;
}
```

Four 256-byte lookup tables. Each byte of the input is XORed with the running
accumulator for that table position, then used to index into the table.
Result is 4 bytes combined into a 32-bit hash.

**To implement**: We need to extract the 4x256 lookup tables from the stbc.exe binary.
They are at static addresses 0x0095c888, 0x0095c988, 0x0095ca88, 0x0095cb88.

### 3.6 Reference String Hash (PTR_DAT_008d9af4)

For checksum request index 0 only, the server also checks a "reference string hash."
PTR_DAT_008d9af4 points to a static string in the binary. Its hash is compared against
the client's hash. If mismatch: opcode 0x23 (reference mismatch) is sent.

This string likely identifies the game version/build. We need to extract it from the binary.

### 3.7 SkipChecksum Flag (DAT_0097f94c)

If this global flag is set, checksum behavior changes significantly. The exact
behavior when set is unclear from the decompilation but it likely skips the file
hash verification entirely and goes straight to ET_CHECKSUM_COMPLETE.

For Phase 1, we should support this flag as a server configuration option.

### 3.8 Opcode 0x22: Checksum Fail - File Mismatch (Server -> Client)

Sent when client's file hashes don't match server's. Contains info about which
file(s) differ. Handler: FUN_006a4c10.

### 3.9 Opcode 0x23: Checksum Fail - Reference Mismatch (Server -> Client)

Sent when the reference string hash doesn't match. This indicates a version mismatch.

### 3.10 Opcode 0x25: File Transfer (Bidirectional)

Used to transfer mismatched files from server to client. Processed by FUN_006a5860
(server side) and FUN_006a3ea0 (client side).

The server checks NetFile hash table C for pending file transfers after each
checksum verification. If files need transferring, opcode 0x25 packets carry the data.

### 3.11 Opcode 0x27: Unknown

Handler at FUN_006a4250. Purpose unclear from decompilation. May be related to
file transfer acknowledgment or cancellation.

### 3.12 Opcode 0x28: All Files Transferred (Server -> Client)

Sent by FUN_006a5860 when hash table C is empty (no pending file transfers).
Signals completion of the file transfer phase.

### 3.13 Server-Side Flow (Sequential)

```
1. NewPlayerHandler fires
2. Call FUN_006a3820(NetFile, peerID)
3. Build 4 requests, queue ALL in hash table B
4. Send request #0 immediately
5. Wait for response (opcode 0x21)
6. FUN_006a4560 verifies:
   a. Look up queued request in hash table B
   b. Compute SERVER-SIDE hash of same directory
   c. Compare client hash vs server hash
   d. For index 0: also compare reference string hash
7. If match: dequeue from B, send NEXT request from B
8. If mismatch: send opcode 0x22 or 0x23
9. When hash table B is empty: fire ET_CHECKSUM_COMPLETE
10. ChecksumCompleteHandler sends opcode 0x00 + 0x01
```

**Key insight**: The server does NOT send all 4 requests at once. It sends #0,
waits for response, verifies, then sends #1, and so on. This is why the priority
queue stall issue blocks progress -- if ACKs aren't working, request #1 never sends.

---

## 4. Game Message Protocol

### 4.1 Opcode 0x00: Settings/Verification (Server -> Client)

Sent by ChecksumCompleteHandler (FUN_006a1b10) after all checksums pass.

```
Offset  Size    Field
0x00    1       opcode = 0x00
0x01    4       gameTime (float32, little-endian)
0x05    1       setting1 (byte) - from DAT_008e5f59
0x06    1       setting2 (byte) - from DAT_0097faa2
0x07    1       playerSlot (byte, 0-15)
0x08    2       mapNameLength (u16, little-endian)
0x0A    N       mapName (NOT null-terminated)
0x0A+N  1       passFail (byte, 0 = pass, 1 = checksum had differences)
```

If passFail == 1, additional data follows containing the hash comparison result.

Built by FUN_006a1b10 using:
- FUN_006cf730(stream, 0x00) - opcode
- FUN_006cf8b0(stream, gameTime) - float write
- FUN_006cf770(stream, setting1) - byte
- FUN_006cf770(stream, setting2) - byte
- FUN_006cf730(stream, playerSlot) - byte
- FUN_006cf7f0(stream, mapNameLen) - u16
- FUN_006cf2b0(stream, mapName, mapNameLen) - raw bytes
- FUN_006cf770(stream, passFail) - byte
- If passFail: FUN_006f3f30(hashData, stream) - additional hash data

Sent as reliable (msg+0x3A = 1).

### 4.2 Opcode 0x01: Status Byte (Server -> Client)

Sent immediately after opcode 0x00:
```
Offset  Size    Field
0x00    1       opcode = 0x01
```

Just a single byte. Sent reliable. Signals "ready for game setup."

### 4.3 Game Opcodes 0x02-0x1F (Partially Known)

These are dispatched by MultiplayerGame::ReceiveMessageHandler (0x0069f2a0).
The handler registrations at FUN_0069efe0 give us the handler NAMES but not which
opcode maps to which handler. The opcodes are determined by the switch statement
inside the ReceiveMessageHandler, which we'd need to decompile separately.

Known handler names (from FUN_0069efe0 registration):
```
ReceiveMessageHandler   - dispatches all game opcodes
DisconnectHandler       - peer disconnected
NewPlayerHandler        - new connection
SystemChecksumPassedHandler
SystemChecksumFailedHandler
DeletePlayerHandler     - player removed
ObjectCreatedHandler    - game object spawned
HostEventHandler        - host-specific events
NewPlayerInGameHandler  - player entered active game
StartFiringHandler      - weapon fire start
StopFiringHandler       - weapon fire stop
StopFiringAtTargetHandler
SubsystemStatusHandler  - subsystem damage/repair
ClientEventHandler      - client-specific events
ChecksumCompleteHandler - all checksums verified
KillGameHandler         - game termination
StartWarpHandler        - warp initiation
SetPhaserLevelHandler   - phaser power adjustment
ObjectExplodingHandler  - object destruction
EnterSetHandler         - entered bridge set
ExitedWarpHandler       - warp completion
TorpedoTypeChangeHandler - torpedo type switch
RetryConnectHandler     - connection retry
DeleteObjectHandler     - object removal
ChangedTargetHandler    - target selection change
StartCloakingHandler    - cloak activation
StopCloakingHandler     - cloak deactivation
AddToRepairListHandler  - repair queue
RepairListPriorityHandler - repair priority
```

**For Phase 1 (lobby only)**: We only need opcodes 0x00 and 0x01.
The in-game opcodes (0x02+) are needed in Phase 2.

---

## 5. Connection Lifecycle

### 5.1 Full Flow: Client Connect -> Lobby

```
CLIENT                              SERVER
  |                                    |
  |--- UDP connection msg (type 3) --->|
  |    [peerID=-1, password, addr]     |
  |                                    |-- ProcessIncomingPackets
  |                                    |-- Creates peer entry
  |<-- UDP connection ACK (type 0) ----|   (FUN_006b63a0)
  |                                    |-- Fires ET_NETWORK_NEW_PLAYER
  |                                    |
  |                                    |-- NewPlayerHandler (FUN_006a0a30):
  |                                    |   - Check +0x1F8 (ready for players)
  |                                    |   - Find empty slot in 16-slot array
  |                                    |   - Call FUN_006a3820 on NetFile
  |                                    |
  |<-- Checksum #0 (opcode 0x20) ------|
  |    [scripts/, App.pyc, nonrec]     |
  |                                    |
  |--- Checksum #0 resp (0x21) ------->|
  |                                    |-- FUN_006a4560 verifies hash
  |                                    |   Match -> send next
  |                                    |
  |<-- Checksum #1 (opcode 0x20) ------|
  |    [scripts/, Autoexec.pyc]        |
  |                                    |
  |--- Checksum #1 resp (0x21) ------->|
  |                                    |-- verify, send next
  |                                    |
  |<-- Checksum #2 (opcode 0x20) ------|
  |    [scripts/ships/, *.pyc, rec]    |
  |                                    |
  |--- Checksum #2 resp (0x21) ------->|
  |                                    |
  |<-- Checksum #3 (opcode 0x20) ------|
  |    [scripts/mainmenu/, *.pyc]      |
  |                                    |
  |--- Checksum #3 resp (0x21) ------->|
  |                                    |-- All passed:
  |                                    |   ET_CHECKSUM_COMPLETE
  |                                    |
  |<-- Settings (opcode 0x00) ---------|
  |    [gameTime, settings, slot, map] |
  |<-- Status (opcode 0x01) ----------|
  |                                    |
  |    CLIENT NOW IN LOBBY             |
```

### 5.2 Player Slot Assignment

MultiplayerGame has a 16-slot player array at this+0x74 (stride 0x18 per slot).

Each slot:
```
+0x00  active flag (1 byte)
+0x04  peer network ID (matches peer+0x18)
+0x08  ... (other player state)
```

Assignment (FUN_006a0a30):
1. Count active players (iterate all 16, count where +0x00 != 0)
2. If count < maxPlayers (this+0x1FC): find first empty slot, activate it
3. If full: send rejection message (type 3 with rejection code) to the connecting peer

### 5.3 Rejection

When server is full:
```c
// From FUN_006a0a30
msg_type = 3;  // rejection
payload[0] = (byte)peerID;
FUN_006b84d0(msg, &payload, 1);
FUN_006b4c10(WSN, peerID, msg, 0);
```

The rejection message is a data message (type 3) with piVar7[0x10]=3 and a 1-byte
payload containing the peer ID. The client recognizes this as a rejection.

### 5.4 Deferred Player Handling

If MultiplayerGame+0x1F8 == 0 (not ready for new players yet):
- A timer event is created with exponential backoff
- When the timer fires, RetryConnectHandler (FUN_006a2a40) re-attempts the join

For Phase 1 server: set +0x1F8 = 1 immediately during bootstrap (as the proxy does).

### 5.5 Disconnect

Disconnect can happen via:
1. **Timeout**: No packets from peer for disconnectTimeout (45s)
2. **Explicit**: Peer sends disconnect message (type 4)
3. **Server kick**: Server sends disconnect to peer

On disconnect:
- Remove peer from peer array
- Fire ET_NETWORK_DISCONNECT event
- DisconnectHandler (FUN_006a0a20) clears the player slot

---

## 6. Implementation Architecture for OpenBC

### 6.1 Why NOT ENet

ENet uses its own wire format:
- Connection handshake with challenge/response
- Channel-based multiplexing (up to 255 channels)
- Fragment/reassemble with its own framing
- Bandwidth throttling built into protocol
- Completely different header layout

A vanilla stbc.exe client expects TGWinsockNetwork's exact byte format:
- [peerID_byte] [msgCount_byte] [serialized_messages...]
- Custom message type dispatch table
- Custom ACK format
- Custom connection handshake (type 0/3/5 messages)

**There is no compatibility layer possible.** The protocols are fundamentally different
at the wire level. We must match the original byte-for-byte.

### 6.2 Recommended Module Structure

```
src/network/
    legacy/
        transport.h         // TGWinsockNetwork equivalent
        transport.c         // Socket, send/recv, peer management
        peer.h              // Peer data structure
        peer.c              // Peer lifecycle, queues
        message.h           // Message types (connection, data, ack, etc.)
        message.c           // Serialization/deserialization
        reliable.h          // Reliable delivery, ACK, retry
        reliable.c          // Sequence tracking, timeout
        gamespy_qr.h        // GameSpy query/response
        gamespy_qr.c        // LAN discovery responses
        checksum.h          // Checksum exchange protocol
        checksum.c          // Hash computation, request/response
        hash_tables.h       // The 4x256 lookup tables from stbc.exe
        protocol_opcodes.h  // Opcode constants (0x00-0x28)
```

### 6.3 Implementation Order

1. **Socket + basic send/recv** - Get a UDP socket listening on 22101
2. **Packet framing** - Parse/build [peerID][count][messages] format
3. **Message types** - Implement type 0,1,3,4,5 serialization
4. **Peer management** - Sorted array, binary search, creation
5. **Connection handshake** - Accept type 3 from peerID=-1, respond type 0
6. **GameSpy router** - Peek-based demux, respond to \basic\ queries
7. **Reliable delivery** - ACK generation, retry, sequence tracking
8. **Checksum exchange** - Implement opcodes 0x20, 0x21, hash function
9. **Settings/status** - Implement opcodes 0x00, 0x01
10. **Integration test** - Vanilla BC client connects to our server

### 6.4 Testing Strategy

- **Unit test**: Packet serialization round-trip
- **Unit test**: Peer array sorted insert / binary search
- **Unit test**: Hash function against known values from stbc.exe
- **Integration test**: Wireshark capture of vanilla BC connecting to proxy server,
  replay same packets against our server, compare responses byte-for-byte
- **End-to-end**: Vanilla BC client connects, sees server in LAN browser,
  joins, completes checksum, reaches lobby

---

## 7. Known Issues and Risks

### 7.1 Priority Queue Stall (DOCUMENTED BUG)

After initial checksum exchange, the priority reliable queue gets stuck at 3 items.
Server retransmits 15-byte packets every ~5s. After 8 retries (~30s), messages drop
and client times out on "Checksumming..."

This was the primary issue in the STBC-Dedicated-Server proxy. Root cause analysis
suggests the ACK matching in FUN_006b61e0 may fail if the message type/flags don't
exactly match what the client sends back.

**Mitigation**: When reimplementing, add extensive logging to the ACK matching logic.
Capture both sides' packets to verify exact byte-level match.

### 7.2 Silent Checksum Failure

If the client's FUN_0071f270 returns 0 (no files found for a given directory/filter),
the client silently drops the response. No error, no timeout notification to the server.
The server just waits forever for a response that never comes.

**Mitigation**: Implement a server-side timeout for checksum responses. If no response
within N seconds, either retry or disconnect the client.

### 7.3 Hash Table Extraction

The 4x256 lookup tables for the hash function must be extracted from stbc.exe at
addresses 0x0095c888-0x0095cb88+255. Without exact table values, checksums won't match.

Similarly, the reference string at PTR_DAT_008d9af4 must be extracted.

**Mitigation**: Write a small extraction tool or document the hex values directly.

### 7.4 Message Type Dispatch Table

The deserialization dispatch table at DAT_009962d4 maps first-byte values to
message constructors. We need to know which byte values map to which message types
to correctly deserialize incoming packets.

From the decompiled code, the table is indexed by the first byte of each serialized
message in a packet. Each entry is a function pointer that creates the appropriate
message object.

### 7.5 Endianness

All multi-byte values in the protocol are little-endian (x86 native). Our server
must also use little-endian encoding regardless of host platform.

### 7.6 Float Encoding

Game time and other floats are IEEE 754 single-precision (4 bytes, little-endian).
The value 0x3f800000 = 1.0f, 0x43b40000 = 360.0f, etc.

### 7.7 Stream Serializer

The original code uses a custom stream serializer (created by FUN_006cefe0, writes via
FUN_006cf730/006cf770/006cf7f0/006cf8b0/006cf2b0). This is essentially a buffer writer:
- FUN_006cf730: write 1 byte
- FUN_006cf770: write 1 byte (same as above, different calling convention)
- FUN_006cf7f0: write 2 bytes (u16 little-endian)
- FUN_006cf8b0: write 4 bytes (float32 little-endian)
- FUN_006cf2b0: write N bytes (raw copy)

Our reimplementation should use an equivalent buffer writer.

### 7.8 Password Handling

The connection message (type 3) includes a password field. When the server has a
password set, it must verify the client's password during the connection handshake.

From FUN_006b3ec0 (HostOrJoin): if param_2 (password) is NULL, an empty password
is used. The password is stored and compared during peer connection setup via
FUN_006c0b50 (which appears to set password data on the peer object).
