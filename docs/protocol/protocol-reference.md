# OpenBC: Verified Wire Protocol Reference

Observed behavior of the Bridge Commander 1.1 multiplayer network protocol, documented from black-box packet captures of stock dedicated servers communicating with stock BC 1.1 clients. All data below reflects observed wire behavior only.

**Date**: 2026-02-17
**Method**: Packet capture with decryption (AlbyRules stream cipher)
**Trace corpus**: 2,648,271 lines / 136MB from a 34-minute 3-player combat session ("Battle of Valentine's Day", 2026-02-14), plus a 4,343-line loopback session for checksum timing
**Clean room statement**: This document contains no decompiled code, no binary addresses, no internal memory offsets, and no handler tables. All formats and behaviors are derived from observable wire data.

---

## 1. Transport Layer

> Moved to [transport-layer.md](transport-layer.md). See also [transport-cipher.md](transport-cipher.md) for the AlbyRules stream cipher algorithm.

---

## 2. Reliable Delivery

> Moved to [transport-layer.md](transport-layer.md) (sections on ACK behavior, sequence numbering, reliable retry, and fragment reassembly).

---

## 3. Connection and Join Flow

> Moved to [../network-flows/join-flow.md](../network-flows/join-flow.md). See also [../wire-formats/checksum-handshake-protocol.md](../wire-formats/checksum-handshake-protocol.md) for the checksum exchange.

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
| 0x04 | BootPlayer | S->C | reliable | 0 | Player kick (see Section 6.13) |
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
| 0x19 | TorpedoFire | owner->all | reliable | 1,090 | Torpedo launch |
| 0x1A | BeamFire | owner->all | reliable | 157 | Beam weapon hit |
| 0x1B | TorpTypeChange | any | reliable | 13 | Torpedo type switch |
| 0x1C | StateUpdate | owner->all | **unreliable** | **199,541** | Position/state sync |
| 0x1D | ObjNotFound | S->C | reliable | -- | Object lookup failure |
| 0x1E | RequestObject | C->S | reliable | -- | Request missing object data |
| 0x1F | EnterSet | S->C | reliable | 1 | Enter game set |
| 0x28 | ChecksumComplete | S->C | reliable | 3 | Checksum phase done signal |
| 0x29 | Explosion | S->C | reliable | 60 | Explosion effect |
| 0x2A | NewPlayerInGame | C->S/S->C | reliable | 3 | Player join handshake |

**Notes:**
- Opcodes 0x04 and 0x05 are dead -- never observed on wire in any trace
- Opcodes 0x20-0x27 are handled by a separate checksum/file dispatcher (see [../wire-formats/checksum-handshake-protocol.md](../wire-formats/checksum-handshake-protocol.md))
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

For detailed wire formats of script messages, see [../wire-formats/script-message-wire-format.md](../wire-formats/script-message-wire-format.md).

---

## 5. Chat Message Format (Opcode 0x2C)

> Moved to [../wire-formats/script-message-wire-format.md](../wire-formats/script-message-wire-format.md).

---

## 6. Key Packet Formats

### 6.1 Settings (Opcode 0x00)

Sent by the server after all 5 checksum rounds pass. Contains game configuration for the joining player.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x00
gameTime          f32 LE     4      Current game clock (seconds)
configFlags       bitpacked  1      3 booleans packed (see Compressed Types, Section 7)
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

> Detailed wire format moved to [../wire-formats/objcreate-wire-format.md](../wire-formats/objcreate-wire-format.md).

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

### 6.6 Explosion (Opcode 0x29)

> Detailed wire format moved to [../wire-formats/explosion-wire-format.md](../wire-formats/explosion-wire-format.md).

### 6.7 CollisionEffect (Opcode 0x15)

> Detailed wire format moved to [../wire-formats/collision-effect-wire-format.md](../wire-formats/collision-effect-wire-format.md).

### 6.8 Score Message (Opcode 0x37)

Per-player score synchronization. The server sends **one 0x37 message per existing player** when a new player joins, giving them the full roster state. Not batched -- each message carries exactly one player's score data.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x37
playerId          i32 LE     4      Network player ID (GetNetID()/wire slot)
kills             i32 LE     4      Kill count
deaths            i32 LE     4      Death count
score             i32 LE     4      Current score (signed, can be negative)
```

Total: 17 bytes per message. Source: readable Python scripts shipped with the game (Mission1.py `SendScoreMessage`).

### 6.9 ScoreChange Message (Opcode 0x36)

Sent when a kill occurs. Contains updated kill/death/score counts for the killer and victim, plus optional score updates for other players who contributed damage.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x36
killerId          i32 LE     4      Killer's network player ID (0 for environmental kills)
[if killerId != 0:]
  killerKills     i32 LE     4      Killer's updated kill count
  killerScore     i32 LE     4      Killer's updated score
victimId          i32 LE     4      Victim's network player ID
victimDeaths      i32 LE     4      Victim's updated death count
updateCount       u8         1      Number of extra score updates
[repeated updateCount times:]
  playerId        i32 LE     4      Player's network player ID
  score           i32 LE     4      Player's updated score
```

Variable length. When `killerId == 0` (environmental death, e.g., collision or self-destruct), the `killerKills` and `killerScore` fields are omitted.

### 6.10 EndGame Message (Opcode 0x38)

Broadcast to all clients when the match ends.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x38
reason            i32 LE     4      End reason code (see below)
```

| Reason | Code | Description |
|--------|------|-------------|
| OVER | 0 | Generic game over |
| TIME_UP | 1 | Time limit reached |
| FRAG_LIMIT | 2 | Frag/kill limit reached |
| SCORE_LIMIT | 3 | Score limit reached |
| STARBASE_DEAD | 4 | Starbase destroyed (objective mode) |
| BORG_DEAD | 5 | Borg destroyed (objective mode) |
| ENTERPRISE_DEAD | 6 | Enterprise destroyed (objective mode) |

### 6.11 RestartGame Message (Opcode 0x39)

Broadcast to all clients to restart the match. No payload.

```
Field             Type       Size   Notes
-----             ----       ----   -----
opcode            u8         1      0x39
```

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

| Code | Meaning |
|------|---------|
| 0 | Generic kick |
| 1 | Version mismatch |
| 2 | Server full |
| 3 | Banned |
| 4 | Checksum validation failed |

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

**Precision note**: The encode path divides the mantissa by 4096.0f while the decode path divides by 4095.0f. This intentional asymmetry means a round-trip encode->decode is not perfectly lossless, but the error is less than 0.025%.

### CompressedVector3 (cv3)

Used for direction vectors (velocity direction, orientation forward/up).

```
Wire format: [dirX:u8][dirY:u8][dirZ:u8] = 3 bytes
Each byte is a signed direction component: ftol(component * 127.0)
Range: -1.0 to +1.0 per component
```

### CompressedVector4 (cv4)

Used for position deltas and impact positions. A cv3 direction plus a magnitude.

```
Wire format: [dirX:u8][dirY:u8][dirZ:u8][magnitude:u16] = 5 bytes
Direction: same as cv3 (signed bytes / 127.0)
Magnitude: CompressedFloat16 encoding
```

---

## 8. StateUpdate Deep Dive (Opcode 0x1C)

> Moved to [../wire-formats/stateupdate-wire-format.md](../wire-formats/stateupdate-wire-format.md). See also [../game-systems/ship-subsystems.md](../game-systems/ship-subsystems.md) for the subsystem index table.

---

## 9. GameSpy LAN Discovery

> Moved to [gamespy-protocol.md](gamespy-protocol.md).

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
- **[transport-layer.md](transport-layer.md)** -- UDP transport: packet framing, message types, reliability
- **[gamespy-protocol.md](gamespy-protocol.md)** -- GameSpy LAN discovery, master server registration, challenge-response crypto
- **[../network-flows/join-flow.md](../network-flows/join-flow.md)** -- Connection lifecycle state machine (connect through gameplay)
- **[../game-systems/combat-system.md](../game-systems/combat-system.md)** -- Damage pipeline, shields, cloaking, tractor beams, repair system
- **[../game-systems/ship-subsystems.md](../game-systems/ship-subsystems.md)** -- Fixed subsystem index table, HP values, StateUpdate serialization
- **[../wire-formats/checksum-handshake-protocol.md](../wire-formats/checksum-handshake-protocol.md)** -- Checksum exchange details and hash algorithms
- **[../network-flows/disconnect-flow.md](../network-flows/disconnect-flow.md)** -- Player disconnect detection and cleanup
- **[../wire-formats/stateupdate-wire-format.md](../wire-formats/stateupdate-wire-format.md)** -- StateUpdate wire format and dirty flags
- **[../wire-formats/objcreate-wire-format.md](../wire-formats/objcreate-wire-format.md)** -- ObjCreateTeam wire format
- **[../wire-formats/script-message-wire-format.md](../wire-formats/script-message-wire-format.md)** -- Script message wire formats (chat, scoring, game lifecycle)
