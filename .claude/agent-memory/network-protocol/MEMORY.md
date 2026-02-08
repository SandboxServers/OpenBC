# Network Protocol Agent Memory

## Key Files
- [legacy-protocol-plan.md](legacy-protocol-plan.md) - Phase 1 standalone server implementation plan
- [tgnetwork-layout.md](tgnetwork-layout.md) - TGWinsockNetwork struct layout (0x34C bytes)
- [peer-layout.md](peer-layout.md) - Peer data structure offsets
- [gameplay-relay-analysis.md](gameplay-relay-analysis.md) - Full gameplay relay mechanics analysis

## Critical Facts
- TGWinsockNetwork is 0x34C bytes. State at +0x14 (4=disconnected, 2=host, 3=client)
- Socket stored at WSN+0x194. Port at WSN+0x338. Default port 0x5655 (22101)
- Peer array at WSN+0x2C (sorted by peer ID at peer+0x18, binary searched)
- 3 send queues per peer: unreliable (+0x64/+0x7C), reliable (+0x80/+0x98), priority (+0x9C/+0xB4)
- Sequence numbers per peer: unreliable at peer+0x28, reliable at peer+0x26, priority at peer+0x2A
- Max 255 messages per packet (count byte at offset 1)
- Packet buffer size 0x400 (1024 bytes), configurable at WSN+0x2B (param_1[0x2B])
- WSN+0x10C = send enabled flag. WSN+0x10E = isHost flag. WSN+0x10F = client initial-send flag
- GameSpy and TGNetwork share same UDP socket; peek-based router distinguishes by '\' prefix
- Hash function FUN_007202e0 is a custom 4-table byte-XOR hash (NOT MD5/CRC32)
- 4 lookup tables at 0x0095c888, 0x0095c988, 0x0095ca88, 0x0095cb88
- NetFile dispatcher handles opcodes 0x20-0x27; MultiplayerGame handles 0x00-0x1F
- Checksum exchange sends requests sequentially (not all at once), next sent only after ACK
- Priority queue stall issue: retransmits every ~5s, drops after 8 retries (~30s)
- MultiplayerGame player slots: 16 max, each 0x18 bytes at this+0x74

## Gameplay Relay Architecture
- BC is star-topology: host relays, NOT server-authoritative simulation
- TWO relay channels: C++ engine objects (automatic) + Python script messages (explicit)
- C++ relay: FUN_0069f620 deserializes object, then iterates 16 player slots, clones raw msg, sends to all others
- Python relay: scripts call SendTGMessage(0, msg) broadcast or SendTGMessageToGroup("NoMe", msg)
- FUN_006b51e0 is network-level relay (connection msgs only, NOT game data)
- FUN_006b6640 handles incoming type-3 data msgs; host path assigns peer IDs for NEW connections
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
