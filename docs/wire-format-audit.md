# OpenBC Wire Format Audit Report

**Date**: 2026-02-16
**Source**: Stock BC dedicated server packet traces (90MB+)
**Traces analyzed**:
- `/mnt/c/Users/Steve/source/projects/STBC-Dedicated-Server/game/stock-dedi/packet_trace.log`
- `/mnt/c/Users/Steve/source/projects/STBC-Dedicated-Server/logs/stock/battle-of-valentines-day/packet_trace.log`
- `/mnt/c/Users/Steve/source/projects/STBC-Dedicated-Server/game/stock-dedi/message_trace.log`

Cross-referenced against decompiled code in `STBC-Dedicated-Server/reference/decompiled/`.

---

## Summary of Findings

| Area | Status | Severity |
|------|--------|----------|
| 1. Transport framing | MATCH (with caveats) | -- |
| 2. Connect handshake | MATCH | -- |
| 3. Checksum rounds 0-3 | MATCH | -- |
| 4. Checksum final round 0xFF | **MISMATCH** | HIGH |
| 5. TGBufferStream bit-packing | **MISMATCH** | **CRITICAL** |
| 6. Settings (0x00) payload | **MISMATCH** (due to #5) | **CRITICAL** |
| 7. GameInit (0x01) | MATCH | -- |
| 8. NewPlayerInGame (0x2A) direction | **MISMATCH** | HIGH |
| 9. Opcode 0x28 missing | **MISMATCH** | HIGH |
| 10. Server keepalive format | **MISMATCH** | MEDIUM |
| 11. ACK flags | MISMATCH | LOW |
| 12. UICollisionSetting (0x16) | MISMATCH (ordering) | HIGH |

---

## 1. Transport Framing

### What OpenBC does
- Header: `[direction:1][msg_count:1]` (2 bytes)
- ACK: `[0x01][counter:1][0x00][flags:1]` (4 bytes fixed)
- Reliable: `[0x32][totalLen:1][flags:1][seqHi:1][seqLo:1][payload...]`
- Other: `[type:1][totalLen:1][data...]`

### What traces show
Identical framing. Examples from trace packet #8 (S->C):
```
01 02 03 06 C0 00 00 02 32 1B 80 00 00 20 00 08 00 73 63 72 69 70 74 73 2F ...
^^ ^^ -- msgs=2
      |-- msg0: Connect(0x03) len=6 body=4
                              |-- msg1: Reliable seq=0 len=27
```

### Verdict: MATCH
Transport framing is correct. Connect-type messages (0x03-0x06) use the same `[type][totalLen][data]` format as the "Other" case, where `totalLen` includes the type byte. The 2-byte flags_len encoding (bit15=reliable, bit14=priority, bits13-0=totalLen) happens to have the total length in the first byte for messages under 256 bytes.

**Note**: OpenBC's generic parser correctly handles Connect messages because it reads `totalLen = data[pos+1]`, which works for small messages. For messages >255 bytes, a 2-byte length parser would be needed, but Connect messages are always small.

---

## 2. Connect Handshake Sequence

### What OpenBC does
```
C->S: Connect(0x03) dir=INIT
S->C: Connect(0x03) response with slot byte
S->C: ChecksumReq round 0 (separate packet)
```

### What traces show
```
C->S: [FF 01 03 0F C0 00 00 0A 0A 0A EF 5F 0A 00 00 00 00]
       dir=INIT, msgs=1, Connect(0x03) len=15

S->C: [01 02 03 06 C0 00 00 02 32 1B 80 00 00 20 00 ...]
       dir=S, msgs=2:
         msg0: Connect(0x03) len=6, payload=[00 00 02] (seq=0, slot=0x02)
         msg1: Reliable seq=0, ChecksumReq round 0
```

### Verdict: MATCH (with batching difference)
The stock dedi **batches** the Connect response and first ChecksumReq in a single UDP packet (msgs=2). OpenBC sends them separately. This is functionally equivalent -- the client handles both cases -- but the batching reduces round-trip latency.

**Slot assignment**: Stock dedi assigns slot 0x02 (wire_slot = peer_index + 1). First joiner gets slot 2 (index 1), matching OpenBC's `(u8)(slot + 1)`.

---

## 3. Checksum Exchange (Rounds 0-3)

### What OpenBC does
```
Round 0: dir="scripts/"          filter="App.pyc"       recursive=false
Round 1: dir="scripts/"          filter="Autoexec.pyc"   recursive=false
Round 2: dir="scripts/ships/"    filter="*.pyc"           recursive=true
Round 3: dir="scripts/mainmenu/" filter="*.pyc"           recursive=false
```

### What traces show
```
Round 0: dir="scripts/"          filter="App.pyc"       recursive=0  (27 bytes)
Round 1: dir="scripts/"          filter="Autoexec.pyc"   recursive=0  (32 bytes)
Round 2: dir="scripts/ships"     filter="*.pyc"           recursive=1  (30 bytes)
Round 3: dir="scripts/mainmenu"  filter="*.pyc"           recursive=0  (33 bytes)
```

### Verdict: MATCH (with trailing slash detail)
The directory strings in the trace do NOT have trailing slashes:
- `scripts/ships` not `scripts/ships/`
- `scripts/mainmenu` not `scripts/mainmenu/`

OpenBC's round definitions use trailing slashes (`scripts/ships/`, `scripts/mainmenu/`). The base directory `scripts/` appears both with a trailing slash in rounds 0-1. For rounds 2-3, the trailing slash **may affect the dir_hash** used for checksum validation. This should be verified.

**Wire format**: `[0x20][round:u8][dirLen:u16][dir:bytes][filterLen:u16][filter:bytes][recursive:bit]` -- matches.

The bit byte for recursive=true is `0x21` (from "pyc!" in hex: `70 79 63 21` -- the `21` after "pyc" is the bit-packed byte with count=0, bit0=1).

For recursive=false, the bit byte is `0x20` (count=0, bit0=0). Wait -- from the trace `20` appears at the end of non-recursive rounds. But `0x20` with count field = (0x20>>5)=1 would mean count=1, 1 bit, value=0. This doesn't match the OpenBC encoding either.

Actually wait, let me re-read. The last byte of round 0: `... 41 70 70 2E 70 79 63 20`. The `20` is the trailing byte = 0x20 = space character. But in the bit encoding, `0x20 = (1<<5) | 0 = count=1, value=0`. With the real TGBufferStream encoding, count=1 means 1 bit was written with value=0 (recursive=false). This is CORRECT for the real encoding.

With OpenBC's encoding for 1 bit (recursive=false): `count-1 = 0`, so byte = `(0<<5) | 0 = 0x00`.
With real encoding: count=1, byte = `(1<<5) | 0 = 0x20`.

**This confirms the bit-packing mismatch**: even for the checksum request's recursive flag, OpenBC encodes `0x00` while the real game encodes `0x20`. This corrupts the checksum request wire format.

---

## 4. Checksum Final Round (0xFF)

### What OpenBC does
Sends minimal: `[0x20][0xFF]` (2 bytes)

### What traces show
Sends a FULL checksum request with round=0xFF:
```
Packet #24 (stock dedi) payload:
  20 FF 13 00 53 63 72 69 70 74 73 2F 4D 75 6C 74
  69 70 6C 61 79 65 72 05 00 2A 2E 70 79 63 21
```

Decoded:
```
opcode  = 0x20 (ChecksumReq)
round   = 0xFF
dirLen  = 0x0013 (19)
dir     = "Scripts/Multiplayer"    <-- Note capital S
filterLen = 0x0005 (5)
filter  = "*.pyc"
recursive = true  (bit byte = 0x21)
```

### Verdict: **MISMATCH -- HIGH severity**
OpenBC sends only `[0x20][0xFF]`. The real server sends a complete checksum request for `Scripts/Multiplayer/*.pyc` (recursive). The client expects to scan this directory and return checksums. Sending only 2 bytes will likely cause a parse error on the client side, or the client will respond with incomplete/wrong data.

**Fix needed**: Add a 5th checksum round definition:
```
{ "Scripts/Multiplayer", "*.pyc", true }
```

Note: capital "S" in "Scripts" (differs from rounds 0-3 which use lowercase "scripts/").

---

## 5. TGBufferStream Bit-Packing (CRITICAL)

### What OpenBC does
The `bc_buf_write_bit` function stores the count as `bit_count - 1`:
```c
byte = (byte & 0x1F) | (((buf->bit_count - 1) & 0x7) << 5);
```

For 1 bit:  count field = 0 (byte = 0x00 | value)
For 2 bits: count field = 1 (byte = 0x20 | values)
For 3 bits: count field = 2 (byte = 0x40 | values)

### What the real TGBufferStream does
From decompiled FUN_006cf770 at 0x006cf770:
```c
bVar3 = (bVar3 >> 5) + 1;   // increment count
...
data[bookmark] = bVar2 | bVar3 * 0x20;   // store count * 32
```

For 1 bit:  count field = 1 (byte = 0x20 | value)
For 2 bits: count field = 2 (byte = 0x40 | values)
For 3 bits: count field = 3 (byte = 0x60 | values)

### Wire evidence
Settings payload bit byte (both traces): `0x61 = (3<<5) | 0x01`

With 3 booleans (collision=1, friendlyFire=0, checksumCorrection=0):
- Real encoding: `(3<<5) | 0x01 = 0x61` -- MATCHES trace
- OpenBC encoding: `((3-1)<<5) | 0x01 = 0x41` -- DOES NOT match

Checksum request recursive=false bit byte: `0x20`
- Real encoding: `(1<<5) | 0x00 = 0x20` -- MATCHES trace
- OpenBC encoding: `((1-1)<<5) | 0x00 = 0x00` -- DOES NOT match

### Impact
The BC client reader (FUN_006cf580) uses `1 << count_field` as the threshold to determine how many bits to read from the packed byte. If OpenBC sends count=2 but the client expects count=3 (because the client's reader is calibrated for the real encoding), the reader will terminate after 2 bits and start a new bit group for the 3rd ReadBit, consuming an extra byte from the stream. **This corrupts the entire stream parse from that point forward.**

### Verdict: **CRITICAL -- will break all messages containing bit-packed fields**
Affected messages include:
- Settings (0x00): 3 booleans
- ChecksumReq (0x20): 1 boolean (recursive flag)
- UICollisionSetting (0x16): 1 boolean
- StateUpdate (0x1C): CompressedVector uses bits internally
- Any message using WriteBit

### Fix required
Change `bc_buf_write_bit` to store count (not count-1):
```c
// WRONG (current):
byte = (byte & 0x1F) | (((buf->bit_count - 1) & 0x7) << 5);

// CORRECT:
byte = (byte & 0x1F) | ((buf->bit_count & 0x7) << 5);
```

Change `bc_buf_read_bit` to NOT add 1 when reading count:
```c
// WRONG (current):
u8 count = ((byte >> 5) & 0x7) + 1;

// CORRECT:
u8 count = (byte >> 5) & 0x7;
```

---

## 6. Settings (0x00) Payload

### What OpenBC sends
```
[0x00][gameTime:f32][collision:bit][friendlyFire:bit][playerSlot:u8]
[mapLen:u16][mapName:bytes][checksumFlag:bit]
```
Total: 1 + 4 + 1(bits) + 1 + 2 + mapLen + 0(bits shared) = varies
Bit byte: `0x41` (count=2 with OpenBC's encoding, 3 bits)

### What traces show
```
Packet #27 (stock dedi) Settings payload (46 bytes):
  00 00 32 98 42 61 00 25 00 4D...31
  ^  |---f32----| ^  ^  |u16-| |--mapName--|
  op             bit sl
```

Byte-by-byte:
- `00`: opcode 0x00
- `00 32 98 42`: f32 gameTime = 76.10 (LE: 0x42983200)
- `61`: bit-packed byte (count=3, 3 bits: collision=1, friendlyFire=0, checksumCorr=0)
- `00`: playerSlot = 0
- `25 00`: mapLen = 37
- 37 bytes: "Multiplayer.Episode.Mission1.Mission1"

### Verdict: **MISMATCH due to bit-packing bug (#5)**
The field order and types are correct. The only difference is the bit-packed byte encoding:
- Real: `0x61` (count=3)
- OpenBC: `0x41` (count=2)

**Player slot note**: Both traces show slot=0. This means the server assigns slot 0 to the joining player in Settings, even though the wire_slot (direction byte) is 2. The slot in Settings appears to be the player's game-level slot, not the network slot. OpenBC sends `(u8)peer_slot` which starts at 1 for the first joiner. This may need investigation -- the stock dedi sends 0.

Actually, re-reading: the stock dedi uses slot 0 for the dedi server itself. But the decoded Settings shows `slot=0`. Let me re-examine. OpenBC sends `player_slot` as the peer_slot value. If the first joiner is at array index 1, OpenBC sends slot=1. But the trace shows slot=0.

Wait -- the `00` after the bit byte could be the player slot. Both traces show `00` (slot 0 for both). But the first joiner gets wire_slot=2 (peer index 1). So the Settings `slot` value is different from the wire slot. Looking at the decompiled handler: `FUN_006cf730(local_43c, (char)iVar3)` where `iVar3` is derived from `FUN_006a19c0(this, *(int*)(param_1 + 0x28))` -- this is a lookup of the peer in the player slot array. The result is the PLAYER SLOT INDEX (0-15), not the network peer index.

**Possible issue**: OpenBC might be sending the wrong slot value. Need to verify whether the stock dedi player slot 0 represents something different from our peer index. However, since slot=0 is the dedi server and first joiner should be slot 1, the trace showing slot=0 is suspicious. It might mean the "player slot" in Settings is actually a different numbering scheme.

---

## 7. GameInit (0x01)

### What OpenBC sends
`[0x01]` (1 byte, just the opcode)

### What traces show
```
32 06 80 07 00 01
```
Reliable len=6, payload = `01` (1 byte)

### Verdict: **MATCH**

---

## 8. NewPlayerInGame (0x2A) Direction

### What OpenBC does
Server sends `[0x2A][0x20]` to ALL clients after completing handshake:
```c
u8 npig[2] = { BC_OP_NEW_PLAYER_IN_GAME, 0x20 };
send_to_all(npig, 2);
```

### What traces show
**0x2A is sent by the CLIENT to the SERVER**, not the other way around:
```
Packet #30 (C->S): [02 01 32 07 80 05 00 2A 20]
  dir=CLIENT, msgs=1, Reliable: [0x2A 0x20]
```

After the server sends Settings + GameInit, the CLIENT processes them and sends 0x2A back to the server. The server then responds with MissionInit (0x35).

Both trace sessions confirm: 0x2A is always client-to-server. No server-to-client 0x2A was observed.

### Verdict: **MISMATCH -- HIGH severity**
OpenBC proactively sends 0x2A, but it should WAIT for the client to send it. The correct flow:
1. Server sends Settings (0x00) + GameInit (0x01)
2. Client processes, sends NewPlayerInGame (0x2A) back
3. Server receives 0x2A, then sends MissionInit (0x35)

**Fix needed**: Remove the `send_to_all(npig, 2)` call. Add a handler for incoming 0x2A that triggers MissionInit send.

---

## 9. Missing Opcode 0x28

### What OpenBC does
Does not send opcode 0x28 at any point.

### What traces show
The server sends opcode 0x28 as the FIRST message after checksum completion, BEFORE Settings:

```
Packet #27 (S->C), msg0:
  32 06 80 05 00 28
  Reliable seq=1280, payload=[28] (1 byte, opcode 0x28)
```

Sequence:
1. Checksum final round response received
2. Server sends: 0x28 (Unknown_28) -- 1 byte, no payload
3. Server sends: 0x00 (Settings) -- 46 bytes
4. Server sends: 0x01 (GameInit) -- 1 byte

All three are batched in one packet.

### Verdict: **MISMATCH -- HIGH severity**
The stock dedi always sends opcode 0x28 before Settings. This opcode is listed in `opcodes.h` as `BC_OP_UNKNOWN_28` (0x28). Its purpose is unclear from the name, but it may signal "checksum exchange complete" or trigger client-side state transition. Omitting it may cause the client to mishandle the subsequent Settings message.

**Fix needed**: Send `[0x28]` (1 byte reliable) before Settings.

---

## 10. Server Keepalive Format

### What OpenBC does
Sends minimal keepalive: `[0x00][0x02]` (2 bytes, type=keepalive, totalLen=2, no body)

### What traces show
Server sends full keepalive with identity data (22 bytes):
```
Packet #11 (S->C):
  00 16 C0 01 00 02 0A 0A 0A EF 43 00 61 00 64 00 79 00 32 00 00 00
```

Format: `[0x00][totalLen=0x16][flags=0xC0][?=0x01][?=0x00][slot=0x02][IP:4][name_utf16le]`

The server echoes back the client's keepalive data (same format the client sent). This includes:
- flags (0xC0 = reliable+priority markers in the keepalive context)
- some unknown bytes
- the client's wire slot
- the client's IP address
- the client's UTF-16LE name

### Verdict: **MISMATCH -- MEDIUM severity**
The minimal keepalive may work for keeping the connection alive, but the stock server mirrors the client's identity data back. The client might expect this echo for connection state validation.

**Possible fix**: Cache the client's keepalive data and echo it back instead of sending minimal keepalives. Or construct a server-side keepalive with the dedi's own identity.

---

## 11. ACK Flags

### What OpenBC does
Always sends ACK with flags=0x80:
```c
bc_outbox_add_ack(&peer->outbox, seq, 0x80);
```

### What traces show
Server ACKs use varying flags:
```
01 00 00 02  -> flags=0x02 (after Connect/keepalive)
01 01 00 02  -> flags=0x02
01 00 00 00  -> flags=0x00 (after reliable game messages)
01 02 00 01  -> flags=0x01
```

The pattern suggests:
- flags=0x02: ACK for keepalive/connection messages
- flags=0x00: ACK for standard reliable messages
- flags=0x01: ACK for fragment-capable messages

### Verdict: **MISMATCH -- LOW severity**
The client likely doesn't use the ACK flags byte for anything critical (it just needs the seq number to clear its retransmit queue). However, using 0x80 is an unusual value not seen in any trace. Safer to use 0x00 as the default.

**Suggested fix**: Change default ACK flags to 0x00 instead of 0x80.

---

## 12. UICollisionSetting (0x16) Ordering

### What OpenBC does
Sends UICollisionSetting (0x16) BETWEEN Settings and GameInit:
```
Settings (0x00) -> UICollisionSetting (0x16) -> GameInit (0x01)
```

### What traces show
NO UICollisionSetting (0x16) is sent during the handshake. The server sends:
```
0x28 (Unknown_28) -> 0x00 (Settings) -> 0x01 (GameInit)
```

UICollisionSetting may be sent later or not at all during the initial handshake sequence.

### Verdict: **MISMATCH -- HIGH severity**
Sending 0x16 during handshake is not observed in traces. The collision setting is already included in the Settings (0x00) message via the bit-packed flags. The 0x16 opcode may be used during gameplay for runtime setting changes, not during initial setup. Sending it during handshake might confuse the client's state machine.

**Fix needed**: Remove UICollisionSetting from the handshake sequence.

---

## Complete Handshake: Expected vs. OpenBC

### Stock Dedi (from trace)
```
C->S: Connect(0x03) dir=INIT
S->C: Connect(0x03) + ChecksumReq round 0          (batched, 1 packet)
C->S: ACK + ACK + Keepalive(name)                   (batched)
S->C: ACK + Keepalive(echo)                          (server echoes identity)
C->S: ChecksumResp round 0
S->C: ACK + ChecksumReq round 1
C->S: ACK + ChecksumResp round 1
S->C: ACK + ChecksumReq round 2
C->S: ACK + ChecksumResp round 2 (fragmented)
S->C: ACK + ACK + ChecksumReq round 3
C->S: ACK + ChecksumResp round 3
S->C: ACK + ChecksumReq round 0xFF (Scripts/Multiplayer)
C->S: ACK + ChecksumResp round 0xFF (fragmented)
S->C: ACK + 0x28 + Settings(0x00) + GameInit(0x01)  (batched, 1 packet)
C->S: ACK + ACK + ACK
C->S: NewPlayerInGame(0x2A)                          (CLIENT sends this)
S->C: ACK + MissionInit(0x35) + 0x17                 (server responds)
```

### OpenBC (current)
```
C->S: Connect(0x03) dir=INIT
S->C: Connect(0x03) response                         (separate packet)
S->C: ChecksumReq round 0                            (separate packet)
C->S: ACK + Keepalive(name)
S->C: Keepalive(minimal 2 bytes)                      <-- WRONG format
C->S: ChecksumResp round 0
S->C: ChecksumReq round 1
...rounds 1-3 same...
S->C: [0x20][0xFF]                                    <-- WRONG: minimal, not full request
C->S: ChecksumResp round 0xFF
S->C: Settings(0x00)                                  <-- WRONG: bit packing (0x41 not 0x61)
S->C: UICollisionSetting(0x16)                        <-- WRONG: not in real handshake
S->C: GameInit(0x01)
S->C: NewPlayerInGame(0x2A)                           <-- WRONG: server shouldn't send this
S->C: MissionInit(0x35)                               <-- WRONG: should wait for client 0x2A
```

---

## Priority Fix List

### P0 -- Will break client parsing (CRITICAL)
1. **Fix bit-packing encoding**: Change count field from `bit_count-1` to `bit_count` in `bc_buf_write_bit`. Update `bc_buf_read_bit` to match. This affects ALL bit-packed messages.

### P1 -- Will cause handshake failure (HIGH)
2. **Fix checksum round 0xFF**: Send full checksum request `[0x20][0xFF][dirLen][Scripts/Multiplayer][filterLen][*.pyc][recursive=1]`, not just `[0x20][0xFF]`.
3. **Add opcode 0x28**: Send `[0x28]` (1 byte reliable) before Settings.
4. **Fix NewPlayerInGame direction**: Don't send 0x2A. Wait for client to send it, then respond with MissionInit.
5. **Remove UICollisionSetting from handshake**: Don't send 0x16 during initial connection.

### P2 -- May cause subtle issues (MEDIUM)
6. **Fix server keepalive**: Echo client identity data instead of sending minimal `[0x00][0x02]`.
7. **Verify Settings player_slot**: Stock dedi sends slot=0 but OpenBC sends peer_slot (1+). Need to verify correct semantics.
8. **Verify checksum directory trailing slashes**: Rounds 2-3 may need trailing slash removed (`scripts/ships` vs `scripts/ships/`).

### P3 -- Cosmetic / defensive (LOW)
9. **Fix ACK flags**: Use 0x00 instead of 0x80.
10. **Batch Connect + ChecksumReq**: Optional optimization to match stock behavior.

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/protocol/buffer.c` | Fix `bc_buf_write_bit` and `bc_buf_read_bit` count encoding |
| `src/protocol/handshake.c` | Fix final round 0xFF to send full request; fix checksum round directories |
| `src/server/main.c` | Add 0x28 send; remove UICollisionSetting from handshake; fix 0x2A direction; fix keepalive |
| `include/openbc/opcodes.h` | No changes needed |
| `src/network/transport.c` | Fix default ACK flags |
| `tests/test_protocol.c` | Update tests for new bit-packing encoding |
