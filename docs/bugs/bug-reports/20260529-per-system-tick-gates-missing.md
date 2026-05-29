# Bug Report: Per-System Tick Gates Missing (Power 30x, Weapons 10x Too Fast)

**Date**: 2026-05-29
**Severity**: HIGH (cadence drift visible in StateUpdate streams; balance impact)
**Status**: NOT FIXED — requires per-system review across power + weapons + (likely) other ship subsystems.
**Affected Systems**: PoweredMaster, WeaponSystem child Update, server tick loop, StateUpdate emission cadence
**Verified Against**: STBC.exe live decompile (FUN_00563780, FUN_005847d0), .rdata constant addresses

---

## Summary

OpenBC runs ship power and weapon subsystem ticks **every main-loop tick** (~30 Hz). Stock
STBC.exe has strict per-system rate gates: **PoweredMaster ticks at 1.0s strict (1 Hz)**;
**WeaponSystem child Update ticks at 0.33s (3 Hz)**. OpenBC is therefore running power
ticks at **30x stock rate** and weapon ticks at **10x stock rate**, with downstream
effects on StateUpdate dirty-flag emission cadence and combat balance.

---

## Symptom

- StateUpdate (opcode 0x1C) cadence for subsystem and weapon fields drifts from stock
- Power state churn (battery/reactor/conduit recompute) every tick produces dirty-flag
  emissions in the 0x40 family far more frequently than stock
- Weapon charge/torpedo-reload increments per main tick rather than per 0.33s window —
  observable as faster phaser-charge / torpedo-reload visuals on remote views
- Combat pacing differs from stock — weapon DPS effectively scales with tick rate when
  the per-tick gate is missing

---

## Evidence

### Specific gaps

#### PoweredMaster::Update — 30x too fast

| Item | Value | Address |
|------|-------|---------|
| Stock function | `PoweredMaster_Update` | 0x00563780 |
| Stock gate | `currentTime - ship+0xc0 > 1.0s` | binary |
| Stock interval constant | `_DAT_00892e20 = 0x3F800000 = 1.0f` | 0x00892e20 |
| OpenBC behavior | calls `bc_ship_power_tick` every tick | every main-loop iteration |
| Drift factor | **30x** | 30 Hz tick / 1 Hz strict gate |

PoweredMaster computes battery + reactor + conduit state. Running every tick
means power-budget recompute happens 30x more often than stock, producing 30x
more StateUpdate flag-0x40 emissions when battery state changes.

#### WeaponSystem child Update — 10x too fast

| Item | Value | Address |
|------|-------|---------|
| Stock function | `WeaponSystem::Update` | FUN_005847d0 |
| Stock per-child gate | `child+0x12 (accumulator) > 0.33s` | inner loop in FUN_005847d0 |
| Stock interval constant | `_DAT_00892fc0 = 0x3EA8F5C3 = 0.33f` | 0x00892fc0 |
| Stock effective rate | 3 Hz per child | derived |
| OpenBC behavior (charge) | calls `bc_combat_charge_tick` every tick | per phaser per tick |
| OpenBC behavior (torpedo) | calls `bc_combat_torpedo_tick` every tick | per torpedo per tick |
| Drift factor | **10x** | 30 Hz tick / 3 Hz gate |

WeaponSystem child Update drives phaser charge accumulation and torpedo reload. Running
every tick means phaser charge increments 10x faster than stock per tick. Even if the
per-increment delta is scaled to match, the integration cadence differs and produces
different damage-output curves under load.

> **Pass 1 note (2026-05-29)**: The host's WeaponSystem child Update **does NOT
> emit any wire events** from its tick path — neither opcode 0x06 PythonEvent,
> 0x07 StartFiring, 0x1A BeamFire, nor any other game opcode is generated from
> the host-side `WeaponSystem::UpdateWeapons / TryFireWeapon` chain. The only
> weapon-related event in the local engine is 0x80006B SUBSYSTEM_HIT (emitted by
> `ShipSubsystem_SetCondition @ 0x0056C470`) which is LOCAL-ONLY. Weapon-state
> changes replicate to other peers via opcode 0x1C StateUpdate (subsystem-health
> round-robin), not via per-tick event emission. Therefore the cadence bug
> documented here is a StateUpdate emission-rate issue (downstream of the
> health-state mutation), not an event-emission rate issue. Source:
> `host-event-emission-catalog-20260529` Section 2.

### Stock tick architecture

The stock main loop in `UtopiaApp_PerFrameTick` (0x00438e20) runs unthrottled
PeekMessage spin (~60-200 Hz on client; ~20 Hz floor when GameSpy registered and idle).
The proxy DLL replaces this with a 33ms WM_TIMER (~30 Hz). OpenBC currently approximates
the proxy rate.

Per-system gates inside the main tick chain are what slow individual subsystems to their
intended rates:

```
TopWindow::Update (per main tick)
  └── Ship.Update (per main tick)
       └── PoweredMaster::Update (gates internally to 1 Hz)
       └── WeaponSystem::Update (per main tick)
            └── child Update (gates internally to 3 Hz)
       └── PoweredSubsystem::Update (per main tick — no gate)
       └── CloakingSubsystem::Update (per main tick — no gate)
       └── RepairSubsystem::Update (per main tick — no gate)
```

The gates are inside the body of each subsystem's Update — the dispatcher calls them every
tick, but they early-exit if their internal time accumulator hasn't crossed the threshold.

### STBC RE memo

`C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\tick-rate-inventory-validation-20260528.md`

STBC-side doc (updated this pass): `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\architecture\main-loop-timing.md`

The memo is an exhaustive tick-rate inventory across every Update/tick driver in
stbc.exe, with .rdata constant addresses for every gate. PoweredMaster 1 Hz and
WeaponSystem 3 Hz are the only mid-rate strict gates discovered; the rest run at parent
rate.

---

## Root Cause

OpenBC's main tick loop calls every per-system tick function unconditionally each
iteration:

```c
// src/server/main.c (current OpenBC)
for (each tick) {
    for (each ship) {
        bc_ship_power_tick(ship);       // SHOULD gate to 1 Hz
        bc_combat_charge_tick(ship);    // SHOULD gate to 3 Hz
        bc_combat_torpedo_tick(ship);   // SHOULD gate to 3 Hz
        // ... other per-tick work
    }
}
```

There is no per-system, per-ship `last_tick_time` tracking that would let each subsystem
early-exit if its threshold hasn't elapsed.

---

## Affected Files

- `src/server/main.c` — tick loop (calls into per-system tick functions every tick)
- `src/shared/game/combat.c` — `bc_combat_charge_tick`, `bc_combat_torpedo_tick`
- `src/shared/game/ship_power.c` — `bc_ship_power_tick`

---

## Fix Plan

### Fix shape

Add per-ship per-system `last_tick_time: f32` field, then gate each per-system tick on
the threshold:

```c
// In bc_ship_state_t (or per-subsystem state)
f32 last_power_tick_time;     // PoweredMaster: 1.0s gate
f32 last_charge_tick_time;    // WeaponSystem child: 0.33s gate per phaser
f32 last_torpedo_tick_time;   // WeaponSystem child: 0.33s gate per torpedo

// In server tick loop
f32 now = bc_clock_now();
for (each ship) {
    if (now - ship->last_power_tick_time >= 1.0f) {
        bc_ship_power_tick(ship);
        ship->last_power_tick_time = now;
    }
    if (now - ship->last_charge_tick_time >= 0.33f) {
        bc_combat_charge_tick(ship);
        ship->last_charge_tick_time = now;
    }
    if (now - ship->last_torpedo_tick_time >= 0.33f) {
        bc_combat_torpedo_tick(ship);
        ship->last_torpedo_tick_time = now;
    }
    // ... ungated ticks continue
}
```

### Per-system review needed

The two gates above are confirmed. The full list of stock per-system tick rates from the
STBC memo:

| Subsystem | Rate | Constant address | OpenBC status |
|-----------|------|------------------|---------------|
| PoweredMaster | 1.0s strict | 0x00892e20 | **30x too fast** |
| WeaponSystem child | 0.33s | 0x00892fc0 | **10x too fast** |
| PoweredSubsystem | per main tick (no gate) | n/a | parity (no change needed) |
| CloakingSubsystem | per main tick (no gate) | n/a | parity |
| RepairSubsystem | per main tick (no gate) | n/a | parity |
| ShieldGenerator::BoostShield | per main tick | n/a | parity |
| AI::Update | per main tick (up to 4 catch-up cycles) | n/a | review needed |
| TGTimerManager::Update | per main tick (full drain) | n/a | n/a |
| TGEventManager::ProcessQueue | per main tick (full drain) | n/a | n/a |

The per-system review should walk every OpenBC tick function and verify its rate matches
stock — only the two listed gates require strict throttling, but the review confirms
nothing else has drifted.

### Acceptance criteria

1. PoweredMaster tick fires at most once per 1.0s wall-clock interval per ship
2. WeaponSystem charge/torpedo ticks fire at most once per 0.33s per child
3. StateUpdate flag-0x40 (battery) emission cadence under steady-state matches stock
   distribution
4. Phaser charge accumulation time from empty-to-full matches stock measurements

---

## Cross-References

- STBC memo: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\tick-rate-inventory-validation-20260528.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\architecture\main-loop-timing.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\gameplay\power-system.md` (PoweredMaster behavior)
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\gameplay\weapon-firing-mechanics.md` (WeaponSystem child Update)
- Related bug: `20260226-stateupdate-authority-and-cadence-gap.md` — cadence drift on the downstream side
