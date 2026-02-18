# Transport Layer Specification

Complete specification of the Bridge Commander UDP transport layer: message framing, reliable delivery, fragmentation, and reassembly. Derived from protocol analysis of stock BC dedicated server packet captures (30,000+ packets across multiple sessions).

This document **supersedes** the transport section in `phase1-verified-protocol.md` Section 1, which had an incorrect header format (separate totalLen:1 + flags:1 bytes).

---

## 1. Packet Structure

After decryption (see [transport-cipher.md](transport-cipher.md)), each UDP packet has:

```
Offset  Size  Field
------  ----  -----
0       1     peer_id       Sender identity (0x01=server, 0x02+=clients, 0xFF=unassigned)
1       1     msg_count     Number of transport messages in this packet (1-255)
2+      var   messages      Sequence of self-describing transport messages
```

The receive loop reads `peer_id`, then `msg_count`, then iterates `msg_count` times. Each iteration reads a type byte and dispatches to a type-specific deserializer.

### Encryption Boundary

The cipher operates on `buffer+1` with `length-1`. **Byte 0 (peer_id) is never encrypted.** The first PRNG XOR byte happens to be 0x00 (due to the key schedule), so byte 0 would survive unchanged regardless, but the engine explicitly skips it.

---

## 2. Transport Message Types

Seven message types are registered in the factory table:

| Type | Name | Purpose |
|------|------|---------|
| 0x00 | DataMessage | Small control data (keepalive, peer info). 14-bit length, NO fragment support |
| 0x01 | HeaderMessage (ACK) | Acknowledgment for reliable delivery |
| 0x02 | ConnectMessage | Connection request (client→server and server→client) |
| 0x03 | ConnectAckMessage | Connection acknowledgment with slot assignment |
| 0x04 | BootMessage | Peer kick notification |
| 0x05 | DisconnectMessage | Graceful disconnect |
| 0x32 | Message (base) | **ALL game payloads**. 13-bit length, fragment support |

**Key architectural split**: Types < 0x32 are connection management. Type 0x32 carries all game-layer content. This distinction matters because they use **separate reliable sequence counters** per peer.

---

## 3. Wire Formats

### Type 0x32 — Data Message (game payloads)

This is the workhorse message type. Every game opcode (0x00-0x2A, 0x2C-0x39) is carried inside a type 0x32 wrapper.

```
Offset  Size  Field
------  ----  -----
0       1     type          Always 0x32
1       2     flags_len     LE uint16 (see bit layout below)
[if reliable:]
3       2     seq_num       LE uint16 reliable sequence number
[if fragmented:]
+0      1     frag_idx      Fragment index (0-based)
[if frag_idx == 0:]
+1      1     total_frags   Total number of fragments
[end if]
+N      var   payload       Game opcode byte + opcode-specific data
```

#### flags_len Bit Layout (LE uint16)

```
bit 15    (0x8000): reliable — message requires ACK, seq_num field present
bit 14    (0x4000): ordered — priority delivery queue
bit 13    (0x2000): fragment — fragment metadata (frag_idx, total_frags) follows seq_num
bits 12-0 (0x1FFF): total message size in bytes, INCLUDING the 0x32 type byte (max 8191)
```

**CRITICAL**: This is a 16-bit little-endian field, NOT two separate bytes. When reading from wire bytes `[lo][hi]`, the value is `(hi << 8) | lo`. The length is in bits 12-0, NOT in a separate byte.

#### Byte-Level Decoding Example

Wire bytes: `[32][1B][80]`
- Type = 0x32
- flags_len LE = `(0x80 << 8) | 0x1B` = 0x801B
- bit 15 = 1 → reliable
- bit 14 = 0 → not ordered
- bit 13 = 0 → not fragmented
- bits 12-0 = 0x001B = 27 → total message is 27 bytes
- Payload = 27 - 5 (type + flags_len + seq_num) = 22 bytes

Wire bytes: `[32][11][81]`
- flags_len LE = 0x8111
- bit 15 = 1 → reliable
- bits 12-0 = 0x0111 = 273 → total message is 273 bytes
- **NOT fragmented** (bit 13 = 0). The `0x01` in the low byte is part of the LENGTH, not a flag.

Wire bytes: `[32][9A][A1]`
- flags_len LE = 0xA19A
- bit 15 = 1 → reliable
- bit 14 = 0 → not ordered
- bit 13 = 1 → **fragmented** (has frag_idx prefix)
- bits 12-0 = 0x019A = 410 → total message is 410 bytes

#### Common flags_len High Byte Values

| High byte | Meaning |
|-----------|---------|
| `0x80` | Reliable, no fragment, length fits in low byte (< 256) |
| `0x81` | Reliable, no fragment, length bit 8 set (256-511 bytes) |
| `0xA0` | Reliable + fragmented, length fits in low byte |
| `0xA1` | Reliable + fragmented, length bit 8 set |
| `0x00` | Unreliable, no fragment (used by StateUpdate 0x1C) |

### Type 0x00 — Control Data Message

Used for keepalive/peer info. Same header structure but with 14-bit length and NO fragment support:

```
Offset  Size  Field
------  ----  -----
0       1     type          Always 0x00
1       2     flags_len     LE uint16
[if reliable:]
3       2     seq_num       LE uint16
+N      var   payload

flags_len bit layout:
  bits 13-0 (0x3FFF): total message size (14-bit, max 16383)
  bit 14    (0x4000): ordered
  bit 15    (0x8000): reliable
  (NO fragment bit — type 0x00 does not support fragmentation)
```

### Type 0x01 — ACK (Header Message)

Different format from data messages — NO flags_len field:

```
Offset  Size  Field
------  ----  -----
0       1     type          Always 0x01
1       2     seq_num       LE uint16 — sequence number being acknowledged
3       1     flags         bit 0: is_fragment_ack, bit 1: unused
[if is_fragment_ack:]
4       1     frag_idx      Fragment index being acknowledged
```

Total size: 4 bytes (normal ACK) or 5 bytes (fragment ACK).

### Types 0x02-0x05 — Connection Management

These share the same `[type:1][flags_len:2 LE][seq:2][payload...]` format as type 0x00 (14-bit length, no fragment support). Payload content varies by type:

- **0x02 (Connect)**: `[slot:1][IP:4][name:UTF-16LE+null]` — peer identity
- **0x03 (ConnectAck)**: `[assigned_slot:1]` — slot assignment response
- **0x04 (Boot)**: Kick notification
- **0x05 (Disconnect)**: Graceful shutdown

---

## 4. Reliable Delivery

### Sequence Numbers

Two independent 16-bit sequence counters exist per peer:
- **Counter A**: For types < 0x32 (connection management messages)
- **Counter B**: For types >= 0x32 (game data messages)

Each counter increments independently. The send helper checks the message type and selects the appropriate counter.

### ACK Flow

1. Sender sets `reliable = 1` on the message, assigns next seq_num from the appropriate counter
2. Receiver processes the message, then creates a type 0x01 ACK with the received seq_num
3. Sender maintains a retransmit queue; when ACK arrives, removes the message from the queue
4. If no ACK within timeout, sender retransmits

### Retransmit Strategy

Each message has a retry strategy field (0, 1, or 2):
- **Strategy 0 (fixed)**: Retransmit at fixed intervals using `base_delay`
- **Strategy 1 (additive)**: Increase delay by `delay_factor` each retry
- **Strategy 2 (doubling)**: Double the delay each retry

After 8 retransmissions without ACK, the peer is considered disconnected.

### ACK for Fragments

When acknowledging a fragmented message, the ACK includes the fragment index (5-byte ACK format). Each fragment is ACKed independently.

---

## 5. Fragment Reassembly

### When Fragmentation Occurs

Messages larger than the maximum per-message size (~480 bytes after headers) are split into multiple type 0x32 messages. Fragmentation is only supported for type 0x32, not type 0x00.

### Send-Side Fragmentation

1. If the message fits in `max_size`, send as a single message (no fragmentation)
2. If too large, force `reliable = 1` (fragments are always reliable)
3. Split payload into N chunks, creating N clone messages
4. Each clone gets:
   - `is_fragment = 1`
   - `fragment_index = 0, 1, 2, ...`
5. Fragment 0 additionally carries `total_fragments = N`
6. The fragment flag (bit 13 of flags_len) is set on each fragment's wire header

### Wire Format of Fragments

Fragment 0: `[0x32][flags_len with bit 13 set][seq][frag_idx=0][total_frags=N][payload_chunk_0...]`
Fragment K: `[0x32][flags_len with bit 13 set][seq][frag_idx=K][payload_chunk_K...]`

Note: `total_frags` only appears in fragment 0. Subsequent fragments have only `frag_idx`.

### Receive-Side Reassembly

1. When a message arrives with `is_fragment` set, enter reassembly
2. Allocate a 256-element array indexed by fragment_index
3. Scan the pending message queue for fragments with matching sequence number
4. Place each fragment into the array by its index
5. Check if fragment 0 exists (it carries `total_frags`)
6. If ALL fragments (0 through total_frags-1) collected:
   - Allocate a combined buffer
   - Copy each fragment's payload in index order
   - Replace the message's buffer with the reassembled data
   - Clear the `is_fragment` flag
   - Remove consumed fragments from the queue
7. Deliver the reassembled message to the application layer

### Fragment Detection: NOT a "More Fragments" Bit

There is **no "more fragments follow" flag**. The receiver detects completion by counting collected fragments against the `total_frags` value from fragment 0. If fragment 0 hasn't arrived yet, reassembly waits.

---

## 6. Observed Fragmentation in Checksum Exchange

### Round 2 (ships/*.pyc — ~410 bytes, fragmented into 3 chunks)

```
Fragment 0:  flags_len_hi=0xA1 (reliable + fragment + length bit 8)
             [frag_idx=0][total_frags=3][checksum_data...]
Fragment 1:  flags_len_hi=0xA1
             [frag_idx=1][checksum_data...]
Fragment 2:  flags_len_hi=0xA0 (reliable + fragment, no bit 8)
             [frag_idx=2][checksum_data...]
```

### Round 0xFF (Multiplayer/*.pyc — ~268 bytes, NOT fragmented)

```
Single msg:  flags_len_hi=0x81 (reliable, length=273)
             [no frag_idx][checksum_data...]
```

**Key insight**: Round 0xFF fits in a single message (273 bytes < ~480 byte limit). The `0x81` high byte does NOT indicate fragmentation — it means the total message length has bit 8 set (273 = 0x111, bit 8 = 1).

Previous documentation incorrectly interpreted `0x81` as `reliable(0x80) + more_fragments(0x01)`. The `0x01` is part of the 13-bit length field, not a flag.

---

## 7. Why Small Messages "Accidentally" Parse Correctly

For messages under 256 bytes total, the low byte of flags_len equals the total length (bits 7-0). A parser that reads `totalLen = wire[1]` as a u8 gets the correct length by coincidence:

```
flags_len = 0x801B → low byte = 0x1B = 27 → correct!
flags_len = 0x8111 → low byte = 0x11 = 17 → WRONG (should be 273)
```

The breakpoint is 256 bytes. Messages under 256 bytes work with a u8 length parser. Messages at 256+ bytes require the full u16 LE parser. In practice, the only messages that exceed 256 bytes are large checksum responses (round 2 with many ship files, or round 0xFF).

---

## 8. Corrections to Previous Documentation

| Previous Understanding | Corrected |
|----------------------|-----------|
| Header: `[type:1][totalLen:1][flags:1]` (3 separate bytes) | Header: `[type:1][flags_len:2 LE]` (1 type + 1 uint16) |
| flags byte: 0x80=reliable, 0x20=fragment, 0x01=more_frags | flags_len: bit15=reliable, bit14=ordered, bit13=fragment, bits12-0=length |
| 0x81 = reliable + more_fragments_follow | 0x81 high byte = reliable + length has bit 8 set (total >= 256 bytes) |
| Fragment continuation detected by 0x01 flag | Fragment completion detected by collecting all indices 0..total_frags-1 |
| `totalLen` is always the low byte | `totalLen` is bits 12-0 of the LE uint16 (can exceed 255) |
| Round 0xFF response is fragmented | Round 0xFF fits in a single ~273 byte message (not fragmented) |

---

## 9. Implementation Notes

### Parser Pseudocode

```
pos = 2  // skip peer_id + msg_count
for i in range(msg_count):
    type = packet[pos]
    if type == 0x01:  // ACK
        seq = u16_le(packet[pos+1..pos+3])
        flags = packet[pos+3]
        size = 4
        if flags & 0x01:  // fragment ACK
            frag_idx = packet[pos+4]
            size = 5
    elif type == 0x32:  // game data
        flags_len = u16_le(packet[pos+1..pos+3])
        reliable  = (flags_len >> 15) & 1
        ordered   = (flags_len >> 14) & 1
        fragment  = (flags_len >> 13) & 1
        total_len = flags_len & 0x1FFF
        payload_start = pos + 3
        if reliable:
            seq = u16_le(packet[payload_start..payload_start+2])
            payload_start += 2
        if fragment:
            frag_idx = packet[payload_start]
            payload_start += 1
            if frag_idx == 0:
                total_frags = packet[payload_start]
                payload_start += 1
        payload_len = total_len - (payload_start - pos)
        size = total_len
    else:  // types 0x00, 0x02-0x05
        flags_len = u16_le(packet[pos+1..pos+3])
        reliable  = (flags_len >> 15) & 1
        ordered   = (flags_len >> 14) & 1
        total_len = flags_len & 0x3FFF  // 14-bit for type 0x00
        // ... similar to 0x32 but no fragment support
        size = total_len
    pos += size
```

### Packet Size Budget

Maximum UDP payload for BC: 512 bytes (buffer size). After peer_id (1) + msg_count (1) = 2 bytes header, 510 bytes remain for messages. A single type 0x32 message with reliable delivery uses: 1 (type) + 2 (flags_len) + 2 (seq) = 5 bytes overhead, leaving ~505 bytes for payload. The fragment threshold is approximately 480 bytes (with margin for batching multiple messages).

---

## Related Documents

- [transport-cipher.md](transport-cipher.md) — AlbyRules stream cipher (encryption/decryption)
- [phase1-verified-protocol.md](phase1-verified-protocol.md) — Game-layer opcodes (carried inside type 0x32)
- [checksum-handshake-protocol.md](checksum-handshake-protocol.md) — Checksum exchange that demonstrates fragmentation
- [join-flow.md](join-flow.md) — Complete join sequence showing transport message flow
- [wire-format-audit.md](wire-format-audit.md) — Previous audit (transport section now superseded by this doc)
