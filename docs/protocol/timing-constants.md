# Protocol Timing Constants

Complete catalog of timing-critical constants in the Bridge Commander multiplayer protocol. An implementation must honor these values to avoid client timeouts, desyncs, or network flooding.

**Clean room statement**: This document describes timing behavior as observable through packet capture analysis, keepalive interval measurement, and retry pattern observation. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Connection Management

| Constant | Value | Notes |
|----------|-------|-------|
| **Keepalive send interval** | **5.0 seconds** | Server sends keepalive to each peer if no other data sent in 5s |
| **Peer timeout** | **45.0 seconds** | Disconnect peer if no data received in 45s (9 missed keepalives) |
| **Stale peer cleanup** | **15.0 seconds** | Drop peer during state replication if stale for 15s |

**Correction**: Some earlier documentation states a "~12 second keepalive interval." The actual keepalive send interval is **5.0 seconds**. The ~12-second observation likely reflects packet captures where keepalives were suppressed because game data was flowing (keepalives are only sent when no other data has been sent to the peer recently).

---

## StateUpdate Timing

| Constant | Value | Notes |
|----------|-------|-------|
| **Update rate** | **Every frame** | No timer — one peer updated per tick via round-robin index rotation |
| **Effective rate** | **~10 Hz per ship** | Result of frame rate (~30fps) divided by peer count |
| **Forced absolute position** | **1.0 seconds** | Full position write every 1.0s (prevents delta drift) |
| **Subsystem budget** | **10 bytes per tick** | Round-robin subsystem health serialization budget |

The StateUpdate rate is NOT a fixed timer. The server sends one StateUpdate per frame, rotating through connected peers. With 3 players at 30fps, each player receives ~10 updates/second.

---

## Reliable Transport

| Constant | Value | Notes |
|----------|-------|-------|
| **Initial retransmit interval** | **1.0 seconds** | First retransmit after 1.0s with no ACK |
| **Retransmit step (additive mode)** | **1.0 seconds** | Interval increases by 1.0s per retry, capped at 5.0s |
| **Max retransmit count** | **9** | Drop message after 9th retry |
| **Max UDP send buffer** | **512 bytes** | Maximum single UDP datagram payload |
| **Max UDP recv buffer** | **32,768 bytes** | Receive buffer size |
| **Unreliable message max age** | **360.0 seconds** | Drop stale unreliable messages after 6 minutes |

### Retransmit Modes

Three retransmit backoff strategies are used for different message types:

| Mode | Behavior | Retry sequence (seconds) |
|------|----------|-------------------------|
| Fixed | Constant 1.0s interval | 1, 1, 1, 1, 1, 1, 1, 1, 1 |
| Additive | +1.0s per retry, cap 5.0s | 1, 2, 3, 4, 5, 5, 5, 5, 5 |
| Exponential | Double each retry (from base) | 1, 1, 3, 5, 9, 17, 33, ... |

**Fixed mode** total time to drop: ~9 seconds (9 retries × 1.0s)
**Additive mode** total time to drop: ~35 seconds (1+2+3+4+5+5+5+5+5)
**Exponential mode** total time to drop: ~75 seconds (grows rapidly)

---

## Collision System

| Constant | Value | Notes |
|----------|-------|-------|
| **Server distance validation** | **26.0 game units** | Reject CollisionEffect if bounding sphere gap ≥ 26.0 |
| **Velocity-squared threshold** | **~1e-7** | Skip collision if both ships below this speed² |
| **Cooldown range** | **0.1 – 1.0 seconds** | Distance-adaptive per-pair cooldown |

See [collision-rate-limiting.md](../game-systems/collision-rate-limiting.md) for the full cooldown algorithm.

---

## Power System

| Constant | Value | Notes |
|----------|-------|-------|
| **Power tick interval** | **1.0 seconds** | Battery recharge + power distribution cycle |

---

## Event Processing

| Constant | Value | Notes |
|----------|-------|-------|
| **Event processing budget** | **0.01 seconds** | Maximum time per frame processing queued game events |

---

## Derived Timing Requirements

These are not protocol constants but practical requirements derived from the constants above:

### Client Must...

| Requirement | Derived From | Consequence of Violation |
|-------------|-------------|------------------------|
| Send data at least every 45s | Peer timeout = 45s | Server disconnects client |
| ACK reliable messages within 9s | Max retransmit count × fixed interval | Server gives up and may disconnect |
| Process StateUpdates at ~10Hz | Server sends at frame rate / peer count | Stale position data, rubber-banding |

### Server Must...

| Requirement | Derived From | Consequence of Violation |
|-------------|-------------|------------------------|
| Send keepalives every 5s (if idle) | Keepalive interval = 5s | Client may timeout server |
| Generate StateUpdates every frame | Round-robin per-tick | Clients lose position sync |
| Write forced absolute position every 1s | Delta drift prevention | Clients accumulate position error |
| Limit UDP datagrams to 512 bytes | Max send buffer | Message may be silently truncated |

---

## Timing Diagram: Normal Operation

```
Time    Server                              Client
0.0s    ─── StateUpdate (unreliable) ───>
0.033s  ─── StateUpdate ───>
0.066s  ─── StateUpdate ───>
 ...    (continues at ~30fps, round-robin)
1.0s    ─── StateUpdate (forced absolute) ──>
 ...
5.0s    ─── Keepalive ───>                  (if no game data sent in 5s)
        <─── Keepalive echo ───
 ...
45.0s   (if no data from client: DISCONNECT)
```

---

## Related Documents

- **[transport-layer.md](transport-layer.md)** — Transport message types, reliable delivery, fragmentation
- **[../network-flows/join-flow.md](../network-flows/join-flow.md)** — Connection timing (connect → gameplay)
- **[../network-flows/disconnect-flow.md](../network-flows/disconnect-flow.md)** — Timeout detection and cleanup
- **[../game-systems/collision-rate-limiting.md](../game-systems/collision-rate-limiting.md)** — Collision cooldown algorithm
- **[../wire-formats/stateupdate-wire-format.md](../wire-formats/stateupdate-wire-format.md)** — StateUpdate field format
