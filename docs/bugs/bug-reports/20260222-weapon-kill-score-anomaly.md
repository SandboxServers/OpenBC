# Bug Report: SCORE_CHANGE Not Sent for Weapon Kills (Stock Dedi Anomaly)

**Date**: 2026-02-22
**Severity**: MEDIUM
**Status**: OPEN
**Affects**: Stock BC 1.1 dedicated server (confirmed anomaly)
**OpenBC action**: Send SCORE_CHANGE for ALL death types (improve on stock)

---

## Summary

The stock Bridge Commander dedicated server does not send SCORE_CHANGE (0x36) messages
when a ship is destroyed by weapon fire (phasers, torpedoes). It DOES correctly send
SCORE_CHANGE for collision kills and self-destructs. This means clients never receive
score updates for the most common kill type in multiplayer — weapon kills.

---

## Evidence

### Battle of Valentine's Day Trace (2026-02-14)

- **Duration**: 33.5 minutes
- **Players**: 3
- **Total ship deaths**: 59
- **Weapon kills**: 55
- **Self-destructs**: 4
- **SCORE_CHANGE messages**: **0** (zero)

### Collision Test Trace (same session)

- **Duration**: 28 seconds
- **Players**: 2
- **Total ship deaths**: 1 (collision kill)
- **SCORE_CHANGE messages**: **1** (correct)

### Self-Destruct Trace (earlier verification, 2026-02-21)

- Self-destruct deaths correctly produce SCORE_CHANGE with death counted and no kill credit

### Summary Table

| Kill Type | Deaths Observed | SCORE_CHANGE Sent | Correct? |
|-----------|----------------|-------------------|----------|
| Weapon (beam/torpedo) | 55 | 0 | **NO** |
| Self-destruct | 4 | 4 | YES |
| Collision | 1 | 1 | YES |

---

## Root Cause Analysis

The ObjectKilledHandler (Python mission script) is responsible for computing scores and
broadcasting SCORE_CHANGE. It runs when the OBJECT_EXPLODING event fires on the host.

For collision kills and self-destructs, the damage pipeline runs entirely on the host,
which triggers OBJECT_EXPLODING locally and the ObjectKilledHandler fires.

For weapon kills, the damage pipeline runs on each **receiving client** independently
(receiver-local hit detection). The host may not process the weapon hit damage at all,
so OBJECT_EXPLODING may never fire on the host. The Explosion (0x29) message IS sent
(59 observed), but this appears to be triggered by a different code path that doesn't
invoke the Python scoring handler.

---

## Recommendation for OpenBC

OpenBC should send SCORE_CHANGE for **ALL death types**, regardless of the damage source:

1. **Weapon kills**: When the server detects a ship death (hull HP reaches zero), compute
   scores from the damage ledger and broadcast SCORE_CHANGE
2. **Collision kills**: Already working (verified in collision test trace)
3. **Self-destructs**: Already working (verified in self-destruct trace)
4. **Explosion damage**: Same as weapon kills

This is an intentional improvement over stock behavior. The scoring system exists and
works correctly — it's only the triggering path that's broken for weapon kills on the
stock dedicated server.

---

## Related

- [gamemode-system.md](../../planning/gamemode-system.md) — Scoring system spec (contains anomaly note)
- [ship-death-lifecycle.md](../../game-systems/ship-death-lifecycle.md) — Ship death sequence
- [server-authority.md](../../architecture/server-authority.md) — Scoring is server-authoritative
