# Bug Report: ShieldDelay (1.0s) Window During Cloak Transitions Missing

**Date**: 2026-05-29
**Severity**: MEDIUM (combat balance — cloak vulnerability/grace windows absent)
**Status**: NOT FIXED — structural feature addition: needs new timer field + event scheduling on cloak transitions.
**Affected Systems**: CloakingSubsystem, ShieldGenerator, combat damage gate, StateUpdate cloak/shield serialization
**Verified Against**: STBC.exe live decompile (CloakingSubsystem at FUN_0055e500, FUN_0055f110, FUN_0055f7f0, FUN_0055f3e0), .rdata constant at 0x008E4E20

---

## Summary

Stock STBC.exe delays shield disable by **1.0 second** after a cloak-up transition
starts, and delays shield re-enable by **1.0 second** after decloak completes. OpenBC has
no ShieldDelay window — shields toggle instantly with the cloak state machine. The result
is that cloak-up has no vulnerability window (shields drop in lock-step with cloak
engagement), and decloak has no grace window (shields enable in lock-step with full
visibility).

Combined with the (separately reported) CloakTime 5.0 fix, restoring ShieldDelay produces
the stock-intended combat dynamic: **1 second exposed during cloak-up** (cloak field
beginning to render but shields still active for that first second), and **1 second
grace during decloak** (fully visible but shields still down for that first second of
visibility).

---

## Symptom

- Cloak-up is instant-vulnerable: at t=0 of cloak request, shields drop and cloak field
  begins simultaneously. No exposed-with-shields window.
- Decloak is instant-protected: at t=CloakTime of decloak completion, ship becomes fully
  visible AND shields re-enable simultaneously. No vulnerability-while-fully-visible
  window.
- Cloaked ships are effectively harder to hit during cloak-up (no shield-flash giveaway,
  no momentary shield damage opportunity) than stock intended.
- Decloaked ships are effectively safer post-decloak than stock intended.

---

## Evidence

### Stock binary anchors

**ShieldDelay constant**:

| Address | Bytes | Float | Meaning |
|---------|-------|-------|---------|
| 0x008E4E20 | `00 00 80 3F` | **1.0f** | ShieldDelay default — delay between cloak transition and shield state change |

This was discovered in this pass; the STBC `cloaking-state-machine.md` doc previously
listed it as "speculated but not statically verified." The byte-confirmed value resolves
that open question.

**Event 0x0080007B** (shield re-enable delayed event):

Posted in three sites:
- `FUN_0055f110` (BeginCloaking path) — schedule shield disable at gameTime + ShieldDelay
- `FUN_0055f7f0` (DecloakComplete) — schedule shield re-enable at gameTime + ShieldDelay
- `FUN_0055f3e0` (InstantCloak) — schedule shield disable at gameTime + ShieldDelay

The event is dispatched via the TGTimerManager + TGEventManager chain:
- Schedule via `FUN_006dc490` (TGTimerManager::Update) with `due_time = gameTime +
  ShieldDelay`
- When timer fires, event 0x0080007B is posted to EventManager
- Shield subsystem reacts to the event, toggling shield-active state

**Cloak state machine reference**:

| State | Field | Description |
|-------|-------|-------------|
| 0 | ship+0xB0 | DECLOAKED |
| 2 | ship+0xB0 | CLOAKING (transition in) |
| 3 | ship+0xB0 | CLOAKED (fully cloaked) |
| 5 | ship+0xB0 | DECLOAKING (transition out) |
| +0xAC | byte | isFullyCloaked (1 when state==3) |
| +0xAD | byte | tryingToCloak (cleared in StopCloaking) |

CloakTime = 5.0f (constant at 0x008E4E1C — verified this pass). ShieldDelay = 1.0f
(constant at 0x008E4E20 — verified this pass).

**Timeline (stock)**:

```
t=0.0  BeginCloaking: state -> CLOAKING(2), schedule shield-disable event @ t+1.0
t=1.0  Shield-disable event fires: shields go inactive
t=5.0  Cloak transition complete: state -> CLOAKED(3), isFullyCloaked=1
       --- ship is fully invisible AND shields are down ---
t=X    BeginDecloaking: state -> DECLOAKING(5), tryingToCloak=0
t=X+5  Decloak transition complete: state -> DECLOAKED(0), schedule shield-enable @ t+X+5+1.0
t=X+6  Shield-enable event fires: shields go active
       --- between X+5 and X+6: fully visible, shields still down (grace window) ---
```

### STBC RE memos

- `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\gameplay-mid-cloaking-validation-20260528.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\gameplay\cloaking-state-machine.md`

The validation memo resolved both CloakTime (5.0f, not 3.0f as OpenBC originally assumed)
and ShieldDelay (1.0f) from the .rdata constants. Both addresses are independently
xref-verified across all four cloak transition handlers.

---

## Root Cause

OpenBC's cloak implementation gates shield activity directly on cloak state:

```c
// Current OpenBC (approximate)
bc_cloak_shields_active(ship) {
    return ship->cloak_state == DECLOAKED;
}
```

This is wrong in two ways: (1) it transitions instantly with `cloak_state`, and (2) it
doesn't account for the 1.0s delay window that stock schedules via timer events.

There is no `shield_delay_timer` field tracking the deferred shield transition, and no
event scheduling on cloak-up start / decloak-complete to fire the delayed shield
transition.

---

## Affected Files

- `src/shared/game/combat.c` — `bc_combat_apply_damage` shield-absorption gate (line ~311-313),
  `bc_combat_shield_tick` (line ~422)
- `src/shared/game/cloak.c` (or equivalent) — cloak state transitions
- `bc_ship_state_t` struct definition — needs new `shield_delay_timer` field

---

## Fix Plan

### Add timer field

```c
struct bc_ship_state_t {
    // ... existing fields
    f32 shield_delay_timer;        // gameTime when shield state changes; 0 = inactive
    u8  shield_delay_pending_on;   // 1 = pending enable, 0 = pending disable
};
```

### Gate shield-active

```c
// In bc_cloak_shields_active or wherever shield-active is computed
bool bc_cloak_shields_active(const bc_ship_state_t* ship) {
    f32 now = bc_clock_now();
    if (ship->shield_delay_timer != 0 && now >= ship->shield_delay_timer) {
        // Timer has expired — use the pending state
        return ship->shield_delay_pending_on;
    }
    // Timer still pending OR no timer — use current cloak-derived state
    // (which, before the timer expires, is the OLD shield state)
    return ship->cloak_state == DECLOAKED && ship->shield_delay_timer == 0;
}
```

(Exact gating depends on OpenBC's existing shield model — the principle is that the
timer overrides cloak_state during the 1.0s window.)

### Schedule events on transitions

```c
// In cloak-up handler (transition to CLOAKING state)
ship->shield_delay_timer = bc_clock_now() + 1.0f;
ship->shield_delay_pending_on = 0;  // shields will go OFF after 1.0s

// In decloak-complete handler (transition to DECLOAKED state)
ship->shield_delay_timer = bc_clock_now() + 1.0f;
ship->shield_delay_pending_on = 1;  // shields will go ON after 1.0s
```

### Coordinate with combat code

- `bc_combat_apply_damage` (combat.c line 311-313): shield-absorption gate must read the
  ShieldDelay-aware `bc_cloak_shields_active`, so damage during the 1.0s window goes
  through (cloak-up) or is blocked (decloak grace).
- `bc_combat_shield_tick` (combat.c line 422): shield recharge must respect the delayed
  state — no recharge during pending-disable window; recharge starts only after pending-
  enable timer fires.

### Combat balance verification

With both fixes applied (this report + the CloakTime 5.0 fix):

- **Cloak-up**: 1 second exposed (cloak field starting to render but shields still active)
- **Cloaked state**: 5 seconds of cloak ramp + indefinite cloaked duration
- **Decloak**: 5 seconds of decloak ramp + 1 second grace (visible but shields still down)

Matches stock behavior.

---

## Cross-References

- STBC memo: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\gameplay-mid-cloaking-validation-20260528.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\gameplay\cloaking-state-machine.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\gameplay\shield-system.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\gameplay\combat-mechanics-re.md`
- Related bug: `20260220-shield-flicker-and-collision-damage.md` (shield-side replication issues)
