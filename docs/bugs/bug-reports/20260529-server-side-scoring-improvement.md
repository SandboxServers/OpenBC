# Bug Report: Implement Server-Side Scoring (Closes Weapon-Kill SCORE_CHANGE Coverage Gap)

**Date**: 2026-05-29
**Severity**: MEDIUM (gameplay correctness — clients never see score updates for the most common kill type)
**Status**: NOT FIXED — implementation pending; design specified in `docs/game-systems/scoring-system.md`
**Affected Systems**: Server scoring, damage attribution, kill detection, end-game logic
**Improves On**: A known coverage gap that affects implementations relying on script-layer damage callbacks

---

## Summary

OpenBC currently has no scoring module. The wire protocol defines five scoring opcodes (`0x35` MISSION_INIT, `0x36` SCORE_CHANGE, `0x37` SCORE, `0x38` END_GAME, `0x39` RESTART_GAME), and clients expect these messages to maintain the scoreboard, but the OpenBC server does not emit any of them.

Beyond the missing baseline, network-trace observation of comparable script-layer scoring implementations reveals a coverage gap worth designing around proactively: **weapon kills frequently produce explosion visuals without a corresponding SCORE_CHANGE on the wire**, while collision kills and self-destructs reliably emit SCORE_CHANGE. The proposed fix is to implement scoring **entirely server-side**, with continuous damage attribution and kill detection driven by the `ObjectExplodingEvent`, which closes the gap and matches the wire-format expectations clients already have.

---

## Symptom

### Baseline (current OpenBC)

- The server never emits any of the scoring opcodes.
- The client scoreboard stays at all zeros for the entire match.
- End-game conditions (frag limit, time limit) are never evaluated; matches don't end.
- Restart cannot be triggered through the protocol.

### Coverage gap that motivates the design

Observed in network traces of script-layer scoring implementations:

| Kill type | Explosion visual seen on wire | SCORE_CHANGE seen on wire |
|-----------|-------------------------------|---------------------------|
| Weapon kill (beam/torpedo) | YES | NO (0 of 55 in one observed session) |
| Collision kill | YES | YES (100%) |
| Self-destruct | YES | YES (100%) |

The asymmetry reflects how those implementations couple scoring to per-damage-event callbacks: when a weapon kill's killing blow is delivered via per-tick health replication rather than a discrete damage callback, the script-layer scoring code never runs, even though the ship-death wire signal (`ObjectExplodingEvent`) still arrives correctly.

The result on the affected sessions:

- Players never see kill or death counters update from weapon fire.
- Score-limit and frag-limit games never end via weapon kills.
- Only collision and self-destruct deaths advance the match toward an end-game.

OpenBC should not reproduce this gap.

---

## Root Cause

The wire protocol's scoring messages can be emitted from either the script layer or the server layer; the binary protocol is identical. Script-layer scoring is fragile because it relies on a damage callback that may not fire for every damage event — specifically, weapon damage that propagates via per-tick health-state replication can produce a kill without producing the damage callback that script-layer scoring listens to.

Server-side scoring is more robust because:

1. Damage attribution is recorded inline with damage computation (in `src/shared/game/combat.c`), not in a separate callback that may or may not fire.
2. Kill detection uses the `ObjectExplodingEvent` payload (`source`, `dest` fields), which is the canonical kill wire signal and arrives reliably for every player ship death.
3. The two halves are independent: the ledger is populated whenever damage is applied, and SCORE_CHANGE is emitted whenever a kill event is observed. Neither depends on the other's plumbing.

---

## Fix

Implement a server-side scoring subsystem per [docs/game-systems/scoring-system.md](../../game-systems/scoring-system.md). Summary of changes:

### New files

| File | Purpose |
|------|---------|
| `include/openbc/scoring.h` | Public scoring API |
| `src/server/bc_scoring.c` | Scoring state, attribution ledger, kill-credit algorithm, end-game checks, restart reset |

### Modified files

| File | Change |
|------|--------|
| `src/shared/game/combat.c` | Call `bc_scoring_record_damage()` at every damage-application call site (beam, torpedo, collision) |
| `src/server/server_dispatch.c` | On inbound `ObjectExplodingEvent` (0x06 PythonEvent), call `bc_scoring_on_kill()`; check end-game conditions afterward; on inbound `RESTART_GAME` (or admin trigger), call `bc_scoring_reset()` and broadcast 0x39 |
| `src/server/server_send.c` | Add wire builders for 0x35 MISSION_INIT, 0x36 SCORE_CHANGE, 0x37 SCORE, 0x38 END_GAME, 0x39 RESTART_GAME |
| `src/server/server_handshake.c` | On join completion, send 0x35 MISSION_INIT then N × 0x37 SCORE to the joining peer |
| `src/server/main.c` | 1-second timer tick that calls `bc_scoring_check_end_conditions()` for the time-limit case |

### Data model

```c
typedef struct bc_player_score {
    i32 player_id;
    i32 kills;
    i32 deaths;
    i32 score;
} bc_player_score_t;

typedef struct bc_damage_attribution {
    i32 shooter_player_id;
    f32 shield_damage;
    f32 hull_damage;
} bc_damage_attribution_t;

typedef struct bc_target_ledger {
    i32 target_ship_id;
    bc_damage_attribution_t contributors[BC_MAX_CONTRIBUTORS];
    int contributor_count;
} bc_target_ledger_t;
```

Score formula:

```
score_delta_per_contributor = round((shield_damage + hull_damage) / 10.0)
```

End-game triggers:

- Time expired → reason `1` (TIME_UP)
- Any player kills `>= frag_limit` → reason `2` (NUM_FRAGS)
- Any player score `>= frag_limit * 10000` (score-limit mode) → reason `3` (SCORE_LIMIT)

---

## Implementation Scope

**Estimated**: ~200 LoC across 4-5 files.

Breakdown:

| Component | LoC estimate |
|-----------|--------------|
| `bc_scoring.c` (state, attribution, kill credit, end-game) | ~120 |
| Wire-format builders in `server_send.c` (5 opcodes) | ~50 |
| Damage hooks in `combat.c` (3 call sites) | ~10 |
| Dispatch hook for ObjectExplodingEvent in `server_dispatch.c` | ~15 |
| Join-sequence hooks in `server_handshake.c` | ~10 |

Tests (add to `tests/`):

| Test | Coverage |
|------|----------|
| Damage attribution roundtrip | weapon, torpedo, collision damage all reach the ledger |
| Weapon kill | 1 kill, SCORE_CHANGE emitted with correct killer/killed/contributors |
| Multi-contributor kill | 3 shooters share damage, all 3 appear in additional-contributors list |
| Self-destruct | death-only SCORE_CHANGE, `killer_player_id = 0`, no killer kills field |
| AI kill | death recorded, no kill credit assigned to AI ship |
| End-game on frag limit | kill that hits frag_limit triggers END_GAME reason 2 |
| End-game on score limit | kill that hits score_limit triggers END_GAME reason 3 |
| End-game on timer | time-out triggers END_GAME reason 1 |
| Restart resets state | RESTART_GAME zeros all entries, clears ledger |
| Join sequence | MISSION_INIT + N × SCORE in correct order |
| Reconnection preserves score | rejoining player sees prior totals |

---

## Acceptance Criteria

1. Server emits MISSION_INIT (0x35) to every joining peer after checksum handshake.
2. Server emits one SCORE (0x37) per known player to every joining peer.
3. Server emits SCORE_CHANGE (0x36) for **every** player ship death, regardless of damage source:
   - Weapon kills (beam, torpedo) — **closes the documented coverage gap**.
   - Collision kills.
   - Self-destructs.
4. Multi-contributor kills correctly list all damage contributors in the additional-scores tail.
5. AI kills produce death entries but no kill credit on the AI side.
6. Frag-limit games end via END_GAME (0x38) reason `2` when the limit is reached by any damage type.
7. Score-limit games end via END_GAME (0x38) reason `3` when the threshold is reached.
8. Time-limit games end via END_GAME (0x38) reason `1` after the configured duration.
9. RESTART_GAME (0x39) broadcast resets all scoring state server-side and client-side.
10. A 5-minute multi-player test session produces a SCORE_CHANGE per kill (no missing scoring events) and ends correctly when frag/time/score limit is reached.

---

## Out of Scope (Follow-Up Work)

- **Team Deathmatch (Mission2 / Mission3)**: opcodes 0x3F SCORE_INIT, 0x40 TEAM_SCORE, 0x41 TEAM are not covered here. Team scoring is a separate extension on top of the FFA design specified in `docs/game-systems/scoring-system.md`.
- **Scenario-specific end conditions**: reasons 4-6 (STARBASE_DEAD, BORG_DEAD, ENTERPRISE_DEAD) require scripted-scenario logic outside the base server.
- **Ship-class damage modifiers**: stock multiplayer has all flyable ships at class 1, so the modifier is always 1.0. The hook point exists in the damage attribution call; mod support can be added later by multiplying `shield_damage` and `hull_damage` by a class-pair modifier before recording.

---

## Cross-References

- **OpenBC spec**: [docs/game-systems/scoring-system.md](../../game-systems/scoring-system.md) — full behavioral specification with wire formats and algorithm
- **OpenBC spec**: [docs/game-systems/ship-death-lifecycle.md](../../game-systems/ship-death-lifecycle.md) — `ObjectExplodingEvent` is the kill signal scoring observes
- **OpenBC spec**: [docs/planning/gamemode-system.md](../../planning/gamemode-system.md) — broader gamemode design, including team-mode opcodes 0x3F/0x40/0x41 reserved for follow-up
- **OpenBC wire format**: [docs/wire-formats/pythonevent-wire-format](../../wire-formats/pythonevent-wire-format) — payload encoding for the kill event
- **Related bug**: [20260529-explosion-overemission.md](20260529-explosion-overemission.md) — separate issue: 0x29 emission cleanup; unrelated to scoring but adjacent in the kill-event chain
