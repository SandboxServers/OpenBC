# Gameplay Server Design Intent Analysis

## Source Context
All analysis grounded in reference scripts at:
- `/mnt/c/users/Steve/source/projects/STBC-Dedicated-Server/reference/scripts/Multiplayer/`
- `/mnt/c/users/Steve/source/projects/STBC-Dedicated-Server/src/scripts/Custom/DedicatedServer.py`
- `/mnt/c/users/Steve/source/projects/STBC-Dedicated-Server/docs/multiplayer-flow.md`

---

## 1. What Does the HOST Actually Do During Gameplay?

### Host-Only Logic (confirmed from scripts)

The host runs **three categories** of exclusive logic:

#### A. Scoring Authority
In ALL game modes (Mission1/2/3/5), `SetupEventHandlers()` has an `IsHost()` gate:

```python
# Mission1.py line 193-196 (identical pattern in Mission2/3/5)
if (App.g_kUtopiaModule.IsHost()):
    # Only hosts handling scoring.
    App.g_kEventManager.AddBroadcastPythonFuncHandler(
        App.ET_OBJECT_EXPLODING, pMission, __name__ + ".ObjectKilledHandler")
    App.g_kEventManager.AddBroadcastPythonFuncHandler(
        App.ET_WEAPON_HIT, pMission, __name__ + ".DamageEventHandler")
```

The host is the SOLE authority for:
- **Damage tracking** (`DamageHandler`): Accumulates damage-per-player in `g_kDamageDictionary` keyed by ship ObjID
- **Kill attribution** (`ObjectKilledHandler`): Determines who gets credit for the killing blow, awards kills/deaths
- **Score computation**: Converts accumulated damage into score points (damage/10.0), tracks per-player and per-team scores
- **Score broadcasting**: After computing scores, sends `SCORE_CHANGE_MESSAGE` to all clients via `SendTGMessageToGroup("NoMe", ...)`
- **Win condition checking**: `CheckFragLimit()` runs only on the host after each kill, calls `EndGame()` if frag/score limit reached
- **Time limit enforcement**: `UpdateTimeLeftHandler` checks `if App.g_kUtopiaModule.IsHost(): EndGame(END_TIME_UP)` when timer expires (MissionShared.py line 299)

#### B. Game State Initialization for Late Joiners
`InitNetwork(iToID)` runs ONLY on the host when a new player joins mid-game:
- Sends `MISSION_INIT_MESSAGE` with: player limit, system ID, time limit, frag limit, remaining time
- Sends current `SCORE_MESSAGE` for every player in the score dictionaries
- In team modes (Mission2/3/5), also sends `SCORE_INIT_MESSAGE` (with team assignment) and `TEAM_SCORE_MESSAGE`

#### C. AI Entity Creation (Mission5 only)
```python
# Mission5.py line 1428
if not App.g_kUtopiaModule.IsHost():
    return  # "The client isn't supposed to create AI ships"
```
The host creates and controls the Starbase AI entity. This is the ONLY game mode with server-side AI.

#### D. Message Relay for Team Assignment
In Mission2/3/5, when the host receives a `TEAM_MESSAGE` from a client:
```python
# Mission2.py line 413-419
if (App.g_kUtopiaModule.IsHost()):
    # If I'm the host, I have to forward this information to
    # everybody else so they'll know what team this player is on
    pNetwork = App.g_kUtopiaModule.GetNetwork()
    if (pNetwork):
        pCopyMessage = pMessage.Copy()
        pNetwork.SendTGMessageToGroup("NoMe", pCopyMessage)
```
The host explicitly relays team assignment messages. This confirms the host-as-relay model.

#### E. Game Lifecycle Control
- `EndGame()` is host-only: sends `END_GAME_MESSAGE` to all players, sets `ReadyForNewPlayers(0)`
- `RestartGameHandler` (host-only event): sends `RESTART_GAME_MESSAGE` to all players via `SendTGMessage(0, ...)` (target 0 = broadcast)

### What the Host Does NOT Do
The host does NOT:
- Validate ship positions or movement (each client simulates locally)
- Validate weapon firing (each client fires locally, damage events are C++ engine events)
- Arbitrate physics/collisions (each client runs its own ProximityManager)
- Validate ship creation parameters (clients choose ships independently)

**Design Intent**: The host was always intended as the scoring authority and game state master, not a full simulation server. The C++ engine handles the heavy simulation (physics, weapons, shields) locally on each client. The Python layer only manages scoring, win conditions, and game lifecycle -- things that need a single authoritative source.

---

## 2. Message Relay Model

### How Messages Flow

The network model is **hub-and-spoke with the host as relay**, NOT peer-to-peer.

#### TGMessage API Patterns Observed:

1. **`SendTGMessage(playerID, message)`** - Send to specific player
   - `playerID = 0` means broadcast to ALL (host sends to all clients, client sends to host who relays)
   - `playerID = specific` means targeted (e.g., `InitNetwork(iToID)` sends to a single joining player)

2. **`SendTGMessageToGroup("NoMe", message)`** - Send to everyone except myself
   - Used by host after computing scores to notify all clients
   - "NoMe" group is created by MultiplayerGame C++ class at startup

3. **No peer-to-peer**: There is NO evidence of clients sending directly to other clients. All Python message sends either:
   - Go to host (client->host): implicit, since clients only know the host's network address
   - Go from host to all: via `SendTGMessageToGroup("NoMe", ...)` or `SendTGMessage(0, ...)`
   - Go from host to one: via `SendTGMessage(specificID, ...)`

#### Phaser Fire Example: Client A fires at Client B
Based on the code analysis, the flow is:

1. **Client A** fires phasers locally (C++ engine handles weapon creation, projectile physics)
2. **C++ engine** broadcasts weapon-fire network message (this is opcode-level, below Python)
3. **Host receives** the weapon-fire; C++ engine creates the projectile locally
4. **Host relays** to Client B and all others (C++ engine handles this at the opcode level)
5. **Client B** receives weapon-fire, C++ engine creates projectile locally, applies damage
6. **If the hit kills B's ship**: C++ fires `ET_OBJECT_EXPLODING` event locally on ALL machines that have the ship
7. **Host's Python handler** (`ObjectKilledHandler`) catches `ET_OBJECT_EXPLODING`, computes scores, sends `SCORE_CHANGE_MESSAGE` to all

**Critical insight**: The weapon/damage simulation happens in C++ at the network opcode level (opcodes 0x00-0x28). The Python scoring layer sits ABOVE that -- it reacts to C++ events (`ET_WEAPON_HIT`, `ET_OBJECT_EXPLODING`) rather than driving the simulation. The host needs the C++ simulation running to generate these events.

#### Team Message Relay
The TEAM_MESSAGE pattern in Mission2/3/5 is the clearest proof of hub-relay:
```python
# A client sends team choice -> host receives it
# Host explicitly copies and forwards to everyone else
pCopyMessage = pMessage.Copy()
pNetwork.SendTGMessageToGroup("NoMe", pCopyMessage)
```

### Implications for Dedicated Server
For a pure relay server, C++ opcode-level messages (ship creation, movement, weapon fire) MUST be relayed blindly by the server. The server does not need to understand them -- it just needs to forward from sender to all other peers.

Python-level TGMessages (scoring, team assignment, game lifecycle) require the server to run the mission scripts, because the host's Python code is the authority.

---

## 3. Ship Creation on Server -- Real Objects vs Stubs?

### What the Scripts Actually Need

The scoring scripts call these methods on ship objects:

| Call | Where | Purpose |
|------|-------|---------|
| `App.ShipClass_Cast(pEvent.GetDestination())` | DamageHandler, ObjectKilledHandler | Cast event target to ship |
| `pShip.IsPlayerShip()` | DamageHandler, ObjectKilledHandler | Filter: only score player ships |
| `pShip.GetObjID()` | DamageHandler, ObjectKilledHandler | Key into g_kDamageDictionary |
| `pShip.GetNetPlayerID()` | ObjectKilledHandler, ObjectCreatedHandler | Map ship to player for scoring |
| `pShip.GetNetType()` | DamageHandler | Ship class for damage modifiers |
| `pShip.GetName()` | DoKillSubtitle, ResetEnemyFriendlyGroups | Display name / group membership |
| `pGame.GetShipFromPlayerID(id)` | DamageHandler, DoKillSubtitle | Reverse lookup: player -> ship |
| `pEvent.GetFiringPlayerID()` | DamageHandler, ObjectKilledHandler | Who fired the weapon |
| `pEvent.GetDamage()` | DamageHandler | Damage amount for scoring |
| `pEvent.IsHullHit()` | DamageEventHandler | Shield vs hull damage classification |

### Design Intent Analysis

These calls fall into two categories:

**Category 1: Data lookups (could be stubs)**
- `GetObjID()`, `GetNetPlayerID()`, `GetNetType()`, `GetName()`, `IsPlayerShip()` -- These are simple property getters. A lightweight stub that stores {objID, netPlayerID, netType, name, isPlayerShip} would satisfy these.

**Category 2: C++ event-driven (need simulation)**
- `pEvent.GetDestination()`, `pEvent.GetFiringPlayerID()`, `pEvent.GetDamage()`, `pEvent.IsHullHit()` -- These come FROM the C++ event system. The question is: does the C++ engine generate `ET_WEAPON_HIT` and `ET_OBJECT_EXPLODING` events on the host if the host doesn't have a full ship simulation?

**The hard truth**: In the original game, the host IS a full game client. The C++ engine simulates ships, weapons, damage, and shields on the host machine. `ET_WEAPON_HIT` and `ET_OBJECT_EXPLODING` are generated by the C++ collision/damage system, not by Python. If the host doesn't have real ship objects in the C++ engine, these events never fire, and the Python scoring handlers never get called.

### What This Means for OpenBC

**Option A: Full ship simulation on server** (what the original game does)
- Ships exist as real C++ objects with physics, shields, weapons
- C++ damage model generates ET_WEAPON_HIT / ET_OBJECT_EXPLODING events
- Python scoring scripts work unmodified
- Server needs ~60% of the game engine

**Option B: Event synthesis from network messages** (new approach for OpenBC)
- Server parses C++ opcode messages (weapon fire, damage) as they pass through
- Server synthesizes ET_WEAPON_HIT / ET_OBJECT_EXPLODING Python events from parsed opcode data
- Ship objects are lightweight stubs with property getters
- Server needs: network layer + Python scripting + stub objects

**Option C: Trust the clients for scoring events** (simplest, but different from original)
- Server just relays all messages
- One designated client runs the scoring Python (effectively becomes the logical host)
- Server tracks only connections, not game state
- Problem: the "host" client is authoritative, so disconnection = game over

**Recommendation from original dev perspective**: We probably would have gone with Option B for a clean reimplementation. Option A is what we actually shipped because the host WAS a client. Option C is what the DDraw proxy community solution does -- it makes the host machine run the full game in a hidden window.

---

## 4. What Game State Does the Server Track?

### State Tracked in Python (explicit in scripts)

#### Per-Player State (all modes):
- `g_kKillsDictionary[playerID]` = int (kill count)
- `g_kDeathsDictionary[playerID]` = int (death count)
- `g_kScoresDictionary[playerID]` = int (accumulated damage-based score)

#### Per-Ship State (transient):
- `g_kDamageDictionary[shipObjID][attackerPlayerID]` = [shieldDamage, hullDamage]
  - Created when ship first takes player damage
  - Deleted when ship is destroyed (ObjectKilledHandler clears it)
  - This is the ONLY "health-like" state the host tracks, and it's damage attribution, not actual HP

#### Per-Team State (Mission2/3/5 only):
- `g_kTeamDictionary[playerID]` = int (team number, 0 or 1)
- `g_kTeamScoreDictionary[teamNum]` = int (team score)
- `g_kTeamKillsDictionary[teamNum]` = int (team kills)

#### Game Lifecycle State:
- `g_bGameOver` = 0/1
- `g_bGameStarted` = 0/1
- `g_iTimeLeft` = float (seconds remaining)
- `g_pStartingSet` = Set object reference (the star system)

#### Mission5-Specific:
- `g_pStarbase` = ship object reference
- `g_bStarbaseDead` = 0/1
- `g_pAttackerGroup` = ObjectGroupWithInfo (list of attacker ship names)
- `g_bStarbaseCutsceneStarted` = 0/1

### State NOT Tracked by Python
The server/host does NOT track:
- Ship positions, velocities, orientations
- Shield status, hull HP, subsystem status
- Weapon charge, torpedo counts
- Power allocation
- Any continuous simulation state

All of that lives in the C++ engine on each client. The Python layer only tracks discrete events (kills, deaths, damage-done-for-scoring).

### Implications
The server's Python state is TINY. The entire gameplay state for an 8-player FFA is roughly:
- 8 entries in kills dict (32 bytes)
- 8 entries in deaths dict (32 bytes)
- 8 entries in scores dict (32 bytes)
- N entries in damage dict (grows with combat, cleared on kills)
- A few scalar flags

This is overwhelmingly a scoreboard server, not a simulation server.

---

## 5. Dedicated Server History -- Was It Intended?

### Evidence: YES, It Was Intended

The code is remarkably clear on this. The dedicated server feature was **designed, partially implemented, and shipped in a semi-functional state** in the retail game.

#### UI Evidence
`MultiplayerMenus.py` has a fully implemented "Dedicated Server" toggle button on the host creation screen:
```python
# Line 1628-1648
g_pHostDedicatedButton = CreateButton(pDatabase.GetString("Dedicated Server"),
    ET_DEDICATED_CLICKED, pPane, 0, HOST_DEDICATED_BUTTON_WIDTH,
    HOST_DEDICATED_BUTTON_HEIGHT)
g_pHostDedicatedButton.SetChoosable(1)
```

#### Config Persistence
The setting is saved/loaded from Options.cfg:
```python
App.g_kConfigMapping.SetIntValue("Multiplayer Options", "Dedicated Server",
    g_pHostDedicatedButton.IsChosen())
```

#### Host Start Logic
`HandleHostStartClicked` (line 2989-3006) explicitly handles the dedicated mode:
```python
if g_pHostDedicatedButton.IsChosen():
    App.g_kUtopiaModule.SetIsClient(0)  # Host but NOT a client
else:
    App.g_kUtopiaModule.SetIsClient(1)  # Host AND client (normal)
```

#### Dedicated Server Detection Pattern
Throughout the codebase, the pattern `IsHost() and (not IsClient())` is used EXTENSIVELY (found in 20+ locations) to mean "we are a dedicated server":
```python
# MissionMenusShared.py line 1554-1555
if (App.g_kUtopiaModule.IsHost() and not App.g_kUtopiaModule.IsClient()):
    # dedicated server. Hide the options window.
```

```python
# Mission2Menus.py line 146-147
if (App.g_kUtopiaModule.IsHost() and not App.g_kUtopiaModule.IsClient()):
    # dedicated servers do not chose teams.
```

#### Host Score Init Awareness
Mission1.py line 175 explicitly avoids adding the host as a player when it's a dedicated server:
```python
if (App.g_kUtopiaModule.IsHost() and App.g_kUtopiaModule.IsClient()):
    # Only add host to score dict if host is ALSO a client
    pNetwork = App.g_kUtopiaModule.GetNetwork()
    iPlayerID = pNetwork.GetHostID()
    if (not g_kKillsDictionary.has_key(iPlayerID)):
        g_kKillsDictionary[iPlayerID] = 0
```

#### NewPlayerHandler Comment
Mission1.py line 836 and Mission5.py line 1173 have this comment:
```python
# check if player is host and not dedicated server.  If dedicated server, don't
# add the host in as a player.
```

### What Was Missing
The dedicated server mode was designed to work where the host machine runs the full game engine in a window but the host user doesn't pick a ship or participate as a player. It was NOT designed as a headless/windowless mode:

1. **No headless mode**: The game always needed a window, a GPU, and the full rendering pipeline. "Dedicated" meant "the host machine isn't playing" not "no display needed."
2. **No command-line startup**: You had to click through the UI to set up a dedicated game.
3. **No remote administration**: Once the game started, the host saw a barebones options window.

### What We Would Have Done Differently

If we had time to ship proper dedicated server support, we would have:

1. **Command-line config**: Read server settings from a config file instead of requiring UI clicks
2. **Headless rendering**: Disable the rendering pipeline entirely (skip NIF loading, texture loading, etc.)
3. **Console output**: Log server events (joins, kills, score changes) to stdout
4. **Auto-restart**: Automatically restart the match when it ends
5. **No ship simulation**: Since the dedicated server doesn't need to SEE anything, we would have created lightweight ship stubs that satisfy the Python API without full NIF models or rendering
6. **RCON**: Some way to administer the server remotely (kick players, change map, etc.)

The DDraw proxy approach (community DedicatedServer.py) is actually a clever hack that achieves goals 1-3 by intercepting the rendering pipeline at the DDraw level and driving the game engine's initialization from Python.

---

## 6. Game Modes and Server Requirements

### Mission1 (FFA Deathmatch) -- RELAY SERVER: YES
- Host authority: scoring only
- No AI entities
- No team state
- Simplest mode: pure relay + scoreboard
- **Server requirements**: Message relay, Python scoring scripts, lightweight ship stubs or event synthesis

### Mission2 (Team Deathmatch) -- RELAY SERVER: YES (with relay enhancement)
- Same as FFA plus team dictionaries
- Host must relay `TEAM_MESSAGE` from sender to all others
- Team-based score computation (friendly fire penalty)
- **Server requirements**: Same as Mission1 + team state + explicit message relay for TEAM_MESSAGE

### Mission3 (Team Objectives / Fed vs Non-Fed) -- RELAY SERVER: YES (with relay enhancement)
- Structurally identical to Mission2 in the Python layer
- Same team relay pattern
- Different scoring formula (Fed vs Non-Fed ship classes)
- **Server requirements**: Same as Mission2

### Mission5 (Coop - Starbase Defense) -- RELAY SERVER: PARTIALLY
- **Starbase AI runs on the HOST** (Mission5.py `CreateStarbase()` line 1428: "The client isn't supposed to create AI ships")
- The starbase is a non-player ship with `StarbaseAI.CreateAI()` -- a compound AI that uses `AI.Compound.StarbaseAttack`
- The host creates the starbase, assigns it a random position, sets its AI
- All clients see the starbase because it's a networked object (the C++ engine replicates it)
- **Server requirements**: FULL ship simulation for the starbase entity, AI system, and all the C++ game logic for that NPC. This is NOT just a relay.

### What This Means for Phase 1
For the initial dedicated server, **support Mission1 (FFA) first**. It has the simplest requirements:
1. Relay all C++ opcode messages between clients
2. Run Python scoring scripts (ObjectKilledHandler, DamageEventHandler)
3. Handle InitNetwork for late joiners
4. Handle EndGame for time/frag limits

Mission2/3 add team state but are structurally the same.

Mission5 is a different beast entirely -- it requires server-side AI and NPC ship simulation. Defer this to a later phase.

---

## 7. What Can Go Wrong?

### A. Client Lies About Damage (Cheating)

In the original game, there is **zero anti-cheat**. The architecture is fundamentally trust-based:

- Each client runs its own damage simulation (C++ engine)
- The C++ engine fires `ET_WEAPON_HIT` events based on local collision detection
- The host's `ET_WEAPON_HIT` handler receives `pEvent.GetDamage()` which comes from the host's LOCAL simulation of the weapon impact
- BUT: the weapon projectile was created from a network message sent by the firing client

**The real question is: who determines the damage amount?**

In BC's lockstep model, all clients simulate the same physics. If Client A fires at Client B, ALL machines (including the host) see the projectile hit and compute damage locally. The host's locally-computed damage is what goes into the scoring system. So a client can't directly lie about damage values -- the host computes damage independently.

**However**, a client CAN cheat by:
- Sending fabricated weapon-fire messages (firing weapons that don't exist, or faster than allowed)
- Sending incorrect position updates (making their ship unhittable or teleporting behind enemies)
- Modifying their ship's stats (more HP, faster shields)

We never built countermeasures for this. The checksum system validates that everyone has the same SCRIPTS, but it doesn't validate runtime behavior. This was a known limitation; with a 2-8 player limit on LAN/broadband, we expected the social contract to handle cheaters. This was 2001 -- competitive multiplayer anti-cheat was barely a concept.

**For OpenBC**: The lockstep model means the host's local simulation IS the authority for scoring. As long as the server runs the damage simulation, it can't be fooled by clients lying about damage. The vulnerability is in the input layer (fabricated inputs), not the output layer (fabricated results).

### B. Two Clients Disagree About a Collision

This WILL happen. BC's networking sends position updates at intervals, and each client interpolates between updates. Two clients can legitimately compute different collision results because they're working with slightly different ship positions.

**Original behavior**: Each client resolves collisions locally. If Client A thinks they collided with Client B but Client B disagrees, both clients see different things. This is a visual-only desync for most collisions. Damage from collisions IS handled by the ProximityManager locally, so collision damage amounts may differ between clients.

**For scoring purposes**: The HOST's collision simulation is authoritative. If the host's ProximityManager says a collision happened, the damage event fires on the host, and the host records it in the scoring system. Other clients may see different visual results, but scores are consistent.

**This was a known issue we accepted.** With 2001 network latency, perfect collision agreement was impossible. The visual glitches were minor enough that most players didn't notice, and the scoring was consistent because the host was authoritative.

### C. Host Disconnects Mid-Game

**Original behavior**: **GAME OVER.** There is no host migration. If the host disconnects:

1. All clients lose their connection (the hub is gone)
2. Each client fires `ET_NETWORK_DELETE_PLAYER` for every other player
3. `DeletePlayerHandler` checks connection status and may preserve scores for display
4. Eventually, clients fall back to the multiplayer menu

This was the single biggest flaw in the multiplayer design. We knew it was bad. Host migration was discussed but cut for complexity/time reasons. The technical challenge was that the host held:
- The authoritative scoring state (all the score dictionaries)
- The network topology (all peer connections went through the host)
- The game lifecycle state (time remaining, game-over flag)

Migrating all of that to a new host mid-game was non-trivial, especially with 2001 network code.

**For OpenBC dedicated server**: This problem is SOLVED by the dedicated server model. The server IS the host and doesn't disconnect because a player leaves. This is one of the primary motivations for building the dedicated server.

### D. Destruction Message Lost

The scoring system uses `SetGuaranteed(1)` on ALL score-related messages:
```python
pMessage = App.TGMessage_Create()
pMessage.SetGuaranteed(1)  # Yes, this is a guaranteed packet
```

At the C++ level, `SetGuaranteed(1)` enables reliable delivery (the TGWinsockNetwork layer retransmits until acknowledged). So score messages should never be lost.

However, the C++ opcode-level messages (ship destruction animation, explosion effects) may NOT all be guaranteed. The visual destruction might not play on a client that missed the message, but the scoring will be correct because score updates are sent separately and reliably.

**What about the ET_OBJECT_EXPLODING event itself?** This is a LOCAL event fired by the C++ engine when a ship's HP reaches zero. It's not a network message -- it's the result of accumulated damage destroying the ship. If the damage was applied (via network messages that ARE relayed), the ship will eventually die on all machines. The timing might differ slightly, but the end result is deterministic: enough damage = ship dies.

**Edge case**: If a client disconnects exactly as their ship is dying, the `ObjectKilledHandler` on the host will still fire (because the host's copy of the ship still dies). The host will record the kill/death and broadcast scores. The disconnected client won't see the score update, but they've already disconnected, so it doesn't matter. When they rejoin, `InitNetwork` sends them the current scores.

---

## Summary Table: Server Requirements by Feature

| Feature | Relay Only | + Python Scripts | + Ship Stubs | + Full Sim |
|---------|-----------|-----------------|-------------|-----------|
| Player connections | X | | | |
| C++ opcode relay | X | | | |
| Checksum verification | X | | | |
| GameSpy/LAN browser | X | | | |
| Scoring (FFA) | | X | X | |
| Scoring (Team) | | X | X | |
| Team message relay | | X | | |
| Late-join state sync | | X | | |
| Game lifecycle (end/restart) | | X | | |
| Time limit enforcement | | X | | |
| Frag limit checking | | X | X | |
| Mission5 Starbase AI | | | | X |
| Anti-cheat damage validation | | | | X |

### Phase 1 Target: "Relay + Python Scripts + Ship Stubs"
This covers Mission1/2/3 fully. Mission5 is deferred.

The key technical challenge is the bridge between "relay" and "Python scripts":
the host's Python scoring handlers need `ET_WEAPON_HIT` and `ET_OBJECT_EXPLODING` events.
In the original game, these come from the C++ simulation.
In OpenBC, we need to either:
1. Run a lightweight C++ simulation that generates these events
2. Synthesize these events from the relayed network opcodes
3. Have clients report scoring events to the server (changes the trust model)

Option 2 is the most pragmatic for Phase 1. Parse weapon-hit and object-destroyed opcodes
from the network stream, create synthetic Python events, and feed them to the mission scripts.
