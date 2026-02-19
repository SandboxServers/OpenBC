# ACK-Outbox Deadlock — Long-Session Degradation

Behavioral specification of a stock Bridge Commander 1.1 transport layer bug that causes progressive session degradation. Derived from black-box packet captures with runtime ACK diagnostics (OBSERVE_ONLY instrumentation, 2026-02-19) and protocol analysis.

**Clean room statement**: This document contains no decompiled code, no binary addresses, no internal memory offsets, and no handler tables. All behavior is described from observable wire data and diagnostic hook output.

**Related docs**:
- [fragmented-reliable-ack-bug.md](fragmented-reliable-ack-bug.md) — ACK-outbox accumulation evidence, fragment ACK matching failure
- [disconnect-flow.md](disconnect-flow.md) — Disconnect packet carries stale ACKs
- [transport-layer.md](transport-layer.md) — Transport message types and wire formats

---

## Overview

The stock transport layer's ACK-outbox (the per-peer queue of pending ACK messages to send) has a two-pass processing design with a logic deadlock. ACK entries that have been sent 3 or more times become permanently stuck in the outbox — never sent again, never cleaned up, never freed. This causes three compounding effects over long sessions:

1. **Memory leak** — stuck entries accumulate indefinitely (~76 bytes each)
2. **Game data starvation** — fresh ACK entries consume packet buffer space meant for game data
3. **Dedup search degradation** — every incoming reliable message triggers a linear scan of all stuck entries

The engine has correct bounds checking (no buffer overflow is possible), and the message count cap (255 per packet) is properly enforced. The damage is from resource exhaustion and algorithmic degradation, not memory corruption.

---

## 1. Two-Pass ACK Processing

Each send cycle processes the ACK-outbox in two passes with different retransmit count filters:

### Pass 1: Fresh ACKs (retransmit count < 3)

- Iterates the entire ACK-outbox
- Only serializes entries that have been sent fewer than 3 times
- Increments the retransmit count after each send
- **Does not remove entries** — they remain in the queue after serialization
- Entries with retransmit count >= 3 are silently skipped

### Between Passes: Game Data

After Pass 1, the retransmit queue (reliable messages awaiting ACK) and first-send queue (new messages) are serialized into the same packet buffer.

### Pass 2: Stale ACKs (retransmit count >= 3) — With Cleanup

- **Gate condition**: Pass 2 only executes if at least one message was written by Pass 1 or the game data queues, OR the peer is disconnecting
- Only serializes entries with retransmit count >= 3
- Entries that reach retransmit count 9 are **removed from the queue and freed**
- This is the only cleanup path for ACK-outbox entries

---

## 2. The Deadlock Condition

When all ACK-outbox entries have retransmit count >= 3 AND no game data needs to be sent (retransmit and first-send queues empty):

```
Pass 1: Skips all entries (retransmit count too high)
         → message count stays at 0

Game data: Nothing to send
           → message count stays at 0

Pass 2 gate: message count == 0 AND peer is not disconnecting
           → gate FAILS → Pass 2 does not execute

Result: Entries with retransmit count 3-8 are permanently stuck
```

The entries are in a dead zone:
- Pass 1 won't touch them (retransmit count too high)
- Pass 2 won't run (gate condition not met)
- No other mechanism removes them

### Deadlock Exits

The deadlock resolves when:
1. **New game traffic arrives** — any message in the retransmit or first-send queue causes message count > 0, opening the Pass 2 gate. Stuck entries then get incremented toward retransmit count 9 and eventually cleaned up.
2. **Peer disconnects** — the disconnecting flag opens the Pass 2 gate unconditionally.

During active gameplay (combat, movement, events), new messages flow frequently enough that the gate opens most ticks and entries eventually reach retransmit count 9. During quiet periods (lobby, post-combat lulls), the deadlock persists and entries accumulate.

---

## 3. Observable Behavior

### Diagnostic Evidence (2026-02-19 traces)

Both server and client exhibit identical patterns (OBSERVE_ONLY, zero patches):

**Server ACK-outbox growth**:
```
t+0s (pre-connect):  ackOutQ=2,  retx=1
t+3s (client joins): ackOutQ=4,  retx=1
t+6s (checksums):    ackOutQ=10, retx=3-4
t+9s (settings):     ackOutQ=11, retx=7-8
```

**Client ACK-outbox growth**:
```
t+2s (connected):    ackOutQ=12, retx=3
t+5s (gameplay):     ackOutQ=13, retx=7
```

**HandleACK dispatch pattern** — after the initial handshake, nearly every ACK arrival finds an empty retransmit queue:
```
HandleACK: seq=0x0000 below32=1 | retxQ=0  ← nothing to match
HandleACK: seq=0x0000 below32=0 | retxQ=0  ← nothing to match
HandleACK: seq=0x0001 below32=1 | retxQ=0  ← nothing to match
... (repeats for all stale ACK entries)
```

These are stale ACKs from the remote peer's outbox, arriving at a side whose retransmit queue is already clean. They're processed, find nothing to remove, and are silently discarded.

### Wire-Level Impact

Every outbound UDP packet during active gameplay carries all ACK entries with retransmit count < 3:

```
[peer_id] [msg_count=N]
  [ACK seq=0 flags=0x02]        ← 4 bytes, stale
  [ACK seq=1 flags=0x02]        ← 4 bytes, stale
  [ACK seq=2 flags=0x01 idx=0]  ← 5 bytes, stale fragment ACK
  [ACK seq=2 flags=0x01 idx=1]  ← 5 bytes, stale fragment ACK
  [ACK seq=2 flags=0x01 idx=2]  ← 5 bytes, stale fragment ACK
  ... (more stale ACKs)
  [0x32 actual game data]       ← whatever space remains
```

At 38 stale ACK entries (observed at t+5s), this is ~170 bytes of overhead per packet out of a 512-byte budget.

---

## 4. No Buffer Overflow

The engine correctly prevents buffer overflows:

| Protection | How It Works |
|-----------|--------------|
| Write bounds | Each message serializer receives the remaining buffer space as a parameter and returns 0 (failure) if insufficient |
| Loop termination | All serialization loops break when a message returns 0 |
| Message count cap | All loops break at 255 messages (explicit check, not u8 wrap) |
| Buffer size | 512 bytes total, 510 usable. At 4 bytes per ACK, ~127 entries fill the buffer before the 255 cap |

**A reimplementation does not need to worry about buffer overflow from ACK accumulation.** The stock engine handles this correctly. The damage is from resource exhaustion, not memory corruption.

---

## 5. Three Degradation Effects

### 5.1 Memory Leak

Each stuck ACK entry permanently consumes ~76 bytes (message object + queue node). Entries accumulate as new reliable messages are exchanged throughout the session.

| Session Duration | Estimated Stuck Entries | Memory Leaked |
|------------------|------------------------|---------------|
| 2 minutes | ~13 | ~1 KB |
| 30 minutes | ~600 | ~45 KB |
| 2 hours | ~2,400 | ~180 KB |
| 4 hours | ~6,000 | ~450 KB |

Not catastrophic on modern systems, but meaningful in a 32-bit address space with 2002-era memory budgets.

### 5.2 Game Data Starvation

While ACK entries have retransmit count < 3, they consume space in the 512-byte packet buffer. Each ACK is 4-5 bytes. With 38 active stale ACKs:
- **170 bytes consumed** by stale ACKs
- **340 bytes remaining** for game data (StateUpdates, weapon fire, collision effects)
- Critical game messages may be deferred to later ticks during burst scenarios (ship explosions, multiple subsystem damage)

This effect is transient per entry (entries stop consuming buffer space once they reach retransmit count 3 and enter the dead zone), but new entries are constantly being created.

### 5.3 Dedup Search Degradation (Most Dangerous)

Every incoming reliable message triggers a deduplication scan of the **entire** ACK-outbox. The scan is linear (O(N)), checking 4 fields per entry. Stuck entries are included in this scan even though they'll never match new incoming sequence numbers.

| Session Duration | Queue Size | Cost per Reliable Message |
|------------------|------------|---------------------------|
| 2 minutes | ~13 | 13 comparisons (negligible) |
| 30 minutes | ~600 | 600 comparisons |
| 2 hours | ~2,400 | 2,400 comparisons |
| 4 hours | ~6,000 | 6,000 comparisons |

At typical combat rates of ~60 reliable messages per second: **2,400 entries × 60 msgs/sec = 144,000 four-field comparisons per second**. This runs inside the network receive tick. As the cost grows, the network tick takes longer, packets queue up, and the session progressively degrades.

The most likely crash vector for long sessions is the dedup scan eventually making the network tick too slow to keep up with incoming traffic, causing cascading timeouts and peer disconnects.

---

## 6. Reimplementation Guidance

### The Simple Fix

Remove ACK entries from the outbox after they have been sent N times (3 is reasonable). Do not use a two-pass design with a gate condition. A single pass with a retransmit limit is sufficient:

```
for each entry in ack_outbox:
    if timer_expired(entry):
        serialize(entry, buffer)
        entry.retransmit_count++
        if entry.retransmit_count >= MAX_ACK_RETRANSMITS:  // e.g., 3
            remove(entry)
            free(entry)
```

This eliminates all three degradation effects:
- No memory leak (entries freed after 3 sends)
- No starvation (outbox stays small)
- No dedup degradation (scan is always over a small set)

### Compatibility

Stock clients are completely tolerant of not receiving stale ACKs. The retransmit queue on the stock client is empty (retxQ=0) when stale ACKs arrive — they were already cleared. Stopping the stale ACK flow changes nothing from the client's perspective.

### Dedup Optimization

If keeping the dedup scan, consider using a hash map instead of a linear list. Key on `{seq, is_low_type, is_fragmented, frag_idx}` — the same 4 fields used for the linear scan. This makes dedup O(1) regardless of queue size.

### Don't Replicate the Two-Pass Design

The stock two-pass design (fresh ACKs in Pass 1, stale ACKs in Pass 2 with a gate) serves no useful purpose. It was likely intended to prioritize fresh ACKs over stale ones, but the gate condition bug means stale ACKs are never cleaned up during quiet periods. A single-pass design with a hard retransmit limit is simpler and correct.

---

## Summary

| Property | Stock Behavior | Recommended Fix |
|----------|---------------|-----------------|
| ACK-outbox cleanup | Two-pass with deadlock; entries stuck at retx 3-8 | Single pass, remove after N sends |
| Memory leak | ~76 bytes/entry, unbounded growth | Entries freed after N sends |
| Buffer overflow | Not possible (bounds checked) | N/A |
| Game data starvation | ~170 bytes/packet during active phase | Small outbox = minimal overhead |
| Dedup scan | O(N) linear, N grows unboundedly | Hash map or bounded list |
| Long-session crash risk | Progressive degradation from dedup cost | Eliminated by bounded outbox |
| Compatibility impact | None — stock clients ignore stale ACKs | Safe to fix server-side |
