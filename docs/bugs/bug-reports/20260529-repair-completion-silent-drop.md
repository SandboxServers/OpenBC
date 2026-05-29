# Bug Report: REPAIR_COMPLETED + REPAIR_CANNOT_BE_COMPLETED Silently Dropped

**Date**: 2026-05-29
**Severity**: HIGH (playability — stock clients have stuck Engineering panel state)
**Status**: NOT FIXED — emission is silently absent from `bc_repair_tick`.
**Affected Systems**: Repair subsystem tick, PythonEvent (0x06) emission, Engineering UI state on stock clients
**Verified Against**: STBC.exe live decompile (FUN_005652A0, FUN_006A1150), Pass 1 host-event-emission catalog 2026-05-29

---

## Summary

OpenBC's `bc_repair_tick` (in `src/shared/game/combat.c`) silently removes completed
or destroyed repair-queue entries without emitting the corresponding PythonEvent
(0x06) notifications. Stock STBC.exe emits two distinct events from its repair tick
that DO reach the wire as opcode 0x06 PythonEvent to the `NoMe` group (reliable):

- **REPAIR_COMPLETED (event id 0x00800074)** — when a queued subsystem's
  `cur_condition / max_condition >= 1.0f`.
- **REPAIR_CANNOT_BE_COMPLETED (event id 0x00800075)** — when a queued subsystem's
  `cur_condition <= 0.0f`, emitted at TWO sites in stock (one per failure branch).

Both are TGObjPtrEvent (factory 0x010C). Both are gated host-only via the
HostEventHandler subscription installed at `MultiplayerGame_Ctor @ 0x0069E590` under
`DAT_0097fa8a != 0` (IS_MULTIPLAYER). Both are emitted by stock and consumed by stock
clients to update their Engineering panel UI.

OpenBC currently emits **neither**.

---

## Symptom

Stock clients connected to an OpenBC server have stuck Engineering panel state:

- When the host's simulation completes repair of a subsystem (cur HP reaches max HP),
  the stock client's Engineering panel never receives the REPAIR_COMPLETED
  PythonEvent. The repair-list UI continues displaying the subsystem as if it is
  still being repaired. Stock's local handler for REPAIR_COMPLETED is what removes
  the entry from the client's display list and triggers the "repair finished" sound
  cue.

- When the host's simulation observes a queued subsystem reach 0 HP mid-repair
  (e.g., taking further damage while waiting in the queue), the stock client never
  receives the REPAIR_CANNOT_BE_COMPLETED PythonEvent. The client's UI does not move
  the subsystem to the "destroyed" display area; it stays in the queue display.

Both UI symptoms are observable on stock clients connected to an OpenBC host where
the repair simulation is producing the underlying state changes correctly (StateUpdate
0x1C transports the subsystem HP). The wire-event notifications that drive UI state
transitions are missing.

---

## Evidence

### Stock binary anchors (Pass 1, byte-anchored)

**Function**: `RepairSubsystem::Update @ 0x005652A0`

| Emission site | Address | Event ID | Branch |
|---------------|---------|----------|--------|
| Success | 0x00565447 | 0x00800074 REPAIR_COMPLETED | `(curCondition / maxCondition) >= 1.0f` |
| Failure (in-queue) | 0x005653A4 | 0x00800075 REPAIR_CANNOT_BE_COMPLETED | `curCondition <= 0.0f` during queue walk |
| Failure (post-queue scan) | 0x005654E0 | 0x00800075 REPAIR_CANNOT_BE_COMPLETED | `curCondition <= 0.0f` post-scan branch |

**Event class**: TGObjPtrEvent, factory 0x010C.

**Routing**: Both event IDs are subscribed at `MultiplayerGame_Ctor @ 0x0069E590` to
MultiplayerGame's HostEventHandler vtable slot:
```
FUN_006db380(&DAT_00800074, this, "MultiplayerGame::HostEventHandler", 1, 1, ...);
FUN_006db380(&DAT_00800075, this, "MultiplayerGame::HostEventHandler", 1, 1, ...);
```
Subscription block is gated `DAT_0097fa8a != '\0'` (IS_MULTIPLAYER). The resolved
handler `FUN_006A1150` serializes the event via vtable[0x34] into a TGBufferStream
prefixed with byte 0x06, wraps in a TGMessage, sets the reliable flag, and calls
`TGWinsockNetwork_SendTGMessageToGroup(this, &DAT_008e5528 "NoMe", pMessage)`.

### OpenBC current code

`src/shared/game/combat.c:808-871` (`bc_repair_tick`):

```c
/* Skip destroyed subsystems (0 HP) but keep in queue */
if (ship->subsystem_hp[ss_idx] <= 0.0f) continue;

f32 complexity = cls->subsystems[ss_idx].repair_complexity;
if (complexity <= 0.0f) complexity = 1.0f;
f32 gain = per_sub / complexity;

f32 max_hp = cls->subsystems[ss_idx].max_condition;
ship->subsystem_hp[ss_idx] += gain;
if (ship->subsystem_hp[ss_idx] >= max_hp) {
    ship->subsystem_hp[ss_idx] = max_hp;
    /* Mark for removal (defer to avoid modifying while iterating) */
    ship->repair_queue[q] = 0xFF; /* sentinel */
    repaired++;
}
```

Both transitions occur with **no event emission**:

- Completion: subsystem HP reaches `max_hp`, the entry is sentineled for removal in
  the compaction pass. No REPAIR_COMPLETED event.
- Destruction-while-queued: the destroyed branch is currently a `continue` — the entry
  is not removed AND no REPAIR_CANNOT_BE_COMPLETED event is emitted. Stock both
  emits the event AND removes the entry; OpenBC does neither.

### STBC RE memo

`C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\host-event-emission-catalog-20260529.md`
— see Section 4 ("Repair Completion Verdict (DEFINITIVE)") for the binary-truth
verdict that REPAIR_COMPLETED + REPAIR_CANNOT_BE_COMPLETED **DO** go on the wire as
opcode 0x06 PythonEvent NoMe. This resolves the long-standing OpenBC memo
contradiction where an earlier hypothesis suggested these were local-only.

---

## Root Cause

`bc_repair_tick` was implemented around the queue mutation but never wired up to the
PythonEvent emission path. There is no `bc_emit_repair_completed` or
`bc_emit_repair_cannot_be_completed` helper, no `Forward`-group emit, and no caller
in the tick to invoke them.

The OpenBC server-dispatch layer DOES have the opcode 0x06 emission machinery in
place for the existing `ADD_TO_REPAIR_LIST` (0x008000DF) path under #85. The same
serializer + sender plumbing applies for the two REPAIR_* events — only the call sites
inside `bc_repair_tick` are missing.

---

## Affected Files

- `src/shared/game/combat.c` — `bc_repair_tick` needs emission call sites
- (Likely also) `src/server/server_dispatch.c` — builder for the TGObjPtrEvent variant
  of opcode 0x06 if the existing builder doesn't cover the +4-byte obj_ptr extension
- (Possibly) `src/shared/protocol/event_factory.c` (or equivalent) — TGObjPtrEvent
  serializer if not already present

---

## Fix Plan

### Fix shape

Estimated ~50 LoC across `bc_repair_tick` + builder.

```c
/* In bc_repair_tick, replace the destroyed-skip block: */

/* Detect destroyed-while-queued: emit REPAIR_CANNOT_BE_COMPLETED, then drop */
if (ship->subsystem_hp[ss_idx] <= 0.0f) {
    if (ship->is_host_in_mp) {
        bc_emit_repair_cannot_be_completed(ship, ss_idx);
    }
    ship->repair_queue[q] = 0xFF; /* mark for removal */
    /* Don't increment repaired counter — this isn't a successful repair */
    /* But we DO need a separate "dropped" counter so the compaction below
       handles both cases */
    dropped++;
    continue;
}

/* ...repair progress... */

if (ship->subsystem_hp[ss_idx] >= max_hp) {
    ship->subsystem_hp[ss_idx] = max_hp;
    if (ship->is_host_in_mp) {
        bc_emit_repair_completed(ship, ss_idx);
    }
    ship->repair_queue[q] = 0xFF;
    repaired++;
}
```

The compaction loop already removes 0xFF-sentineled entries.

### TGObjPtrEvent builder

Both events use TGObjPtrEvent (factory 0x010C) wrapped in PythonEvent opcode 0x06.
Per Pass 1, the wire shape is approximately:

```
[0x06]                       opcode
[varint factory_id = 0x010C] TGObjPtrEvent factory
[u32 source_obj_id]          ship's network object ID
[u32 event_type]             0x00800074 or 0x00800075
[u32 obj_ptr]                subsystem's network object ID
```

> **Verification needed before shipping**: Pass 1 reports the TGObjPtrEvent extension
> size as +4 bytes (the obj_ptr field). A mid-batch RE memo briefly reported +1 byte.
> The canonical wire-format doc at
> `docs/wire-formats/tgobjptrevent-wire-format.md` should be cross-checked before
> the builder lands. The factory class (0x010C) is confirmed.

Target group: `NoMe`. Reliable flag set.

### Gating

Host-only in multiplayer. Match the stock gate (`DAT_0097fa8a != 0`). In OpenBC
terms: `ship->is_host_in_mp` or whatever the equivalent server-side flag is.

### Acceptance criteria

1. Stock client connected to OpenBC host sees REPAIR_COMPLETED PythonEvent on the
   wire when a queued subsystem finishes repair; client's Engineering panel removes
   the entry and plays the repair-finished cue.
2. Stock client sees REPAIR_CANNOT_BE_COMPLETED when a queued subsystem reaches 0 HP
   mid-repair; client's UI moves the entry to the destroyed display area.
3. Wire trace matches stock byte-for-byte for the TGObjPtrEvent payload (mod the
   subsystem ID values, which are session-specific).
4. No double-emission (the event fires once per state transition per subsystem).

---

## Cross-References

- **Pass 1 memo (source of truth)**:
  `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\host-event-emission-catalog-20260529.md`
  (Section 4: "Repair Completion Verdict (DEFINITIVE)")
- Repair-batch memo (TGObjPtrEvent factory + obj_ptr extension):
  `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\tgobjptrevent-validation-20260528.md`
- OpenBC doc: `docs/game-systems/repair-system.md` (Path 1, Section 6)
- OpenBC doc: `docs/wire-formats/tgobjptrevent-wire-format.md`
- Related: `20260529-per-handler-relay-cascade.md` — Phase 2 of that fix plan depends
  on this emission landing first
- Related: PR/issue #85 — host-side ADD_TO_REPAIR_LIST (0x008000DF) emission (partial)
