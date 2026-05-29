# Scoring System — Clean-Room Specification

Behavioral specification for OpenBC's server-side multiplayer scoring system. Describes damage attribution, kill detection, score broadcasts, mission lifecycle, and end-game logic. Suitable for clean-room implementation.

**Clean-room statement**: This specification is derived from observable network behavior, packet trace analysis, and the shipped Bridge Commander scripting API. No binary addresses, memory offsets, or decompiled code are referenced.

See also:
- [docs/game-systems/ship-death-lifecycle.md](ship-death-lifecycle.md) — kill-event signal source
- [docs/game-systems/combat-system.md](combat-system.md) — damage application pipeline that scoring observes
- [docs/planning/gamemode-system.md](../planning/gamemode-system.md) — broader gamemode design (teams, missions, ship classes)
- [docs/wire-formats/explosion-wire-format.md](../wire-formats/explosion-wire-format.md) — separate visual-effect opcode (0x29), unrelated to scoring

---

## Overview

OpenBC implements **server-authoritative scoring** for multiplayer. The server maintains all per-player scoring state, observes every damage event to track attribution, and emits score broadcasts on every kill it detects.

Two design choices distinguish this from a script-layer scoring implementation:

1. **Damage attribution is observed continuously** — the server records every damage application against player-controlled ships, indexed by (target, shooter), regardless of which replication path carried the damage. Damage that crosses the network via per-tick health updates is recorded the same way as damage that triggers a discrete damage callback.
2. **Kill credit is computed from the inbound exploding-event** — when the server observes an `ObjectExplodingEvent` (the canonical kill signal for player ships), it consults the attribution ledger to compute score deltas for every contributor, not just the killing-blow attacker. SCORE_CHANGE is emitted on every detected kill, with no dependency on which damage type delivered the killing blow.

This design closes a known scoring-coverage gap that affects implementations relying solely on damage-application callbacks: when a player kill is delivered via per-tick health replication rather than a discrete damage callback, downstream scoring logic may never run, producing duplicate explosion visuals but no SCORE_CHANGE on the wire. By observing kills via the exploding-event and reading scores from a continuously-maintained attribution ledger, OpenBC emits SCORE_CHANGE reliably for weapon kills, collision kills, and self-destructs alike.

---

## Server State

### Per-Player Scoring

The server maintains one entry per known player, keyed by `player_id`:

| Field | Type | Description |
|-------|------|-------------|
| `kills` | `i32` | Number of enemy ships destroyed |
| `deaths` | `i32` | Number of times this player's ship was destroyed |
| `score` | `i32` | Cumulative score (integer points; see score formula) |

```c
typedef struct bc_player_score {
    i32 player_id;
    i32 kills;
    i32 deaths;
    i32 score;
} bc_player_score_t;
```

The server holds a map from `player_id → bc_player_score_t`. Entries are created when a player completes the join handshake and **preserved** when a player disconnects, so that reconnections restore prior totals.

### Damage Attribution Ledger

For every player-controlled target ship, the server tracks cumulative shield and hull damage by shooter:

```c
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

The server holds a map from `target_ship_id → bc_target_ledger_t`. Entries are created on first damage and deleted when the target ship is destroyed (after kill processing) or removed from the game.

`BC_MAX_CONTRIBUTORS` should be at least the player limit (currently 8); if a ledger fills, the lowest-damage entry should be evicted before adding a new shooter.

### Game Rules State

| Field | Type | Description |
|-------|------|-------------|
| `frag_limit` | `i32` | Kill threshold; `-1` = no limit |
| `score_limit` | `i32` | Score threshold; `-1` = no limit (mutually exclusive with frag_limit per gamemode setting) |
| `use_score_limit` | `bool` | If true, score-limit mode applies (`score >= frag_limit * 10000`) |
| `time_limit_seconds` | `i32` | Round time limit in seconds; `-1` = no limit |
| `end_time_absolute` | `i32` | Absolute game-clock value when the round ends (sent on join) |
| `game_over` | `bool` | Set true after END_GAME has been broadcast |

---

## Damage Attribution Algorithm

The server must record every damage application against a player-controlled ship, regardless of which gameplay system originated the damage. Three damage paths exist; all funnel into the same attribution call:

### Damage Sources

| Source | Shooter ID | Recorded Fields |
|--------|------------|-----------------|
| Beam / phaser hit | firing player's `player_id` | shield_damage, hull_damage from the damage breakdown |
| Torpedo detonation | firing player's `player_id` | shield_damage, hull_damage from the damage breakdown |
| Collision contact | impacting player's `player_id` (the other ship) | shield_damage, hull_damage from the damage breakdown |

If the shooter is an AI ship (no associated player), the damage is **not recorded** in the ledger. AI ships are not scoring participants.

### Attribution Call

```c
void bc_scoring_record_damage(i32 target_ship_id,
                              i32 shooter_player_id,
                              f32 shield_damage,
                              f32 hull_damage);
```

Implementation:

1. If `shooter_player_id == 0`, return (self-inflicted / environmental damage is not credited).
2. If the target ship is not owned by a player, return (AI targets are not tracked).
3. Look up or create the `bc_target_ledger_t` for `target_ship_id`.
4. Look up or create the contributor entry for `shooter_player_id`.
5. Accumulate `shield_damage` and `hull_damage` into the contributor entry.

This call must be invoked from every damage application site in `src/shared/game/combat.c` and from any server-side damage validation in `src/server/server_dispatch.c`. The call is cheap (constant-time map lookup + add) and idempotent under double-recording from race conditions: scoring tolerates over-attribution far better than missing-attribution, but tests should verify each damage event is recorded exactly once.

### Why this is needed

In implementations that rely on per-damage-event callbacks alone, weapon kills can produce a kill signal without producing a corresponding damage-application callback when health changes propagate via per-tick state replication instead. Tracking the shooter alongside every damage write — at the moment the damage is computed, not at the moment a callback fires — ensures the ledger is complete by the time the kill event arrives.

---

## Kill Detection

The canonical kill signal for a player ship is an inbound `0x06 PythonEvent` carrying an `ObjectExplodingEvent` payload, observed by the server when a ship is destroyed. The relevant fields:

| Field | Description |
|-------|-------------|
| `dest` | The dying ship's object ID |
| `source` | The killer's ship object ID, or `NULL` for self-destruct / environmental |
| `lifetime` | Seconds until the wreckage disappears (`9.5` for combat, `9.5` for self-destruct in stock) |

See [docs/wire-formats/pythonevent-wire-format](../wire-formats/pythonevent-wire-format) for the payload encoding.

### Mapping to player IDs

The server maintains the ship-id → player-id mapping for all spawned player ships. From the exploding event:

```c
i32 killed_player_id = bc_lookup_player_for_ship(ev.dest);
i32 killer_player_id = (ev.source == 0)
                        ? 0
                        : bc_lookup_player_for_ship(ev.source);
```

If `killed_player_id` is 0 (the dying object is not a player ship), kill processing is skipped — only player ship deaths produce SCORE_CHANGE.

### Kill processing

When an `ObjectExplodingEvent` arrives for a player ship:

1. **Early-out guards**:
   - If `game_over` is true: skip (post-end-game deaths don't update scoring).
   - If the dying object is not a player ship: skip.
2. **Death credit**: `players[killed_player_id].deaths += 1`.
3. **Kill credit**:
   - If `killer_player_id == 0` (self-destruct, environmental, AI): no kill credit; proceed to step 4.
   - Else: `players[killer_player_id].kills += 1`.
4. **Score distribution from ledger**:
   - For each contributor `c` in `ledger[killed_player_id's ship].contributors`:
     - `delta = (c.shield_damage + c.hull_damage) / 10.0`
     - `players[c.shooter_player_id].score += round_to_int(delta)`
     - Track which players had scores changed (for SCORE_CHANGE payload).
5. **Ledger cleanup**: delete the ledger entry for the destroyed ship.
6. **Broadcast SCORE_CHANGE** (see wire format below). Sent to all peers; the killer client also receives it (some implementations skip the killer — OpenBC sends to all for simplicity and observability).
7. **End-game check**: see `bc_scoring_check_end_conditions()` below.

### AI kill exception

If the killer is an AI ship (e.g., a starbase defender), `killer_player_id` resolves to 0 because no player owns the ship. This naturally falls through to "no kill credit," and the death is still recorded.

### Self-destruct

Self-destruct produces an `ObjectExplodingEvent` with `source = NULL` (`0`). This produces a death entry but no kill credit, matching the standard "no killer" behavior.

---

## SCORE_CHANGE Emission

Every kill detection produces exactly one SCORE_CHANGE broadcast. The payload includes:

- The killer's updated kills and score (if there is a killer)
- The killed player's updated deaths
- Updated scores for **every contributor** whose score changed (from the ledger distribution step)

The killer is named explicitly in the message; other contributors appear in the variable-length "additional scores" tail.

If a kill happens with zero ledger contributors (e.g., a kill arrives before any damage was recorded — should be rare but possible during initial spawn), the additional-scores count is `0` and only the killer and killed entries are sent.

See the wire format below.

---

## End-Game Logic

End-game conditions are checked after **every kill** and on a timer tick (1-second granularity is sufficient).

```c
bool bc_scoring_check_end_conditions(int *out_reason);
```

Returns true if the game should end and writes the reason code. Order of checks:

1. **Time expired**: if `time_limit_seconds > 0` and current game clock `>= end_time_absolute` → reason = `1` (TIME_UP).
2. **Score limit** (if `use_score_limit`): if any player's `score >= frag_limit * 10000` → reason = `3` (SCORE_LIMIT).
3. **Frag limit** (if `!use_score_limit` and `frag_limit > 0`): if any player's `kills >= frag_limit` → reason = `2` (NUM_FRAGS).
4. **Mission-specific** (scenario gamemodes only): starbase destroyed, Borg destroyed, Enterprise destroyed → reasons `4`, `5`, `6` respectively. These apply only to scripted scenarios and are out of scope for the base server.

On end-game trigger:

1. Set `game_over = true`.
2. Stop accepting new join handshakes (`ready_for_new_players = false`).
3. Broadcast `END_GAME` with the reason code.
4. Stop accumulating damage in the ledger (or accept it and ignore future kill events — both work because `game_over` blocks kill processing).

Reason `0` (ITS_JUST_OVER) is reserved for a generic "manual end" path (e.g., admin command); the server does not emit it automatically.

---

## Mission Lifecycle

### On player join (after checksum handshake completes)

The server sends, to the **joining peer only**:

1. **MISSION_INIT** (`0x35`) — one message, conveys game settings.
2. **SCORE** (`0x37`) — one message per known player (including the joiner, who arrives with kills=0/deaths=0/score=0).

The order is `0x35` first, then a sequence of `0x37`s. Existing players' scoring entries are sent unchanged so the joiner sees the live state.

### On per-tick combat

The ledger is updated continuously from damage application. No wire output is generated until a kill occurs.

### On kill

`SCORE_CHANGE` (`0x36`) is broadcast to all peers, including the killer.

### On end-game

`END_GAME` (`0x38`) is broadcast to all peers.

### On restart

`RESTART_GAME` (`0x39`) is broadcast to all peers (see Restart Flow below).

---

## Restart Flow

The host (or an admin trigger) initiates restart. The server:

1. Broadcasts `RESTART_GAME` (`0x39`) — 1 byte, no payload.
2. Resets all server state:
   - For each player: `kills = 0`, `deaths = 0`, `score = 0` (entries preserved, keys stay).
   - Clear the damage attribution ledger entirely.
   - `game_over = false`.
   - Recompute `end_time_absolute` based on the current game clock and `time_limit_seconds`.
   - `ready_for_new_players = true`.
3. On the wire, the server does not need to resend MISSION_INIT or SCORE messages after restart — clients reset their own scoring state in response to `RESTART_GAME` and the round restarts immediately. New joiners after restart get the standard join sequence.

Clients receiving `RESTART_GAME`:

1. Zero all local scoring data.
2. Reset the local round timer.
3. Return their player to the ship-selection screen.

---

## Wire Formats

All five scoring opcodes are sent **reliably** (guaranteed delivery). Each begins with a one-byte opcode constant.

### 0x35 MISSION_INIT — host → joining client

Sent once per join, immediately after the checksum handshake completes.

```
[u8:0x35]
[u8:player_limit]                    # 1..8
[u8:system_species]                  # map / system ID (1..10)
[u8:time_limit_or_0xFF]              # 0xFF = no time limit; else minutes
  {if time_limit != 0xFF:
    [i32:end_time_absolute]          # game-clock value when round ends
  }
[u8:frag_limit_or_0xFF]              # 0xFF = no frag limit; else threshold
```

Size: **4 bytes** (no time limit, no frag limit) up to **8 bytes** (both limits set).

Observed example: `35 08 08 FF FF` — 8-player limit, system 8, no time/frag limits (5 bytes).

### 0x36 SCORE_CHANGE — host → all peers

Broadcast on every kill detected via `ObjectExplodingEvent`.

```
[u8:0x36]
[i32:killer_player_id]               # 0 if no killer (self-destruct, env)
  {if killer_player_id != 0:
    [i32:killer_kills]                # updated total
    [i32:killer_score]                # updated total
  }
[i32:killed_player_id]
[i32:killed_deaths]                  # updated total
[u8:additional_count]                # N additional contributors
[N × {
  [i32:contributor_player_id]
  [i32:contributor_score]            # updated total
}]
```

Minimum size: **14 bytes** (self-destruct with no other contributors).
Typical size: **22-38 bytes** (one killer + 0-3 additional contributors).

The `additional_count` field is a `u8`, so up to 255 contributors per kill — far more than the player limit. In practice this is bounded by `BC_MAX_CONTRIBUTORS`.

### 0x37 SCORE — host → joining client

Sent N times during the join sequence (one per known player). Full state sync.

```
[u8:0x37]
[i32:player_id]
[i32:kills]
[i32:deaths]
[i32:score]
```

Size: **17 bytes**, fixed.

### 0x38 END_GAME — host → all peers

Broadcast once when end-game conditions are met.

```
[u8:0x38]
[i32:reason]
```

Size: **5 bytes**, fixed.

Reason codes:

| Code | Name | Trigger |
|------|------|---------|
| 0 | ITS_JUST_OVER | Generic / manual termination |
| 1 | TIME_UP | Round time expired |
| 2 | NUM_FRAGS | Frag-limit mode: kill threshold reached |
| 3 | SCORE_LIMIT | Score-limit mode: point threshold reached |
| 4 | STARBASE_DEAD | Scenario gamemode: starbase destroyed |
| 5 | BORG_DEAD | Scenario gamemode: Borg destroyed |
| 6 | ENTERPRISE_DEAD | Scenario gamemode: Enterprise destroyed |

Reasons 4-6 require scripted-scenario logic outside the base server's scope.

### 0x39 RESTART_GAME — host → all peers

Broadcast on host-initiated restart.

```
[u8:0x39]
```

Size: **1 byte**, no payload.

---

## Score Formula

Damage contributes to score by the same factor regardless of damage type or shooter class:

```
score_delta_per_contributor = round((shield_damage + hull_damage) / 10.0)
```

Shield damage and hull damage are summed before division. Both 1000 HP of shield damage and 1000 HP of hull damage produce the same score contribution. Sub-point damage accumulates in the ledger as floats, with the integer-rounded delta sent to the wire only at kill time — this means a player who dealt 9.9 HP cumulative damage to a target gets 1 point on kill, not 0.

### Why divide by 10?

Score limits in stock multiplayer are configured in units of `frag_limit * 10000` points. Most ship max-hull values are in the 5,000-20,000 HP range, so a "to-the-death" kill credit produces roughly 500-2,000 points. A typical score-limit game runs to ~5-10 ship deaths' worth of damage per winner.

### Mod compatibility

If mods adjust ship HP values, score balance shifts proportionally. The divisor is a fixed constant in the protocol — mods cannot change it without breaking wire compatibility. Server admins can adjust `frag_limit` to compensate.

---

## Implementation Outline

Suggested module structure:

| File | Responsibility |
|------|----------------|
| `include/openbc/scoring.h` | Public scoring API (record damage, on-kill, end-game check, restart) |
| `src/server/bc_scoring.c` | Server-side state + algorithm implementation |
| `src/server/server_send.c` | Wire-format builders for 0x35, 0x36, 0x37, 0x38, 0x39 |
| `src/server/server_dispatch.c` | Hook into ObjectExplodingEvent receive path; call into scoring |
| `src/shared/game/combat.c` | Call `bc_scoring_record_damage()` at every damage application |

Public API sketch:

```c
/* Per-player state */
void bc_scoring_init(void);
void bc_scoring_reset(void);                       /* On restart */
void bc_scoring_add_player(i32 player_id);          /* On join handshake */

/* Continuous damage observation */
void bc_scoring_record_damage(i32 target_ship_id,
                              i32 shooter_player_id,
                              f32 shield_damage,
                              f32 hull_damage);

/* Kill event from inbound ObjectExplodingEvent */
void bc_scoring_on_kill(i32 dest_ship_id,
                        i32 source_ship_id);

/* End-game checks */
bool bc_scoring_check_end_conditions(int *out_reason_code);
void bc_scoring_trigger_end_game(int reason_code);

/* Wire builders (in server_send.c) */
int bc_build_mission_init(u8 *buf, int buf_size,
                          u8 player_limit, u8 system_species,
                          u8 time_limit_minutes, i32 end_time_absolute,
                          u8 frag_limit);

int bc_build_score_change(u8 *buf, int buf_size,
                          i32 killer_id, i32 killer_kills, i32 killer_score,
                          i32 killed_id, i32 killed_deaths,
                          const bc_score_update_t *additional,
                          int additional_count);

int bc_build_score(u8 *buf, int buf_size,
                   i32 player_id, i32 kills, i32 deaths, i32 score);

int bc_build_end_game(u8 *buf, int buf_size, i32 reason);

int bc_build_restart_game(u8 *buf, int buf_size);
```

---

## Test Coverage

A complete scoring test suite should verify:

1. **Damage attribution roundtrip**: damage applied in `bc_combat_apply_damage()` shows up in the ledger.
2. **Kill credit assignment**: weapon kills, collision kills, and self-destructs all produce one SCORE_CHANGE with correct fields.
3. **Multi-contributor distribution**: when 3 players damage the same target before the kill, all 3 receive score and appear in the additional-contributors list.
4. **AI kill credit suppression**: an AI ship killing a player produces a death but no kill credit.
5. **Self-destruct**: produces death-only SCORE_CHANGE (`killer_player_id = 0`, no killer kills/score fields).
6. **End-game on frag limit**: kill that pushes a player to `kills == frag_limit` triggers END_GAME reason 2.
7. **End-game on score limit**: damage-attributed kill that pushes a player to `score >= frag_limit * 10000` triggers END_GAME reason 3.
8. **End-game on time**: timer tick after `current_time >= end_time_absolute` triggers END_GAME reason 1.
9. **Restart resets state**: after RESTART_GAME, all player entries have zero kills/deaths/score, ledger is empty, `game_over = false`.
10. **Join sequence**: joining player receives one MISSION_INIT followed by N SCORE messages.
11. **Reconnection preserves score**: a player who disconnects and rejoins sees their previous totals restored in SCORE on rejoin.

---

## Comparison to Script-Layer Scoring

Some implementations place scoring logic in the mission script layer, triggered by damage-application callbacks. This approach has a known coverage gap: when a ship death is delivered via per-tick health replication rather than a discrete damage event, the script-layer damage callback may never fire, and no SCORE_CHANGE is emitted on the wire — even though the explosion visual replicates correctly. The observable symptom is duplicate explosion events appearing on bystander clients with no accompanying score update.

OpenBC's design avoids this by:

- Recording attribution at the damage-computation site, not at the script-callback site.
- Triggering score broadcast on the kill event (`ObjectExplodingEvent`), not on the damage event.

The kill event is reliably observed regardless of damage path, and the ledger is reliably populated regardless of replication path. The two halves are decoupled, so weapon kills, collision kills, and self-destructs all produce SCORE_CHANGE consistently.

---

## Open Questions

- **Score rounding direction**: the spec rounds `(shield + hull) / 10.0` to the nearest integer. Truncation toward zero is also defensible; the difference is at most 1 point per contributor per kill. Choose one and test consistently.
- **Negative score clamping**: friendly-fire (team modes only) records negative damage in the ledger. The spec does not clamp final scores to 0 — negative totals are valid. UI should handle negative score display.
- **Ledger capacity policy**: `BC_MAX_CONTRIBUTORS` evict-lowest-on-overflow is a placeholder. The maximum sensible value equals the player limit (8). Sustained over-attribution would indicate a bug elsewhere.
- **AI kill counting on PvE scenarios**: scenario gamemodes may want AI ships to count toward a "team kills" total even though no player gets credit. This is scenario-specific and out of scope for the base server.

---

## See Also

- [docs/wire-formats/pythonevent-wire-format](../wire-formats/pythonevent-wire-format) — kill-event payload encoding
- [docs/planning/gamemode-system.md](../planning/gamemode-system.md) — broader gamemode design including team scoring
- [docs/bugs/bug-reports/20260529-server-side-scoring-improvement.md](../bugs/bug-reports/20260529-server-side-scoring-improvement.md) — bug report that motivates the server-side design
