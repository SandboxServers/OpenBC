# OpenBC Phase 1: Verified Wire Protocol Reference

## Document Status
- **Created**: 2026-02-15
- **Source**: STBC-Dedicated-Server reverse engineering (Ghidra decompilation + 90MB packet trace validation)
- **Confidence**: HIGH -- all values verified against stock dedicated server with 30,000+ packet captures
- **Cross-reference**: [phase1-re-gaps.md](phase1-re-gaps.md) for gap analysis, `../STBC-Dedicated-Server/docs/wire-format-spec.md` for full format

---

## 1. Transport Layer

### AlbyRules Stream Cipher

All game traffic (NOT GameSpy) is encrypted with a simple XOR stream cipher using the key "AlbyRules!" (10 bytes). Applied after transport framing, removed before parsing.

```
Key: "AlbyRules!" = { 0x41, 0x6C, 0x62, 0x79, 0x52, 0x75, 0x6C, 0x65, 0x73, 0x21 }

Encrypt/Decrypt (symmetric):
  for i in 0..packet_len:
      packet[i] ^= key[i % 10]
```

GameSpy packets (first byte `\` after decrypt) are sent in plaintext and are NOT encrypted.

### Raw UDP Packet Format

After AlbyRules decryption:

```
Offset  Size  Field
------  ----  -----
0       1     direction     (0x01=from server, 0x02=from client, 0xFF=init handshake)
1       1     msg_count     (number of transport messages in this packet)
2+      var   messages      (sequence of self-describing transport messages)
```

Max 254 messages per packet. Default max packet size: 512 bytes (configurable at WSN+0x2B).

### Transport Message Types

Each transport message within the packet is self-describing:

| Type | Name | Format | Size |
|------|------|--------|------|
| 0x01 | ACK | `[0x01][seq:1][0x00][flags:1]` | 4 bytes fixed |
| 0x32 | Reliable Data | `[0x32][totalLen:1][flags:1][seq_hi:1][seq_lo:1][payload...]` | variable |
| 0x00, 0x03-0x06 | Other | `[type:1][totalLen:1][data...]` | variable |

For type 0x32 (reliable), `totalLen` includes the 0x32 byte itself.

### Reliable Data Flags (type 0x32)

```
Byte: [7][6][5][4][3][2][1][0]
       |        |           |
       |        |           +-- 0x01: More fragments follow (0 = last/only fragment)
       |        +-------------- 0x20: Fragmented message
       +----------------------- 0x80: Reliable delivery (always set for 0x32)
```

Fragment layout for large messages:
```
First fragment:  [frag_idx:u8][total_frags:u8][inner_opcode:u8][payload...]
Subsequent:      [frag_idx:u8][continuation_data...]
```

---

## 2. TGMessage Object Layout

The TGNetwork layer wraps all messages in a TGMessage object:

| Offset | Size | Type | Field |
|--------|------|------|-------|
| +0x00 | 4 | vtable* | vtable pointer |
| +0x04 | 4 | void* | pointer to payload data buffer |
| +0x08 | 4 | int | payload data length |
| +0x0C | 4 | int | sender peer ID |
| +0x28 | 4 | void* | pointer to raw buffer |
| +0x3A | 1 | u8 | reliable flag (0x01 = reliable delivery) |
| +0x3B | 1 | u8 | guaranteed flag (set via SetGuaranteed) |
| +0x3D | 1 | u8 | priority flag |

Key vtable methods:
- `vtable+0x08`: Serialize (for send)
- `vtable+0x18`: Clone (deep copy for relay)
- `vtable+0x04(1)`: Release (reference counting)

---

## 3. Reliable Delivery System

### Three-Tier Send Queues (per peer)

| Queue | Peer Offset | Purpose | Delivery |
|-------|-------------|---------|----------|
| Unreliable | +0x64 | StateUpdate, position data | Fire-and-forget |
| Reliable | +0x80 | Game commands, checksums | Guaranteed, ACK required |
| Priority | +0x9C | ACKs, retried reliable messages | Highest priority, max 8 retries |

### Sequence Numbering

- **u16 wrapping** (0x0000 - 0xFFFF)
- Separate counters for reliable (`peer+0x26` send, `peer+0x28` recv) and unreliable (`peer+0x24`)
- Sliding window: 0x4000 range for out-of-window rejection
- "Small" messages (type < 0x32) vs "large" (type >= 0x32) tracked separately

### ACK Flow

1. Sender puts reliable message in reliable queue with sequence number
2. Message sent in next SendOutgoingPackets cycle
3. If not ACKed within timeout, promoted to priority queue
4. Priority queue retries up to 8 times
5. After 8 retries: message dropped + peer disconnected

### Timeouts

| Parameter | Value | WSN Offset | Description |
|-----------|-------|------------|-------------|
| Reliable timeout | 360.0s | +0x2D | Max time waiting for ACK |
| Disconnect timeout | 45.0s | +0x2E | No-packet disconnect threshold |
| Priority retries | 8 | (hardcoded) | Max retries before disconnect |

---

## 4. Connection Handshake (Complete Flow)

```
Time  Client                          Server
----  ------                          ------
T+0   UDP packet (connection req) -->
                                      Create peer entry (FUN_006b7410)
                                      Assign peer ID
                                      Fire ET_NETWORK_NEW_PLAYER
                                      NewPlayerHandler (FUN_006a0a30):
                                        Assign player slot (0-15)
                                        Start checksum exchange
T+0.1                             <-- Checksum request #0 (opcode 0x20)
T+0.2 Checksum response #0 -->
                                      Verify hash, send request #1
T+0.3                             <-- Checksum request #1
T+0.4 Checksum response #1 -->
                                      Verify, send #2
T+0.5                             <-- Checksum request #2
T+0.7 Checksum response #2 -->
                                      Verify, send #3
T+0.8                             <-- Checksum request #3
T+0.9 Checksum response #3 -->
                                      All pass: ET_CHECKSUM_COMPLETE
T+1.0                             <-- Settings (opcode 0x00) [reliable]
                                  <-- GameInit (opcode 0x01) [reliable]
      Client reaches ship select
T+1.4                             <-- NewPlayerInGame (opcode 0x2A)
                                      Python: Mission1.InitNetwork(peerID)
                                  <-- MISSION_INIT_MESSAGE (0x35) [reliable]
T+5.0 Client selects ship
      ObjectCreateTeam (0x03) -->
                                      DeferredInitObject loads NIF model
                                      33 subsystems created
T+8.0                                 StateUpdate (0x1C) with flags=0x20
                                      Collision/subsystem damage working
```

### Checksum Requests (4 rounds)

| # | Directory | Filter | Recursive |
|---|-----------|--------|-----------|
| 0 | scripts/ | App.pyc | No |
| 1 | scripts/ | Autoexec.pyc | No |
| 2 | scripts/ships/ | *.pyc | Yes |
| 3 | scripts/mainmenu/ | *.pyc | No |

**Exempt**: `scripts/Custom/` directory and `scripts/Local.py` are NOT checksummed.

---

## 5. Verified Game Opcode Table

Dispatched by MultiplayerGame ReceiveMessageHandler at `0x0069f2a0`. Jump table at `0x0069F534` (41 entries, index = opcode - 2).

### Game Opcodes (0x00 - 0x2A)

| Opcode | Name | Handler | Direction | Payload Summary | Stock Count/15min |
|--------|------|---------|-----------|-----------------|-------------------|
| 0x00 | Settings | FUN_00504d30 | S->C | gameTime, settings, playerSlot, mapName | at join |
| 0x01 | GameInit | FUN_00504f10 | S->C | (empty -- just opcode byte) | at join |
| 0x02 | ObjectCreate | FUN_0069f620 | S->C | type=2, ownerSlot, serializedObject | rare |
| 0x03 | ObjectCreateTeam | FUN_0069f620 | S->C | type=3, ownerSlot, teamId, serializedObject | 11 |
| 0x04 | BootPlayer | (inline) | S->C | reason code (2=full, 3=in progress, 4=kicked) | rare |
| 0x06 | PythonEvent | FUN_0069f880 | any | eventCode(u32), eventPayload | **3432** |
| 0x07 | StartFiring | FUN_0069fda0 | any | objectId, event data (-> 0x008000D7) | **2282** |
| 0x08 | StopFiring | FUN_0069fda0 | any | objectId, event data (-> 0x008000D9) | common |
| 0x09 | StopFiringAtTarget | FUN_0069fda0 | any | objectId, event data (-> 0x008000DB) | common |
| 0x0A | SubsysStatus | FUN_0069fda0 | any | objectId, event data (-> 0x0080006C) | common |
| 0x0B | AddToRepairList | FUN_0069fda0 | any | objectId, event data (-> 0x008000DF) | occasional |
| 0x0C | ClientEvent | FUN_0069fda0 | any | objectId, event from stream (preserve=0) | occasional |
| 0x0D | PythonEvent2 | FUN_0069f880 | any | eventCode(u32), eventPayload (same as 0x06) | alternate |
| 0x0E | StartCloaking | FUN_0069fda0 | any | objectId, event data (-> 0x008000E3) | occasional |
| 0x0F | StopCloaking | FUN_0069fda0 | any | objectId, event data (-> 0x008000E5) | occasional |
| 0x10 | StartWarp | FUN_0069fda0 | any | objectId, event data (-> 0x008000ED) | occasional |
| 0x11 | RepairListPriority | FUN_0069fda0 | any | objectId, event data (-> 0x008000E1) | occasional |
| 0x12 | SetPhaserLevel | FUN_0069fda0 | any | objectId, event data (-> 0x008000E0) | 33 |
| 0x13 | HostMsg | FUN_006a0d90 | C->S | host-specific dispatch (self-destruct) | rare |
| 0x14 | DestroyObject | FUN_006a01e0 | S->C | objectId(i32) | on death |
| 0x15 | CollisionEffect | FUN_006a2470 | C->S | objectId, collision params | 84 |
| 0x16 | UICollisionSetting | FUN_00504c70 | S->C | collisionDamageFlag(bit) | at join |
| 0x17 | DeletePlayerUI | FUN_006a1360 | S->C | player UI cleanup | on disconnect |
| 0x18 | DeletePlayerAnim | FUN_006a1420 | S->C | player deletion animation | on disconnect |
| 0x19 | TorpedoFire | FUN_0069f930 | owner->all | objId, flags, velocity(cv3), [target, impact] | **897** |
| 0x1A | BeamFire | FUN_0069fbb0 | owner->all | objId, flags, targetDir(cv3), [targetId] | common |
| 0x1B | TorpTypeChange | FUN_0069fda0 | any | objectId, event data (-> 0x008000FD) | occasional |
| 0x1C | StateUpdate | FUN_005b21c0 | owner->all | objectId, gameTime, dirtyFlags, [fields...] | **continuous** |
| 0x1D | ObjNotFound | FUN_006a0490 | S->C | objectId | rare |
| 0x1E | RequestObject | FUN_006a02a0 | C->S | objectId | rare |
| 0x1F | EnterSet | FUN_006a05e0 | S->C | objectId, setData | at join |
| 0x28 | (no handler) | (default) | S->C | vestigial, 1 byte | 3 |
| 0x29 | Explosion | FUN_006a0080 | S->C | objectId, impact(cv4), damage(cf16), radius(cf16) | on hit |
| 0x2A | NewPlayerInGame | FUN_006a1e70 | S->C | (triggers InitNetwork + object replication) | at join |

**Notes:**
- Opcodes 0x05 and 0x20-0x27 are NOT in this dispatcher (0x20-0x27 go to NetFile dispatcher)
- Opcode 0x05 does not exist as a game opcode (the jump table skips it)
- MAX_MESSAGE_TYPES = **0x2B** (43)

---

## 6. Python Script Message Table

Python-level messages bypass all C++ dispatchers. They are sent via `SendTGMessage()` and received by Python `ReceiveMessage` handlers in multiplayer scripts.

**MAX_MESSAGE_TYPES = 0x2B.** All offsets calculated from this base.

| Opcode | Offset | Name | Direction | Relay Pattern |
|--------|--------|------|-----------|---------------|
| 0x2C | MAX+1 | CHAT_MESSAGE | relayed | Client->Host, Host->Group("NoMe") |
| 0x2D | MAX+2 | TEAM_CHAT_MESSAGE | relayed | Client->Host, Host->teammates only |
| 0x35 | MAX+10 | MISSION_INIT_MESSAGE | S->C | Host->specific client (on join) |
| 0x36 | MAX+11 | SCORE_CHANGE_MESSAGE | S->C | Host->Group("NoMe") |
| 0x37 | MAX+12 | SCORE_MESSAGE | S->C | Host->specific client (full sync at join) |
| 0x38 | MAX+13 | END_GAME_MESSAGE | S->C | Host->all (broadcast) |
| 0x39 | MAX+14 | RESTART_GAME_MESSAGE | S->C | Host->all (broadcast) |
| 0x3F | MAX+20 | SCORE_INIT_MESSAGE | S->C | Team modes (Mission2/3) |
| 0x40 | MAX+21 | TEAM_SCORE_MESSAGE | S->C | Team modes (Mission2/3) |
| 0x41 | MAX+22 | TEAM_MESSAGE | S->C | Team modes (Mission2/3) |

---

## 7. Key Packet Formats

### Settings Packet (opcode 0x00, server -> client)

Sent by ChecksumCompleteHandler (FUN_006a1b10) after all 4 checksum rounds pass.

```
Offset  Size  Type     Field                    Notes
------  ----  ----     -----                    -----
0       1     u8       opcode = 0x00
1       4     f32      game_time                From clock+0x90
+0      bit   bool     settings_byte1           DAT_008e5f59 (collision damage toggle)
+0      bit   bool     settings_byte2           DAT_0097faa2 (friendly fire toggle)
+0      1     u8       player_slot              Assigned player index (0-15)
+0      2     u16      map_name_length
+0      var   string   map_name                 Mission TGL file path
+0      bit   bool     checksum_result_flag     1 = checksums needed correction
[if flag == 1:]
+0      var   data     checksum_correction_data
```

Write sequence:
```c
WriteByte(stream, 0x00);
WriteFloat(stream, gameTime);
WriteBit(stream, DAT_008e5f59);     // collision damage
WriteBit(stream, DAT_0097faa2);     // friendly fire
WriteByte(stream, playerSlot);
WriteShort(stream, mapNameLen);
WriteBytes(stream, mapName, len);
WriteBit(stream, checksumFlag);
```

### ObjectCreateTeam (opcode 0x03)

Ship creation message. Sent when a client selects a ship.

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      type_tag = 3 (object with team)
1       1     u8      owner_player_slot (0-15)
2       1     u8      team_id
3+      var   data    serialized_object (vtable+0x10C output)
```

The `serialized_object` data includes: object type ID, position, rotation, health, subsystem states, weapon loadouts, AI state. Produced by `object->vtable[0x10C](buffer, maxlen)`.

### StateUpdate (opcode 0x1C) - Most Frequent Message

Sent per-ship per-tick (~10Hz). Uses dirty flags to minimize bandwidth.

```
Offset  Size  Type     Field
------  ----  ----     -----
0       1     u8       opcode = 0x1C
1       4     i32      object_id         Ship's network object ID
5       4     f32      game_time         Current game clock
9       1     u8       dirty_flags       Bitmask of which fields follow
```

#### Dirty Flags

| Bit | Mask | Name | Data Format | Size |
|-----|------|------|-------------|------|
| 0 | 0x01 | POSITION_ABSOLUTE | 3x f32 + bit(has_hash) + [u16 hash] | 12-14 bytes |
| 1 | 0x02 | POSITION_DELTA | CompressedVector4 (3 dir bytes + u16 mag) | 5 bytes |
| 2 | 0x04 | ORIENTATION_FWD | CompressedVector3 (3 signed bytes / 127.0) | 3 bytes |
| 3 | 0x08 | ORIENTATION_UP | CompressedVector3 (3 signed bytes / 127.0) | 3 bytes |
| 4 | 0x10 | SPEED | CompressedFloat16 | 2 bytes |
| 5 | 0x20 | SUBSYSTEM_STATES | Round-robin subsystem health data | ~10 bytes max |
| 6 | 0x40 | CLOAK_STATE | u8 (0=decloaked, nonzero=cloaked) | 1 byte |
| 7 | 0x80 | WEAPON_STATES | Round-robin weapon health data | ~6 bytes max |

**Direction-based split** (verified from 30,000+ packets):
- **Client -> Server**: always sends 0x80 (weapons), never 0x20
- **Server -> Client**: always sends 0x20 (subsystems), never 0x80

### Chat Message Format (opcode 0x2C)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x2C
1       1     u8      sender_slot (player slot index)
2       3     bytes   padding (0x00 0x00 0x00)
5       2     u16     string_length (little-endian)
7       var   string  message_text (ASCII)
```

Delivery: reliable (`SetGuaranteed(1)`).
Team chat (0x2D) uses identical format.

### MISSION_INIT_MESSAGE (opcode 0x35)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x35
1       1     u8      player_limit
2       1     u8      system_species
3       1     u8      time_limit
4       4     f32     end_time (absolute game time)
8       1     u8      frag_limit
```

### SCORE_CHANGE_MESSAGE (opcode 0x36)

Score delta message sent on kills:
```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x36
1+      var   data    scorer_id, victim_id, delta values
```

### Explosion (opcode 0x29)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode (skipped by handler)
1       4     i32     object_id (ReadInt32v)
+0      5     cv4     impact_position (CompressedVector4 with u16 magnitude)
+0      2     u16     damage (CompressedFloat16)
+0      2     u16     radius (CompressedFloat16)
```

### TorpedoFire (opcode 0x19)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x19
1       4     i32     object_id (torpedo subsystem)
+0      1     u8      flags1 (subsystem index / type)
+0      1     u8      flags2 (bit 0=has_arc, bit 1=has_target)
+0      3     cv3     velocity (CompressedVector3, torpedo direction)
[if has_target (flags2 bit 1):]
  +0    4     i32     target_id
  +0    5     cv4     impact_point (CompressedVector4)
```

### BeamFire (opcode 0x1A)

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x1A
1       4     i32     object_id (phaser subsystem)
+0      1     u8      flags
+0      3     cv3     target_position (CompressedVector3)
+0      1     u8      more_flags (bit 0 = has_target_id)
[if has_target_id:]
  +0    4     i32     target_object_id
```

---

## 8. Compressed Data Types

### CompressedFloat16 (Logarithmic Scale)

Used for speed, damage, distances. 16-bit encoding with ~12 bits of precision per octave.

```
Format: [sign:1][scale:3][mantissa:12]
        Bit 15=sign, Bits 14-12=scale exponent, Bits 11-0=mantissa

Encode (FUN_006d3a90):
1. If value < 0: set sign bit, negate
2. Find scale (0-7) such that value < BASE * MULT^scale
3. mantissa = ftol(value / range * 4096)
4. Result = (sign_flag * 8 + scale) * 0x1000 + mantissa

Decode (FUN_006d3b30):
1. mantissa = encoded & 0xFFF
2. scale = (encoded >> 12) & 0x7
3. sign = (encoded >> 15) & 1
4. Compute range from scale
5. result = range * mantissa * (1/4096)
6. If sign: negate
```

Constants BASE (`DAT_00888b4c`) and MULT (`DAT_0088c548`) define the logarithmic scale ranges.

### CompressedVector3

Used for direction vectors (velocity, orientation).

Wire format: `[dirX:u8][dirY:u8][dirZ:u8]` = **3 bytes**

Each byte is a signed direction component: `ftol(component / magnitude * 127.0)`.

### CompressedVector4

Used for position deltas with magnitude.

Wire format (param4=1): `[dirX:u8][dirY:u8][dirZ:u8][magnitude:u16]` = **5 bytes**
Wire format (param4=0): `[dirX:u8][dirY:u8][dirZ:u8][magnitude:f32]` = **7 bytes**

### Bit Packing

`WriteBit`/`ReadBit` pack up to 5 booleans into a single byte:

```
Byte layout:  [count:3][bits:5]
              MSB          LSB

count (bits 7-5): Number of bits packed (1-5), stored as (count-1)
bits  (bits 4-0): Boolean values, one per bit position
```

Used in Settings packet (collision damage, friendly fire flags) and StateUpdate (subsystem hash flag).

---

## 9. Checksum/NetFile Opcodes (0x20-0x28)

Handled by NetFile dispatcher (`FUN_006a3cd0`), separate from the game opcode dispatcher.

| Opcode | Name | Direction | Handler | Payload |
|--------|------|-----------|---------|---------|
| 0x20 | ChecksumRequest | S->C | FUN_006a5df0 | index, directory, filter, recursive |
| 0x21 | ChecksumResponse | C->S | FUN_006a4260 | index, hashes |
| 0x22 | VersionMismatch | S->C | FUN_006a4c10 | failing filename |
| 0x23 | SystemChecksumFail | S->C | FUN_006a4c10 | failing filename |
| 0x25 | FileTransfer | S->C | FUN_006a3ea0 | filename, filedata |
| 0x27 | FileTransferACK | C->S | FUN_006a4250 | (empty) |

### Hash Algorithm (FUN_007202e0)

4-table byte-XOR substitution hash. NOT MD5, NOT CRC32.

```
Tables: 4 x 256 bytes at:
  0x0095c888, 0x0095c988, 0x0095ca88, 0x0095cb88

Algorithm:
  a = b = c = d = 0
  for each byte in input:
      a = table0[byte ^ a]
      b = table1[byte ^ b]
      c = table2[byte ^ c]
      d = table3[byte ^ d]
  hash = (a << 24) | (b << 16) | (c << 8) | d
```

---

## 10. GameSpy LAN Discovery

### Peek Router

GameSpy and TGNetwork share the same UDP socket (WSN+0x194):
- `MSG_PEEK` first byte without consuming
- `\` prefix = GameSpy query (plaintext)
- Binary = game packet (AlbyRules encrypted)
- `qr_t+0xE4` set to 0 to disable GameSpy's own recvfrom loop

### Query/Response Format

Standard GameSpy QR SDK protocol. Backslash-delimited key-value pairs:

```
Query:    \basic\     (or \status\, \info\)
Response: \hostname\OpenBC Server\numplayers\2\maxplayers\16\mapname\DeepSpace9\gametype\Deathmatch\hostport\22101\
```

Response fragmentation if > 0x545 bytes. `queryid` appended to each response fragment.

Default port: 22101 (0x5655).

---

## 11. Data Structures

### TGWinsockNetwork (0x34C bytes)

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| +0x14 | 4 | connState | 2=HOST, 3=CLIENT, 4=DISCONNECTED |
| +0x18 | 4 | localPeerID | |
| +0x2B | 2 | packetSize | Default 0x200 (512) |
| +0x2C | 4 | peerArray | Sorted by peer ID |
| +0x30 | 4 | peerCount | |
| +0x2D | 4 | reliableTimeout | 360.0f |
| +0x2E | 4 | disconnectTimeout | 45.0f |
| +0xA8 | 4 | maxPendingBytes | 0x8000 |
| +0x10C | 1 | sendEnabled | |
| +0x10E | 1 | isHost | |
| +0x10F | 1 | isConnecting | |
| +0x194 | 4 | socket (SOCKET) | UDP socket handle |
| +0x338 | 4 | port | Bound port number |
| +0x348 | ptr | peerAddrList | Linked list of peer addresses |

### Peer (~0xC0 bytes)

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| +0x18 | 4 | peerID | Unique network identifier |
| +0x1C | 4 | address | sockaddr_in |
| +0x24 | 2 | seqRecvUnreliable | |
| +0x26 | 2 | seqSendReliable | |
| +0x28 | 2 | seqRecvReliable | |
| +0x2A | 2 | seqSendPriority | |
| +0x2C | 4 | lastRecvTime | float (game clock) |
| +0x30 | 4 | lastSendTime | float |
| +0x64 | - | unreliableQueue | Send queue |
| +0x7C | 4 | unreliableCount | |
| +0x80 | - | reliableQueue | Send queue |
| +0x98 | 4 | reliableCount | |
| +0x9C | - | priorityQueue | Send queue (ACKs + retries) |
| +0xB4 | 4 | priorityCount | |
| +0xB8 | 4 | disconnectTime | float |
| +0xBC | 1 | isDisconnecting | **UNRELIABLE** -- use peer-array detection instead |

### MultiplayerGame

| Offset | Size | Field |
|--------|------|-------|
| +0x74 | 16 * 0x18 | playerSlots[16] |
| +0x1F8 | 4 | readyForNewPlayers |
| +0x1FC | 4 | maxPlayers |

### Player Slot (0x18 bytes each)

| Offset | Size | Field |
|--------|------|-------|
| +0x00 | 1 | active flag |
| +0x04 | 4 | peer network ID |
| +0x08 | 4 | player object ID |
| +0x0C | 12 | additional (team, name, etc.) |

### Ship Object (damage-relevant offsets)

| Offset | Type | Field |
|--------|------|-------|
| +0x18 | NiNode* | Scene graph root (DoDamage gate check) |
| +0xD8 | float | Ship mass |
| +0x128 | void** | Subsystem damage handler array |
| +0x130 | int | Handler count |
| +0x13C | void* | Hull damage receiver |
| +0x140 | NiNode* | Damage target reference (DoDamage gate check) |
| +0x1B8 | float | Damage resistance multiplier |
| +0x1BC | float | Damage falloff multiplier |
| +0x280 | int | Subsystem count (linked list, for state updates) |
| +0x284 | void* | Subsystem linked list HEAD |
| +0x2B0-0x2E4 | ptrs | Named subsystem slots (15 types) |

### Network Object ID Allocation

```
Player N base = 0x3FFFFFFF + N * 0x40000
Extract player slot from objID: (objID - 0x3FFFFFFF) >> 18
IDs per player: 262,143 (0x40000)
```

### TGBufferStream

| Offset | Size | Field |
|--------|------|-------|
| +0x00 | 4 | vtable* |
| +0x1C | 4 | buffer_ptr |
| +0x20 | 4 | buffer_capacity |
| +0x24 | 4 | current_position |
| +0x28 | 4 | bit_pack_bookmark |
| +0x2C | 1 | bit_pack_state |

### Stream I/O Functions

| Function | Address | Type | Size |
|----------|---------|------|------|
| WriteByte | FUN_006cf730 | u8 | 1 |
| WriteBit | FUN_006cf770 | bool | 0-1 |
| WriteShort | FUN_006cf7f0 | u16 LE | 2 |
| WriteInt32 | FUN_006cf870 | i32 | 4 |
| WriteFloat | FUN_006cf8b0 | f32 | 4 |
| WriteBytes | FUN_006cf2b0 | raw | N |
| ReadByte | FUN_006cf540 | u8 | 1 |
| ReadBit | FUN_006cf580 | bool | 0-1 |
| ReadShort | FUN_006cf600 | u16 LE | 2 |
| ReadInt32 | FUN_006cf670 | i32 | 4 |
| ReadFloat | FUN_006cf6b0 | f32 | 4 |
| ReadBytes | FUN_006cf230 | raw | N |

---

## 12. Event Handler Registration Table

From FUN_0069efe0 (MultiplayerGame handler registration):

| Address | Handler Name |
|---------|-------------|
| 0x0069f2a0 | ReceiveMessageHandler (main dispatch) |
| 0x006a0a20 | DisconnectHandler |
| 0x006a0a30 | NewPlayerHandler |
| 0x006a0c60 | SystemChecksumPassHandler |
| 0x006a0c90 | SystemChecksumFailHandler |
| 0x006a0ca0 | DeletePlayerHandler |
| 0x006a0f90 | ObjectCreatedHandler |
| 0x006a1150 | HostEventHandler |
| 0x006a1590 | NewPlayerInGameHandler |
| 0x006a1790 | StartFiringHandler |
| 0x006a17a0 | StartWarpHandler |
| 0x006a17b0 | TorpedoTypeChangeHandler |
| 0x006a18d0 | StopFiringHandler |
| 0x006a18e0 | StopFiringAtTargetHandler |
| 0x006a18f0 | StartCloakingHandler |
| 0x006a1900 | StopCloakingHandler |
| 0x006a1910 | SubsystemStatusHandler |
| 0x006a1920 | AddToRepairListHandler |
| 0x006a1930 | ClientEventHandler |
| 0x006a1940 | RepairListPriorityHandler |
| 0x006a1970 | SetPhaserLevelHandler |
| 0x006a1a60 | DeleteObjectHandler |
| 0x006a1a70 | ChangedTargetHandler |
| 0x006a1b10 | ChecksumCompleteHandler |
| 0x006a2640 | KillGameHandler |
| 0x006a2a40 | RetryConnectHandler |
| 0x006a1240 | ObjectExplodingHandler |
| 0x006a07d0 | EnterSetHandler |
| 0x006a0a10 | ExitedWarpHandler |
