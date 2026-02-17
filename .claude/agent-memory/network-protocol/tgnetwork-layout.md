# TGWinsockNetwork Struct Layout (844 bytes)

Reconstructed from behavioral analysis of the constructor and usage across the network module.

## Core Fields
- **vtable** - virtual function table pointer
- **connState** - connection state: 4=disconnected, 2=host, 3=client
- **selfPeerID** - own peer ID (0xFFFFFFFF initially)
- **hostPeerID** - host's peer ID (1 initially)
- **peerHash** - hash structure for peer lookup
- **peerArray** - pointer to sorted array of peer pointers
- **peerCount** - number of connected peers
- **peerCapacity** - allocated capacity for peer array

## Queue Groups
Multiple queue groups follow the same pattern: head, tail, cursor fields, a -1 sentinel, and a count. These appear through the first ~200 bytes of the object.

## Configuration
- **maxBufferSize** - 32768 (0x8000) bytes max pending data
- **packetSize** - 1024 bytes default (TGWinsockNetwork overrides to 512)
- **roundRobinIndex** - cycles through peers for fair sending (starts at 0)
- **reliableTimeout** - 360.0 seconds (float) before dropping unacked reliable messages
- **disconnectTimeout** - 45.0 seconds (float) before disconnecting inactive peers
- **lastActivityTime** - current game time reference
- **maxRetries** - 2 (default retry count)
- **sendEnabled** (1 byte) - initially 1, controls whether outbound packets are sent
- **forceDisconnect** (1 byte) - initially 0
- **isHost** (1 byte) - 1 for host, 0 for client
- **clientInitialSendFlag** (1 byte) - initially 0, set when client sends first packet
- **statsEnabled** (1 byte) - initially 0, enables bandwidth tracking

## TGWinsockNetwork-Specific Fields (at higher offsets)
- **vtable2** - secondary virtual table pointer
- **socket** (SOCKET) - the UDP socket handle, 0xFFFFFFFF (INVALID_SOCKET) initially
- **packetQueue** - linked list of buffered packets
- **portOverrideList** - linked list: [peerID, port, next] for per-peer port overrides
- **port** - 22101 (default game port)

## Socket Creation
- socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
- If isHost: bind to INADDR_ANY:port
- ioctlsocket(FIONBIO) for non-blocking mode
- Socket stored in the socket field

## Peer Lookup
- Binary search on peerArray[0..peerCount-1] by peer ID
