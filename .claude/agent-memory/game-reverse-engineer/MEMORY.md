# Game Reverse Engineer - Agent Memory

## Key Reference Files
- Reference source: `reference/` (organized analysis files)
- Protocol docs: `docs/` (RE documentation)
- Python scripts: `reference/scripts/` (~1228 .py files)

## Detailed Analysis Files
- [phase1-gap-analysis.md](phase1-gap-analysis.md) - Complete Phase 1 RE gap analysis and work items
- [gamespy-server-discovery.md](gamespy-server-discovery.md) - Complete GameSpy LAN + Internet server discovery analysis

## Key Patterns Discovered
- Ghidra labels (LAB_xxx) are handler callback addresses registered via the event system
- TGNetwork connState: 2=HOST (not client), 3=CLIENT (counterintuitive)
- The ReceiveMessageHandler is a label inside the MultiplayerGame constructor, not a separate function
- Event types: network_msg, host_start, disconnect, new_player, delete_player
- GameSpy query handler uses qr_t SDK with callback function pointers
- GameSpy object (244 bytes): contains qr_t (server QR1) and server_browser_t (client browser)
- LAN discovery: client broadcasts `\status\` to 255.255.255.255 ports 22101-22201 (101 ports, step 1)
- Internet discovery: client TCP connects to master at port 28900, gets IP:port list, then UDP queries each
- Master server: stbridgecmnd01.activision.com (overridable via masterserver.txt), gamename="bcommander", secret="Nm3aZ9"
- Server-side QR1 shares game socket, client-side LAN creates separate broadcast socket with SO_BROADCAST
- Peer entries in peer array sorted and binary searched by peer ID
- Player slot array: 16 slots max, 24 bytes per slot
- Three send queues per peer: unreliable, reliable, priority reliable

## Critical Architecture Finding: Multiplayer Relay
- BC multiplayer is PEER-TO-PEER RELAY, not server-authoritative
- The core relay function (ProcessGameMessage) clones msgs and sends to all other active peers
- The relay loop iterates 16 player slots, skips sender + host, sends via TGNetwork::Send
- Native opcodes 0x02-0x1F: engine deserializes on host for bookkeeping, then relays raw clone
- Python opcodes (MAX_MESSAGE_TYPES+N): handled entirely in mission scripts, scripts do own forwarding
- Chat (opcode 0x2D) is sent to HOST only, Python script forwards to "NoMe" group
- NO ship selection protocol -- ship creation is local, ET_OBJECT_CREATED triggers network sync
- Game start/end/restart are Python script messages (MISSION_INIT, END_GAME, RESTART_GAME)
- Two network groups: "NoMe" = all peers except host, "Forward" = selective relay
- MAX_MESSAGE_TYPES likely = 0x2C (44), making chat=0x2D, mission_init=0x36, end_game=0x39

## Connect Handshake (VERIFIED from packet traces)
- Server responds to client Connect(0x03) with Connect(0x03), NOT ConnectAck(0x05)
- Connect messages use 2-byte flags/length: [type:1][flags_len:2 LE][seq:2][data...]
- flags_len: bit15=reliable, bit14=priority, bits13-0=totalLen (includes type byte)
- Server Connect response payload: 1 byte = assigned peer slot number
- Wire: [0x03][0x06][0xC0][0x00][0x00][slot] = 6 bytes transport message
- ConnectAck(0x05) is used ONLY for shutdown notification cascade, not connection acceptance
- ConnectData (0x04) is for REJECTION/NOTIFICATION only, NOT peer mesh info
- ConnectData codes: 1=peer timeout, 2=transport reject, 3=game-full reject, 5=duplicate reject
- Code 3 comes from checksum handler (after checksum pass, no player slot available)
- Peer mesh info is carried by Keepalive (0x00) messages: [slot:1][IP:4][name:UTF-16LE+null]

## Message Factory Table (indexed by type)
- 0x00: Keepalive -- 64 bytes
- 0x01: ACK
- 0x02: Unknown type 2
- 0x03: Connect -- 64 bytes
- 0x04: ConnectData -- 68 bytes
- 0x05: ConnectAck
- 0x32: Reliable data message

## Wire Format Audit -- Critical Findings
- **CRITICAL BUG**: TGBufferStream bit-packing count field is off-by-one
  - Real game stores count directly (1,2,3); OpenBC stores count-1 (0,1,2)
  - Fix: bc_buf_write_bit must store bit_count, NOT bit_count-1; reader must NOT add 1
  - Affects ALL messages with WriteBit: Settings, ChecksumReq, UICollision, StateUpdate
- **NewPlayerInGame (0x2A) is CLIENT-to-SERVER**, server should NOT send it
  - Client sends 0x2A after receiving GameInit; server responds with MissionInit
- **Opcode 0x28**: stock dedi sends [0x28] (1 byte reliable) BEFORE Settings+GameInit
- **Checksum round 0xFF**: must be a full checksum request with dir="Scripts/Multiplayer" (capital S)
  - NOT the minimal [0x20][0xFF] that was originally attempted
- **Server keepalive**: stock dedi echoes client identity data (22 bytes), not minimal ping
- **UICollisionSetting (0x16)**: NOT sent during handshake in stock dedi
- **ACK flags**: stock uses 0x00/0x01/0x02, not 0x80
- **Settings slot byte**: stock sends slot=0 for first joiner, may differ from peer index

## TGBufferStream Functions (VERIFIED)
- WriteU8 (does NOT reset bit accumulator)
- WriteBit (bookmark-based, count incremented each call)
- WriteU16
- WriteFloat
- WriteBytes
- ReadBit
- ReadU8
- ReadU16
- ReadFloat
- ReadBytes
- TGBufferStream constructor
- SetBuffer method

## Key Globals
- collision damage flag (confirmed by g_pCollisionButton reference)
- friendly fire flag (used in damage handler to check team affiliation)
- player slot index
- IsClient flag
- IsHost flag
- IsMultiplayer flag
- game time (float, on clock object)

## Lessons
- Decompiled file organization is by address range not by topic -- always verify file contents by searching for specific functions
- The game object deserializer reads typeID + classID + state from network payload
- TGMessage::GetData returns payload pointer and size
- SendToGroup finds group by name, sends to all members
- SendToGroupMembers sends to all members of a group object
- Connect/ConnectAck/ConnectData/Keepalive ALL use 2-byte flags_len framing, NOT 1-byte len
- Checksum rounds: stock dedi sends 5 rounds (0, 1, 2, 3, 255), NOT 4
- Round 255 = final Scripts/Multiplayer check (recursive *.pyc)
- Non-bit TGBufferStream writes (WriteU8, WriteFloat, etc.) do NOT reset the bit accumulator
- All WriteBit calls across an entire message share the same byte via the bookmark mechanism
