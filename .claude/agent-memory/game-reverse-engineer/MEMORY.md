# Game Reverse Engineer - Agent Memory

## Key Reference Files
- Decompiled source: `/mnt/c/users/Steve/source/projects/STBC-Dedicated-Server/reference/decompiled/`
- Protocol docs: `/mnt/c/users/Steve/source/projects/STBC-Dedicated-Server/docs/`
- Python scripts: `/mnt/c/users/Steve/source/projects/STBC-Dedicated-Server/reference/scripts/`

## Detailed Analysis Files
- [phase1-gap-analysis.md](phase1-gap-analysis.md) - Complete Phase 1 RE gap analysis and work items
- [reversed-functions.md](reversed-functions.md) - Catalogue of reversed function signatures
- [data-structures.md](data-structures.md) - Recovered data structure layouts
- [gameplay-relay-analysis.md](gameplay-relay-analysis.md) - Full gameplay relay analysis for playable dedicated server
- [gamespy-server-discovery.md](gamespy-server-discovery.md) - Complete GameSpy LAN + Internet server discovery analysis
- [connectdata-analysis.md](connectdata-analysis.md) - ConnectData (0x04) is rejection/notification, NOT peer mesh

## Key Patterns Discovered
- Ghidra labels (LAB_xxx) are handler callback addresses registered via FUN_006da130/FUN_006db380
- TGNetwork connState: 2=HOST (not client), 3=CLIENT (counterintuitive)
- The ReceiveMessageHandler at 0x0069f2a0 is a label inside FUN_0069e590, not a separate function
- Event types: 0x60001=network msg, 0x60002=host start, 0x60003=disconnect, 0x60004=new player, 0x60005=delete player
- GameSpy query handler (FUN_006ac1e0) uses qr_t SDK with callback function pointers at offsets 0x32-0x35
- GameSpy object (0xF4 bytes): +0xDC=qr_t (server QR1), +0xE0=server_browser_t (client browser)
- LAN discovery: client broadcasts `\status\` to 255.255.255.255 ports 22101-22201 (101 ports, step 1)
- Internet discovery: client TCP connects to master at port 28900, gets IP:port list, then UDP queries each
- Master server: stbridgecmnd01.activision.com (overridable via masterserver.txt), gamename="bcommander", secret="Nm3aZ9"
- Server-side QR1 shares game socket (qr_t[0] == WSN+0x194), client-side LAN creates separate broadcast socket with SO_BROADCAST
- Peer structure stride: entries in peer array at WSN+0x2C, indexed by binary search on peer+0x18 (peer ID)
- Player slot array: MultiplayerGame+0x74, stride 0x18, 16 slots max
- Three send queues per peer: unreliable (+0x64/+0x7C), reliable (+0x80/+0x98), priority reliable (+0x9C/+0xB4)

## Critical Architecture Finding: Multiplayer Relay
- BC multiplayer is PEER-TO-PEER RELAY, not server-authoritative
- FUN_0069f620 (ProcessGameMessage) is the core relay: clone msg, send to all other active peers
- The relay loop iterates 16 player slots, skips sender + host, sends via TGNetwork::Send
- Native opcodes 0x02-0x1F: engine deserializes on host for bookkeeping, then relays raw clone
- Python opcodes (MAX_MESSAGE_TYPES+N): handled entirely in mission scripts, scripts do own forwarding
- Chat (opcode 0x2D) is sent to HOST only, Python script forwards to "NoMe" group
- NO ship selection protocol -- ship creation is local, ET_OBJECT_CREATED triggers network sync
- Game start/end/restart are Python script messages (MISSION_INIT, END_GAME, RESTART_GAME)
- Two network groups: "NoMe" (DAT_008e5528) = all peers except host, "Forward" = selective relay
- MAX_MESSAGE_TYPES likely = 0x2C (44), making chat=0x2D, mission_init=0x36, end_game=0x39

## Connect Handshake (VERIFIED from packet traces)
- Server responds to client Connect(0x03) with Connect(0x03), NOT ConnectAck(0x05)
- Connect messages use 2-byte flags/length: [type:1][flags_len:2 LE][seq:2][data...]
- flags_len: bit15=reliable, bit14=priority, bits13-0=totalLen (includes type byte)
- Server Connect response payload: 1 byte = assigned peer slot number
- Wire: [0x03][0x06][0xC0][0x00][0x00][slot] = 6 bytes transport message
- ConnectAck(0x05) is used ONLY for shutdown notification cascade, not connection acceptance
- FUN_006b6640 is the server-side Connect handler, FUN_006b7540 allocates peer slot
- FUN_006be730 constructs Connect message objects (vtable PTR_LAB_008959ec)
- FUN_006bf2e0 constructs ConnectAck message objects (vtable PTR_LAB_00895a0c)
- FUN_006bac70 constructs ConnectData messages (vtable PTR_LAB_0089596c, 0x44 bytes), [0x10]=reason code
- ConnectData (0x04) is for REJECTION/NOTIFICATION only, NOT peer mesh info
- ConnectData codes: 1=peer timeout, 2=transport reject, 3=game-full reject, 5=duplicate reject
- Code 3 comes from 10_netfile_checksums.c:444 (after checksum pass, no player slot available)
- Peer mesh info is carried by Keepalive (0x00) messages: [slot:1][IP:4][name:UTF-16LE+null]
- FUN_006bc5b0 (vtable 0089598c) is Keepalive constructor, NOT ConnectData
- See [connect-handshake-analysis.md](connect-handshake-analysis.md) and [connectdata-analysis.md](connectdata-analysis.md)

## Message Factory Table (0x009962d4, indexed by type*4)
- 0x00: FUN_006bc6a0 (Keepalive) -- vtable 0x0089598C, size 0x40
- 0x01: FUN_006bd1f0 (ACK) -- vtable 0x008959AC
- 0x02: FUN_006bdd10 (unknown type 2)
- 0x03: FUN_006be860 (Connect) -- vtable 0x008959EC, size 0x40
- 0x04: FUN_006badb0 (ConnectData) -- vtable 0x0089596C, size 0x44
- 0x05: FUN_006bf410 (ConnectAck) -- vtable 0x00895A0C
- 0x32: FUN_006b83f0 (Reliable) -- vtable 0x008958D0

## Wire Format Audit (2026-02-16) -- see docs/wire-format-audit.md
- **CRITICAL BUG**: TGBufferStream bit-packing count field is off-by-one
  - Real game stores count directly (1,2,3); OpenBC stores count-1 (0,1,2)
  - Decompiled WriteBit at FUN_006cf770: `bVar3 = (bVar3 >> 5) + 1` then stores
  - Reader FUN_006cf580 uses `1 << (byte >> 5)` as threshold -- matches the writer
  - Fix: bc_buf_write_bit must store bit_count, NOT bit_count-1; reader must NOT add 1
  - Affects ALL messages with WriteBit: Settings, ChecksumReq, UICollision, StateUpdate
- **NewPlayerInGame (0x2A) is CLIENT-to-SERVER**, server should NOT send it
  - Client sends 0x2A after receiving GameInit; server responds with MissionInit
- **Opcode 0x28**: stock dedi sends [0x28] (1 byte reliable) BEFORE Settings+GameInit
- **Checksum round 0xFF**: must be a full checksum request with dir="Scripts/Multiplayer" (capital S)
  - NOT the minimal [0x20][0xFF] that OpenBC currently sends
- **Server keepalive**: stock dedi echoes client identity data (22 bytes), not minimal [0x00][0x02]
- **UICollisionSetting (0x16)**: NOT sent during handshake in stock dedi
- **ACK flags**: stock uses 0x00/0x01/0x02, not 0x80
- **Settings slot byte**: stock sends slot=0 for first joiner, may differ from peer index

## TGBufferStream Function IDs (VERIFIED)
- FUN_006cf730 = WriteU8 (does NOT reset bit accumulator)
- FUN_006cf770 = WriteBit (bookmark-based, count incremented each call)
- FUN_006cf7f0 = WriteU16
- FUN_006cf8b0 = WriteFloat
- FUN_006cf2b0 = WriteBytes
- FUN_006cf580 = ReadBit
- FUN_006cf540 = ReadU8
- FUN_006cf600 = ReadU16
- FUN_006cf6b0 = ReadFloat
- FUN_006cf230 = ReadBytes
- FUN_006cefe0 = TGBufferStream constructor
- FUN_006cf180 = TGBufferStream::SetBuffer

## Key Globals
- DAT_008e5f59 = collision damage flag (confirmed by g_pCollisionButton reference)
- DAT_0097faa2 = friendly fire flag (used in damage handler to check team affiliation)
- DAT_0097fa84 = player slot index
- DAT_0097fa88 = IsClient flag
- DAT_0097fa89 = IsHost flag
- DAT_0097fa8a = IsMultiplayer flag
- DAT_009a09d0+0x90 = game time (float)

## Lessons
- FUN_006b0030 in 11_tgnetwork.c is actually Bink video code, NOT TGNetwork - file organization is by address range not by topic
- The file 14_gamespy.c contains VarManager/Config code, NOT GameSpy SDK - actual GameSpy is in 10_netfile_checksums.c (0x006Axxxx range)
- Always verify file contents by grep for specific addresses rather than trusting filenames
- FUN_005a1f50 deserializes game objects from network payload (reads typeID + classID + state)
- FUN_006b8530 is TGMessage::GetData(int* outSize) -- returns payload ptr and size
- FUN_006b4de0 is SendToGroup(groupName, msg) -- finds group by name, sends to all members
- FUN_006b4ec0 is SendToGroupMembers(groupPtr, msg) -- sends to all members of group object
- FUN_0047dab0 is ShipObject constructor (creates network ship wrapper from deserialized data)
- Connect/ConnectAck/ConnectData/Keepalive ALL use 2-byte flags_len framing, NOT 1-byte len
- The packet_trace_and_decode.inc.c decoder treats them as [type:1][len:1] which is WRONG but happens to produce correct message boundaries because totalLen matches
- Checksum rounds: stock dedi sends 5 rounds (0, 1, 2, 3, 255), NOT 4
- Round 255 = final Scripts/Multiplayer check (recursive *.pyc)
- Non-bit TGBufferStream writes (WriteU8, WriteFloat, etc.) do NOT reset the bit accumulator
- All WriteBit calls across an entire message share the same byte via the bookmark mechanism
