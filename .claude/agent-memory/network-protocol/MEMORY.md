# Network Protocol Agent Memory

## Key Files
- [legacy-protocol-plan.md](legacy-protocol-plan.md) - Phase 1 standalone server implementation plan
- [tgnetwork-layout.md](tgnetwork-layout.md) - TGWinsockNetwork struct layout (844 bytes)
- [peer-layout.md](peer-layout.md) - Peer data structure field descriptions
- [gameplay-relay-analysis.md](gameplay-relay-analysis.md) - Full gameplay relay mechanics analysis

## Critical Facts
- TGWinsockNetwork is 844 bytes. Connection state: 4=disconnected, 2=host, 3=client
- Socket field stores the UDP socket handle. Default port 22101
- Peer array is sorted by peer ID, accessed via binary search
- 3 send queues per peer: unreliable, reliable, priority reliable
- Sequence numbers per peer: one for unreliable, one for reliable, one for priority
- Max 255 messages per packet (count byte at offset 1)
- Packet buffer size 1024 bytes (configurable)
- Send-enabled flag, isHost flag, and client initial-send flag are tracked
- GameSpy and TGNetwork share same UDP socket; peek-based router distinguishes by '\' prefix
- Hash function is a custom 4-table Pearson byte-XOR hash (NOT MD5/CRC32)
- Four 256-byte lookup tables form Mutually Orthogonal Latin Squares (MOLS)
- NetFile dispatcher handles opcodes 0x20-0x27; MultiplayerGame handles 0x00-0x1F
- Checksum exchange sends requests sequentially (not all at once), next sent only after ACK
- Priority queue stall issue: retransmits every ~5s, drops after 8 retries (~30s)
- MultiplayerGame player slots: 16 max, each 24 bytes

## Gameplay Relay Architecture
- BC is star-topology: host relays, NOT server-authoritative simulation
- TWO relay channels: C++ engine objects (automatic) + Python script messages (explicit)
- C++ relay: message handler deserializes object, then iterates 16 player slots, clones raw msg, sends to all others
- Python relay: scripts call SendTGMessage(0, msg) broadcast or SendTGMessageToGroup("NoMe", msg)
- Network-level relay is for connection messages only, NOT game data
- Data message handler processes incoming type-3 data msgs; host path assigns peer IDs for NEW connections
- Position updates use UNRELIABLE queue; script messages (scores/chat/game-end) use RELIABLE
- Clients own their own physics; server just relays (no server-authoritative validation in original)
- SendTGMessage(0, msg) = broadcast ALL; SendTGMessage(id, msg) = unicast; SendTGMessageToGroup(name, msg) = named group
- "NoMe" group = all peers except self; "Forward" group = forwarding peers
- Game object serialization: ~60-100 bytes per object (type ID + position + rotation + velocity + flags)
- Bandwidth estimate: 5-10 KB/s per client for 6-player; 12-24 KB/s for 16-player
- Phase 1 server should use byte-perfect relay (forward raw bytes, no deserialization for relay)
- Python scripts still need full TGMessage/TGBufferStream for script-level message processing

## Decisions
- Phase 1: Reimplement transport from scratch (NOT using ENet) for byte-perfect compatibility
- ENet wire format differs from TGWinsockNetwork; vanilla clients cannot connect to ENet
- Use raw UDP sockets matching the original binary protocol exactly
- Phase 1 gameplay: byte-perfect relay (clone and forward raw msgs to all peers)
