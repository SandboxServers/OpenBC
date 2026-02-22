# Bug Report: Self-Destruct Death Pipeline Sends Wrong Messages

**Date**: 2026-02-21
**Severity**: HIGH (3 of 5 anomalies are HIGH)
**Affected System**: Ship death pipeline, self-destruct handling
**Verified Against**: Stock dedicated server packet traces (proxy DLL instrumentation)

---

## Summary

When a player self-destructs (Ctrl+D / opcode 0x13), the OpenBC server sends the wrong
sequence of messages compared to stock behavior. This causes:
- No explosion animation (ship vanishes instantly)
- Server auto-respawns the player with wrong ownership (ghost ship)
- Client is forced into a ship it didn't pick

---

## Test Procedure

1. Start server, connect one client
2. Client spawns as Sovereign, waits ~10 seconds
3. Client presses Ctrl+D (self-destruct)
4. Observe client behavior during and after explosion
5. Compare packet traces between stock and OpenBC servers

---

## Anomalies

### 1. HIGH: ObjectExplodingEvent Has Wrong Fields

**Expected (stock)**:
| Field | Value |
|-------|-------|
| source | NULL (0x00000000) — no attacker |
| dest | Ship object ID (e.g., 0x3FFFFFFF) |
| lifetime | 9.5 seconds |

**Actual (OpenBC)**:
| Field | Value |
|-------|-------|
| source | Ship object ID (0x3FFFFFFF) — WRONG, should be NULL |
| dest | 0xFFFFFFFF (sentinel) — WRONG, should be ship ID |
| lifetime | 1.0 second — WRONG, should be 9.5 |

**Impact**: The client doesn't know which ship is exploding (dest is a sentinel, not a real
object ID), and the explosion animation is cut from 9.5s to 1.0s. The `source` and `dest`
fields are swapped — stock puts the dying ship in `dest` with `source=NULL` (no attacker
for self-destruct).

### 2. HIGH: DestroyObject (0x14) Should NOT Be Sent

**Expected (stock)**: Zero DestroyObject messages for any ship death. Verified across 59
combat kills (Battle of Valentine's Day trace, 33.5 minutes) and self-destruct tests. The
ship object lives as wreckage during the 9.5-second explosion animation.

**Actual (OpenBC)**: DestroyObject (0x14) sent 0.141 seconds after self-destruct request.
This immediately removes the ship from the game world before the explosion animation can
play, causing the ship to vanish instantly.

### 3. HIGH: Server Auto-Respawns (Should NOT)

**Expected (stock)**: No server-initiated respawn. After the 9.5s explosion, the client
returns to the ship selection screen. The **client** sends ObjCreateTeam (0x03) when the
player picks a new ship.

**Actual (OpenBC)**: Server sends ObjCreateTeam (0x03) automatically 5.0 seconds after
self-destruct. This respawn has wrong fields:
- `owner_slot = 0` (host's slot, not the client's slot 1)
- `team = 0` (wrong, client was team 2)
- Position far off-map: (-1959, -51, 333)

The client also picks its own ship and sends its own ObjCreateTeam, resulting in a
**double spawn** — the server's ghost ship plus the client's actual pick both exist.

### 4. MEDIUM: Respawn Uses Wrong owner_slot and team

**Expected**: If respawn were sent (it shouldn't be), it should use the dying player's
slot and team assignment.

**Actual**: Respawn uses `owner_slot=0` (host) and `team=0` instead of `owner_slot=1`
(client) and `team=2` (client's original team). This creates a ship owned by the wrong
player.

### 5. LOW: MissionInit totalSlots=1

**Expected (stock)**: `totalSlots=9` in MissionInit (0x35) message.

**Actual (OpenBC)**: `totalSlots=1`. This is cosmetic but may affect client-side player
list allocation or UI layout.

---

## Complete Timeline Comparison

### Stock Server

```
T+0.000s   C->S  0x13 HostMsg (self-destruct, 1 byte)
T+0.004s   S->C  0x06 ObjectExplodingEvent (source=NULL, dest=ship, lifetime=9.5)
T+0.004s   S->C  0x36 SCORE_CHANGE (deaths+1)
T+0.004s   S->C  4x 0x06 TGSubsystemEvent (repair list)
           --- 9.5s explosion animation, StateUpdates continue ---
T+9.498s   S->C  2x 0x06 TGSubsystemEvent (debris damage)
           --- client returns to spawn menu ---
           --- client disconnected (chose not to respawn in test) ---
```

### OpenBC Server

```
T+0.000s   C->S  0x13 HostMsg (self-destruct, 1 byte)
T+0.016s   S->C  22x 0x06 TGSubsystemEvent (repair list)
T+0.125s   S->C  3x 0x06 TGSubsystemEvent
T+0.125s   S->C  0x06 ObjectExplodingEvent (source=SHIP, dest=0xFFFFFFFF, lifetime=1.0)
T+0.141s   S->C  0x14 DestroyObject (ship removed)              *** SHOULD NOT BE SENT ***
T+0.141s   S->C  0x36 SCORE_CHANGE (deaths+1)
           --- ship vanishes, client goes to spawn menu ---
T+5.047s   S->C  0x03 ObjCreateTeam (owner=0, team=0)           *** SHOULD NOT BE SENT ***
           --- client is forced into ghost ship ---
T+10.766s  C->S  0x03 ObjCreateTeam (client's actual pick)
           --- double spawn: ghost ship + client's ship both exist ---
```

---

## Root Cause Analysis

1. **ObjectExplodingEvent fields**: The event construction has source/dest swapped and uses
   a hardcoded lifetime of 1.0 instead of 9.5.

2. **DestroyObject**: The death handler is calling object destruction and broadcasting 0x14.
   Stock behavior keeps the ship alive as wreckage — it is never explicitly destroyed.

3. **Auto-respawn**: The server has a respawn timer or handler that creates a new ship after
   death. Stock behavior relies entirely on the client to initiate respawn.

4. **Wrong owner_slot**: The auto-respawn code uses the host's player slot (0) instead of
   the dead player's slot. This also applies to team assignment.

---

## Fix Priority

1. **Fix ObjectExplodingEvent fields** — swap source/dest, set lifetime=9.5
2. **Remove DestroyObject (0x14) from death pipeline** — do not destroy ship objects on death
3. **Remove auto-respawn** — let the client send ObjCreateTeam when ready
4. **Fix respawn ownership** (will be moot after removing auto-respawn, but relevant if
   combat-kill auto-respawn also has this bug)
5. **Set MissionInit totalSlots=9** (low priority)

---

## Related Documents

- **[self-destruct-system.md](../../game-systems/self-destruct-system.md)** — Self-destruct behavioral spec
- **[ship-death-lifecycle.md](../../game-systems/ship-death-lifecycle.md)** — Ship death sequence spec
- **[pythonevent-wire-format.md](../../wire-formats/pythonevent-wire-format.md)** — ObjectExplodingEvent wire format
