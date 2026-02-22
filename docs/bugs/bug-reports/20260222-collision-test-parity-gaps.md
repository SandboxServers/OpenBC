# Bug Report: Collision Test Parity Gaps (Stock Dedi vs OpenBC)

**Date**: 2026-02-22
**Severity**: HIGH (2 HIGH, 3 MEDIUM, 1 LOW)
**Status**: OPEN
**Affected Systems**: Collision damage, ship respawn, subsystem events, scoreboard
**Verified Against**: Stock BC dedicated server packet traces (proxy DLL instrumentation)

---

## Summary

Comparative packet trace analysis between stock BC dedicated server and OpenBC, using the
same test scenario: client connects, spawns Sovereign, collides with environment until
death, respawns, and collides again. Wire format encoding is byte-for-byte correct for all
tested opcodes. Six behavioral gaps identified.

---

## Test Procedure

1. Start stock dedi / OpenBC server with collision damage enabled
2. Connect one client, spawn as Sovereign
3. Fly into environment object (asteroid/station) until hull reaches zero
4. After explosion animation, respawn as Sovereign
5. Collide again after respawn
6. Compare packet traces and server logs

---

## Bug 1: Post-Respawn Collision Ownership Failure {#bug-1}

**Severity**: HIGH
**Issue**: After respawn, ALL collision effects are silently rejected

After a player respawns (new ObjCreateTeam with obj=0x40000021), the server's ship-to-player
ownership mapping still points to the dead ship (obj=0x3FFFFFFF). All collision validation
fails because the sender's ship ID doesn't match the target's ship ID.

**Observed**: 7/7 post-respawn collisions rejected:
```
[WARN] collision ownership fail (sender=0x3FFFFFFF src=0 tgt=0x40000021)
```

**Expected**: Collisions should process normally after respawn.

**Fix**: When ObjCreateTeam (0x03) arrives for an existing player slot, update the
player-to-ship-ID mapping to the new object ID.

---

## Bug 2: Subsystem Object ID Allocation Range {#bug-2}

**Severity**: HIGH
**Issue**: Subsystem TGObject IDs allocated from wrong range

Subsystem IDs must be in the player's range (base = 0x3FFFFFFF + N x 0x40000) for clients
to resolve them via ReadObjectRef. Currently assigned sequential global IDs that the client's
hash table doesn't contain.

**Observed**:
```
Stock:   source_obj_id = 0x40000002 (player 1's range)
OpenBC:  source_obj_id = 0x00000010 (global counter)
```

All 14 stock SubsystemDamageEvents use 0x40xxxxxx IDs. All 4 OpenBC events use 0x000000xx IDs.
Client drops all events silently — no visual damage feedback.

**Fix**: Allocate subsystem IDs from the player's ID range during ship initialization.

---

## Bug 3: Missing DeletePlayerUI (0x17) at Join {#bug-3}

**Severity**: MEDIUM
**Issue**: Server doesn't send DeletePlayerUI after NewPlayerInGame

Stock server sends DeletePlayerUI (0x17) after NewPlayerInGame (0x2A), carrying event
ET_NEW_PLAYER_IN_GAME (0x008000F1). This adds the player to the engine's internal
TGPlayerList, required for scoreboard display.

**Observed**:
- Stock: 1x DeletePlayerUI at join time
- OpenBC: 0x DeletePlayerUI at any point

**Fix**: After processing NewPlayerInGame (0x2A), send DeletePlayerUI (0x17) for the joining
player to that client. Wire format: 18 bytes (factory 0x0866, event 0x008000F1, src=0,
tgt=ship_obj_id, wire_peer_id).

See [delete-player-ui-wire-format.md](../wire-formats/delete-player-ui-wire-format.md).

---

## Bug 4: Spurious Explosion on Collision Kill {#bug-4}

**Severity**: MEDIUM
**Issue**: Server sends Explosion (0x29) for collision-induced kills

Stock server does NOT send Explosion (0x29) for collision-induced ship deaths — only
ObjectExplodingEvent (0x06, factory 0x8129). OpenBC sends both, which may cause a
double-explosion visual on the client.

**Observed**:
- Stock collision kill: 0 Explosion messages
- OpenBC collision kill: 1 Explosion message

**Fix**: When processing a ship death from collision damage, send only ObjectExplodingEvent.
Do not send Explosion (0x29). Explosion should only be sent for weapon kills.

> Note: Weapon kill behavior still needs trace verification (MEDIUM confidence).

---

## Bug 5: Too Few SubsystemDamage Events at Death {#bug-5}

**Severity**: MEDIUM
**Issue**: Insufficient per-subsystem ADD_TO_REPAIR_LIST events on collision death

Stock server sends ~13 ADD_TO_REPAIR_LIST PythonEvents per collision death (one per
damaged subsystem). OpenBC sends only 3-5. Missing events mean the client's repair queue
is incomplete and subsystem damage indicators may not display.

**Observed**:
- Stock death: 13 TGSubsystemEvent (factory 0x0101, code 0x800000DF)
- OpenBC death: 4 TGSubsystemEvent (1 at death + 3 pre-death)

Stock subsystem indices: 2, 13, 14, 17, 18, 31, 32, 28, 5, 25, 26, 3, 1
OpenBC subsystem indices: 16, 19, 30, 42 (INVALID)

**Fix**: On collision death, iterate all subsystems and generate ADD_TO_REPAIR_LIST for
each subsystem whose condition decreased below maximum. Subsystem IDs must be in the
player's ID range (see Bug 2).

---

## Bug 6: MissionInit maxPlayers Byte {#bug-6}

**Severity**: LOW
**Issue**: MissionInit sends maxPlayers=7 instead of 8

Stock MissionInit (0x35) byte[0] is 0x08 (8 players). OpenBC sends 0x07 (7 players).

```
Stock:   35 08 01 FF FF
OpenBC:  35 07 01 FF FF
```

**Fix**: Verify the totalSlots calculation. If the server supports 8 player slots,
MissionInit should report 8.

---

## Confirmed Wire Format Matches

These opcodes are **byte-for-byte identical** between stock and OpenBC:

| Opcode | Name | Evidence |
|--------|------|----------|
| Transport | Header, ACK, reliable/unreliable | Framing bytes match |
| 0x03 | ObjCreateTeam | 118-byte payload identical |
| 0x15 | CollisionEffect | Factory ID, event code, contact encoding, force float |
| 0x36 | ScoreChange | `36 00 00 00 00 02 00 00 00 01 00 00 00 00` |
| 0x00 | Settings | Field order, bit byte (0x61), map string |
| 0x01 | GameInit | Single byte, no payload |
| 0x1C | StateUpdate | Dirty flags, CF16, CompressedVector format |
| 0x20-0x27 | Checksum exchange | All 5 rounds |
| 0x06 (0x8129) | ObjectExplodingEvent | Factory, event code, field layout |
| 0x06 (0x0101) | TGSubsystemEvent | Factory, event code, field layout (IDs wrong) |

---

## Related Documents

- [collision-damage-event-chain.md](../collision-damage-event-chain.md) — Collision → PythonEvent chain
- [ship-death-lifecycle.md](../../game-systems/ship-death-lifecycle.md) — Ship death/respawn sequence
- [wire-format-audit.md](../../network-flows/wire-format-audit.md) — Per-opcode match/mismatch table
- [delete-player-ui-wire-format.md](../../wire-formats/delete-player-ui-wire-format.md) — DeletePlayerUI wire format
