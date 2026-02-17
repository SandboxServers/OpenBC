# Peer Data Structure Layout

Reconstructed from behavioral analysis of the network send/receive pipeline.

## Per-Peer Structure (estimated ~192 bytes)

### Identity
- **peerID** (int) - unique identifier, used for binary search in sorted peer array
- **address** - socket address info (sockaddr or equivalent)

### Sequence Tracking
- **seqRecvUnreliable** (ushort) - expected unreliable sequence from this peer
- **seqSendReliable** (ushort) - next reliable sequence to assign
- **seqRecvReliable** (ushort) - expected reliable sequence from this peer
- **seqSendPriority** (ushort) - next priority sequence to assign

### Timestamps
- **lastRecvTime** (float, game time) - last received packet timestamp
- **lastSendTime** (float, game time) - last sent packet timestamp

### Unreliable Send Queue
- Linked list with head, tail, cursor, iteration index, and count fields
- Count tracks total items in queue

### Reliable Send Queue
- Same linked list structure as unreliable queue
- Separate head, tail, cursor, iteration index, and count

### Priority Reliable Send Queue
- Same linked list structure
- Separate head, tail, cursor, iteration index, and count

### Statistics
- **bytesRecvPayload** - received payload bytes
- **bytesRecvTotal** - received total bytes (payload + headers)
- **bytesSentPayload** - sent payload bytes
- **bytesSentTotal** - sent total bytes

### Connection State
- **disconnectTimestamp** (float) - when disconnect initiated
- **isDisconnecting** (1 byte) - disconnect in progress flag

## Queue Node (linked list element)
Each queue is a singly-linked list of 8-byte nodes:
- **messagePtr** - pointer to message object
- **nextNode** - pointer to next node, or NULL

Head points to first node, tail points to last node. Count tracks total items.

## Message Object Layout (approx 68 bytes)

### Fields
- **vtable** - polymorphic message types (determines serialization behavior)
- **senderPeerID** - who sent this message
- **recipientSockAddr** - destination address
- **sequenceNumber** (ushort) - for ordering
- **sendTimestamp** (float) - when first sent
- **lastSendTimestamp** (float) - when last retransmitted
- **targetPeerID** - intended recipient
- **fragmentFlag** (byte) - fragmentation indicator
- **isReliable** (byte) - 0=unreliable, 1=reliable
- **isPriority** (byte) - priority queue flag

## Message Types (vtable dispatch)
Message type returned by GetType virtual method:
- 0: Keepalive message
- 1: Reliable ACK message
- 2: Internal/discard
- 3: Connect message (connection handshake)
- 4: ConnectData message (rejection/notification)
- 5: ConnectAck message (shutdown notification)
- 0x32: Data message (normal game data -- reliable)

## Retry Logic
From the outbound packet processing:
- Ready-to-send check uses timing comparison
- Priority queue: retries up to 3 before escalating; drops after 8 retries
- Reliable queue: drops after reliableTimeout (360.0s default)
- Max 254 (0xFE) messages per outbound packet
