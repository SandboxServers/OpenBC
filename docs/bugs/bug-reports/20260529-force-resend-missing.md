# Bug Report: StateUpdate Force-Resend (1.0s) Missing

**Date**: 2026-05-29
**Severity**: MEDIUM (drift accumulation if dirty-flag-only emission is dropped)
**Status**: NOT FIXED — surgical addition: ~30 LoC in main tick loop.
**Affected Systems**: StateUpdate (opcode 0x1C) emission, position anchor resync
**Verified Against**: STBC.exe live decompile (Ship__WriteStateUpdate at 0x005B17F0), .rdata constant at 0x00888860

---

## Summary

Stock STBC.exe force-resends a full StateUpdate **every 1.0 second** per ship per peer,
regardless of dirty-flag state. OpenBC only emits StateUpdate when dirty flags are
non-zero. If a StateUpdate is dropped on the wire (UDP unreliable) AND no further state
changes happen for an extended interval, OpenBC clients can drift indefinitely from the
authoritative state — there is no resync trigger.

---

## Symptom

- Client and server position/orientation/state drift accumulates if dirty-flag-driven
  emissions are lost
- A ship at rest (no dirty flags) emits nothing — its last known position on remote
  clients gradually becomes stale (relative to other server-driven references like
  collision frames and respawn positions)
- After a packet drop sequence, no recovery emission triggers — drift persists until the
  next dirty-flag-emission (which could be many seconds away in low-activity scenarios)

---

## Evidence

### Stock binary anchors

| Item | Value | Address |
|------|-------|---------|
| Force-resend interval | **1.0 second** | `_DAT_00888860 = 0x3F800000 = 1.0f` at 0x00888860 |
| Function | `Ship__WriteStateUpdate` | 0x005B17F0 |
| Gate (full force-resend) | `gameTime - tracker+0x4 > 1.0s` -> sets bForceResendPos flag | 0x005B17F0 |
| Gate (position-anchor force) | `gameTime - tracker+0x24 > 1.0s` -> position-anchor force | 0x005B17F0 |

The tracker structure has TWO timestamps gated by this same 1.0s constant:
- `tracker+0x4` — last full force-resend (drives bForceResendPos)
- `tracker+0x24` — last position-anchor force (separate from full resend)

Both reset to current `gameTime` when their respective forced emission fires. The
behavior is essentially "emit a full state every 1.0s no matter what."

### Tick architecture context

Stock emits StateUpdate per ship per peer **every main tick** (~30 Hz on dedi via the
proxy WM_TIMER, ~60-200 Hz on stock client) — but dirty-flag suppression normally keeps
most ticks no-op when the ship state hasn't changed. The 1.0s force-resend is the
recovery mechanism that overrides dirty-flag suppression.

```
Every main tick:
    For each ship × peer:
        If (gameTime - tracker.lastForceResend > 1.0s):
            bForceResendPos = true                    ; force full resend
            tracker.lastForceResend = gameTime
        if (dirty_flags || bForceResendPos):
            emit StateUpdate (opcode 0x1C)
```

When `bForceResendPos` is set, the emission includes the full state regardless of
individual dirty-bit values — effectively a periodic full sync.

### STBC RE memo

`C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\tick-rate-inventory-validation-20260528.md`

The memo's "Rate-Limit Gates Discovered" table lists `_DAT_00888860 = 1.0f` and identifies
both `tracker+0x4` (full force-resend) and `tracker+0x24` (position anchor) gates at
0x005B17F0.

---

## Root Cause

OpenBC's StateUpdate emission in `src/server/main.c` (around lines 900-944) is purely
dirty-flag-driven:

```c
// Current OpenBC approximate
for (each peer) {
    for (each ship) {
        u8 dirty_flags = bc_ship_compute_dirty_flags(ship, peer);
        if (dirty_flags != 0) {
            bc_send_state_update(ship, peer, dirty_flags);
        }
        // No force-resend path — if dirty_flags == 0, nothing is sent
    }
}
```

There is no per-ship per-peer `last_state_update_emit` tracking and no force-emit
override when the 1.0s threshold has elapsed.

---

## Affected Files

- `src/server/main.c` — StateUpdate broadcast section (lines ~900-944)
- per-peer-per-ship state struct (whichever currently tracks dirty flags)

---

## Fix Plan

### Per-ship per-peer timestamp

Add a `last_state_update_emit: f32` field to the per-(ship, peer) tracker. Already-
existing OpenBC state-tracker infrastructure should have a place for this — it's the
same kind of tracker that holds dirty flags.

### Force-emit gate

In the StateUpdate emission loop, after computing dirty flags but before the
emit-or-skip decision:

```c
// In src/server/main.c, StateUpdate broadcast section
f32 now = bc_clock_now();
for (each peer) {
    for (each ship) {
        u8 dirty_flags = bc_ship_compute_dirty_flags(ship, peer);
        bool force_resend = (now - tracker->last_state_update_emit) >= 1.0f;

        if (dirty_flags != 0 || force_resend) {
            if (force_resend) {
                // Override dirty_flags to include the position/anchor bits
                // (or pass a force-resend flag through to bc_send_state_update)
                dirty_flags |= BC_STATE_UPDATE_FORCE_POS;
            }
            bc_send_state_update(ship, peer, dirty_flags);
            tracker->last_state_update_emit = now;
        }
    }
}
```

(Exact dirty-flag bit semantics depend on OpenBC's existing StateUpdate writer — the
principle is: at 1.0s intervals, emit a full-position StateUpdate regardless of
dirty-bit state. Match stock's "force position resend" behavior.)

### Scope: ~30 LoC

- Add `f32 last_state_update_emit;` field to the tracker
- Add the force-resend computation + override in the emission loop
- Initialize the timestamp at connect/spawn so the first emission happens promptly
  (not 1.0s after spawn)

### Acceptance criteria

1. Each (ship × peer) emits at least one StateUpdate per 1.0s wall-clock interval, even
   when dirty flags are otherwise zero
2. Force-resends are at the "full position" granularity (not just dirty-bit-set)
3. Drift recovery: after a simulated packet drop, position resync occurs within ~1.0s
4. No emission count regression on busy-state scenarios (dirty-driven emissions still
   suppress force-emit when overlapping)

---

## Cross-References

- STBC memo: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\tick-rate-inventory-validation-20260528.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\protocol\stateupdate.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\architecture\main-loop-timing.md`
- Related bug: `20260226-stateupdate-authority-and-cadence-gap.md` — broader StateUpdate authority/cadence issues
- Related bug: `20260529-per-system-tick-gates-missing.md` — per-system tick gates (same general cadence theme)
