# OpenBC Phase 1: Verified Wire Protocol Reference

Observed behavior of the Bridge Commander 1.1 multiplayer network protocol, documented from black-box packet captures of stock dedicated servers communicating with stock BC 1.1 clients. All data below reflects observed wire behavior only.

**Date**: 2026-02-17
**Method**: Packet capture with decryption (AlbyRules stream cipher)
**Trace corpus**: 2,648,271 lines / 136MB from a 34-minute 3-player combat session ("Battle of Valentine's Day", 2026-02-14), plus a 4,343-line loopback session for checksum timing
**Clean room statement**: This document contains no decompiled code, no binary addresses, no internal memory offsets, and no handler tables. All formats and behaviors are derived from observable wire data.

---

## 1. Transport Layer

### AlbyRules Stream Cipher

All game traffic is encrypted with a PRNG-based stream cipher using the key `"AlbyRules!"` (10 bytes). GameSpy traffic (first byte `\`) is sent in plaintext and is NOT encrypted.

```
Key: "AlbyRules!" = { 0x41, 0x6C, 0x62, 0x79, 0x52, 0x75, 0x6C, 0x65, 0x73, 0x21 }
```

**WARNING**: The cipher is NOT a simple repeating XOR. It is a complex PRNG with cross-multiplication (multiplier `0x4E35`, addend `0x015A`), per-byte key scheduling (5 rounds per byte), and plaintext feedback into the key string. Each byte's output depends on all preceding plaintext bytes.

Key properties:
- Byte 0 (direction flag) is transmitted **unencrypted**
- Per-packet reset (no session state)
- Encrypt and decrypt are **not symmetric** (plaintext feedback timing differs)
- Sign extension on input bytes (MOVSX behavior)

See **[transport-cipher.md](transport-cipher.md)** for the complete algorithm specification.

### Raw UDP Packet Format

After AlbyRules decryption, every game packet has a 2-byte header:

```
Offset  Size  Field
------  ----  -----
0       1     direction     (0x01=from server, 0x02+=from client, 0xFF=initial handshake)
1       1     msg_count     (number of transport messages in this packet)
2+      var   messages      (sequence of self-describing transport messages)
```

The `direction` byte also serves as a peer index for client packets (0x02=first client, 0x03=second, etc.). The initial Connect packet uses 0xFF.

### Transport Message Types

Each transport message within the packet is self-describing. Two formats exist:

**ACK (type 0x01)** -- fixed 4 bytes:
```
[0x01][seq_hi:1][seq_lo:1][ack_flags:1]
```

**All other types** -- variable length with self-describing size:
```
[type:1][totalLen:1][body...]
```
`totalLen` includes the type byte itself. Body size = totalLen - 2.

**For type 0x32 (Data)** -- the body starts with a flags byte:
```
[0x32][totalLen:1][flags:1][seq_hi:if reliable][seq_lo:if reliable][game_payload...]
```
If `flags & 0x80` (reliable): 2 sequence bytes present after flags. Payload = totalLen - 5.
If `flags == 0x00` (unreliable): no sequence bytes. Payload = totalLen - 3.

**Observed transport types**:

| Type | Name | Format | Notes |
|------|------|--------|-------|
| 0x00 | Keepalive | `[0x00][totalLen][body]` | Periodic liveness |
| 0x01 | ACK | `[0x01][seq_hi][seq_lo][flags]` | Fixed 4 bytes |
| 0x02 | Transport | `[0x02][totalLen][body]` | Internal transport control |
| 0x03 | Connect | `[0x03][totalLen][body]` | Connection request |
| 0x05 | ConnectAck | `[0x05][totalLen][body]` | Connection response |
| 0x06 | Disconnect | `[0x06][totalLen][body]` | Graceful disconnect |
| 0x32 | Data | See above | Game payload carrier |

### Reliable Data Flags (type 0x32)

```
Byte: [7][6][5][4][3][2][1][0]
       |        |           |
       |        |           +-- 0x01: More fragments follow (0 = last/only fragment)
       |        +-------------- 0x20: Fragmented message
       +----------------------- 0x80: Reliable delivery (sequence tracked, ACK expected)
```

Common observed flag combinations:
- `0x80` -- reliable, unfragmented (most game commands)
- `0x00` -- unreliable, unfragmented (StateUpdate)
- `0xA1` -- reliable + fragmented + more fragments follow
- `0xA0` -- reliable + fragmented + last fragment

### Fragment Layout

Large messages that exceed the transport's per-message limit are fragmented. Observed in checksum round 2 responses (~400 bytes) and round 0xFF responses (~275 bytes).

```
First fragment:      [frag_idx:u8][total_frags:u8][inner_opcode:u8][payload...]
Subsequent frags:    [frag_idx:u8][continuation_data...]
```

Fragment indices are 0-based. `total_frags` appears only in the first fragment. The receiver must reassemble all fragments before parsing the inner game message.

**Reliable data flags for fragments**:
- `0xA1` = reliable + fragmented + more fragments follow (`0x80 | 0x20 | 0x01`)
- `0xA0` = reliable + fragmented + last fragment (`0x80 | 0x20`)

The flag byte `0x01` indicates "more fragments follow" (not "first fragment"). The receiver detects the first fragment by checking whether reassembly is already in progress.

### Hex Dump: Packet #1 -- Connect (Client -> Server)

```
Client -> Server, 17 bytes (Battle trace, first packet):
0000: FF 01 03 0F C0 00 00 0A 0A 0A EF F9 78 00 00 00  |............x...|
0010: 00                                               |.|

Header: dir=0xFF (initial handshake), msg_count=1
  [msg 0] Connect (0x03) totalLen=15
```

### Hex Dump: Packet #2 -- ConnectAck + ChecksumReq (Server -> Client)

```
Server -> Client, 39 bytes:
0000: 01 03 01 00 00 02 03 06 C0 00 00 02 32 1B 80 00  |............2...|
0010: 00 20 00 08 00 73 63 72 69 70 74 73 2F 07 00 41  |. ...scripts/..A|
0020: 70 70 2E 70 79 63 20                             |pp.pyc |

Header: dir=0x01 (server), msg_count=3
  [msg 0] ACK (0x01) seq=0
  [msg 1] Connect (0x03) totalLen=6 -- server's ConnectAck
  [msg 2] Reliable (0x32) totalLen=27 flags=0x80 seq=0
          ChecksumReq: round=0 dir="scripts/" filter="App.pyc" recursive=0x20 (false)
```

Note how the server bundles the ConnectAck and first ChecksumReq into a single UDP packet with 3 transport messages.

### Connect Response Wire Format

The server's ConnectAck (type 0x05) carries the client's assigned slot and IP:

```
Wire: [0x05][0x0A][0xC0][status][0x00][slot][ip:4]
  totalLen = 0x0A (10 bytes)
  flags = 0xC0
  status = 0x02 (accept connection)
  slot = 1-based player index
  ip = client IP in network byte order
```

For connection rejection or server shutdown, the same type 0x05 is sent with `status = 0x00`.

### ConnectAck in Multi-Message Packets

The ConnectAck is typically bundled with other messages. A separate ConnectAck also appears as type 0x02 (internal transport control) in the same packet:

```
[0x02][0x03][0x06][0xC0][0x00][0x00][slot]
  type = 0x02 (Transport), totalLen = 3
  Connect: [0x06][0xC0][0x00][0x00][slot]
```

---

## 2. Reliable Delivery

### ACK Behavior

Observed from 88,532 ACK messages across the 34-minute session:

- Every reliable message (flags & 0x80) receives an ACK in return
- ACK carries `seq_hi`/`seq_lo` matching the reliable message being acknowledged
- ACKs are often bundled with outgoing data in the same packet (e.g., packet #2 bundles an ACK with a ConnectAck and a ChecksumReq)

### Sequence Numbering

Reliable messages carry a 2-byte big-endian sequence number (`seq_hi`, `seq_lo`). Observed pattern:
- Server's first reliable: seq=0 (hi=0x00, lo=0x00)
- Second: seq=256 (hi=0x01, lo=0x00)
- Third: seq=512 (hi=0x02, lo=0x00)
- Pattern: `hi` byte increments by 1 per reliable message, `lo` byte stays 0x00

Client and server maintain independent sequence counters.

### Unreliable Delivery

StateUpdate (opcode 0x1C) is the only game message observed using unreliable delivery (flags=0x00 in the 0x32 wrapper). All other game commands use reliable delivery (flags=0x80).

### Reliable Retry

Reliable messages that go unacknowledged are retransmitted:
- **Retransmit interval**: 2000ms (2 seconds)
- **Maximum retries**: 8 attempts
- **Timeout**: After 8 failed retries (~16 seconds), the peer is considered dead and disconnected

### Observed Timeouts

From the trace, clients that stop sending packets are disconnected after approximately 45 seconds (keepalive timeout). Reliable messages that go unacknowledged are retried per the schedule above; after sustained failure, the peer is disconnected.

---

## 3. Connection and Join Flow

The full connection flow from UDP connect through gameplay. For the checksum exchange details, see [checksum-handshake-protocol.md](checksum-handshake-protocol.md).

### Observed Sequence (First Player)

```
Time      Event                                    Packet
--------  ---------------------------------------- ------
T+0.000   Client sends Connect                     #1
T+0.004   Server: ConnectAck + ChecksumReq[0]      #2
          ... 5 checksum rounds (see checksum doc) ...
T+0.098   Server: 0x28 + Settings + GameInit        #17
T+0.124   Client: 0x2A NewPlayerInGame               #19
T+0.130   Server: 0x35 MISSION_INIT + 0x17 cleanup   #20
T+4.592   Client selects ship, sends ObjCreateTeam   #56
T+4.750   Server begins StateUpdate stream            #57+
```

Total connect-to-gameplay: ~5 seconds (most time is the player choosing a ship).

### Post-Checksum Triple (Packet #17)

After all 5 checksum rounds pass, the server sends three messages in one packet:

```
Server -> Client, 69 bytes (packet #17):
0000: 01 04 01 04 00 00 32 06 80 05 00 28 32 33 80 06  |......2....(23..|
0010: 00 00 00 20 BF 41 61 00 25 00 4D 75 6C 74 69 70  |... .Aa.%.Multip|
0020: 6C 61 79 65 72 2E 45 70 69 73 6F 64 65 2E 4D 69  |layer.Episode.Mi|
0030: 73 73 69 6F 6E 31 2E 4D 69 73 73 69 6F 6E 31 32  |ssion1.Mission12|
0040: 06 80 07 00 01                                   |.....|

Header: dir=0x01 (server), msg_count=4
  [msg 0] ACK seq=4
  [msg 1] Reliable seq=1280: opcode 0x28 (checksum-complete signal, no game payload)
  [msg 2] Reliable seq=1536: opcode 0x00 (Settings)
          gameTime=23.89  collision=ON  friendlyFire=OFF  slot=0
          map="Multiplayer.Episode.Mission1.Mission1"  checksumCorrection=OFF
  [msg 3] Reliable seq=1792: opcode 0x01 (GameInit trigger, no payload)
```

The client receives this and transitions to the ship selection screen.

### Client NewPlayerInGame (Packet #19)

```
Client -> Server, 9 bytes:
0000: 02 01 32 07 80 05 00 2A 20                       |..2....* |

  [msg 0] Reliable seq=1280: opcode 0x2A (NewPlayerInGame)
          trailing byte: 0x20 (bit-packed boolean, value=false)
```

### Server Response: MISSION_INIT + DeletePlayerUI (Packet #20)

```
Server -> Client, 39 bytes:
0000: 01 03 01 05 00 00 32 0A 80 08 00 35 08 01 FF FF  |......2....5....|
0010: 32 17 80 09 00 17 66 08 00 00 F1 00 80 00 00 00  |2.....f.........|
0020: 00 00 91 07 00 00 02                             |.......|

  [msg 0] ACK seq=5
  [msg 1] Reliable seq=2048: opcode 0x35 (MISSION_INIT)
          data: [08 01 FF FF] -- playerLimit=8, species=1, timeLimit=255, fragLimit=255
  [msg 2] Reliable seq=2304: opcode 0x17 (DeletePlayerUI)
          17 bytes UI cleanup data
```

### Second Player Join (Packet #311)

When a second player completes checksums and joins, the server sends a richer packet to that player containing the existing game state:

```
Server -> Client (Peer#1), 177 bytes:
0000: 01 05 01 05 00 00 32 0A 80 08 00 35 08 01 FF FF  |......2....5....|
0010: 32 16 80 09 00 37 02 00 00 00 00 00 00 00 00 00  |2....7..........|
0020: 00 00 00 00 00 00 32 74 80 0A 00 03 00 02 08 80  |......2t........|
0030: 00 00 FF FF FF 3F 01 5A 34 94 42 26 2F 09 C2 5B  |.....?.Z4.B&/..[|
0040: F4 3C C2 C2 02 66 3F D9 6D 9F 3E 37 F9 E4 3D B5  |.<...f?.m.>7..=.|
0050: BF 93 3E CA 54 4D F2 FB A3 40 05 43 61 64 79 32  |..>.TM...@.Cady2|
0060: 06 4D 75 6C 74 69 31 FF FF 64 FF FF FF FF FF FF  |.Multi1..d......|
0070: FF 64 FF FF FF FF FF FF 64 FF FF FF FF FF FF FF  |.d......d.......|
0080: FF FF 64 60 01 FF FF FF 64 FF FF FF FF FF FF FF  |..d`....d.......|
0090: 64 00 FF 64 FF FF FF 64 01 FF 32 17 80 0B 00 17  |d..d...d..2.....|
00A0: 66 08 00 00 F1 00 80 00 00 00 00 00 91 07 00 00  |f...............|
00B0: 03                                               |.|

Header: dir=0x01, msg_count=5
  [msg 0] ACK seq=5
  [msg 1] Reliable: 0x35 MISSION_INIT [08 01 FF FF]
  [msg 2] Reliable: 0x37 PlayerRoster -- existing player scores (16 bytes, all zeros)
  [msg 3] Reliable: 0x03 ObjCreateTeam -- existing player's ship
          owner=0 team=2 playerName="Cady2" shipClass="Multi1"
          + serialized object: position, orientation, subsystem health
  [msg 4] Reliable: 0x17 DeletePlayerUI (cleanup)
```

The second player receives the first player's ship object so it can render it immediately.

---

## 4. Game Opcode Table

Complete table of observed game-level opcodes. Frequency data from the "Battle of Valentine's Day" trace (34 minutes, 3 players, active combat).

### C++ Dispatcher Opcodes (0x00 - 0x2A)

| Opcode | Name | Direction | Delivery | Observed Count | Notes |
|--------|------|-----------|----------|----------------|-------|
| 0x00 | Settings | S->C | reliable | at join | Game config (see Section 6.1) |
| 0x01 | GameInit | S->C | reliable | at join | Trigger, no payload |
| 0x02 | ObjectCreate | S->C | reliable | rare | Non-team object creation |
| 0x03 | ObjCreateTeam | S->C | reliable | 42 | Ship creation (see Section 6.2) |
| 0x04 | (dead) | -- | -- | 0 | Never observed on wire |
| 0x05 | (dead) | -- | -- | 0 | Never observed on wire |
| 0x06 | PythonEvent | any | reliable | 3,825 | Primary script event forwarding |
| 0x07 | StartFiring | C->S->all | reliable | 2,918 | Weapon fire begin |
| 0x08 | StopFiring | C->S->all | reliable | 1,448 | Weapon fire end |
| 0x09 | StopFiringAtTarget | any | reliable | -- | Stop firing at specific target |
| 0x0A | SubsysStatus | any | reliable | 64 | Subsystem toggle (shields, etc.) |
| 0x0B | AddToRepairList | any | reliable | 0 | Crew repair assignment |
| 0x0C | ClientEvent | any | reliable | 0 | Generic event forward |
| 0x0D | PythonEvent2 | any | reliable | -- | Alternate Python event path |
| 0x0E | StartCloak | any | reliable | 4 | Cloaking device engage |
| 0x0F | StopCloak | any | reliable | -- | Cloaking device disengage |
| 0x10 | StartWarp | any | reliable | 1 | Warp drive engage |
| 0x11 | RepairListPriority | any | reliable | -- | Repair priority ordering |
| 0x12 | SetPhaserLevel | any | reliable | -- | Phaser power adjustment |
| 0x13 | HostMsg | C->S | reliable | 4 | Host message dispatch (self-destruct) |
| 0x14 | DestroyObject | S->C | reliable | 1 | Object destruction notification |
| 0x15 | CollisionEffect | C->S | reliable | 317 | Collision damage report |
| 0x16 | UICollisionSetting | S->C | reliable | at join | Collision toggle bit |
| 0x17 | DeletePlayerUI | S->C | reliable | at join/leave | Player UI cleanup |
| 0x18 | DeletePlayerAnim | S->C | reliable | -- | Player deletion animation |
| 0x19 | TorpedoFire | owner->all | reliable | 1,090 | Torpedo launch (see Section 6.4) |
| 0x1A | BeamFire | owner->all | reliable | 157 | Beam weapon hit (see Section 6.5) |
| 0x1B | TorpTypeChange | any | reliable | 13 | Torpedo type switch |
| 0x1C | StateUpdate | owner->all | **unreliable** | **199,541** | Position/state sync (see Section 8) |
| 0x1D | ObjNotFound | S->C | reliable | -- | Object lookup failure |
| 0x1E | RequestObject | C->S | reliable | -- | Request missing object data |
| 0x1F | EnterSet | S->C | reliable | 1 | Enter game set |
| 0x28 | ChecksumComplete | S->C | reliable | 3 | Checksum phase done signal |
| 0x29 | Explosion | S->C | reliable | 60 | Explosion effect (see Section 6.6) |
| 0x2A | NewPlayerInGame | C->S/S->C | reliable | 3 | Player join handshake |

**Notes:**
- Opcodes 0x04 and 0x05 are dead -- never observed on wire in any trace
- Opcodes 0x20-0x27 are handled by a separate checksum/file dispatcher (see [checksum-handshake-protocol.md](checksum-handshake-protocol.md))
- Opcode 0x28 is the checksum-complete signal sent after all 5 checksum rounds pass
- StateUpdate (0x1C) accounts for ~97% of all game messages by volume

### Python Script Messages (0x2C - 0x41)

These messages bypass the C++ opcode dispatcher. They are sent via Python `SendTGMessage()` and received by Python `ReceiveMessage` handlers.

**MAX_MESSAGE_TYPES = 0x2B** (43). Python message opcodes start at MAX+1 = 0x2C.

| Opcode | Name | Direction | Observed Count | Notes |
|--------|------|-----------|----------------|-------|
| 0x2C | CHAT_MESSAGE | relayed | 57 | Client->Host->all others |
| 0x2D | TEAM_CHAT_MESSAGE | relayed | -- | Client->Host->teammates |
| 0x35 | MISSION_INIT_MESSAGE | S->C | at join | Player limit, species, time/frag limits |
| 0x36 | SCORE_CHANGE_MESSAGE | S->C | on kill | Score deltas |
| 0x37 | SCORE_MESSAGE | S->C | at join | Full player roster sync |
| 0x38 | END_GAME_MESSAGE | S->C | at end | Game over broadcast |
| 0x39 | RESTART_GAME_MESSAGE | S->C | at restart | Game restart broadcast |
| 0x3F | SCORE_INIT_MESSAGE | S->C | -- | Team modes only |
| 0x40 | TEAM_SCORE_MESSAGE | S->C | -- | Team modes only |
| 0x41 | TEAM_MESSAGE | S->C | -- | Team modes only |

---

## 5. Chat Message Format (Opcode 0x2C)

### Wire Format

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x2C (or 0x2D for team chat)
1       1     u8      sender_slot (player index 0-15)
2       3     bytes   padding (always 0x00 0x00 0x00)
5       2     u16le   text_length
7       var   string  message_text (ASCII, not null-terminated)
```

Delivery: reliable. Chat is relayed by the host: client sends to host, host forwards to all other players (or teammates only for 0x2D).

### Hex Dump: "IT WORKS" Chat (Client -> Server)

```
Client (Peer#2) -> Server, 22 bytes (packet #3029):
0000: 03 01 32 14 80 2B 00 2C 03 00 00 00 08 00 49 54  |..2..+.,......IT|
0010: 20 57 4F 52 4B 53                                | WORKS|

  Reliable seq=11008: opcode 0x2C (ChatMessage)
  senderSlot=3  padding=00 00 00  textLen=8  text="IT WORKS"
```

### Hex Dump: Server Relays Chat to Another Player

```
Server -> Client (Peer#1), 28 bytes (packet #8093):
0000: 01 01 32 1A 80 8A 00 2C 04 00 00 00 0E 00 74 6F  |..2....,......to|
0010: 72 70 73 20 61 72 65 20 73 6C 6F 77              |rps are slow|

  Reliable seq=35328: opcode 0x2C (ChatMessage)
  senderSlot=4  padding=00 00 00  textLen=14  text="torps are slow"
```

---

## 6. Key Packet Formats

### 6.1 Settings (Opcode 0x00)

Sent by the server after all 5 checksum rounds pass. Contains game configuration for the joining player.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x00
gameTime          f32 LE     4      Current game clock (seconds)
configFlags       bitpacked  1      3 booleans packed (see Bit Packing, Section 7)
                                    bit0: collision damage (1=on)
                                    bit1: friendly fire (1=on)
                                    bit2: checksum correction needed (1=yes)
playerSlot        u8         1      Assigned player index (0-15)
mapNameLen        u16 LE     2      Length of map name string
mapName           string     N      Mission script path (not null-terminated)
[if correction:]  data       var    Checksum correction data (only if bit2=1)
```

**Verified from packet #17**: gameTime=23.89 (bytes: `00 20 BF 41` LE = 0x41BF2000), configFlags=0x61 (collision=ON, ff=OFF, correction=OFF), slot=0, map="Multiplayer.Episode.Mission1.Mission1" (37 chars).

**Second player (packet #298)**: same format, gameTime=38.81 (bytes: `00 40 1B 42`), slot=1.

### 6.2 ObjCreateTeam (Opcode 0x03)

Ship creation message. Sent by the client when selecting a ship, relayed by the server to other players.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x03
ownerSlot         u8         1      Player index (0-15) who owns this ship
teamId            u8         1      Team assignment
serializedObject  data       var    Full ship state (108 bytes observed)
```

The serialized object blob contains (decoded from hex dumps of packets #56 and #311):

```
Field             Type       Size   Notes
-----             ----       ----   -----
headerByte1       u8         1      Observed: 0x08 (purpose uncertain)
headerByte2       u8         1      Observed: 0x80 (purpose uncertain, possibly flags)
padding           u8[2]      2      Observed: 0x00 0x00
objectId          i32 LE     4      Ship's network object ID (e.g., 0x3FFFFFFF)
speciesId         u8         1      Ship species/type (observed: 0x01)
posX              f32 LE     4      Position X
posY              f32 LE     4      Position Y
posZ              f32 LE     4      Position Z
orientW           f32 LE     4      Orientation quaternion W
orientX           f32 LE     4      Orientation quaternion X
orientY           f32 LE     4      Orientation quaternion Y
orientZ           f32 LE     4      Orientation quaternion Z
speed             f32 LE     4      Current speed (0.0 at spawn)
padding2          u8[2]      2      Observed: 0x00 0x00
nameLen           u8         1      Player name length
name              bytes      N      Player name (ASCII, not null-terminated)
classLen          u8         1      Ship class name length
className         bytes      M      Ship class name (e.g., "Multi1")
subsystems        bytes      var    Subsystem health array (see below)
```

The subsystem health data uses a compact encoding. Each subsystem has one byte of health (0xFF = 100%, 0x00 = destroyed). Some subsystems use 0x64 (100 decimal) as a delimiter or sentinel value. The exact grouping pattern is consistent across all observed ship creation messages but its internal structure (which byte maps to which subsystem) requires further analysis.

### Hex Dump: Ship Creation (Packet #56, first player selects ship)

```
Client -> Server (embedded in 208-byte packet):
0000: 02 0E 32 74 80 06 00 03 00 02 08 80 00 00 FF FF  |..2t............|
0010: FF 3F 01 00 00 B0 42 00 00 84 C2 00 00 92 C2 F5  |.?....B.........|
0020: 4A 6F 3F FE 8C 96 3E 84 E3 4B 3E 38 78 4E 3C 00  |Jo?...>..K>8xN<.|
0030: 00 00 00 00 00 00 05 43 61 64 79 32 06 4D 75 6C  |.......Cady2.Mul|
0040: 74 69 31 FF FF 64 FF FF FF FF FF FF FF 64 FF FF  |ti1..d.......d..|
0050: FF FF FF FF 64 FF FF FF FF FF FF FF FF FF 64 60  |....d.........d`|
0060: 01 FF FF FF 64 FF FF FF FF FF FF FF 64 00 FF 64  |....d.......d..d|
0070: FF FF FF 64 01 FF                                |...d..|

  Reliable seq=1536 len=116: opcode 0x03 (ObjCreateTeam)
  owner=0  team=2  objId=0x3FFFFFFF
  pos=(88.0, -66.0, -73.0)  playerName="Cady2"  shipClass="Multi1"
  subsystems: mostly 0xFF (full health)
```

### 6.3 MISSION_INIT (Opcode 0x35)

Sent to each player after they send NewPlayerInGame (0x2A).

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x35
playerLimit       u8         1      Max players (observed: 8)
systemIndex       u8         1      Star system index
timeLimit         u8         1      Time limit (0xFF = no limit)
[if timeLimit != 0xFF:]
  endTime         i32 LE     4      Absolute game clock time when match ends
fragLimit         u8         1      Frag limit (0xFF = no limit)
```

**Conditional field**: The `endTime` field is only present when `timeLimit != 0xFF`. When time limit is disabled (0xFF), the field is omitted entirely, and `fragLimit` immediately follows `timeLimit`.

**Verified from packet #20**: `35 08 01 FF FF` -- playerLimit=8, systemIndex=1, timeLimit=255 (none, no endTime field), fragLimit=255 (none).

### 6.4 TorpedoFire (Opcode 0x19)

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x19
objectId          i32        4      Torpedo subsystem network ID
subsysIndex       u8         1      Subsystem index / type tag
flags             u8         1      bit 0: has_arc, bit 1: has_target
velocity          cv3        3      Torpedo direction (CompressedVector3)
[if has_target (bit 1):]
  targetId        i32        4      Target object ID
  impactPoint     cv4        5      Impact position (CompressedVector4)
```

### Hex Dump: TorpedoFire (Packet #844)

```
Client -> Server, 25 bytes:
0000: 02 01 32 17 80 0D 00 19 0D 00 00 40 02 01 DF 87  |..2........@....|
0010: 11 FF FF 03 40 00 88 D8 5C                       |....@...\|

  Reliable seq=3328: opcode 0x19 (TorpedoFire)
  obj=0x4000000D  flags=0x02,0x01  vel=(-0.26,-0.95,0.13) + arc data
```

### 6.5 BeamFire (Opcode 0x1A)

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x1A
objectId          i32        4      Phaser subsystem network ID
flags             u8         1      Beam properties
targetDir         cv3        3      Target direction (CompressedVector3)
moreFlags         u8         1      bit 0: has_target_id
[if has_target_id:]
  targetObjectId  i32        4      Target object ID
```

### Hex Dump: BeamFire (Packet #31026, client fires; #31027, server relays)

```
Client -> Server (2 beams in one packet):
0000: 02 03 32 13 80 DB 00 1A 77 00 00 40 02 75 0E D2  |..2.....w..@.u..|
0010: 03 68 00 08 40 32 13 80 DC 00 1A 78 00 00 40 02  |.h..@2.....x..@.|
0020: 75 09 D2 03 68 00 08 40 ...                      |u...h..@...|

  [msg 0] Reliable: 0x1A BeamFire obj=0x40000077 targetDir=(0.92,0.11,-0.36) target=0x40080068
  [msg 1] Reliable: 0x1A BeamFire obj=0x40000078 targetDir=(0.92,0.07,-0.36) target=0x40080068

Server relays both beams to other clients in separate packet (same data, different seq numbers).
```

### 6.6 Explosion (Opcode 0x29)

Server-only message. Sent to all clients when an explosion occurs.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x29
objectId          i32        4      Source object ID
impactPos         cv4        5      Impact position (CompressedVector4: 3 dir + u16 mag)
damage            cf16       2      Damage amount (CompressedFloat16)
radius            cf16       2      Explosion radius (CompressedFloat16)
```

Total: 14 bytes per explosion message.

### Hex Dump: Explosion (Packets #2829-2830)

```
Server -> Client (Peer#2), 21 bytes each:

Packet #2829:
0000: 01 01 32 13 80 0F 00 29 FF FF FF 3F 1D 7A 0C 95  |..2....)...?.z..|
0010: 61 1B 57 E2 78                                   |a.W.x|
  obj=0x3FFFFFFF  impact=(43.2,181.6,17.9)  dmg=50.0  radius=5997.8

Packet #2830:
0000: 01 01 32 13 80 10 00 29 FF FF FF 3F E6 7C 06 F0  |..2....)...?.|..|
0010: 61 03 50 E2 78                                   |a.P.x|
  obj=0x3FFFFFFF  impact=(-42.8,204.1,9.9)  dmg=10.1  radius=5997.8
```

Note: Multiple explosions sent in rapid succession (same timestamp) for a single collision event.

### 6.7 CollisionEffect (Opcode 0x15)

Client reports collision to server. Observed 317 times in the combat session.

```
Client -> Server, 31 bytes total (25-byte payload):
0000: 03 01 32 1F 80 07 00 15 24 81 00 00 50 00 80 00  |..2.....$...P...|
0010: 00 00 00 00 FF FF 03 40 01 27 77 11 B8 9D 47 25  |.......@.'w...G%|
0020: 44                                               |D|

  Reliable: opcode 0x15 (CollisionEffect)
  25 bytes of collision parameters (object IDs, position, force)
```

### 6.8 Score Message (Opcode 0x37)

Per-player score synchronization. The server sends **one 0x37 message per existing player** when a new player joins, giving them the full roster state. Not batched -- each message carries exactly one player's score data.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x37
playerId          i32 LE     4      Player's object ID (from GetObjID())
kills             i32 LE     4      Kill count
deaths            i32 LE     4      Death count
score             i32 LE     4      Current score (signed, can be negative)
```

Total: 17 bytes per message. Source: readable Python scripts shipped with the game (Mission1.py `SendScoreMessage`).

**Verified from packet #311** (second player joining): The server sends one 0x37 message with 17 bytes for the existing player (host). All four i32 values are zero (fresh game, no kills/deaths/score yet).

### 6.9 ScoreChange Message (Opcode 0x36)

Sent when a kill occurs. Contains updated kill/death/score counts for the killer and victim, plus optional score updates for other players who contributed damage.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x36
killerId          i32 LE     4      Killer's object ID (0 for environmental kills)
[if killerId != 0:]
  killerKills     i32 LE     4      Killer's updated kill count
  killerScore     i32 LE     4      Killer's updated score
victimId          i32 LE     4      Victim's object ID
victimDeaths      i32 LE     4      Victim's updated death count
updateCount       u8         1      Number of extra score updates
[repeated updateCount times:]
  playerId        i32 LE     4      Player's object ID
  score           i32 LE     4      Player's updated score
```

Variable length. When `killerId == 0` (environmental death, e.g., collision or self-destruct), the `killerKills` and `killerScore` fields are omitted. The `updateCount` list carries damage-share score updates for players who contributed damage to the victim but didn't get the final kill.

Source: readable Python scripts shipped with the game (MissionShared.py `SendScoreChangeMessage`).

### 6.10 EndGame Message (Opcode 0x38)

Broadcast to all clients when the match ends.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x38
reason            i32 LE     4      End reason code (see below)
```

Total: 5 bytes.

| Reason | Code | Description |
|--------|------|-------------|
| OVER | 0 | Generic game over |
| TIME_UP | 1 | Time limit reached |
| FRAG_LIMIT | 2 | Frag/kill limit reached |
| SCORE_LIMIT | 3 | Score limit reached |
| STARBASE_DEAD | 4 | Starbase destroyed (objective mode) |
| BORG_DEAD | 5 | Borg destroyed (objective mode) |
| ENTERPRISE_DEAD | 6 | Enterprise destroyed (objective mode) |

Source: readable Python scripts shipped with the game (MissionShared.py `ReceiveEndGameMessage`).

### 6.11 RestartGame Message (Opcode 0x39)

Broadcast to all clients to restart the match. No payload.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x39
```

Total: 1 byte. Source: readable Python scripts shipped with the game (MissionShared.py `SendRestartGameMessage`).

### 6.12 DeletePlayerAnim (Opcode 0x18)

Triggers a "Player X has left" floating notification on the client.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x18
nameLen           u16 LE     2      Player name string length
name              bytes      N      Player name (ASCII, not null-terminated)
```

### 6.13 BootPlayer (Opcode 0x04)

Kicks a player from the game. Sent by the host to the target player.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x04
reason            u8         1      Reason code (see below)
```

Reason codes:
| Code | Meaning |
|------|---------|
| 0 | Generic kick |
| 1 | Version mismatch |
| 2 | Server full |
| 3 | Banned |
| 4 | Checksum validation failed |

**Note**: On the wire, BootPlayer uses the game-layer opcode 0x04. The transport-layer type 0x05 (ConnectAck) with status=0x00 serves as a separate shutdown notification mechanism (see Section 1).

### 6.14 Keepalive Wire Format

The server echoes the client's identity data as a keepalive message (transport type 0x00). The client's keepalive payload contains:

```
Field             Type       Size   Notes
-----             ----       ----   -----
padding           u8         1      0x00
totalLen          u8         1      Total keepalive body length
flags             u8         1      0x80
padding           u8[2]      2      0x00 0x00
slot              u8         1      Player slot (1-based)
ip                u8[4]      4      Client IP in network byte order
name              u16le[]    var    Player name in UTF-16LE
```

The server caches the client's keepalive payload and echoes it back approximately once per second. Timeout: ~30 seconds without any packets triggers disconnect.

---

## 7. Compressed Data Types

### Bit-Packed Booleans

Multiple boolean values are packed into a single byte using a count+bits scheme:

```
Byte layout:  [count:3][padding:variable][bits:count]
              MSB                                 LSB

Single boolean (count=1):
  0x20 = 0b001_00000 = false
  0x21 = 0b001_00001 = true

Three booleans (count=3):
  0x61 = 0b011_00001 = bit0=1, bit1=0, bit2=0
```

The upper 3 bits encode how many booleans are packed (1-5). The lowest N bits hold the boolean values. This encoding is used for the recursive flag in checksum requests, the config flags in Settings, and the subsystem hash flag in StateUpdate.

**Critical**: Writing a plain `0x00`/`0x01` byte instead of bit-packed `0x20`/`0x21` will desynchronize the stream parser, causing all subsequent field reads to be misaligned.

### CompressedFloat16

16-bit logarithmic float encoding. Used for speed, damage, radius values.

```
Format: [sign:1][scale:3][mantissa:12]
        Bit 15 = sign (1=negative)
        Bits 14-12 = scale exponent (0-7)
        Bits 11-0 = mantissa (0-4095)
```

**Precision note**: The encode path divides the mantissa by 4096.0f while the decode path divides by 4095.0f. This intentional asymmetry means a round-trip encode→decode is not perfectly lossless, but the error is less than 0.025%.

Observed decoded values:
- Speed 5.130, 7.618, 7.598 (movement speeds)
- Damage 50.0, 10.1 (explosion damage)
- Radius 5997.8 (explosion radius)

### CompressedVector3 (cv3)

Used for direction vectors (velocity direction, orientation forward/up).

```
Wire format: [dirX:u8][dirY:u8][dirZ:u8] = 3 bytes
Each byte is a signed direction component: ftol(component * 127.0)
Range: -1.0 to +1.0 per component
```

Observed example from TorpedoFire: bytes `DF 87 11` = direction (-0.26, -0.95, 0.13).

### CompressedVector4 (cv4)

Used for position deltas and impact positions. A cv3 direction plus a magnitude.

```
Wire format: [dirX:u8][dirY:u8][dirZ:u8][magnitude:u16] = 5 bytes
Direction: same as cv3 (signed bytes / 127.0)
Magnitude: CompressedFloat16 encoding
```

Observed in Explosion impact positions and StateUpdate position deltas.

---

## 8. StateUpdate Deep Dive (Opcode 0x1C)

The most frequent message type (199,541 occurrences, ~97% of all game messages). Sent per-ship at approximately 10Hz using **unreliable** delivery.

### Wire Format

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x1C
objectId          i32        4      Ship's network object ID
gameTime          f32 LE     4      Current game clock
dirtyFlags        u8         1      Bitmask of which fields follow
[conditional fields based on flags]
```

### Dirty Flag Table

| Bit | Mask | Name | Data Format | Size | Notes |
|-----|------|------|-------------|------|-------|
| 0 | 0x01 | POSITION | 3x f32 LE + bitpacked(hasHash) + [u16 hash] | 12-14 | Absolute position |
| 1 | 0x02 | DELTA | cv4 (3 dir bytes + u16 magnitude) | 5 | Position delta |
| 2 | 0x04 | FORWARD | cv3 (3 signed bytes) | 3 | Forward orientation |
| 3 | 0x08 | UP | cv3 (3 signed bytes) | 3 | Up orientation |
| 4 | 0x10 | SPEED | cf16 (CompressedFloat16) | 2 | Current speed |
| 5 | 0x20 | SUBSYSTEMS | startIdx + round-robin health data | variable | Subsystem health |
| 6 | 0x40 | CLOAK | bitpacked boolean (NOT raw byte) | 1 | Cloaking state |
| 7 | 0x80 | WEAPONS | round-robin weapon health data | variable | Weapon health |

### Direction-Based Split

Verified across 199,541 StateUpdate messages:

- **Ship owner -> server**: flags always include 0x80 (WEAPONS), never include 0x20 (SUBSYSTEMS). Common patterns: 0x9D (pos+fwd+up+speed+wpn), 0x96 (delta+fwd+speed+wpn), 0x92 (delta+speed+wpn), 0x8C (fwd+up+wpn), 0xDA (delta+up+speed+cloak+wpn)
- **Server -> all clients**: flags always include 0x20 (SUBSYSTEMS), never include 0x80 (WEAPONS). Common pattern: 0x20 (subsystems only), 0x2C (fwd+up+subsystems)

This split reflects the authority model: ship owners are authoritative for position, orientation, speed, and weapon state. The server is authoritative for subsystem health (damage is computed server-side).

### Subsystem Hash (Flag 0x01)

When POSITION (0x01) is set, the position data includes a bit-packed boolean indicating whether a subsystem hash follows:

```
Position data: [x:f32][y:f32][z:f32][bitpacked(hasHash)]
If hasHash=true: [subsysHash:u16]
```

The hash is an anti-cheat integrity check. Observed hash value: 0xFB37 (consistent across all position updates for the same ship in a session).

### Cloak State (Flag 0x40)

The cloak field is encoded as a **bit-packed boolean** (0x20=OFF, 0x21=ON), NOT a raw byte. This matches the bit-packing scheme used elsewhere in the protocol.

### Hex Dump: Full Client StateUpdate (flags=0x9D)

```
Client -> Server (embedded in packet #56):
  [0x1C StateUpdate]
  obj=0x3FFFFFFF  gameTime=28.19  flags=0x9D [POS FWD UP SPD WPN]

  Raw payload bytes:
  1C FF FF FF 3F 00 80 E1 41 9D 00 00 B0 42 00 00
  84 C2 00 00 92 C2 21 37 FB 0B 68 46 30 BB 5E 00
  00 01 CC 02 CC 04 CC

  Decoded:
    Position: (88.0, -66.0, -73.0)
    SubsysHash: 0xFB37 (present, bitpacked=true=0x21)
    Forward: (0.09, 0.82, 0.55)
    Up: (0.38, -0.54, 0.74)
    Speed: 0.000
    Weapons: [subsys1:100%, subsys2:100%, subsys4:100%]
```

### Hex Dump: Server Subsystem StateUpdate (flags=0x20)

```
Server -> Client (packet #300), 30 bytes:
0000: 01 01 32 1C 00 1C FF FF FF 3F 00 A0 1B 42 20 08  |..2......?...B .|
0010: FF 60 FF FF FF FF FF FF FF FF FF FF FF FF        |.`............|

  Unreliable: [0x1C StateUpdate]
  obj=0x3FFFFFFF  gameTime=38.91  flags=0x20 [SUB]
    Subsystems: startIdx=8, round-robin cycle of 6 subsystems
    Health bytes: FF=100%, 60=~38%, rest=100%
```

Note: Subsystem health data uses round-robin encoding. Each StateUpdate carries health for a sliding window of ~6 subsystems, cycling through all subsystems over multiple updates.

---

## 9. GameSpy LAN Discovery

GameSpy and game traffic share the same UDP socket. The server distinguishes them by peeking at the first byte:

- `\` (0x5C) prefix = GameSpy query (plaintext, NOT encrypted)
- Binary (non-`\`) = game packet (AlbyRules encrypted)

### Query / Response Format

Standard GameSpy QR (Query & Reporting) protocol. Backslash-delimited key-value pairs.

**Client query** (broadcast):
```
\status\                                    (8 bytes)
```

The client broadcasts `\status\` to `255.255.255.255` on UDP ports 22101 through 22201 (101 ports total). Any server listening on one of those ports responds.

**Server response** (observed, 267 bytes, single unfragmented packet):
```
\gamename\bcommander\gamever\60\location\1\hostname\My Game23\missionscript\
Multiplayer.Episode.Mission1.Mission1\mapname\DM\numplayers\0\maxplayers\8\
gamemode\openplaying\timelimit\-1\fraglimit\-2\system\Multi1\password\0\
player_0\Dedicated Server\final\\queryid\2.1
```

**Verified field order**: gamename, gamever, location, hostname, missionscript, mapname, numplayers, maxplayers, gamemode, timelimit, fraglimit, system, password, player_N (for each connected player), final, queryid.

Key values:
- **gamename**: always `"bcommander"` (hardcoded)
- **gamever**: always `"60"` (hardcoded)
- **gamemode**: `"openplaying"` (in-game) or `"settings"` (lobby)
- **hostname**: prefixed with `*` if password-protected (e.g., `\hostname\*My Server`)
- **`\final\`** is followed by `\` (double backslash), then `queryid`

Default game port: 22101 (0x5655).

### Master Server Heartbeat

Observed on the wire:
```
Server -> Master (81.205.81.173:27900), 47 bytes:
0000: 5C 68 65 61 72 74 62 65 61 74 5C 30 5C 67 61 6D  |\heartbeat\0\gam|
0010: 65 6E 61 6D 65 5C 62 63 6F 6D 6D 61 6E 64 65 72  |ename\bcommander|
0020: 5C 73 74 61 74 65 63 68 61 6E 67 65 64 5C 31     |\statechanged\1|

Text: \heartbeat\0\gamename\bcommander\statechanged\1
```

The original master server (`stbridgecmnd01.activision.com`) has been dead since ~2012. Community replacement servers (333networks) accept the same heartbeat protocol.

---

## 10. Network Object IDs

### Observed Allocation Pattern

Each player slot gets a contiguous block of 262,143 object IDs:

```
Player 0 base: 0x3FFFFFFF  (first ship: 0x3FFFFFFF)
Player 1 base: 0x4003FFFF  (first ship: 0x4003FFFF)
Player 2 base: 0x4007FFFF  (first ship: 0x4007FFFF)
Player 3 base: 0x400BFFFF
```

**Formula**: `Player_N_base = 0x3FFFFFFF + N * 0x40000`

To extract the player slot from an object ID: `(objectId - 0x3FFFFFFF) >> 18`

Each player can create up to 262,143 (0x40000) objects (ships, torpedoes, subsystems).

Verified from the battle trace:
- Player 0 ship: 0x3FFFFFFF (host)
- Player 1 ship: 0x4003FFFF (first client)
- Player 2 ship: 0x4007FFFF (second client)
- Torpedo subsystems: sequential IDs starting from player base (e.g., 0x4000000D)
- Phaser subsystems: e.g., 0x40000077, 0x40000078

---

## Summary

### Protocol Stack

```
Application:  Game opcodes (0x00-0x2A) + Python messages (0x2C-0x41)
Transport:    Reliable (0x32 flags=0x80) / Unreliable (0x32 flags=0x00)
Framing:      [direction:1][msg_count:1][transport_messages...]
Encryption:   AlbyRules PRNG cipher (see transport-cipher.md)
Network:      UDP (single shared socket for game + GameSpy)
```

### Quick Reference

| Property | Value |
|----------|-------|
| Cipher key | `"AlbyRules!"` (10 bytes, PRNG stream cipher) |
| Default port | 22101 (UDP) |
| Max players | 16 (observed limit 8 in config) |
| Checksum rounds | 5 (indices 0, 1, 2, 3, 0xFF) |
| Post-checksum | 0x28 + Settings (0x00) + GameInit (0x01) |
| Most frequent message | StateUpdate (0x1C), ~97% of traffic |
| StateUpdate delivery | Unreliable (fire-and-forget) |
| StateUpdate rate | ~10 Hz per ship |
| Game commands delivery | Reliable (ACK required) |
| GameSpy discrimination | First byte `\` = plaintext GameSpy |
| GameSpy game name | `"bcommander"` |
| Object ID formula | `0x3FFFFFFF + playerSlot * 0x40000` |

### Message Frequency (34-minute combat session, 3 players)

| Message | Count | % of Total |
|---------|-------|------------|
| StateUpdate (0x1C) | 199,541 | ~97% |
| PythonEvent (0x06) | 3,825 | ~1.9% |
| StartFiring (0x07) | 2,918 | |
| StopFiring (0x08) | 1,448 | |
| TorpedoFire (0x19) | 1,090 | |
| CollisionEffect (0x15) | 317 | |
| BeamFire (0x1A) | 157 | |
| Keepalive | 94 | |
| SubsysStatus (0x0A) | 64 | |
| Explosion (0x29) | 60 | |
| ChatMessage (0x2C) | 57 | |
| ObjCreate/Team | 42 | |
| TorpTypeChange (0x1B) | 13 | |
| StartCloak (0x0E) | 4 | |
| HostMsg (0x13) | 4 | |

---

## Related Documents

- **[transport-cipher.md](transport-cipher.md)** -- Complete AlbyRules cipher algorithm (key schedule, PRNG, plaintext feedback)
- **[gamespy-protocol.md](gamespy-protocol.md)** -- GameSpy LAN discovery, master server registration, challenge-response crypto
- **[join-flow.md](join-flow.md)** -- Connection lifecycle state machine (connect through gameplay)
- **[combat-system.md](combat-system.md)** -- Damage pipeline, shields, cloaking, tractor beams, repair system
- **[ship-subsystems.md](ship-subsystems.md)** -- Fixed subsystem index table, HP values, StateUpdate serialization
- **[checksum-handshake-protocol.md](checksum-handshake-protocol.md)** -- Checksum exchange details and hash algorithms
- **[disconnect-flow.md](disconnect-flow.md)** -- Player disconnect detection and cleanup
