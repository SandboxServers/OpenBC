# Fragmented Reliable Message ACK Bug — Behavioral Specification

## Overview

Bridge Commander 1.1 has a confirmed client-side bug where fragmented reliable messages are never acknowledged despite the server sending correct ACKs. The client retransmits all fragments every ~2 seconds for the entire session duration. Non-fragmented reliable messages are acknowledged correctly.

This bug exists in the stock game and affects both the original dedicated server and any reimplementation that follows the stock ACK protocol. It is harmless in practice — the retransmitted fragments are idempotent and the server ignores duplicates — but it wastes bandwidth.

### Reimplementation Guidance

- **Server**: Send per-fragment ACKs as described below. The client bug means fragments will retransmit regardless, but correct ACKs allow a future client fix.
- **Client**: If reimplementing the transport layer, ensure the ACK handler correctly matches fragment ACKs to fragmented retransmit queue entries (see Matching Logic below).
- **Tolerance**: Both client and server should silently ignore duplicate reliable messages (already-seen sequence numbers). The stock game does this.

---

## Transport Layer Reliable Delivery

### Reliable Message Protocol

1. Sender marks a message as reliable (requires ACK)
2. Message is serialized with a sequence number (u16, per-peer, per-type-category counter)
3. Message enters the first-send queue
4. After first transmission, message moves to the retransmit queue (awaiting ACK)
5. If no ACK arrives within the retransmit interval, the message is resent
6. When an ACK arrives matching this message, it is removed from the retransmit queue

Two separate sequence counter pairs exist per peer:
- One pair for "low" message types (connection management, types < 0x32)
- One pair for "high" message types (game data, types >= 0x32)

Each pair has a send counter (incremented when sending) and an expected counter (incremented when receiving).

### Fragment Protocol

When a message exceeds the maximum UDP payload size, it is split into N fragments:

1. The message is forced to reliable (fragments always require ACK)
2. N clones are created, each carrying a slice of the original payload
3. **All N fragments share the same sequence number** (the counter is incremented once, not per fragment)
4. Each fragment has a fragment_index (0 to N-1)
5. Fragment 0 carries the total_fragment_count
6. Each fragment has an is_fragmented flag set to true

On the receive side, fragments are collected by sequence number. When all fragments for a sequence have arrived, they are reassembled into the original message.

### Three Per-Peer Send Queues

Each peer connection maintains three linked-list queues:

| Queue | Purpose |
|-------|---------|
| First-send | New messages awaiting their first transmission |
| Retransmit | Reliable messages that have been sent but not yet ACKed |
| ACK-outbox | ACK messages to send back to the remote peer |

Processing order per send cycle: ACK-outbox first, then retransmit, then first-send.

---

## ACK Message Format

ACK messages are a separate transport message type (type 0x01). They are always unreliable (ACKs are never themselves acknowledged).

```
[type: 0x01]
[seq: u16 LE]           sequence number of the message being acknowledged
[flags: u8]             bit 0: is_fragmented (original message was a fragment)
                        bit 1: is_low_type (original message type was in the low category)
[if is_fragmented:]
  [frag_idx: u8]        fragment index being acknowledged
```

Total size: 4 bytes (non-fragment) or 5 bytes (fragment).

### Per-Fragment ACK Creation

When the receiver gets a reliable message (including individual fragments), it creates an ACK entry:

1. Extract four fields from the incoming message: sequence number, is_fragmented, fragment_index, and type category (low vs high)
2. Search the ACK-outbox for an existing entry matching all four fields (deduplication)
3. If found: refresh the timer (already scheduled)
4. If not found: create a new ACK message with those four fields, append to ACK-outbox

For a 3-fragment reliable message, this creates **three separate ACK entries**, each with a different fragment_index.

---

## ACK Matching Logic

When the sender receives an ACK back, it searches its retransmit queue for a matching entry:

```
For each entry in retransmit queue:
  1. Type category must match:
     ACK.is_low_type must equal (queued_msg.type is in low category)
  2. Sequence number must match:
     ACK.seq must equal queued_msg.seq
  3. Fragment status must be compatible:
     - Both fragmented with same fragment_index → MATCH
     - Both non-fragmented → MATCH
     - One fragmented and one not → NO MATCH
  4. On MATCH: remove entry, stop searching (return immediately)
```

The handler removes **one** entry per ACK and returns. For per-fragment ACKs, each ACK invocation clears the corresponding fragment entry.

---

## Retransmit Behavior

### Timer

Each message in the retransmit queue tracks:
- Current retransmit interval (seconds)
- Last send time
- Retransmit count

When `current_time - last_send_time > retransmit_interval`, the message is retransmitted.

### Backoff Strategy

Three modes are supported per message:
- **Fixed**: Constant interval between retransmissions
- **Linear**: Interval increases linearly with retransmit count
- **Exponential**: Interval doubles each time, clamped to a maximum

### No Maximum Retry Count

There is **no maximum retry count**. Messages retransmit indefinitely until:
- An ACK is received and the entry is removed
- The peer connection times out (last activity exceeds keepalive threshold)
- The peer disconnects

---

## The Stock Bug

### Symptoms

- Client sends a fragmented reliable message (typically 3 fragments)
- All fragments share the same sequence number with flags indicating reliable+fragmented
- Server sends ACK(s) back
- Client continues retransmitting all fragments every ~2 seconds
- Non-fragmented reliable messages in the same session are ACKed correctly

### Root Cause Analysis

Static analysis of the decompiled client code shows all individual paths appear logically correct:

- Fragment creation correctly assigns shared seq, unique frag_idx, is_fragmented=1
- ACK creation generates per-fragment entries with distinct frag_idx values
- ACK serialization includes frag_idx when is_fragmented is set
- ACK deserialization correctly parses frag_idx from the wire
- ACK handler matching uses all four fields including frag_idx

The bug likely involves a runtime interaction between the send and receive paths:

1. **ACK format mismatch**: If the outgoing ACK has `is_fragmented=0` despite acknowledging a fragment, the matching logic rejects it (mixed fragment status = no match). This would cause the client to never clear any fragment entries.

2. **Partial ACK delivery**: If only some of the per-fragment ACKs reach the client (e.g., due to packet batching limits), only those fragments are cleared. The remaining fragments in the retransmit queue trigger retransmission of the entire set.

3. **Retransmit re-entry**: When fragments are retransmitted, they may create duplicate entries in the retransmit queue, causing an ACK to clear one copy while others persist.

### Impact

The bug is **cosmetically annoying but functionally harmless**:

- Retransmitted fragments are duplicates; the receiver ignores them (already-processed sequence numbers are dropped)
- The wasted bandwidth is small (~3 fragments × retransmit interval)
- Game functionality is not affected
- The bug exists in the stock game and has been present since release

### Workaround for Reimplementations

A reimplemented server can work around the client bug by:

1. Simply tolerating the duplicate fragment traffic (recommended — matches stock behavior)
2. If implementing a new client: ensure the ACK handler correctly matches per-fragment ACKs

A reimplemented client should:

1. Verify that received ACKs with `is_fragmented=1` are correctly matched against fragmented retransmit entries using both seq AND frag_idx
2. Ensure the matching logic handles the mixed case (fragmented ACK vs non-fragmented entry and vice versa) by rejecting the match, not by ignoring the fragment flag

---

## Wire Trace Example

### Client sends (3 fragments, seq=2):

```
Fragment 0: [0x32] [flags_len: 0xA1 ...] [seq: 0x00 0x02] [frag_idx: 0x00] [total: 0x03] [payload...]
Fragment 1: [0x32] [flags_len: 0xA1 ...] [seq: 0x00 0x02] [frag_idx: 0x01] [payload...]
Fragment 2: [0x32] [flags_len: 0xA0 ...] [seq: 0x00 0x02] [frag_idx: 0x02] [payload...]
```

flags_len high byte: 0xA1 = reliable(0x80) + fragment(0x20) + length_bit8(0x01)
Last fragment: 0xA0 = reliable(0x80) + fragment(0x20)

### Server should respond (3 per-fragment ACKs):

```
ACK 0: [0x01] [0x00 0x02] [0x01] [0x00]    seq=2, flags=fragmented, frag_idx=0
ACK 1: [0x01] [0x00 0x02] [0x01] [0x01]    seq=2, flags=fragmented, frag_idx=1
ACK 2: [0x01] [0x00 0x02] [0x01] [0x02]    seq=2, flags=fragmented, frag_idx=2
```

### What actually happens:

The client ignores the ACKs and retransmits all 3 fragments approximately every 2 seconds for the duration of the session.
