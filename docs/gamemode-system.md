# Gamemode System — Clean-Room Behavioral Spec

Behavioral specification for Bridge Commander's multiplayer gamemode system. Covers scoring, game flow messages, team support, and end/restart conditions. Suitable for clean-room reimplementation.

## Architecture

The gamemode system is split into two layers:

- **Transport layer** (mission-agnostic): network messaging, object lifecycle, event dispatch, state sync. Knows nothing about scoring or gamemodes.
- **Game logic layer** (gamemode-specific): scoring, time/frag limits, team assignment, end game, restart. All gamemode logic is server-side.

The server is the authority for all scoring decisions. Clients are passive receivers of score data.

## Available Gamemodes

Three gamemodes ship with stock BC 1.1 that do not require AI:

| ID | Name | Type | Teams | Description |
|----|------|------|-------|-------------|
| mission1 | Deathmatch | FFA | No | Free-for-all. Kill count and damage score determine winner. |
| mission2 | Team Deathmatch | Team | 2 generic | Two teams ("Team 1" / "Team 2"). Team kills and score determine winner. |
| mission3 | Team Deathmatch (Faction) | Team | 2 faction | Identical to mission2 but teams are labeled "Federation" / "Non-Federation". |

A fourth shipped gamemode (mission5, "Starbase Defense") requires in-game AI for the starbase and is excluded from this spec.

### Mission2 vs Mission3

Mission3 is functionally identical to Mission2. The only difference is cosmetic: team labels display faction names ("Federation" / "Non-Federation") instead of generic names ("Team 1" / "Team 2"). Team assignment is still player-chosen in both modes — Mission3 does NOT auto-assign teams based on ship faction.

## Game Flow

### Server Configuration

The server selects:
- **Gamemode**: mission script path (e.g., `"Multiplayer.Episode.Mission1.Mission1"`)
- **Map**: system species ID (1-10, corresponding to multiplayer arena maps)
- **Player limit**: 1-8
- **Time limit**: minutes (-1 = no limit)
- **Frag limit**: kill count (-1 = no limit)
- **Score limit mode**: if enabled, limit is `fragLimit * 10000` points instead of kills

### Player Join Sequence

When a new player joins mid-game:

1. Server sends **MISSION_INIT** message with game settings (map, time, frag limit)
2. Server sends **score sync** messages for each existing player:
   - FFA: one SCORE message per player (kills, deaths, score)
   - Team: one SCORE_INIT message per player (kills, deaths, score, team), plus TEAM_SCORE messages for each team
3. Server replicates all existing game objects to the new player

### End Game Triggers

The game ends when any of these conditions is met (checked by the server):

| Trigger | Reason Code | Description |
|---------|-------------|-------------|
| Time expires | 1 (TIME_UP) | Countdown reaches zero |
| Frag limit (FFA) | 3 (SCORE_LIMIT_REACHED) | Any player reaches kill count or score threshold |
| Frag limit (Team) | 3 (SCORE_LIMIT_REACHED) | Any team reaches kill count or score threshold |

When triggered:
1. Server sets `gameOver = true`
2. Server broadcasts **END_GAME** message with reason code to all players
3. Server stops accepting new connections (`ReadyForNewPlayers = false`)
4. All clients clear ships and show end-game dialog

### Restart Flow

1. Host initiates restart (from UI)
2. Server broadcasts **RESTART_GAME** message to all players
3. All nodes:
   - Zero all scoring values (keys preserved — players stay in the game)
   - Clear `gameOver` flag
   - Delete all player ships and torpedoes
   - Reset time countdown (`timeLeft = timeLimit * 60`)
   - Show ship selection screen

## Scoring System

### Damage Tracking (Server Only)

The server tracks cumulative damage dealt by each player to each ship:

```
damageLedger[targetShipID][attackerPlayerID] = {shieldDamage, hullDamage}
```

**Rules:**
- Only damage to player-controlled ships is tracked (NPC/AI ships ignored)
- Both shield and hull damage are tracked separately
- A ship class modifier is applied to damage: `damage *= classModifier(attackerClass, targetClass)`
  - In stock BC 1.1, all flyable ships are class 1, so the modifier is always 1.0
  - The modifier table exists for mod support (e.g., class 2 ships get 3x bonus vs class 1)
- **Team friendly fire**: if attacker and target are on the same team, damage is recorded as **negative**, reducing the attacker's score

### Kill Processing (Server Only)

When a player ship is destroyed:

1. **Kill credit**: awarded to the player who fired the killing shot
2. **Death**: +1 death to the destroyed ship's owner
3. **Score distribution**: ALL players who damaged the destroyed ship receive score:
   - `score = (shieldDamage + hullDamage) / 10.0`
   - Each contributing player's running score total is updated
4. **FFA kill**: +1 frag to the killer (always)
5. **Team kill**: +1 frag to the killer ONLY if killer and killed are on different teams. +1 team kill to the killer's team.
6. **Team score**: each contributing player's damage score is also added to their team's score total
7. **No-player kills**: if `killerPlayerID == 0` (self-destruct, environmental damage), no frag is awarded but death still counts
8. **Damage ledger cleanup**: the destroyed ship's entry is deleted from the damage ledger (no memory leak)

### Frag/Score Limit Check

After every kill, the server checks:

**FFA (Deathmatch)**:
- Score limit mode: `any player score >= fragLimit * 10000` → end game
- Frag limit mode: `any player kills >= fragLimit` → end game

**Team (Team Deathmatch)**:
- Score limit mode: `any team score >= fragLimit * 10000` → end game
- Frag limit mode: `any team kills >= fragLimit` → end game

### Score Preservation

When a player disconnects, their scoring entries are **preserved** (not deleted). If the player reconnects, their previous kills, deaths, and score are synced back to them. This prevents score loss from brief disconnections.

### Winner Determination

At end of game:
- **FFA**: player with highest score wins. Ties are possible (multiple winners displayed).
- **Team**: team with highest score wins.

## Team System (Mission2 / Mission3 Only)

### Team Assignment

- Players choose their team via the ship selection UI
- Client sends a **TEAM** message to the server with their chosen team (0 or 1)
- Server forwards the TEAM message to all other players
- Team assignment is stored in a dictionary: `teamDict[playerID] = teamID`
- Team assignment persists across respawns within the same round
- Team assignment resets on game restart

### Team Scoring

Additional tracking for team modes:
- `teamKills[teamID]` — total enemy kills by team members
- `teamScore[teamID]` — sum of all team members' damage scores
- Two teams initialized at game start: team 0 and team 1, both starting at 0

### Same-Team Detection

Two players are on the same team if:
1. Both have non-zero player IDs
2. Both have entries in the team dictionary
3. Their team values are equal

If either player has no team assignment, they are treated as NOT on the same team.

## Wire Formats

All messages use guaranteed (reliable) delivery. Data is serialized into a byte stream. The first byte is always the message type constant.

### MISSION_INIT (type 0x35) — Server → Joining Client

```
[byte:0x35]
[byte:playerLimit]          # max players (1-8)
[byte:systemSpecies]        # map/system ID
[byte:timeLimitOrNone]      # 255 = no time limit, else limit in minutes
  [if != 255: int32:endTime] # absolute game clock value when round ends
[byte:fragLimitOrNone]      # 255 = no frag limit, else kill/score threshold
```

Size: 4-8 bytes. The `endTime` field is the absolute game clock value at which the round should end. The client computes remaining time as `endTime - currentGameTime`.

### SCORE_CHANGE (type 0x36) — Server → All Clients

Sent on kill events.

> **Known anomaly**: In stock dedicated server traces, SCORE_CHANGE is sent for collision
> kills but NOT for weapon kills. A 33.5-minute session with 59 weapon kills produced
> zero SCORE_CHANGE messages. This may be a stock server bug — the kill handler may not
> be triggered for all death paths. Implementations should ensure SCORE_CHANGE is sent
> for ALL kill types (collision, weapon, explosion, self-destruct).

```
[byte:0x36]
[int32:killerPlayerID]       # 0 if no player (self-destruct)
  [if killerPlayerID != 0:
    int32:killerKills         # updated kill count
    int32:killerScore]        # updated total score
[int32:killedPlayerID]       # player who died
[int32:killedDeaths]         # updated death count
[byte:additionalCount]      # N additional players with score changes
  [N times:
    int32:playerID
    int32:playerScore]
```

Size: variable, minimum 10 bytes. The "additional" entries are damage contributors other than the killer whose scores changed due to the kill event.

### SCORE (type 0x37) — Server → Joining Client (FFA only)

Full score sync for one player. Sent once per existing player during player join.

```
[byte:0x37]
[int32:playerID]
[int32:kills]
[int32:deaths]
[int32:score]
```

Size: 17 bytes fixed.

### END_GAME (type 0x38) — Server → All

```
[byte:0x38]
[int32:reason]               # end game reason code (0-6)
```

Size: 5 bytes fixed.

End game reason codes:
- 0: generic (game just ended)
- 1: time expired
- 2: frag limit reached
- 3: score limit reached
- 4: starbase destroyed (Mission5, AI-dependent)
- 5: borg destroyed (cut content)
- 6: enterprise destroyed (cut content)

### RESTART_GAME (type 0x39) — Server → All

```
[byte:0x39]
```

Size: 1 byte fixed. No payload.

### SCORE_INIT (type 0x3F) — Server → Joining Client (Team modes only)

Extended SCORE message that includes team assignment.

```
[byte:0x3F]
[int32:playerID]
[int32:kills]
[int32:deaths]
[int32:score]
[byte:teamID]               # 0 or 1 (255 = no team assigned)
```

Size: 18 bytes fixed.

### TEAM_SCORE (type 0x40) — Server → All Clients (Team modes only)

```
[byte:0x40]
[byte:teamID]               # 0 or 1
[int32:teamKills]
[int32:teamScore]
```

Size: 10 bytes fixed.

### TEAM (type 0x41) — Client → Server, then Server → All Clients

Client sends team selection. Server stores it and forwards to all other players.

```
[byte:0x41]
[int32:playerID]
[byte:teamID]               # 0 or 1
```

Size: 6 bytes fixed.

## Event Flow Summary

### Per-Kill Event Chain (Server)

```
1. Ship destroyed → ET_OBJECT_EXPLODING event fires
2. ObjectKilledHandler:
   a. Skip if gameOver flag is set
   b. Skip if destroyed object is not a player ship
   c. Get killerPlayerID from event
   d. Award kill/death (frag only if different team in team modes)
   e. Compute score for ALL damage contributors from damage ledger
   f. Update team scores (team modes)
   g. Broadcast SCORE_CHANGE to all clients
   h. Broadcast TEAM_SCORE for each team (team modes)
   i. Delete damage ledger entry for destroyed ship
   j. Check frag/score limit → EndGame if exceeded
```

### Per-Damage Event Chain (Server)

```
1. Weapon hits ship → ET_WEAPON_HIT event fires
2. DamageEventHandler:
   a. Skip if no firing player (playerID == 0)
   b. Skip if target is not a player ship
   c. Apply class modifier to damage
   d. Negate damage if same team (friendly fire penalty)
   e. Accumulate in damage ledger: damageLedger[targetShipID][attackerPlayerID]
```

### Player Join Event Chain (Server)

```
1. New player connects → checksums pass → ET_NEW_PLAYER_IN_GAME
2. NewPlayerHandler: initialize empty scoring entries (kills=0, deaths=0)
3. InitNetwork(playerID):
   a. Send MISSION_INIT with game settings
   b. Send score sync for each existing player:
      - FFA: SCORE per player
      - Team: SCORE_INIT per player + TEAM_SCORE per team
4. C++ layer: replicate all existing game objects to new player
```

## Server Implementation Notes

### State the Server Must Track

**All modes:**
- `kills[playerID]` — int, kills per player
- `deaths[playerID]` — int, deaths per player
- `scores[playerID]` — int, total score per player
- `damageLedger[shipObjID][attackerPlayerID]` — (float, float), shield/hull damage
- `gameOver` — bool
- `timeLeft` — float, seconds remaining (-1 if no time limit)
- `fragLimit` — int (-1 if no limit)
- `useScoreLimit` — bool (if true, limit is fragLimit * 10000 points)

**Team modes additionally:**
- `teams[playerID]` — int (0 or 1)
- `teamKills[teamID]` — int
- `teamScores[teamID]` — int

### Messages the Server Must Send

| When | Message | Destination |
|------|---------|-------------|
| Player joins | MISSION_INIT (0x35) | Joining player |
| Player joins (FFA) | SCORE (0x37) × N | Joining player |
| Player joins (Team) | SCORE_INIT (0x3F) × N + TEAM_SCORE (0x40) × 2 | Joining player |
| Player ship killed | SCORE_CHANGE (0x36) | All other players |
| Player ship killed (Team) | TEAM_SCORE (0x40) × 2 | All other players |
| Game ends | END_GAME (0x38) | All players (broadcast) |
| Game restarts | RESTART_GAME (0x39) | All players (broadcast) |

### Messages the Server Must Handle

| Message | From | Action |
|---------|------|--------|
| TEAM (0x41) | Client | Store team assignment, forward to all other clients |
| RESTART_GAME (0x39) | Self (UI trigger) | Broadcast to all, reset all state |

### Ship Class Modifier Table

```
         Target Class 0  Class 1  Class 2
Attacker
Class 0:      1.0         1.0      1.0
Class 1:      1.0         1.0      1.0
Class 2:      1.0         3.0      1.0
```

All 16 flyable ships in stock BC 1.1 are class 1. The table is extensible for mods.
