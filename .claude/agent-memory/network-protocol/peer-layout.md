# Peer Data Structure Layout

Reconstructed from decompiled code, especially FUN_006b55b0 (SendOutgoingPackets).

## Per-Peer Structure (estimated 0xC0+ bytes)
```
+0x18  peerID (int, used for binary search in sorted peer array)
+0x1C  address info (sockaddr or equivalent)
+0x24  seqRecvUnreliable (ushort) - expected sequence from this peer
+0x26  seqSendReliable (ushort) - next reliable sequence to assign
+0x28  seqRecvReliable (ushort) - expected reliable sequence from this peer
+0x2A  seqSendPriority (ushort) - next priority sequence to assign
+0x2C  lastRecvTime (float, game time)
+0x30  lastSendTime (float, game time)

# Unreliable send queue
+0x64  unreliable_head (linked list node ptr)
+0x68  unreliable_tail
+0x6C  unreliable_cursor (iteration ptr)
+0x70  unreliable_cursor2
+0x74  unreliable_iterIndex
+0x78  unreliable_???
+0x7C  unreliable_count (int)

# Reliable send queue
+0x80  reliable_head
+0x84  reliable_tail
+0x88  reliable_???
+0x8C  reliable_cursor
+0x90  reliable_iterIndex
+0x94  reliable_???
+0x98  reliable_count (int)

# Priority reliable send queue
+0x9C  priority_head
+0xA0  priority_tail
+0xA4  priority_???
+0xA8  priority_cursor
+0xAC  priority_iterIndex
+0xB0  priority_???
+0xB4  priority_count (int)

# Stats
+0x38  bytesRecvPayload
+0x40  bytesRecvTotal (payload + headers)
+0x48  bytesSentPayload
+0x54  bytesSentTotal
+0x38  bytesRecv (alternate interpretation for host-peer self stats)
+0xB8  disconnectTimestamp (float)
+0xBC  isDisconnecting (1 byte)

# Recv queues (incoming, parallel structure)
+0x38  (incoming queue uses offsets 0x38-0x54 area for stats)
+0x70  incomingQueue (linked list) - used in DispatchIncomingQueue
+0x8C  incomingReliableQueue (linked list)
```

## Queue Node (linked list element)
Each queue is a singly-linked list of 8-byte nodes:
```
+0x00  messagePtr (pointer to message object)
+0x04  nextNode (pointer to next node, or 0)
```
Head points to first node, tail points to last node.
Count tracks total items.

## Message Object Layout (approx 0x44 bytes)
```
+0x00  vtable (polymorphic message types)
+0x0C  senderPeerID
+0x10  recipientSockAddr
+0x14  sequenceNumber (ushort)
+0x18  ???
+0x20  sendTimestamp (float)
+0x24  lastSendTimestamp (float)
+0x28  targetPeerID
+0x38  ???
+0x39  fragmentFlag (byte)
+0x3A  isReliable (byte, 0=unreliable, 1=reliable)
+0x3B  isPriority (byte)
+0x3C  ???
+0x3D  ???
+0x40  boolFlag
```

## Message Types (vtable dispatch)
Message type returned by vtable[0] (GetType):
- 0: Connection message (FUN_006b63a0)
- 1: Reliable ACK message (FUN_006b64d0)
- 2: Internal/discard
- 3: Data message (FUN_006b6640) - normal game data
- 4: Disconnect message (FUN_006b6a70)
- 5: Keepalive/ping (FUN_006b6a20)

## Retry Logic
From FUN_006b55b0:
- FUN_006b8700 checks if message is ready to send (timing check)
- FUN_006b8670 sets retry count
- Priority queue: retries up to 3 before escalating; drops after 8 retries
- Reliable queue: drops after reliableTimeout (360.0f default)
- Max 0xFE (254) messages per outbound packet
