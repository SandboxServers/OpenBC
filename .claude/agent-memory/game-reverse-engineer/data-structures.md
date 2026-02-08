# Recovered Data Structure Layouts

## TGWinsockNetwork (0x34C bytes)
```
Offset  Size  Name                    Notes
+0x00   4     vtable
+0x14   4     connState               2=HOST, 3=CLIENT, 4=DISCONNECTED
+0x18   4     localPeerID             Own peer ID (1 when hosting)
+0x1C   4     localAddress
+0x20   4     hostPeerID              Remote host's peer ID
+0x24   4     hostAddress
+0x28   4     groupList               TGGroupList ptr
+0x2C   4     peerArray               Sorted array of TGPeer* (binary search by peerID)
+0x30   4     peerCount               Number of entries in peer array
+0x38-0x6C    incomingQueue           Priority queue for incoming messages
+0x54-0x6C    reliableInQueue         Reliable incoming queue
+0x70-0x9C    appDeliveryQueue        Messages ready for application
+0x8C-0x9C    orderedDeliveryQueue
+0xA8   4     maxPendingSends         Max messages in send queues
+0xAC   4     sendBufferSize          Max packet size
+0xB0   4     sendTimeout             Timeout in seconds (float)
+0xB4   4     connectionTimeout       Connection timeout (float)
+0xB8   4     lastPingTime
+0xBC   4     pingInterval
+0xC0   4     lastHostPingTime
+0x10C  1     sendEnabled             Must be != 0 for SendOutgoing to work
+0x10D  1     forceDisconnect         Client-path force disconnect flag
+0x10E  1     isHost                  1=host, 0=client
+0x10F  1     joinInProgress          Set during client connection
+0x194  4     socketHandle            UDP SOCKET (shared with GameSpy)
+0x2B   4     maxPacketSize?          Used in SendOutgoingPackets buffer alloc
+0x2C   4     roundRobinCounter       For fair peer iteration in send
+0x2D   4     retryBaseTimeout        Float, base timeout for retry
+0x2E   4     retryTimeoutValue       Float
+0x2F   4     lastSendTimestamp       Float
+0x30   4     lastRecvTimestamp       Float
+0x338  4     portNumber              UDP port (default 22101 / 0x5655)
+0x44   1     profilingEnabled
```
NOTE: Many offsets above 0x100 are uncertain. The object is 0x34C bytes but internal layout is partially known.

## TGPeer (~0xC0 bytes)
```
Offset  Size  Name                    Notes
+0x18   4     peerID                  Unique peer identifier
+0x1C   4     address                 sockaddr_in or similar
+0x24   2     reliableSeqExpected     Expected incoming reliable sequence
+0x26   2     reliableSeqNext         Next outgoing reliable sequence
+0x28   2     unreliableSeqExpected   Expected incoming unreliable sequence
+0x2A   2     unreliableSeqNext       Next outgoing unreliable sequence
+0x2C   4     lastRecvTime            Float timestamp
+0x30   4     lastSendTime            Float timestamp
+0x38   4     bytesReceived           Stats counter
+0x40   4     totalBytesRecv          Including headers
+0x48   4     bytesSent               Stats counter
+0x54   4     totalBytesSent          Including headers
+0x64   4     unreliableQueue.head    Linked list node ptr (send queue)
+0x68   4     unreliableQueue.tail
+0x6C   4     unreliableQueue.???
+0x70   4     unreliableQueue.current
+0x74   4     unreliableQueue.index
+0x78   4     unreliableQueue.???
+0x7C   4     unreliableQueue.count
+0x80   4     reliableQueue.head      Linked list (send queue)
+0x84   4     reliableQueue.tail
+0x8C   4     reliableQueue.current
+0x90   4     reliableQueue.index
+0x98   4     reliableQueue.count
+0x9C   4     priorityQueue.head      Linked list (send queue)
+0xA0   4     priorityQueue.tail
+0xA8   4     priorityQueue.current
+0xAC   4     priorityQueue.index
+0xB4   4     priorityQueue.count
+0xB8   4     disconnectTime          Float timestamp
+0xBC   1     disconnecting           Flag: peer is being disconnected
```

## MultiplayerGame
```
Offset  Size  Name                    Notes
+0x00   4     vtable                  PTR_FUN_0088b480
+0x54   4     localShipObject         Ship object pointer (client only)
+0x74   0x180 playerSlots[16]         16 slots, 0x18 bytes each
+0x78   1     slot[0].active          Per-slot active flag
+0x7C   4     slot[0].peerID          Per-slot peer network ID
+0x80   4     slot[0].objectID        Per-slot game object ID
+0x1F4  end   (end of slot array)
+0x1F8  1     readyForNewPlayers      Set to 1 to accept connections
+0x1FC  4     maxPlayers              Maximum allowed (set via SWIG)
```

## Player Slot (0x18 bytes each, partially known)
```
Offset  Size  Name                    Notes
+0x00   1     active                  0=empty, 1=active
+0x04   4     peerID                  TGNetwork peer identifier
+0x08   4     objectID                Game object ID for this player
+0x0C-0x17    unknown                 Possibly: name ptr, team, ready state, checksum state
```

## NetFile / ChecksumManager (0x48 bytes)
```
Offset  Size  Name                    Notes
+0x00   4     vtable
+0x18   ???   hashTableA              Tracking hash table
+0x24   ???   hashTableA.buckets
+0x28   ???   hashTableB              Queued checksum requests
+0x34   ???   hashTableB.buckets
+0x38   ???   hashTableC              Pending file transfers
+0x44   ???   hashTableC.buckets
```

## TGMessage (base, ~0x40+ bytes)
```
Offset  Size  Name                    Notes
+0x00   4     vtable                  Type-specific operations
+0x03   4     senderPeerID            Set by recv path
+0x04   4     senderAddress
+0x05   2     sequenceNumber          At message+5 in DispatchToApplication
+0x14   2     ackSequence             Used by ReliableACKHandler
+0x28   4     destPeerID              Set during send
+0x38   ???   size
+0x39   1     flags?
+0x3A   1     reliable                1=reliable, 0=unreliable
+0x3B   1     priority                1=priority (goes to front of unreliable queue)
+0x3C   1     ackFlag?
+0x3D   1     sentFlag?
+0x3E-0x3F    ???
+0x40   1     retryRelatedFlag
```
NOTE: Message layout is very uncertain. The vtable provides:
- vtable+0x00: GetType() -> int
- vtable+0x04: Release(int flag)
- vtable+0x08: Serialize(char* buf, int bufLen) -> int bytesWritten
- vtable+0x14: GetPayloadSize() -> int
- vtable+0x18: Clone() -> TGMessage*
- vtable+0x1C: Split(int* outParts, int maxPartSize) -> TGMessage**

## GameSpy qr_t (0xF4 bytes, stored at UtopiaModule+0x7C)
```
Offset  Size  Name                    Notes
+0x00   4     socketFD                SOCKET handle (shared with WSN+0x194)
+0x32   4     basicCallback           Function ptr: basic query response builder
+0x33   4     rulesCallback           Function ptr: rules query response builder
+0x34   4     playersCallback         Function ptr: players query response builder
+0x35   4     extraCallback           Function ptr: extra info response builder
+0x37   4     queryCounter            Incremented per query
+0x38   4     responsePartCounter     For multi-part responses
+0x3A   1     stateFlag
+0x3B   4     userData                Passed to callbacks
+0xDC   4     socketPtr               SOCKET* for sendto (GameSpy class at +0xDC)
+0xE4   1     recvfromEnabled         Set to 0 to disable GameSpy's own recv loop
```
NOTE: qr_t offsets use DWORD indexing (param_1[0x32]) not byte offsets. Actual byte offset for callback at index 0x32 is 0xC8.

## Event Types (confirmed from decompiled string refs)
```
0x60001   ET_NETWORK_MESSAGE_EVENT
0x60002   ET_HOSTING_STARTED
0x60003   ET_NETWORK_DISCONNECT
0x60004   ET_NETWORK_NEW_PLAYER
0x60005   ET_NETWORK_DELETE_PLAYER
0x60006   ET_GAMESPY_PROCESS_QUERY
0x8000C5  ET_EXITED_WARP
0x8000C8  ET_OBJECT_CREATED
0x8000D8  ET_START_FIRING
0x8000DA  ET_STOP_FIRING
0x8000DC  ET_STOP_FIRING_AT_TARGET
0x8000DD  ET_SUBSYSTEM_STATE_CHANGED
0x8000DF  ET_HOST_EVENT
0x8000E0  ET_SET_PHASER_LEVEL
0x8000E2  ET_START_CLOAKING
0x8000E4  ET_STOP_CLOAKING
0x8000E6  ET_CHECKSUM_RESULT (individual)
0x8000E7  ET_SYSTEM_CHECKSUM_FAILED
0x8000E8  ET_CHECKSUM_COMPLETE
0x8000E9  ET_KILL_GAME
0x8000EC  ET_START_WARP
0x8000F1  ET_NEW_PLAYER_IN_GAME
0x8000F4  (timer event?)
0x8000F8  ET_GAMESPY_CONNECTION_FAILED
0x8000FE  ET_TORPEDO_TYPE_CHANGED
0x8000FF  ET_RETRY_CONNECT
0x800053  ET_START
0x80004A  ET_CREATE_SERVER
0x80004E  ET_OBJECT_EXPLODING
0x800058  ET_CHANGED_TARGET
0x80005D  ET_ENTER_SET
0x800074  (host event variant 1)
0x800075  (host event variant 2)
0x800076  ET_REPAIR_LIST_PRIORITY
```
