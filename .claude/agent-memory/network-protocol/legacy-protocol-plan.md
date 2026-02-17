# Legacy BC Network Protocol - Phase 1 Implementation Plan

## Decision: Raw UDP, NOT ENet

ENet uses its own wire format (ENet protocol headers, channel system, connection handshake).
A vanilla BC client speaks TGWinsockNetwork's custom binary protocol. The two are incompatible
at the wire level. There is no way to make ENet speak TGWinsockNetwork's protocol without
essentially reimplementing the transport layer anyway.

**We must reimplement TGWinsockNetwork from scratch using raw UDP sockets.**

ENet may still be useful as a reference for reliable UDP patterns, but the actual bytes on
the wire must match what the original game client expects.

---

## 1. Transport Layer (TGWinsockNetwork Reimplementation)

### 1.1 Socket Management

Create a single non-blocking UDP socket on port 22101:
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

States:
- **4 (DISCONNECTED)**: Initial state. No socket. No processing.
- **2 (HOST)**: Listening for connections. Processes all 3 sub-functions in Update.
  Fires ET_NETWORK_NEW_PLAYER when new peers connect.
  Dequeues received messages and posts them as ET_NETWORK_MESSAGE_EVENT.
- **3 (CLIENT)**: Connected to host. Sends initial connection packet.
  Processes incoming, dispatches to event system.

For Phase 1 (standalone server), we only implement HOST state.

### 1.3 Peer Tracking

**Peer Array**: Sorted array of peer pointers. Binary-searched by peer ID.

Each peer tracks:
```c
struct Peer {
    int32_t  peerID;           // unique identifier
    uint32_t address;          // sockaddr_in equivalent
    uint16_t seqRecvUnrel;     // expected unreliable seq from peer
    uint16_t seqSendReliable;  // next reliable seq to assign
    uint16_t seqRecvReliable;  // expected reliable seq from peer
    uint16_t seqSendPriority;  // next priority seq to assign
    float    lastRecvTime;
    float    lastSendTime;

    // 3 outbound queues (linked lists)
    Queue    unreliable;       // with count field
    Queue    reliable;         // with count field
    Queue    priority;         // with count field

    // Stats
    int32_t  bytesSentPayload;
    int32_t  bytesSentTotal;
    int32_t  bytesRecvPayload;
    int32_t  bytesRecvTotal;

    float    disconnectTime;
    uint8_t  isDisconnecting;
};
```

**Peer creation**: Allocates peer, inserts into sorted array, assigns peer ID.
For incoming connections with peerID=-1, assigns new ID.

**Self-peer**: The host creates a peer for itself (loopback messages and stats tracking).

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

Message count capped at 254.

### 1.5 Message Serialization

Each message in the packet is serialized by its virtual Serialize method.
The exact format depends on message type. Each message also has a virtual
method that returns the serialized size.

Messages have a type code returned by GetType virtual method:
```
Type 0: Keepalive/ping
Type 1: Reliable ACK
Type 2: Internal/padding (discarded on receive)
Type 3: Connect message (connection handshake data)
Type 4: ConnectData (rejection/notification)
Type 5: ConnectAck (shutdown notification)
Type 0x32: Reliable data message (game payload - carries opcodes 0x00-0x27)
```

### 1.6 Data Message (Type 0x32) Serialization

The data message (type 0x32) is the primary carrier. Its serialized form includes:
```
[message_type_byte]     - identifies this as a data message
[flags_length: u16]     - bit15=reliable, bit14=priority, bits13-0=totalLen
[sequence_number: u16]  - for reliable ordering
[payload_data]          - the actual game/checksum opcode data
```

The payload starts with the opcode byte (0x00 for settings, 0x20 for checksum request, etc.).

### 1.7 Message Queuing (TGNetwork::Send)

When application code calls Send(WSN, peerID, message, flags):
1. Binary search peer array for peerID
2. Call QueueMessageToPeer to enqueue
3. Queue selection checks message type and reliable flag:
   - type < 50 with reliable flag -> sequence from reliable counter, queue to reliable
   - type >= 50 with reliable flag -> sequence from priority counter, queue to priority
   - unreliable -> queue to unreliable
4. After queuing, increment the appropriate sequence counter
5. Check buffer limit (32768 bytes max); reject if peer queue too full (return 10)

### 1.8 Outbound Processing (SendOutgoingPackets)

Called every tick from TGNetwork::Update. For each peer (round-robin):

1. Check send-enabled flag - skip if disabled
2. Allocate packet buffer (512 bytes for TGWinsockNetwork)
3. Write sender peerID low byte at offset 0
4. Reserve offset 1 for message count
5. Start writing messages from offset 2:

**Priority queue first**:
   - Iterate linked list via cursor
   - For each message: check ready-to-send timing
   - If ready AND retryCount < 3: serialize, increment retryCount, record send time
   - If retryCount >= 8: drop message (timeout)
   - Cap at 254 total messages

**Reliable queue second**:
   - Similar iteration
   - If message exceeds reliableTimeout (360.0s): drop it
   - Otherwise serialize and bump retry count

**Unreliable queue last**:
   - First unreliable message always serialized
   - After serialize: for reliable messages, move to reliable retry queue
   - For unreliable: free after sending

6. Write final message count at offset 1
7. If messageCount > 0 AND statsEnabled:
   - Update peer stats (bytesSent)
   - Update self-peer stats
8. Call sendto with the buffer

### 1.9 Inbound Processing (ProcessIncomingPackets)

Called every tick. Loops on recvfrom:

1. Call recvfrom wrapper to get packet + source address
2. Parse first byte as senderPeerID
3. Parse second byte as messageCount
4. For each message in packet:
   - Deserialize using dispatch table indexed by first byte of message
   - Set message's senderPeerID and source address
   - Look up peer by senderPeerID in peer array

   **If peerID == -1 (new connection)**:
   - Check message type 3 (connection) or type 5 (keepalive)
   - For type 3: extract proposed peer info, create peer
   - For type 5: similar new-peer handling

   **If known peer**:
   - Update peer's lastRecvTime
   - Update stats if enabled

5. If message is reliable (reliable flag set):
   - Call ACK handler
   - Search priority queue for matching sequence/flags
   - If found: reset retry counter (message was ACKed)
   - If not found: create ACK entry and add to priority queue

6. Call sequence validator to validate and queue for application delivery

### 1.10 Dispatch to Application

Sequence validation:
- Compare message sequence against peer's expected sequence
- If sequence matches expected: accept, increment expected
- If behind expected (within window of 16384): discard as duplicate
- If ahead: buffer for later delivery (out-of-order handling)

Message dispatch by type:
- Type 0: Connection established - fires ET_NETWORK_NEW_PLAYER
- Type 1: ACK processing - removes from reliable queue
- Type 3: Data delivery - queues for application
- Type 4: Disconnect
- Type 5: Keepalive

For HOST state, dequeued messages become ET_NETWORK_MESSAGE_EVENT events
posted to EventManager.

### 1.11 ACK System Details

When a reliable message arrives:
1. Receiver creates an ACK message (type 1) containing the sequence number
2. ACK is queued in the receiver's priority queue for the sender
3. ACK is sent in the next outbound packet

When sender receives ACK back:
1. ACK handler matches against priority queue entries
2. Matching criteria: sequence number + type flag + fragment flag
3. On match: reset retry counter to 0, mark as acknowledged
4. Acknowledged messages are removed from the retry queue

**Retry timing**: Messages have a send timestamp and a retry interval. If
current_time - send_time > retry_interval, message is ready for resend.

### 1.12 Timeout and Disconnect

- **Reliable timeout** (360.0 seconds): If a reliable message has been
  retransmitted for this long without ACK, it's dropped
- **Disconnect timeout** (45.0 seconds): If no activity from a peer
  for this long, the peer is disconnected
- **Priority max retries**: After 8 retries, priority messages are dropped
- **Disconnect detection**: In SendOutgoingPackets, after processing all queues,
  check each peer's lastActivity time. If exceeded, call disconnect callback

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

The GameSpy object is 244 bytes. The qr_t is stored in it at a known offset.
The qr_t socket is the same socket as the game socket -- they share it.

### 2.3 Peek-Based Router

The shared socket problem: both GameSpy queries and game packets arrive on port 22101.

Solution:
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

**Critical**: The GameSpy recvfrom-enable flag must be set to 0 to disable GameSpy's
own recvfrom loop. GameSpy_Tick is still called for internal state management but
doesn't read the socket.

### 2.4 Implementation for OpenBC

For the standalone server, we implement GameSpy QR response directly:
- Parse incoming `\basic\`, `\status\`, `\info\` queries
- Respond with appropriate key-value pairs
- No need to link the actual GameSpy SDK; it's a simple text protocol

---

## 3. Checksum Exchange Protocol

### 3.1 Overview

After a client connects, the server verifies that the client has matching script files.
This is a 5-round sequential exchange where the server sends one request at a time
and waits for the client's response before sending the next.

### 3.2 Opcode 0x20: Checksum Request (Server -> Client)

```
Offset  Size    Field
0x00    1       opcode = 0x20
0x01    1       index (0-3, or 0xFF for final round)
0x02    2       directory string length (u16, little-endian)
0x04    N       directory string (NOT null-terminated)
0x04+N  2       filter string length (u16, little-endian)
0x06+N  M       filter string (NOT null-terminated)
0x06+N+M 1      recursive flag (0 or 1)
```

For the FIRST request (index 0), the message is sent immediately via TGNetwork::Send.
For subsequent indices, messages are queued and NOT sent yet until the previous response arrives.

### 3.3 The 5 Checksum Requests

| Index | Directory          | Filter          | Recursive |
|-------|--------------------|-----------------|-----------|
| 0     | scripts/           | App.pyc         | 0         |
| 1     | scripts/           | Autoexec.pyc    | 0         |
| 2     | scripts/ships/     | *.pyc           | 1         |
| 3     | scripts/mainmenu/  | *.pyc           | 0         |
| 0xFF  | Scripts/Multiplayer | *.pyc           | 1         |

### 3.4 Opcode 0x21: Checksum Response (Client -> Server)

```
Offset  Size    Field
0x00    1       opcode = 0x21
0x01    1       index (0-3, 0xFF)
0x02    ...     hash data (variable, depends on index)
```

For index 0: includes a reference string hash followed by directory hash and per-file checksums.

For other indices: directory hash + per-file checksums.

The hash data format:
- Scans directory with FindFirstFile/FindNextFile
- For each matching file: computes hash via custom 4-table algorithm
- Builds a sorted list of (filename_hash, file_content_hash) pairs
- The response contains these pairs

### 3.5 Hash Function

**This is NOT MD5 or CRC32.** It is a custom 4-table Pearson byte-XOR hash:

```c
uint32_t hash_string(const char* str) {
    uint8_t a = 0, b = 0, c = 0, d = 0;
    while (*str) {
        uint8_t ch = (uint8_t)*str;
        a = table_A[ch ^ a];
        b = table_B[ch ^ b];
        c = table_C[ch ^ c];
        d = table_D[ch ^ d];
        str++;
    }
    return (a << 24) | (b << 16) | (c << 8) | d;
}
```

Four 256-byte lookup tables forming Mutually Orthogonal Latin Squares (MOLS).
Result is 4 bytes combined into a 32-bit hash.
Verified: StringHash("60") == 0x7E0CE243.

### 3.6 Reference String Hash

For checksum request index 0 only, the server also checks a "reference string hash"
embedded in the game binary. If mismatch: opcode 0x23 (reference mismatch) is sent.
This string identifies the game version/build.

### 3.7 SkipChecksum Flag

If this configuration flag is set, checksum behavior changes significantly --
it likely skips the file hash verification entirely and goes straight to
ET_CHECKSUM_COMPLETE.

For Phase 1, we should support this flag as a server configuration option.

### 3.8 Opcode 0x22: Checksum Fail - File Mismatch (Server -> Client)

Sent when client's file hashes don't match server's. Contains info about which file(s) differ.

### 3.9 Opcode 0x23: Checksum Fail - Reference Mismatch (Server -> Client)

Sent when the reference string hash doesn't match. This indicates a version mismatch.

### 3.10 Opcode 0x25: File Transfer (Bidirectional)

Used to transfer mismatched files from server to client.

### 3.11 Opcode 0x27: Unknown

Purpose unclear. May be related to file transfer acknowledgment or cancellation.

### 3.12 Opcode 0x28: All Files Transferred (Server -> Client)

Sent when no pending file transfers remain. Signals completion of the file transfer phase.

### 3.13 Server-Side Flow (Sequential)

```
1. NewPlayerHandler fires
2. Start checksum exchange for this peer
3. Build 5 requests, queue ALL in hash table B
4. Send request #0 immediately
5. Wait for response (opcode 0x21)
6. Verify response:
   a. Look up queued request
   b. Compute SERVER-SIDE hash of same directory
   c. Compare client hash vs server hash
   d. For index 0: also compare reference string hash
7. If match: dequeue, send NEXT request
8. If mismatch: send opcode 0x22 or 0x23
9. When all requests verified: fire ET_CHECKSUM_COMPLETE
10. ChecksumCompleteHandler sends opcode 0x00 + 0x01
```

**Key insight**: The server does NOT send all 5 requests at once. It sends #0,
waits for response, verifies, then sends #1, and so on.

---

## 4. Game Message Protocol

### 4.1 Opcode 0x00: Settings/Verification (Server -> Client)

Sent by ChecksumCompleteHandler after all checksums pass.

```
Offset  Size    Field
0x00    1       opcode = 0x00
0x01    4       gameTime (float32, little-endian)
0x05    1       setting1 (byte) - collision damage flag
0x06    1       setting2 (byte) - friendly fire flag
0x07    1       playerSlot (byte, 0-15)
0x08    2       mapNameLength (u16, little-endian)
0x0A    N       mapName (NOT null-terminated)
0x0A+N  1       passFail (byte, 0 = pass, 1 = checksum had differences)
```

If passFail == 1, additional data follows containing the hash comparison result.

Sent as reliable.

### 4.2 Opcode 0x01: Status Byte (Server -> Client)

Sent immediately after opcode 0x00:
```
Offset  Size    Field
0x00    1       opcode = 0x01
```

Just a single byte. Sent reliable. Signals "ready for game setup."

### 4.3 Game Opcodes 0x02-0x1F (Partially Known)

These are dispatched by MultiplayerGame::ReceiveMessageHandler.

Known handler names (from registration):
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
  |<-- UDP connection response --------|
  |                                    |-- Fires ET_NETWORK_NEW_PLAYER
  |                                    |
  |                                    |-- NewPlayerHandler:
  |                                    |   - Check ready-for-players flag
  |                                    |   - Find empty slot in 16-slot array
  |                                    |   - Start checksum exchange
  |                                    |
  |<-- Checksum #0 (opcode 0x20) ------|
  |    [scripts/, App.pyc, nonrec]     |
  |                                    |
  |--- Checksum #0 resp (0x21) ------->|
  |                                    |-- Verify hash, send next
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
  |                                    |
  |<-- Checksum #4 (opcode 0x20) ------|
  |    [Scripts/Multiplayer, *.pyc,rec]|
  |                                    |
  |--- Checksum #4 resp (0x21) ------->|
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

MultiplayerGame has a 16-slot player array (24 bytes per slot).

Each slot:
```
active flag (1 byte)
peer network ID (int, matches peer ID)
player object ID (int)
... (remaining bytes)
```

Assignment:
1. Count active players (iterate all 16, count where active != 0)
2. If count < maxPlayers: find first empty slot, activate it
3. If full: send rejection message to the connecting peer

### 5.3 Rejection

When server is full, a ConnectData message (type 4) with reason code 3 is sent.

### 5.4 Deferred Player Handling

If the ready-for-new-players flag is 0:
- A timer event is created with exponential backoff
- When the timer fires, RetryConnectHandler re-attempts the join

For Phase 1 server: set the ready flag to 1 immediately during bootstrap.

### 5.5 Disconnect

Disconnect can happen via:
1. **Timeout**: No packets from peer for disconnectTimeout (45s)
2. **Explicit**: Peer sends disconnect message (type 4)
3. **Server kick**: Server sends disconnect to peer

On disconnect:
- Remove peer from peer array
- Fire ET_NETWORK_DISCONNECT event
- DisconnectHandler clears the player slot

---

## 6. Implementation Architecture for OpenBC

### 6.1 Why NOT ENet

ENet uses its own wire format:
- Connection handshake with challenge/response
- Channel-based multiplexing (up to 255 channels)
- Fragment/reassemble with its own framing
- Bandwidth throttling built into protocol
- Completely different header layout

A vanilla game client expects TGWinsockNetwork's exact byte format:
- [peerID_byte] [msgCount_byte] [serialized_messages...]
- Custom message type dispatch table
- Custom ACK format
- Custom connection handshake

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
        hash_tables.h       // The 4x256 Pearson lookup tables
        protocol_opcodes.h  // Opcode constants (0x00-0x28)
```

### 6.3 Implementation Order

1. **Socket + basic send/recv** - Get a UDP socket listening on 22101
2. **Packet framing** - Parse/build [peerID][count][messages] format
3. **Message types** - Implement types 0,1,3,4,5,0x32 serialization
4. **Peer management** - Sorted array, binary search, creation
5. **Connection handshake** - Accept type 3 from peerID=-1, respond
6. **GameSpy router** - Peek-based demux, respond to \basic\ queries
7. **Reliable delivery** - ACK generation, retry, sequence tracking
8. **Checksum exchange** - Implement opcodes 0x20, 0x21, hash function
9. **Settings/status** - Implement opcodes 0x00, 0x01
10. **Integration test** - Vanilla BC client connects to our server

### 6.4 Testing Strategy

- **Unit test**: Packet serialization round-trip
- **Unit test**: Peer array sorted insert / binary search
- **Unit test**: Hash function against known test vectors (e.g. StringHash("60") == 0x7E0CE243)
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

Root cause analysis suggests the ACK matching may fail if the message type/flags don't
exactly match what the client sends back.

**Mitigation**: When reimplementing, add extensive logging to the ACK matching logic.
Capture both sides' packets to verify exact byte-level match.

### 7.2 Silent Checksum Failure

If the client finds no files for a given directory/filter, the client silently drops
the response. No error, no timeout notification to the server. The server just waits
forever for a response that never comes.

**Mitigation**: Implement a server-side timeout for checksum responses. If no response
within N seconds, either retry or disconnect the client.

### 7.3 Hash Table Data

The 4x256 lookup tables for the hash function are interface constants required for
interoperability. They form Mutually Orthogonal Latin Squares (MOLS) of order 256.
Verified: StringHash("60") == 0x7E0CE243.

### 7.4 Message Type Dispatch Table

The deserialization dispatch table maps first-byte values to message constructors.
We need to know which byte values map to which message types to correctly deserialize
incoming packets.

### 7.5 Endianness

All multi-byte values in the protocol are little-endian (x86 native). Our server
must also use little-endian encoding regardless of host platform.

### 7.6 Float Encoding

Game time and other floats are IEEE 754 single-precision (4 bytes, little-endian).

### 7.7 Stream Serializer

The original code uses a custom stream serializer (buffer writer):
- write 1 byte
- write 2 bytes (u16 little-endian)
- write 4 bytes (float32 little-endian)
- write N bytes (raw copy)

Our reimplementation should use an equivalent buffer writer.

### 7.8 Password Handling

The connection message (type 3) includes a password field. When the server has a
password set, it must verify the client's password during the connection handshake.
