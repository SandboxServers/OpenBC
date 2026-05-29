# Bug Report: Packet Bundling Missing (1 Message Per Datagram vs Stock's 4-Pass Drain)

**Date**: 2026-05-29
**Severity**: HIGH (architectural — bandwidth efficiency, burst absorption, ack-outbox interaction)
**Status**: NOT FIXED — architectural change requiring per-peer drain state + buffer management redesign.
**Affected Systems**: Outbound transport, ACK throughput, peer bandwidth accounting, reliable retransmit pacing
**Verified Against**: STBC.exe `TGWinsockNetwork_SendOutgoingPackets` at 0x006B55B0 (271 lines, byte-confirmed)

---

## Summary

OpenBC's outbound transport sends **one TGMessage per UDP datagram**. Stock STBC.exe runs
a **4-pass drain algorithm** that packs up to 255 messages into a single 512-byte datagram
per peer per tick, using a strict pass order (priority -> reliable (one-shot) -> unreliable
-> priority retx) and well-defined fragmentation rules.

This is an architectural mismatch with significant behavioral consequences. Wire format
compatibility is preserved at the per-message level — each individual TGMessage blob is
still self-delimiting and parseable. But aggregation, burst absorption, and the
ack-outbox-deadlock interaction all differ.

---

## Symptom

- OpenBC emits many small datagrams (5-20 bytes each) for tick-rate state replication
- Stock emits fewer, larger datagrams (typical mid-game tick: ~109 bytes containing 4
  messages)
- Wire frequency from OpenBC is substantially higher than stock for the same backlog
- Peer bandwidth accounting (peer+0x48 / peer+0x54 at the receiver) miscounts IP+UDP
  header overhead when many small datagrams arrive
- ACK-outbox deadlock pattern (documented in `docs/bugs/ack-outbox-deadlock.md`) is
  amplified — without the one-reliable-per-datagram cap, the deadlock characteristics
  change

---

## Wire Format Compatibility

**Per-message bytes are correct.** Only aggregation differs. Each TGMessage in OpenBC's
output is byte-for-byte parseable by stock receivers. The bug is in the *number* and
*pacing* of UDP datagrams, not in their contents.

A stock receiver parses every inbound datagram independently — it does not require
multi-message datagrams to function. The deadlock and accounting issues come from
secondary effects (peer-level bandwidth tracking, stale-peer detection, ACK pacing).

---

## Evidence

### Stock binary anchors — `TGWinsockNetwork_SendOutgoingPackets` @ 0x006B55B0

**Datagram size budget (BYTE-CONFIRMED)**:

| Field | Value | Address |
|-------|-------|---------|
| MTU (subclass override) | **512 bytes** | 0x006B9C13: `MOV [param_1+0xAC],0x200` |
| Header reservation | **2 bytes** | 0x006B5672: `ADD EAX,0x2` / 0x006B5675: `SUB EBX,0x2` |
| Usable payload | 510 bytes | derived |
| Header layout | `[u8 senderPeerId][u8 messageCount]` | 0x006B5B08-0x006B5B0E |
| Per-datagram message cap | **255** | 0x006B570F, 0x006B5834, 0x006B59D4, 0x006B5AC7 |
| Cipher operates on | `(buf+1, len-1)` | byte 0 stays plaintext for sender lookup |
| Bandwidth-overhead constant | 34 bytes (0x22) | 0x006BAC50 (stats only — NOT on wire) |

**4-pass drain algorithm (BYTE-CONFIRMED)**:

| Pass | Queue | Count field | Iterator | Retx gate | Per-msg break? | Address |
|------|-------|-------------|----------|-----------|----------------|---------|
| 1: Priority (fresh) | peer+0x9C | peer+0xB4 | peer+0xA8 / peer+0xAC | retx < 3 | until cap or end | 0x006B5696-0x006B5740 |
| 2: Reliable (one-shot) | peer+0x80 | peer+0x98 | peer+0x8C / peer+0x90 | none | **unconditional break** | 0x006B5744-0x006B5825 |
| 3: Unreliable (drain) | peer+0x64 | peer+0x7C | peer+0x70 / peer+0x74 | none | until cap or end; dequeue+free each | 0x006B5829-0x006B5A01 |
| 4: Priority retx (stale) | peer+0x9C | peer+0xB4 | peer+0xA8 / peer+0xAC | retx >= 3; free at >= 9 | until cap or end | 0x006B5A25-0x006B5AF4 |

**Pass 2 is one-shot** — the `break` at 0x006B57E5 is unconditional, regardless of
whether the message fit. At most **one reliable message per datagram**. This is the
design intent that makes ACK throughput tractable.

**Pass 4 gate** at 0x006B5A01:
```
(iStack_28 > 0 OR peer+0xBC != 0) AND peer+0xB4 > 0
```

If no messages were packed in Passes 1-3 AND no peer-disconnect flag, Pass 4 is skipped
and nothing gets sent for this peer this tick. **This gate IS the ACK-outbox deadlock**
described in `docs/bugs/ack-outbox-deadlock.md`.

**Fragmentation (decided at queue time, not drain time)**:

- Driver: `TGWinsockNetwork::QueueMessageForPeer` @ 0x006B5080
- Max chunk byte budget: `WSN+0xAC - 100` = **412 bytes** payload per fragment
- Fragmentation impl: `TGBufferStream::Fragment` @ 0x006B8720 (vtable[7])
- Per-fragment overhead: ~5-7 bytes (3 base + 2 seqID + 2 fragment indices)
- On-wire per-fragment: ~415-420 bytes
- The "-100" safety margin accommodates the 2-byte datagram header + ACK envelopes + one
  fresh priority + one reliable + the fragment all coexisting in a 512-byte datagram

### Concrete wire examples (from stock)

- Single small reliable: `[01][01][ body 18 bytes ]` = 20 bytes
- Typical mid-game tick: `[01][04][ ACK(7B) ][ StateUpdate(60B) ][ EventForward(35B) ][ Heartbeat(5B) ]` = 109 bytes
- Fully-stuffed worst case: `[01][N=255][ 510 bytes of mixed messages ]` = 512 bytes

### STBC RE memo

Full evidence chain with all addresses, decompile excerpts, and pseudocode for all 4 passes:

`C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\packet-bundling-validation-20260528.md`

STBC-side doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\networking\packet-bundling.md`
(new doc derived from the memo)

---

## Root Cause

OpenBC's spec did not include a bundling layer. Each `bc_net_send` call results in one
UDP datagram. Stock's transport queue-and-drain pattern (per-peer outbox + tick-driven
4-pass drain) was not modeled.

---

## Fix Plan

### Architectural shape (~200 LoC + design discussion)

1. **Per-peer outbox state**: Each peer needs four queues (priority, reliable,
   unreliable, plus the priority-retx subset). Storage shape:

   ```rust
   struct PeerOutbox {
       priority: VecDeque<QueuedMessage>,    // peer+0x9C in stock
       reliable: VecDeque<QueuedMessage>,    // peer+0x80
       unreliable: VecDeque<QueuedMessage>,  // peer+0x64
       // counts + cursors + lastSentTime per message
   }
   ```

2. **Per-tick drain entry point**: Called from the main transport tick (currently
   `bc_net_tick`). For each connected peer, allocate a 512-byte buffer, reserve 2 bytes,
   run the 4 passes, send if msgCount > 0.

3. **Pass implementation**: Each pass needs to know:
   - Whether to break on first won't-fit (Passes 1, 3, 4) or break unconditionally after
     first non-stale write (Pass 2)
   - Whether to dequeue+free on success (Pass 3 only)
   - Whether to promote-to-reliable on the +0x3a flag (Pass 3 only)
   - retx gate (Pass 1: retx<3, Pass 4: retx>=3, free at retx>=9)

4. **Pass 4 gate must match stock**: `(msgCount > 0 || peer.has_disconnect_flag) &&
   peer.connection_active`. This gate IS the ACK-outbox deadlock — preserving it
   preserves the deadlock pathology too. OpenBC may need to consciously decide whether to
   inherit the bug for parity or fix it (see cross-reference).

5. **Fragmentation must move to queue time**: Currently any per-message fragmentation in
   OpenBC happens implicitly via UDP MTU. Stock fragments at queue time using a 412-byte
   chunk budget. Each fragment becomes a queued sub-message with `msg+0x39/0x38/0x3C`
   bookkeeping bytes. The drain loop then treats fragments as ordinary queued messages.

### Design questions to resolve

1. **Buffer management**: Allocate per-tick or pool? Stock does `NiAlloc(512)` per peer
   per tick and `NiFree` at end.
2. **Promotion semantics**: Pass 3's "drain unreliable, promote to reliable if
   `msg+0x3a != 0`" — what triggers promotion in OpenBC's message model?
3. **One-reliable-per-datagram preservation**: Should OpenBC inherit the one-shot Pass 2
   break? It is load-bearing for the ack-outbox interaction.
4. **Pass 4 gate**: Match stock (preserve deadlock) or fix at the same time? Coordinate
   with `docs/bugs/ack-outbox-deadlock.md`.

---

## Cross-References

- STBC memo: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\packet-bundling-validation-20260528.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\networking\packet-bundling.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\networking\ack-outbox-deadlock.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\networking\fragmented-ack-bug.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\networking\netimmerse-transport-deep-dive.md`
- OpenBC: `../OpenBC/docs/bugs/ack-outbox-deadlock.md` — the Pass-4 gate IS this deadlock
- OpenBC: `../OpenBC/docs/bugs/fragmented-reliable-ack-bug.md` — related fragment ACK bug
