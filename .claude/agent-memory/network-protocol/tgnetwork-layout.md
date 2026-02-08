# TGWinsockNetwork Struct Layout (0x34C bytes)

Reconstructed from decompiled FUN_006b3a00 (constructor) and usage across 11_tgnetwork.c.

## Core Fields (int offsets from param_1 = this)
```
+0x00  [0]   vtable pointer (&PTR_FUN_008958f0 for TGWinsockNetwork)
+0x14  [5]   connState: 4=disconnected, 2=host, 3=client
+0x18  [6]   selfPeerID (0xFFFFFFFF initially)
+0x1C  [7]   ??? (0 initially)
+0x20  [8]   hostPeerID (1 initially)
+0x24  [9]   ??? (0 initially)
+0x28  [10]  peerHash (initialized by FUN_006bb990)
+0x2C  [11]  peerArray (ptr to sorted array of peer ptrs)
+0x30  [12]  peerCount
+0x34  [13]  peerCapacity
```

## Queue Group 1 (offsets as int[])
```
+0x38  [14]  queue1_head (linked list)
+0x3C  [15]  queue1_...
+0x40  [16]  queue1_...
+0x44  [17]  queue1_...
+0x48  [18]  queue1_...
+0x4C  [19]  -1 initially
+0x50  [20]  queue1_count
+0x54  [21]  queue2_head
+0x58  [22]  ...
+0x5C  [23]  ...
+0x60  [24]  ...
+0x64  [25]  ...
+0x68  [26]  -1 initially
+0x6C  [27]  queue2_count
```

## More queue groups follow same pattern through offset 0xC4

## Configuration
```
+0xA8  [0x2A]  maxBufferSize (0x8000 = 32768)
+0xAC  [0x2B]  packetSize (0x400 = 1024 bytes; TGWinsockNetwork overrides to 0x200 = 512)
+0xB0  [0x2C]  roundRobinIndex (0 initially, cycles through peers)
+0xB4  [0x2D]  reliableTimeout (0x43b40000 = 360.0f)
+0xB8  [0x2E]  disconnectTimeout (0x42340000 = 45.0f)
+0xBC  [0x2F]  lastActivityTime (DAT_0099c6bc = current game time)
+0x100 [0x40]  ??? (0)
+0x108 [0x42]  maxRetries (2)
+0x10C [0x43]  sendEnabled (1 byte, initially 1)
+0x10D        forceDisconnect (1 byte, initially 0)
+0x10E        isHost (1 byte)
+0x10F        clientInitialSendFlag (1 byte, initially 0)
+0x110 [0x44]  statsEnabled (1 byte, initially 0)
```

## TGWinsockNetwork-Specific Fields
```
+0x190 [0x64]  vtable2 (&PTR_FUN_00895968)
+0x194 [0x65]  socket (SOCKET, 0xFFFFFFFF initially)
+0x33C [0xCF]  packetQueue (linked list of buffered packets)
+0x340 [0xD0]  ???
+0x344 [0xD1]  ??? (1 byte)
+0x348 [0xD2]  portOverrideList (linked list: [peerID, port, next])
+0x338 [0xCE]  port (0x5655 = 22101 default)
```

## Socket Creation (FUN_006b9b20)
- socket(AF_INET=2, SOCK_DGRAM=2, IPPROTO_UDP=0x11)
- If isHost: bind to INADDR_ANY:port
- ioctlsocket(FIONBIO) for non-blocking
- Socket stored at +0x194

## Peer Lookup
- Binary search on peerArray[0..peerCount-1] by peer+0x18 (peer ID)
- FUN_00401cc0 does the binary search
