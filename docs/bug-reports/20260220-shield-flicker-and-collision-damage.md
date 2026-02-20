# Bug Report: Shield Flickering & Collision Damage Not Applied

**Date**: 2026-02-20
**Log file**: `build/openbc-20260220-130201.log`
**Session**: 22 seconds active gameplay (Cady2, Sovereign class, 1 player)
**Server**: OpenBC build

---

## Issue 1: Shield/Subsystem UI Flickering

### Symptom

The Shields UI on the client flickers — shield facing values, hull, and other subsystem displays fluctuate rapidly between correct and incorrect values.

### Root Cause: Round-Robin startIndex Cycle Mismatch

The server sends StateUpdate flag 0x20 (subsystem health) using a round-robin serializer. Each packet contains a `start_index` byte that tells the client which subsystem position the data begins at.

**Observed server round-robin cycle** (from S→C StateUpdate packets with flag 0x20):

```
Time        startIdx   Payload
23.500s     0x00       [FF FF FF 20 FF FD FF FF FF FF 20]
24.234s     0x05       [FF FF FF FF FF FF FF 20 FF 20]
24.968s     0x07       [FF FF FF FF FF FF FF FF FF FF 20]
25.656s     0x09       [FF FF FF FF FF 20 FF FF FF 20]
26.375s     0x00       [FF FF FF 20 FF FD FD FF FF FF 20]  ← cycle restarts
27.093s     0x05       ...
27.843s     0x07       ...
28.562s     0x09       ...
29.265s     0x00       ...  ← cycle restarts again
```

**Server cycle**: `0 → 5 → 7 → 9 → 0 → 5 → 7 → 9 → ...`

**Expected Sovereign cycle** (from [ship-subsystems.md](../ship-subsystems.md)): The Sovereign has 11 top-level subsystems. The round-robin should visit indices 0 through 10, cycling based on the 10-byte budget. The stock game cycle for Sovereign is `0 → 2 → 6 → 8 → 10 → 0` (verified from stock dedi packet captures).

**Key mismatch**: The server's round-robin visits indices `{0, 5, 7, 9}`, but the stock game visits `{0, 2, 6, 8, 10}`. This means:

1. **The subsystem list order on the server differs from the client.** Both sides have 11 subsystems, but they're in a different sequence. When the server sends data at startIndex=5, the client reads it as subsystem #5 in *its* order, but the server wrote subsystem #5 from *its* order — which is a completely different subsystem.

2. **The budget consumption differs.** The server hits budget boundaries at different positions because the subsystem byte sizes (determined by WriteState format and child count) don't align. The Sovereign's subsystems have vastly different byte sizes:
   - Hull: 1 byte (Base, no children)
   - Shield Generator: 1 byte (Base, no children)
   - Sensor Array: 3 bytes (Powered, no children)
   - Warp Core: 3 bytes (Power format)
   - Impulse Engines: 5 bytes (Powered, 2 children)
   - Torpedoes: 9 bytes (Powered, 6 children)
   - Repair: 3 bytes (Powered, no children)
   - Bridge: 1 byte (Base, no children)
   - Phasers: 11 bytes (Powered, 8 children)
   - Tractors: 7 bytes (Powered, 4 children)
   - Warp Engines: 5 bytes (Powered, 2 children)

   Different ordering means different bytes per round-robin window, producing different startIndex sequences.

### Effect on Client

The client interprets power allocation bytes as health bytes, hull bytes as shield bytes, etc. This causes values to swing wildly between 0% and 100% or display impossible percentages, appearing as "flickering" on the Shields and other HUD panels.

### Decoding the startIndex=0 Packet

Server sends at t=23.500s:
```
[20] 00 FF FF FF 20 FF FD FF FF FF FF 20
      ^startIdx
```

After the startIndex byte `00`, the data is:
- `FF` = first subsystem condition byte (0xFF = 100%)
- `FF` = second subsystem condition byte (0xFF = 100%)
- `FF 20` = this is where the bit-packing for Powered subsystems starts (`0x20` = has_power_data=false)

The server writes 6 subsystems in this window (indices 0-4 plus starting #5), hitting the budget at index 5. That matches 1+1+3+3+1=9 bytes if the order were Hull(1)+Shield(1)+Sensor(3)+WarpCore(3)+something(1), but the data doesn't decode cleanly to the expected Sovereign order, confirming the list mismatch.

### Fix Required

The server must construct its subsystem serialization list in the **exact same order** as the client. Both are determined by the hardpoint script's `LoadPropertySet()` call sequence. The server must use identical property loading order when setting up the ship's subsystem list.

Reference: [ship-subsystems.md](../ship-subsystems.md) §2 "Subsystem List Order" and §7 "Per-Ship Serialization Lists" for the correct order per ship class.

---

## Issue 2: Collision Damage Not Applied to Client Ship

### Symptom

Client rams an asteroid. The server logs the collision and acknowledges receiving the CollisionEffect (0x15) message. But the client's hull and subsystem health never change — no damage is taken, the ship cannot die from collisions.

### Evidence from Log

Two collisions occurred during the session:

**Collision 1** at t=30.328s:
```
RECV: [15 24 81 00 00 50 00 80 00 00 00 00 00 FF FF FF 3F 01 FB 7E 00 D8 4D D6 8E 44]
```
Decoded:
- opcode=0x15, event_class=0x00008124, event_code=0x00800050
- source_obj=0x00000000 (environment/asteroid)
- target_obj=0x3FFFFFFF (player 0 base = the connected player)
- contact_count=1, contact=(+251,+126,+0,+216)
- collision_force=last 4 bytes = `4D D6 8E 44` → not a standard IEEE754 position

Wait — the doc says force is the last 4 bytes. Let me re-parse:
```
15                      opcode
24 81 00 00             event_class = 0x00008124
50 00 80 00             event_code = 0x00800050
00 00 00 00             source_obj = 0 (environment)
FF FF FF 3F             target_obj = 0x3FFFFFFF
01                      contact_count = 1
FB 7E 00 D8             contact[0]
4D D6 8E 44             force = IEEE754 = 1142.70 (approx)
```

Total = 22 + 1*4 = 26 bytes. ✓ matches `len=26`.

**Collision 2** at t=36.343s:
```
[15 24 81 00 00 50 00 80 00 00 00 00 00 FF FF FF 3F 01 14 7D FE DA 89 C4 F2 43]
```
Same structure, force = `89 C4 F2 43` → ~485.5.

### Server Response

After each collision, the server:
1. ✅ ACKs the reliable message (seq=7, seq=8)
2. ✅ Logs `Collision: Cady2 took 0.5 damage (source=environment)`
3. ✅ Sends a flag 0x20 StateUpdate (round-robin health data) in the same flush
4. ❌ Does **NOT** send any PythonEvent (0x06) messages
5. ❌ Does **NOT** send Explosion (0x29) messages

### Expected Behavior (from [collision-damage-event-chain.md](../collision-damage-event-chain.md))

When the host processes a CollisionEffect (0x15) from a client, the stock game generates approximately **14 PythonEvent (opcode 0x06) messages**:

```
Client sends 0x15 (CollisionEffect)
    ↓
Host validates collision (ownership, proximity)
    ↓
Host applies per-contact damage to subsystems
    ↓
For each damaged subsystem:
    → condition decreases
    → SUBSYSTEM_HIT event posted
    → Repair subsystem adds to repair queue
    → ADD_TO_REPAIR_LIST event posted (host+MP gate)
    → HostEventHandler serializes as PythonEvent (0x06)
    → Sent reliably to "NoMe" group (all peers)
    ↓
~7 subsystems × 2 ships = ~14 PythonEvent messages
```

In this session (single player vs environment), only one ship takes damage, so we'd expect ~7 PythonEvent messages per collision.

### What's Missing

**Zero PythonEvent (0x06) messages were sent during the entire session.** The server correctly receives and logs the collision, and even notes "took 0.5 damage", but the full collision-damage event chain never fires:

1. The server doesn't decompress contact points to identify which subsystems are hit
2. No per-subsystem SetCondition calls fire
3. No SUBSYSTEM_HIT events are posted
4. No ADD_TO_REPAIR_LIST events are posted
5. No PythonEvent (0x06) messages are serialized and sent

The `0.5 damage` log line suggests the server applies a flat damage value rather than processing the collision through the full per-contact, per-subsystem pipeline described in the collision-damage-event-chain doc.

### Additionally: Subsystem Health Never Changes in StateUpdate

Looking at the flag 0x20 packets sent S→C after the collision, all subsystem health bytes remain `0xFF` (100%). The collision has no effect on the server's internal ship state. Compare:

**Before collision** (t=26.375s):
```
startIdx=0: [FF FF FF 20 FF FD FD FF FF FF 20]
```

**After collision** (t=29.265s):
```
startIdx=0: [FF FF FF 20 FF FC FC FF FF FF 20]
```

The `FD→FC` changes are in what appears to be battery/power bytes (Warp Core Format 3 fields), not health. No condition bytes drop from 0xFF, confirming the server never applies collision damage to subsystems.

### Fix Required

The server must implement the full collision damage pipeline:

1. **Parse the CollisionEffect message** per the wire format in [collision-effect-wire-format.md](../collision-effect-wire-format.md)
2. **Decompress contact points** to ship-relative Vec3 positions
3. **Identify affected subsystems** by proximity to each contact point
4. **Apply damage** to each affected subsystem: `damage_per_subsystem = collision_force / contact_count` (clamped to 0.5 max per contact)
5. **Update subsystem condition** (health), which triggers the event chain
6. **Generate PythonEvent (0x06) messages** for each damaged subsystem (ADD_TO_REPAIR_LIST)
7. **Send reliably** to all connected peers

The full expected chain is documented in [collision-damage-event-chain.md](../collision-damage-event-chain.md).

---

## Issue 3: Fragment ACK Format (Possible Contributing Factor)

### Observation

At t=30.718s and t=30.734s, the client sends a 3-fragment reliable message (checksum response, seq=0x0200). The server responds with:

```
[0] ACK seq=2 flags=0x00
[1] ACK seq=2 flags=0x00
[2] ACK seq=2 flags=0x00
```

Three ACKs all with `flags=0x00` (non-fragmented). The stock game sends per-fragment ACKs with `is_fragmented=1` and the correct `frag_idx` in each ACK.

The client's retransmit queue matching logic requires the ACK's fragment flag and index to match the outgoing entry. With `flags=0x00`, the match fails and retransmit queue entries are never drained.

This is documented in [fragmented-ack-bug.md](../fragmented-ack-bug.md) — the ACK flag byte must indicate fragmented=1 and carry the correct fragment index for the client to clear its retransmit queue.

### Impact

While this doesn't directly cause the shield flickering or missing collision damage, it means the client's retransmit queue grows without bound. Over longer sessions this would cause:
- Memory growth from undrained queue entries
- Unnecessary retransmissions consuming bandwidth
- Potential stalls if the retransmit queue reaches capacity

---

## Summary Table

| Issue | Severity | Root Cause | Fix Complexity |
|-------|----------|-----------|----------------|
| Shield flickering | **HIGH** | Subsystem list order mismatch between server and client | Moderate — must match LoadPropertySet order |
| No collision damage | **HIGH** | Server logs collision but doesn't run damage pipeline or generate PythonEvent (0x06) messages | Moderate — implement collision→damage→event chain |
| Fragment ACK format | **MEDIUM** | ACKs for fragmented messages missing fragment flag/index | Low — set is_fragmented=1 and correct frag_idx in ACK |

## Reference Documents

- [ship-subsystems.md](../ship-subsystems.md) — Correct subsystem order per ship class
- [stateupdate-wire-format.md](../stateupdate-wire-format.md) — Flag 0x20 round-robin format
- [collision-effect-wire-format.md](../collision-effect-wire-format.md) — Opcode 0x15 parsing
- [collision-damage-event-chain.md](../collision-damage-event-chain.md) — Expected PythonEvent generation chain
- [collision-detection-system.md](../collision-detection-system.md) — Collision detection pipeline (client-side)
- [combat-system.md](../combat-system.md) — Full damage pipeline
