# Phase 1 Reverse Engineering Gap Analysis

## Comprehensive Report: What's Reversed vs What's Missing for the Standalone Server

Generated: 2026-02-07

---

## SECTION 1: ALREADY REVERSED (High Confidence)

These systems have been traced through decompiled code with verified behavior, documented in the STBC-Dedicated-Server docs.

### 1.1 Network Initialization Pipeline
- **FUN_00445d90 (UtopiaModule::InitMultiplayer)** -- FULLY REVERSED
  - Creates TGWinsockNetwork (0x34C bytes) at UtopiaModule+0x78
  - Creates NetFile (0x48 bytes) at UtopiaModule+0x80
  - Creates GameSpy (0xF4 bytes) at UtopiaModule+0x7C
  - Calls TGNetwork_HostOrJoin for socket setup
  - Confidence: VERIFIED from decompiled code + runtime testing

- **FUN_006b3ec0 (TGNetwork_HostOrJoin)** -- FULLY REVERSED
  - Requires connState==4 (disconnected)
  - param_1==0: HOST mode (sets +0x10E=1, state=2, fires 0x60002 event)
  - param_1!=0: JOIN mode (sets +0x10E=0, state=3, +0x10F=1)
  - Creates UDP socket via vtable+0x60
  - Confidence: VERIFIED from decompiled code + runtime testing

### 1.2 Checksum Exchange Protocol
- **Complete server-side flow** -- FULLY REVERSED
  - FUN_006a0a30 (NewPlayerHandler): Assigns player slot, starts checksums
  - FUN_006a3820 (ChecksumRequestSender): Queues 4 requests, sends #0
  - FUN_006a39b0 (ChecksumRequestBuilder): Builds individual request message
  - FUN_006a3cd0 (NetFile::ReceiveMessageHandler): Opcode dispatcher
  - FUN_006a4260 (ChecksumResponseEntry): Routes to verifier
  - FUN_006a4560 (ChecksumResponseVerifier): Hash compare, sends next
  - FUN_006a4a00 (ChecksumFail): Fires event + sends 0x22/0x23
  - FUN_006a4bb0 (ChecksumAllPassed): Fires ET_CHECKSUM_COMPLETE
  - FUN_006a1b10 (ChecksumCompleteHandler): Sends settings + map to client
  - Confidence: VERIFIED from decompiled code + packet captures

- **Checksum packet formats** -- FULLY REVERSED
  - 0x20 (request): [opcode][index:u8][dir_len:u16][dir][filter_len:u16][filter][recursive:u8]
  - 0x21 (response): [opcode][index:u8][hashes...]
  - 0x00 (verification): [opcode][gameTime:f32][setting1:u8][setting2:u8][playerSlot:u8][mapNameLen:u16][mapName][passFail:u8]
  - 0x01 (status): [opcode] (1 byte)
  - 0x22/0x23 (fail): checksum mismatch notifications
  - Confidence: VERIFIED from decompiled code

### 1.3 Event System Core
- **FUN_006da2c0 (EventManager::ProcessEvents)** -- FULLY REVERSED
  - Dequeues events, dispatches via handler chain
  - FUN_006db380: Registers handler for event type
  - FUN_006da130: Registers named handler function
  - FUN_006db620: Dispatches to handler chain
  - Confidence: VERIFIED from decompiled code

### 1.4 TGNetwork::Update Main Loop
- **FUN_006b4560 (TGNetwork::Update)** -- PARTIALLY REVERSED (structure known)
  - State 2 (host): sends keepalives, calls Send/Recv/Dispatch, dequeues messages
  - State 3 (client): sends join packet once, calls Send/Recv/Dispatch
  - Three sub-calls: FUN_006b55b0, FUN_006b5c90, FUN_006b5f70
  - Confidence: VERIFIED structure, MEDIUM detail on sub-functions

### 1.5 MultiplayerGame Handler Registration
- **FUN_0069efe0 (RegisterMPGameHandlers)** -- FULLY REVERSED
  - All 28 handler names and addresses documented
  - Event type bindings in FUN_0069e590 also documented
  - Confidence: VERIFIED from decompiled code (string references)

### 1.6 Key Global Memory Layout
- UtopiaModule base: 0x0097FA00
- WSN pointer: 0x0097FA78 (UtopiaModule+0x78)
- GameSpy pointer: 0x0097FA7C (+0x7C)
- NetFile/ChecksumMgr: 0x0097FA80 (+0x80)
- IsHost: 0x0097FA88 (+0x88)
- IsMultiplayer: 0x0097FA8A (+0x8A)
- EventManager: 0x0097F838
- Handler Registry: 0x0097F864
- Clock object: 0x009a09d0
- Confidence: VERIFIED from runtime memory inspection

### 1.7 Peek-Based UDP Router
- GameSpy and TGNetwork share same UDP socket (WSN+0x194)
- MSG_PEEK checks first byte: '\' = GameSpy query, binary = game packet
- qr_t+0xE4 set to 0 to disable GameSpy's own recvfrom
- Confidence: VERIFIED from runtime testing

---

## SECTION 2: PARTIALLY REVERSED (Medium Confidence)

These have some understanding but specific details need clarification.

### 2.1 TGNetwork::Send (FUN_006b4c10)
**Known:**
- Binary searches peer array at WSN+0x2C by peer ID at peer+0x18
- Calls FUN_006b5080 (QueueMessageToPeer) to enqueue
- Handles broadcast (param_1==0), specific peer, and group (-1) sends
- Returns error codes: 0=success, 4=wrong state, 0xb=peer not found, 10=queue full

**Gaps:**
- Message object layout only partially known (vtable, size at +0x00, reliable flag at +0x3A, priority at +0x3B)
- Clone operation via vtable+0x18 for broadcast sends
- Message reference counting (vtable+0x04 with param=1 for release)

### 2.2 SendOutgoingPackets (FUN_006b55b0)
**Known:**
- Checks WSN+0x10C (send-enabled flag)
- Iterates peers in round-robin fashion (WSN+0x2C counter)
- Three queue drain loops: priority reliable (+0x9C), reliable (+0x80), unreliable (+0x64)
- Serializes messages via vtable+0x08 (serialize to buffer)
- Sends via vtable+0x70 (socket sendto)
- Packet header: [senderID:u8][messageCount:u8][serialized messages...]
- Max 0xFE messages per packet
- Priority reliable: retried up to 3 times before promoting to retry loop (>3 retries with timeout)
- Reliable: sent once, then moved to priority queue with retry timer
- After 8 retries, message dropped + peer disconnected via vtable+0x74

**Gaps:**
- Exact packet framing (what bytes go where in the UDP datagram)
- Sequence number assignment during serialization
- How the serialization vtable+0x08 writes the per-message header
- Exact timeout calculation formula (uses param_1[0x2D] as base timeout)
- The "encrypt" step (is there one? vtable+0x58 seems to return packet header size)

### 2.3 ProcessIncomingPackets (FUN_006b5c90)
**Known:**
- Calls vtable+0x6C to receive raw packet (recvfrom wrapper)
- Parses sender ID byte and message count byte from header
- Iterates message count, calling dispatch table at DAT_009962d4 indexed by message type byte
- For each deserialized message: sets sender peer ID, calls FUN_006b61e0 (ACK) and FUN_006b6ad0 (dispatch)
- Creates peer entries for unknown senders (connection handshake)

**Gaps:**
- The dispatch table at DAT_009962d4 -- what message type constructors are registered?
- How connection handshake works (type 3 = connection request, type 5 = connection response?)
- Peer creation during handshake (FUN_006b7410 creates peer, what are initial field values?)
- Password validation during connection

### 2.4 ReliableACKHandler (FUN_006b61e0)
**Known:**
- Scans priority reliable queue (peer+0x9C) for matching sequence/flags
- Match found: resets retry counter via FUN_006b8670, updates timestamps
- No match: creates ACK entry in priority queue with peer ID and sequence number

**Gaps:**
- Exact ACK packet format (what bytes constitute an ACK?)
- Sequence number matching logic (uses ushort at message+0x14, peer+0x26/+0x2A)
- How sequence numbers wrap (ushort, so 0xFFFF -> 0x0000)
- Difference between "small" messages (type < 0x32) vs "large" messages (type >= 0x32) for sequence tracking

### 2.5 DispatchIncomingQueue / DispatchToApplication (FUN_006b5f70 / FUN_006b6ad0)
**Known:**
- FUN_006b5f70 iterates queued incoming messages
- Validates sequence numbers against expected (peer+0x24/+0x28)
- Discards out-of-window messages (window size from ushort comparison, 0x4000 range)
- FUN_006b6ad0 handles sequence validation and queues for application delivery

**Gaps:**
- Exact sequence window logic (how does the sliding window work?)
- Out-of-order message handling (are they buffered or dropped?)
- Application delivery queue structure
- How "reliable" incoming messages generate ACKs back to sender

### 2.6 GameSpy Query Handler (FUN_006ac1e0)
**Known:**
- Standard GameSpy QR SDK pattern
- Query type table at PTR_s_basic_0095a71c with 8 entries (indices 0-7)
- Query types: basic(0), info/rules/players(1-3), combined(4), full(5), specific_key(6), echo(7)
- Each type calls a response builder: FUN_006ac5f0(basic), FUN_006ac7a0(rules), FUN_006ac810(players), FUN_006ac880(extra)
- Response builders call function pointers at qr_t+0x32/0x33/0x34/0x35 (callbacks registered at creation)
- Responses are backslash-delimited key-value strings sent via sendto
- FUN_006ac550 appends queryid and sends accumulated response via sendto
- FUN_006ac660 handles response fragmentation if >0x545 bytes

**Gaps:**
- What specific key-value pairs are returned by each callback?
- The callback function pointers (qr_t+0x32 through 0x35) -- what functions are they and what data do they report?
- The qr_t struct layout beyond the callback pointers
- Heartbeat format and timing (FUN_006ab4e0, FUN_006aa2f0, FUN_006aa6b0)
- Master server address and protocol (if used for LAN, is there internet listing?)

### 2.7 Player Slot Management
**Known:**
- 16 slots at MultiplayerGame+0x74, stride 0x18 per slot
- Slot+0x00: active flag (byte)
- Slot+0x04: peer network ID (int)
- Slot+0x08: player object ID (int)
- FUN_006a7770 initializes a player slot
- FUN_0069efc0 initializes all 16 slots at MultiplayerGame construction
- MaxPlayers stored at MultiplayerGame+0x1FC

**Gaps:**
- Complete slot structure (what are all 0x18 bytes?)
- How slot assignment interacts with checksum state
- Player name storage location
- Slot cleanup on disconnect (FUN_006a0ca0 DeletePlayerHandler)
- How "boot player" works (FUN_006a2640 KillGameHandler?)

---

## SECTION 3: NOT YET REVERSED (Critical for Phase 1)

### 3.1 MultiplayerGame ReceiveMessageHandler Opcode Dispatch (CRITICAL)

The handler at 0x0069f2a0 is a label inside FUN_0069e590 (the MultiplayerGame constructor). The actual dispatch logic for game opcodes 0x00-0x1F is in FUN_0069f620 and related functions. These are the opcodes that flow through AFTER the NetFile opcodes (0x20+) are filtered.

**What we know:**
- Opcode 0x00: Verification result + game settings (server->client, decoded)
- Opcode 0x01: Status byte (server->client, decoded)
- FUN_0069f620 is called from the handler and processes opcodes with this=MultiplayerGame

**What's NOT reversed (needs analysis of FUN_0069f620 and siblings):**
- Complete opcode table for 0x00-0x1F
- What game state changes each opcode triggers
- Message format for each opcode
- Which opcodes are server->client vs client->server vs bidirectional
- Opcodes for: ship selection, game start, chat, player settings, map change, kick/ban
- The "type" field in game messages vs the opcode byte

### 3.2 Connection Handshake Protocol (CRITICAL)

**Not reversed:**
- How a client establishes connection at the TGNetwork level (before game events fire)
- The initial UDP packet exchange (connection request/accept/reject)
- Password exchange mechanism
- How peer ID is assigned
- How ET_NETWORK_NEW_PLAYER event is generated from raw UDP reception
- Message type 3 (connection request) and type 5 (connection accept?) in ProcessIncomingPackets
- The peer creation path in FUN_006b5c90

### 3.3 Reliable Delivery Layer Details (CRITICAL)

**Not reversed:**
- Complete packet wire format: [header][message1][message2]...[trailer?]
- Per-message header format: [type:u8][size:u16?][sequence:u16][flags?][payload...]
- ACK packet format: what does an ACK look like on the wire?
- Retry timing formula (base timeout * backoff factor?)
- Maximum retry count before disconnect (appears to be 8 from code)
- Sequence number initialization (what value does it start at?)
- The send throttle mechanism (WSN+0xA8 = max pending, WSN+0xAC = buffer size?)
- How "priority reliable" differs from "reliable" at the wire level
- Keep-alive ping format and timing (host sends to clients periodically)

### 3.4 GameSpy Query Response Fields (IMPORTANT)

**Not reversed:**
- The actual callback functions registered at qr_t+0x32/0x33/0x34/0x35
- What key-value pairs they write (game name, map, player count, etc.)
- The exact GameSpy QR protocol fields for BC specifically
- Heartbeat packet contents and master server interaction
- LAN broadcast mechanism (separate from query/response?)

### 3.5 Lobby State Synchronization (IMPORTANT)

**Not reversed:**
- How game settings changes (friendly fire, game mode, etc.) propagate to all clients
- The message format for settings updates
- Ship selection protocol (how client chooses ship, server validates/broadcasts)
- Map selection and loading sequence
- Ready state management
- How "Start Game" transitions from lobby to gameplay

### 3.6 Message Serialization System (IMPORTANT)

**Not reversed:**
- The message class hierarchy (vtable layout)
- How messages self-serialize via vtable+0x08
- The dispatch table at DAT_009962d4 (message type -> constructor mapping)
- How many message types exist and their type IDs
- The TGStream class used for binary serialization (FUN_006cefe0 et al.)

---

## SECTION 4: PRIORITIZED RE WORK ITEMS

### Priority: BLOCKING (Must complete before implementation can start)

#### WI-1: Connection Handshake Protocol
- **What:** Reverse the complete UDP connection handshake from first packet to ET_NETWORK_NEW_PLAYER
- **Files:** `11_tgnetwork.c` lines 3300-3446 (FUN_006b5c90 ProcessIncomingPackets), lines 1792-1868 (FUN_006b3ec0 HostOrJoin), `11_tgnetwork.c` FUN_006b7410 (peer creation)
- **Approach:** Trace FUN_006b5c90 focusing on the `-1` sender path (unknown peer), follow message type 3 and type 5 dispatch, map peer creation and ID assignment
- **Estimated complexity:** LARGE
- **Why blocking:** Without this, no client can connect

#### WI-2: Packet Wire Format
- **What:** Document the exact byte layout of UDP packets on the wire
- **Files:** `11_tgnetwork.c` FUN_006b55b0 (SendOutgoingPackets, lines 2985-3298), FUN_006b5c90 (ProcessIncomingPackets, lines 3300-3446)
- **Approach:** Focus on the serialization loop in Send and deserialization loop in Recv. Map [senderID][msgCount][per-message-headers][payload] format. Cross-reference with packet captures from existing proxy
- **Estimated complexity:** LARGE
- **Why blocking:** Need exact format to implement packet parsing/building

#### WI-3: Reliable Delivery ACK Format
- **What:** Reverse the ACK packet format, sequence numbering, and retry logic
- **Files:** `11_tgnetwork.c` FUN_006b61e0 (ReliableACKHandler, line 3448), FUN_006b5080 (QueueMessageToPeer, line 2700), FUN_006b8670 (ResetRetryCounter), FUN_006b8700 (CheckRetryTimer)
- **Approach:** Trace sequence number assignment in Send path, ACK generation in Recv path, retry loop in SendOutgoingPackets
- **Estimated complexity:** LARGE
- **Why blocking:** Reliable delivery is required for checksum exchange and all game commands

#### WI-4: Message Type Dispatch Table (DAT_009962d4)
- **What:** Map the complete dispatch table that deserializes incoming messages by type byte
- **Files:** `11_tgnetwork.c` FUN_006b5c90 (line 3334: dispatch table reference), `18_data_tables.c` (may contain the table data)
- **Approach:** Read the data at address 0x009962d4, identify function pointers, trace each to understand message types. Key types: connection request(3), connection accept(5), keepalive/ping, game data, reliable ACK
- **Estimated complexity:** MEDIUM
- **Why blocking:** Must know message types to parse any packet

### Priority: IMPORTANT (Required for functional server)

#### WI-5: MultiplayerGame Opcode Dispatch (0x00-0x1F)
- **What:** Reverse the game-level opcode handler in ReceiveMessageHandler
- **Files:** `09_multiplayer_game.c` FUN_0069f620 (line 5693), FUN_0069f880, FUN_0069f930, FUN_0069fbb0, FUN_0069fda0 and the ~20 functions following
- **Approach:** FUN_0069f620 receives messages and dispatches based on first byte. Trace each case. Focus on lobby-relevant opcodes: settings sync, ship selection, start game, chat
- **Estimated complexity:** LARGE (many sub-handlers)
- **Why important:** These are the game commands that make multiplayer work

#### WI-6: GameSpy Query Response Builder
- **What:** Identify the exact key-value pairs returned in GameSpy query responses
- **Files:** `10_netfile_checksums.c` FUN_006ac5f0/006ac7a0/006ac810/006ac880 (lines 7504-7732), the callback functions at qr_t+0x32-0x35, `09_multiplayer_game.c` GameSpy class (around FUN_0069ccd0)
- **Approach:** Trace qr_t creation to find callback registration. Follow callbacks to see what strings they write. Cross-reference with GameSpy QR SDK docs for standard field names
- **Estimated complexity:** MEDIUM
- **Why important:** LAN server browser needs correct query responses

#### WI-7: Player Slot Full Structure
- **What:** Document all 0x18 bytes of each player slot
- **Files:** `09_multiplayer_game.c` FUN_006a7770 (InitializePlayerSlot, line 5640-5645 via FUN_0069efc0), FUN_006a0a30 (NewPlayerHandler), FUN_006a0ca0 (DeletePlayerHandler)
- **Approach:** Trace all reads/writes to MultiplayerGame+0x74 through +0x1F4 (16 slots * 0x18)
- **Estimated complexity:** SMALL
- **Why important:** Player management is core server functionality

#### WI-8: Lobby Settings Propagation
- **What:** How game settings (map, friendly fire, game mode) propagate to clients
- **Files:** `09_multiplayer_game.c` FUN_006a1b10 (ChecksumCompleteHandler), MultiplayerGame constructor, the opcode 0x00 handler
- **Approach:** Trace settings storage in MultiplayerGame object. Find how changes are serialized into network messages. Likely uses specific opcodes from WI-5
- **Estimated complexity:** MEDIUM
- **Why important:** Clients need correct settings to join

#### WI-9: Peer Structure Layout (TGPeer)
- **What:** Document the complete peer structure (~0xC0 bytes based on field accesses)
- **Files:** `11_tgnetwork.c` FUN_006b7410 (peer creation), all functions that access peer fields
- **Approach:** Catalogue all field accesses: +0x18(peerID), +0x1C(address?), +0x24(reliableSeqIn), +0x26(reliableSeqOut), +0x28(unreliableSeqIn), +0x2A(unreliableSeqOut), +0x2C(lastRecvTime), +0x30(lastSendTime), +0x64-0x7C(unreliable queue), +0x80-0x98(reliable queue), +0x9C-0xB4(priority queue), +0xBC(disconnecting flag), +0xB8(disconnectTime)
- **Estimated complexity:** MEDIUM
- **Why important:** Every network operation touches peer state

#### WI-10: GameSpy Heartbeat and Initialization
- **What:** Reverse GameSpy object creation, heartbeat, and lifecycle
- **Files:** `09_multiplayer_game.c` FUN_0069ccd0 (GameSpy init), `10_netfile_checksums.c` FUN_006ab4e0, FUN_006aa2f0, FUN_006aa6b0
- **Approach:** Trace qr_t creation, socket binding, heartbeat timer, master server communication
- **Estimated complexity:** MEDIUM
- **Why important:** Server must be discoverable on LAN

### Priority: NICE-TO-HAVE (Can be deferred or approximated)

#### WI-11: TGWinsockNetwork Full Object Layout
- **What:** Document all 0x34C bytes of the WSN object
- **Files:** `11_tgnetwork.c` entire file, constructor, all field accesses
- **Estimated complexity:** LARGE
- **Why nice-to-have:** Can implement incrementally as needed

#### WI-12: Event Object Structure
- **What:** TGEvent, TGNetworkEvent, TGTimerEvent struct layouts
- **Files:** `13_events_timers.c`
- **Estimated complexity:** MEDIUM
- **Why nice-to-have:** Can approximate from usage patterns

#### WI-13: TGStream Serialization
- **What:** Binary serialization used by network messages
- **Files:** `12_data_serialization.c`
- **Estimated complexity:** MEDIUM
- **Why nice-to-have:** May be simpler to rewrite than reverse

#### WI-14: File Hash Algorithm (FUN_0071f270 / FUN_007202e0)
- **What:** The exact hash algorithm used for checksum verification
- **Files:** `15_python_swig.c` FUN_0071f270, FUN_007202e0
- **Estimated complexity:** SMALL
- **Why nice-to-have:** Can skip checksums initially or use SHA256

#### WI-15: Chat Message Protocol
- **What:** How chat messages are sent/received in multiplayer
- **Files:** Part of WI-5 (game opcodes)
- **Estimated complexity:** SMALL
- **Why nice-to-have:** Not needed for basic server functionality

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
