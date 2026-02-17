# Phase 1 Reverse Engineering Gap Analysis

## Comprehensive Report: What's Reversed vs What's Missing for the Standalone Server

Generated: 2026-02-07

---

## SECTION 1: ALREADY REVERSED (High Confidence)

These systems have been traced through code analysis with verified behavior.

### 1.1 Network Initialization Pipeline
- **UtopiaModule::InitMultiplayer** -- FULLY REVERSED
  - Creates TGWinsockNetwork (844 bytes) at UtopiaModule network pointer
  - Creates NetFile (72 bytes) at UtopiaModule netfile pointer
  - Creates GameSpy (244 bytes) at UtopiaModule gamespy pointer
  - Calls TGNetwork_HostOrJoin for socket setup
  - Confidence: VERIFIED from code analysis + runtime testing

- **TGNetwork_HostOrJoin** -- FULLY REVERSED
  - Requires connState==4 (disconnected)
  - address==0: HOST mode (sets isHost=1, state=2, fires host-start event)
  - address!=0: JOIN mode (sets isHost=0, state=3, sets initial-send flag)
  - Creates UDP socket via virtual method
  - Confidence: VERIFIED from code analysis + runtime testing

### 1.2 Checksum Exchange Protocol
- **Complete server-side flow** -- FULLY REVERSED
  - NewPlayerHandler: Assigns player slot, starts checksums
  - ChecksumRequestSender: Queues 4 requests, sends #0
  - ChecksumRequestBuilder: Builds individual request message
  - NetFile::ReceiveMessageHandler: Opcode dispatcher
  - ChecksumResponseEntry: Routes to verifier
  - ChecksumResponseVerifier: Hash compare, sends next
  - ChecksumFail: Fires event + sends 0x22/0x23
  - ChecksumAllPassed: Fires ET_CHECKSUM_COMPLETE
  - ChecksumCompleteHandler: Sends settings + map to client
  - Confidence: VERIFIED from code analysis + packet captures

- **Checksum packet formats** -- FULLY REVERSED
  - 0x20 (request): [opcode][index:u8][dir_len:u16][dir][filter_len:u16][filter][recursive:u8]
  - 0x21 (response): [opcode][index:u8][hashes...]
  - 0x00 (verification): [opcode][gameTime:f32][setting1:u8][setting2:u8][playerSlot:u8][mapNameLen:u16][mapName][passFail:u8]
  - 0x01 (status): [opcode] (1 byte)
  - 0x22/0x23 (fail): checksum mismatch notifications
  - Confidence: VERIFIED from code analysis

### 1.3 Event System Core
- **EventManager::ProcessEvents** -- FULLY REVERSED
  - Dequeues events, dispatches via handler chain
  - Handler registration for event types
  - Named handler function registration
  - Dispatch to handler chain
  - Confidence: VERIFIED from code analysis

### 1.4 TGNetwork::Update Main Loop
- **TGNetwork::Update** -- PARTIALLY REVERSED (structure known)
  - State 2 (host): sends keepalives, calls Send/Recv/Dispatch, dequeues messages
  - State 3 (client): sends join packet once, calls Send/Recv/Dispatch
  - Three sub-calls: SendOutgoing, ProcessIncoming, DispatchQueue
  - Confidence: VERIFIED structure, MEDIUM detail on sub-functions

### 1.5 MultiplayerGame Handler Registration
- **RegisterMPGameHandlers** -- FULLY REVERSED
  - All 28 handler names documented
  - Event type bindings also documented
  - Confidence: VERIFIED from code analysis (string references)

### 1.6 Key Global Memory Layout
- UtopiaModule base, network pointer, GameSpy pointer, NetFile/ChecksumMgr pointer
- IsHost, IsMultiplayer flags
- EventManager, Handler Registry
- Clock object
- Confidence: VERIFIED from runtime memory inspection

### 1.7 Peek-Based UDP Router
- GameSpy and TGNetwork share same UDP socket
- MSG_PEEK checks first byte: '\' = GameSpy query, binary = game packet
- GameSpy's own recvfrom disabled (flag set to 0)
- Confidence: VERIFIED from runtime testing

---

## SECTION 2: PARTIALLY REVERSED (Medium Confidence)

These have some understanding but specific details need clarification.

### 2.1 TGNetwork::Send
**Known:**
- Binary searches peer array by peer ID
- Calls QueueMessageToPeer to enqueue
- Handles broadcast (0), specific peer, and group (-1) sends
- Returns error codes: 0=success, 4=wrong state, 11=peer not found, 10=queue full

**Gaps:**
- Message object layout only partially known (vtable, size, reliable flag, priority flag)
- Clone operation via virtual method for broadcast sends
- Message reference counting (virtual release method)

### 2.2 SendOutgoingPackets
**Known:**
- Checks send-enabled flag
- Iterates peers in round-robin fashion
- Three queue drain loops: priority reliable, reliable, unreliable
- Serializes messages via virtual serialize method
- Sends via virtual sendto method
- Packet header: [senderID:u8][messageCount:u8][serialized messages...]
- Max 254 messages per packet
- Priority reliable: retried up to 3 times before promoting to retry loop
- Reliable: sent once, then moved to priority queue with retry timer
- After 8 retries, message dropped + peer disconnected

**Gaps:**
- Exact packet framing (what bytes go where in the UDP datagram)
- Sequence number assignment during serialization
- How the serialization virtual method writes the per-message header
- Exact timeout calculation formula

### 2.3 ProcessIncomingPackets
**Known:**
- Calls virtual recvfrom wrapper to receive raw packet
- Parses sender ID byte and message count byte from header
- Iterates message count, calling dispatch table indexed by message type byte
- For each deserialized message: sets sender peer ID, calls ACK handler and dispatch
- Creates peer entries for unknown senders (connection handshake)

**Gaps:**
- The dispatch table -- what message type constructors are registered
- How connection handshake works at wire level
- Peer creation during handshake (what are initial field values?)
- Password validation during connection

### 2.4 ReliableACKHandler
**Known:**
- Scans priority reliable queue for matching sequence/flags
- Match found: resets retry counter, updates timestamps
- No match: creates ACK entry in priority queue with peer ID and sequence number

**Gaps:**
- Exact ACK packet format
- Sequence number matching logic
- How sequence numbers wrap (ushort, 0xFFFF -> 0x0000)
- Difference between "small" messages (type < 50) vs "large" messages (type >= 50)

### 2.5 DispatchIncomingQueue / DispatchToApplication
**Known:**
- Iterates queued incoming messages
- Validates sequence numbers against expected values
- Discards out-of-window messages (window size ~16384)
- Handles sequence validation and queues for application delivery

**Gaps:**
- Exact sequence window logic
- Out-of-order message handling (buffered or dropped?)
- Application delivery queue structure
- How reliable incoming messages generate ACKs back to sender

### 2.6 GameSpy Query Handler
**Known:**
- Standard GameSpy QR SDK pattern
- Query type table with 8 entries (indices 0-7)
- Query types: basic(0), info/rules/players(1-3), combined(4), full(5), specific_key(6), echo(7)
- Each type calls a response builder
- Responses are backslash-delimited key-value strings sent via sendto
- Response fragmentation if > 1349 bytes

**Gaps:**
- What specific key-value pairs are returned by each callback?
- The callback function pointers -- what functions are they and what data do they report?
- Heartbeat format and timing
- Master server address and protocol

### 2.7 Player Slot Management
**Known:**
- 16 slots, 24 bytes per slot in MultiplayerGame
- Slot fields: active flag (byte), peer network ID (int), player object ID (int)
- InitializePlayerSlot function documented
- MaxPlayers stored in MultiplayerGame

**Gaps:**
- Complete slot structure (what are all 24 bytes?)
- How slot assignment interacts with checksum state
- Player name storage location
- Slot cleanup on disconnect
- How "boot player" works

---

## SECTION 3: NOT YET REVERSED (Critical for Phase 1)

### 3.1 MultiplayerGame ReceiveMessageHandler Opcode Dispatch (CRITICAL)

The dispatcher handles game opcodes 0x00-0x1F. The actual dispatch logic processes opcodes
after the NetFile opcodes (0x20+) are filtered.

**What we know:**
- Opcode 0x00: Verification result + game settings (server->client, decoded)
- Opcode 0x01: Status byte (server->client, decoded)
- The main handler processes opcodes with this=MultiplayerGame

**What's NOT reversed:**
- Complete opcode table for 0x00-0x1F
- What game state changes each opcode triggers
- Message format for each opcode
- Which opcodes are server->client vs client->server vs bidirectional

### 3.2 Connection Handshake Protocol (CRITICAL)

**Not reversed:**
- How a client establishes connection at the TGNetwork level (before game events fire)
- The initial UDP packet exchange (connection request/accept/reject)
- Password exchange mechanism
- How peer ID is assigned
- How ET_NETWORK_NEW_PLAYER event is generated from raw UDP reception

### 3.3 Reliable Delivery Layer Details (CRITICAL)

**Not reversed:**
- Complete packet wire format: [header][message1][message2]...[trailer?]
- Per-message header format
- ACK packet format
- Retry timing formula
- Sequence number initialization
- How "priority reliable" differs from "reliable" at the wire level
- Keep-alive ping format and timing

### 3.4 GameSpy Query Response Fields (IMPORTANT)

**Not reversed:**
- The actual callback functions for query responses
- What key-value pairs they write
- The exact GameSpy QR protocol fields for BC specifically
- Heartbeat packet contents and master server interaction

### 3.5 Lobby State Synchronization (IMPORTANT)

**Not reversed:**
- How game settings changes propagate to all clients
- Ship selection protocol
- Map selection and loading sequence
- Ready state management
- How "Start Game" transitions from lobby to gameplay

### 3.6 Message Serialization System (IMPORTANT)

**Not reversed:**
- The message class hierarchy
- How messages self-serialize via virtual methods
- The dispatch table (message type -> constructor mapping)
- How many message types exist and their type IDs
- The TGStream class used for binary serialization

---

## SECTION 4: PRIORITIZED RE WORK ITEMS

### Priority: BLOCKING (Must complete before implementation can start)

#### WI-1: Connection Handshake Protocol
- **What:** Reverse the complete UDP connection handshake from first packet to ET_NETWORK_NEW_PLAYER
- **Approach:** Trace ProcessIncomingPackets focusing on the unknown-sender path, follow message type 3 and type 5 dispatch, map peer creation and ID assignment
- **Estimated complexity:** LARGE
- **Why blocking:** Without this, no client can connect

#### WI-2: Packet Wire Format
- **What:** Document the exact byte layout of UDP packets on the wire
- **Approach:** Focus on the serialization loop in Send and deserialization loop in Recv. Map [senderID][msgCount][per-message-headers][payload] format. Cross-reference with packet captures from existing proxy
- **Estimated complexity:** LARGE
- **Why blocking:** Need exact format to implement packet parsing/building

#### WI-3: Reliable Delivery ACK Format
- **What:** Reverse the ACK packet format, sequence numbering, and retry logic
- **Approach:** Trace sequence number assignment in Send path, ACK generation in Recv path, retry loop in SendOutgoingPackets
- **Estimated complexity:** LARGE
- **Why blocking:** Reliable delivery is required for checksum exchange and all game commands

#### WI-4: Message Type Dispatch Table
- **What:** Map the complete dispatch table that deserializes incoming messages by type byte
- **Approach:** Read the dispatch table data, identify function pointers, trace each to understand message types. Key types: connection request(3), connection accept(5), keepalive/ping, game data, reliable ACK
- **Estimated complexity:** MEDIUM
- **Why blocking:** Must know message types to parse any packet

### Priority: IMPORTANT (Required for functional server)

#### WI-5: MultiplayerGame Opcode Dispatch (0x00-0x1F)
- **What:** Reverse the game-level opcode handler in ReceiveMessageHandler
- **Approach:** Trace the handler that receives messages and dispatches based on first byte. Focus on lobby-relevant opcodes: settings sync, ship selection, start game, chat
- **Estimated complexity:** LARGE (many sub-handlers)

#### WI-6: GameSpy Query Response Builder
- **What:** Identify the exact key-value pairs returned in GameSpy query responses
- **Approach:** Trace qr_t creation to find callback registration. Follow callbacks to see what strings they write. Cross-reference with GameSpy QR SDK docs
- **Estimated complexity:** MEDIUM

#### WI-7: Player Slot Full Structure
- **What:** Document all 24 bytes of each player slot
- **Approach:** Trace all reads/writes to the player slot array
- **Estimated complexity:** SMALL

#### WI-8: Lobby Settings Propagation
- **What:** How game settings (map, friendly fire, game mode) propagate to clients
- **Approach:** Trace settings storage in MultiplayerGame. Find how changes are serialized into network messages
- **Estimated complexity:** MEDIUM

#### WI-9: Peer Structure Layout (TGPeer)
- **What:** Document the complete peer structure (~192 bytes based on field accesses)
- **Approach:** Catalogue all field accesses: peerID, address, sequence numbers, timestamps, queues, stats, disconnecting flag
- **Estimated complexity:** MEDIUM

#### WI-10: GameSpy Heartbeat and Initialization
- **What:** Reverse GameSpy object creation, heartbeat, and lifecycle
- **Approach:** Trace qr_t creation, socket binding, heartbeat timer, master server communication
- **Estimated complexity:** MEDIUM

### Priority: NICE-TO-HAVE (Can be deferred or approximated)

#### WI-11: TGWinsockNetwork Full Object Layout
- **What:** Document all 844 bytes of the WSN object
- **Estimated complexity:** LARGE

#### WI-12: Event Object Structure
- **What:** TGEvent, TGNetworkEvent, TGTimerEvent struct layouts
- **Estimated complexity:** MEDIUM

#### WI-13: TGStream Serialization
- **What:** Binary serialization used by network messages
- **Estimated complexity:** MEDIUM

#### WI-14: File Hash Algorithm
- **What:** The exact hash algorithm used for checksum verification
- **Estimated complexity:** SMALL

#### WI-15: Chat Message Protocol
- **What:** How chat messages are sent/received in multiplayer
- **Estimated complexity:** SMALL

---

## SECTION 5: DEPENDENCY GRAPH

```
WI-4 (Message Types) ----+
                          |
WI-2 (Packet Format) ----+--> WI-1 (Connection Handshake)
                          |         |
WI-3 (Reliable ACK) -----+         v
                                WI-7 (Player Slots)
                                    |
                                    v
                                WI-5 (Game Opcodes)
                                    |
                                    +---> WI-8 (Settings Sync)
                                    +---> WI-15 (Chat)

WI-6 (GameSpy Query) ---> WI-10 (GameSpy Init)
WI-9 (Peer Structure) supports WI-1, WI-2, WI-3
```

## SECTION 6: RECOMMENDED RE ORDER

1. **WI-4** (Message Type Dispatch Table) - Small effort, high leverage
2. **WI-9** (Peer Structure) - Needed to understand all network code
3. **WI-2** (Packet Wire Format) - Foundation for all network I/O
4. **WI-3** (Reliable ACK) - Required for game messages
5. **WI-1** (Connection Handshake) - Depends on 2,3,4
6. **WI-7** (Player Slots) - Small effort
7. **WI-5** (Game Opcodes) - Large but can be done incrementally
8. **WI-6** (GameSpy Query) - Independent, can parallel with above
9. **WI-10** (GameSpy Init) - Independent
10. **WI-8** (Settings Sync) - After WI-5
