# Bug Report: Explosion (0x29) Over-Emission From Per-Tick Combat

**Date**: 2026-05-29
**Severity**: MEDIUM (duplicate explosion visuals on bystander clients; not a desync, but a parity gap)
**Status**: NOT FIXED — emission-site audit required.
**Affected Systems**: Server-side explosion emission, beam/torpedo kill handlers, explosion relay
**Verified Against**: STBC.exe live decompile (FUN_00595C60, FUN_006A02A0, FUN_006A1E70), Pass 1 host-event-emission catalog 2026-05-29

---

## Summary

OpenBC emits opcode 0x29 (Explosion) from per-tick combat / kill handlers (beam-kill at
`server_dispatch.c:698`, torpedo-kill at `server_dispatch.c:797`) AND relays
client-originated 0x29 to other peers (`server_dispatch.c:1236`). Stock STBC.exe
does NOT emit 0x29 from any per-tick combat path. The **only** stock emission sites
are the two catch-up paths:

1. `MultiplayerGame__RequestObjHandler @ 0x006A02A0` — sends all attached
   explosions to a peer requesting object state (object-recovery flow).
2. `NewPlayerInGameHandler @ 0x006A1E70` — sends all attached explosions to a
   newly joined peer during the initial-join roster sync.

Both go through the single emit function `DamageableObject__SendExplosions_0x29 @
FUN_00595C60`, sent unreliably to a SPECIFIC peer's session (not to a group).

Stock per-tick explosion damage replicates via opcode **0x1C StateUpdate** (subsystem
health round-robin) and via the **0x02/0x03 ObjCreate** initial damageable-object
state — never via per-tick 0x29 emissions.

The OpenBC bug here is therefore **over-emission**, not over-relay. The classification
was incorrectly framed as over-relay in `20260529-per-handler-relay-cascade.md`
pre-revision.

---

## Symptom

Bystander clients may observe duplicate explosion visuals when a kill occurs:

- Once from the OpenBC server's per-tick 0x29 emission (the kill handler fires
  `bc_send_to_all` of a freshly-built explosion blob).
- Once again from downstream replication (the dying ship's StateUpdate transitions
  through the death threshold, the local client's death handler spawns its own
  client-side visual explosion).

Severity is medium rather than high because the duplicate is a visual-only artifact,
not a state divergence. The damage was already applied authoritatively server-side
before the 0x29 was sent. But the duplicate visuals are observably wrong vs stock.

---

## Evidence

### Stock binary anchors (Pass 1, byte-anchored)

**Function**: `DamageableObject__SendExplosions_0x29 @ FUN_00595C60`

This is the SOLE function in stock STBC.exe that emits opcode 0x29 on the wire. It
walks the per-DamageableObject attached-explosions list (`object+0x13C`, with count
at `+0x140`), serializing each explosion as:

```
[0x29]                      opcode
[u32 originatorObjectID]    via FUN_006CF930
[CV4 position]              compressed 4-byte vec3
[CF16 radius]               compressed 16-bit float
[CF16 damageRate]           compressed 16-bit float
```

Sent via `TGWinsockNetwork_SendTGMessage(peerSession, msg, 0)` — **UNRELIABLE**, direct
send to a specific peer's session (NOT to a group).

The two callers, both catch-up paths:

| Caller | Address | When invoked |
|--------|---------|--------------|
| `MultiplayerGame__RequestObjHandler` | 0x006A02A0 | Peer requests object state via opcode 0x1E |
| `NewPlayerInGameHandler` | 0x006A1E70 | New peer completes join handshake (opcode 0x2A) |

There is no per-tick emission in stock. `WeaponHitHandler @ 0x005AF010`,
`ApplyWeaponDamage @ 0x005AF420`, `DoDamage @ FUN_00594020`, and
`ProcessDamage @ FUN_00593E50` do NOT emit 0x29. Per-tick explosion damage volumes are
applied locally; they replicate to other peers via:

- Subsystem HP changes → opcode 0x1C StateUpdate round-robin
- Damage volume state → opcode 0x02/0x03 ObjCreate (full state on initial join)
- Death visual → opcode 0x06 PythonEvent OBJECT_EXPLODING (0x0080004E) reliable, NoMe

### OpenBC current emission sites

**Per-tick kill emissions (incorrect)**:

`src/server/server_dispatch.c:685-699` (beam-kill path inside `bc_beam_hit_callback`):
```c
/* Send Explosion (0x29) -- visual effect for the kill. */
{
    u8 boom[16];
    int blen = bc_build_explosion(boom, sizeof(boom),
                                   target->ship.object_id,
                                   target->ship.pos.x,
                                   target->ship.pos.y,
                                   target->ship.pos.z,
                                   damage, 300.0f);
    if (blen > 0) bc_send_to_all(boom, blen, true);
}
```

`src/server/server_dispatch.c:787-798` (torpedo-kill path inside
`bc_torpedo_hit_callback`):
```c
/* Send Explosion (0x29) -- visual effect for the kill. */
{
    u8 boom[16];
    int blen = bc_build_explosion(boom, sizeof(boom),
                                   target->ship.object_id,
                                   impact_pos.x, impact_pos.y, impact_pos.z,
                                   damage, damage_radius > 0.0f ? damage_radius : 300.0f);
    if (blen > 0) bc_send_to_all(boom, blen, true);
}
```

Both are server-originated **per kill event**, broadcast to **all peers** reliably.
Stock does neither of those things (stock per-kill 0x29 emission: zero; stock target:
specific peer; stock reliability flag: 0 = unreliable).

**Client-relay path**:

`src/server/server_dispatch.c:1229-1238` (`BC_OP_EXPLOSION` case):
```c
case BC_OP_EXPLOSION: {
    bc_explosion_event_t ev;
    if (bc_parse_explosion(payload, payload_len, &ev)) { /* log */ }
    bc_relay_to_others(peer_slot, payload, payload_len, true);
    break;
}
```

This relays client-originated 0x29 to other peers. Per Pass 1, **stock host does not
relay 0x29** — the stock receive handler `Explosion_Net @ FUN_006A0080` is LOCAL-ONLY
(no SendToGroup). Note however that under Pass 1's framing, clients don't routinely
ORIGINATE 0x29 either; 0x29 is server-originated only. If clients are sending 0x29 in
OpenBC's current setup, that's a separate audit question (likely an artifact of
clients echoing server-sent 0x29 back, or a mod behavior).

> **Note on the `20260529-per-handler-relay-cascade.md` line-1533 reference**: the
> previous "lines 698, 797, 1533" footnote in that bug report actually refers to the
> `bc_build_python_exploding_event` call at 1533, which is an OBJECT_EXPLODING (0x06)
> emission, not a 0x29 Explosion. The accurate 0x29 emission sites for OpenBC's
> server-originated paths are **lines 692 and 791 only**. The relay at line 1236 is a
> distinct concern.

### STBC RE memo

`C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\host-event-emission-catalog-20260529.md`
— Section 3 ("Explosion / Death Path Catalog"):

> **Opcode 0x29 Explosion is ONLY sent during catch-up replay**, never as per-tick
> combat damage. The TWO callers of FUN_00595C60 are:
> 1. `MultiplayerGame__RequestObjHandler @ 0x006A02A0` — sends all attached
>    explosions to a peer requesting object state.
> 2. `NewPlayerInGameHandler @ 0x006A1E70` — sends all attached explosions to a
>    newly joined peer.

---

## Root Cause

OpenBC's beam-kill and torpedo-kill paths were architected around the assumption that
0x29 is the "kill visual" emission. Pass 1 shows that's not the stock model. The stock
kill-visual emission is OBJECT_EXPLODING via opcode 0x06 PythonEvent (which OpenBC
also emits — see the `bc_build_python_exploding_event` calls at 678-682 and 776-784).
The 0x06 PythonEvent IS the wire signal for "ship is exploding"; the 0x29 emission
adds a duplicate visual.

The relay of client-originated 0x29 in `BC_OP_EXPLOSION` is a legacy mirror of the
older transport-layer-relay model and is similarly not present in stock.

---

## Affected Files

- `src/server/server_dispatch.c` — per-tick 0x29 emission sites (lines 692, 791) and
  the relay path (line 1236)
- (Pending) catch-up emission path — OpenBC needs to emit 0x29 in response to
  `BC_OP_REQUEST_OBJ` (opcode 0x1E) and as part of the NewPlayerInGame (opcode 0x2A)
  handshake, IF the server is tracking attached explosions per object

---

## Fix Plan

### Phase 1 — Remove per-tick 0x29 emissions

1. Delete lines 685-699 (beam-kill 0x29 emit) in `bc_beam_hit_callback`.
2. Delete lines 787-798 (torpedo-kill 0x29 emit) in `bc_torpedo_hit_callback`.
3. Verify the OBJECT_EXPLODING PythonEvent (0x06) emission at lines 678-682 and
   776-784 is sufficient for client-side "ship exploded" visuals. Stock clients
   spawn the destruction visual in response to the EXPLODING event, not the 0x29.

### Phase 2 — Audit the client-relay path

Investigate whether any OpenBC client ever originates 0x29:
- If never: remove the `BC_OP_EXPLOSION` relay case (or convert it to a logged drop).
- If self-destruct / chain-reaction emits 0x29 from a client, evaluate whether that
  should instead be a server-side computed emission.

### Phase 3 (optional) — Implement catch-up 0x29 emission

If OpenBC plans to track per-object attached explosions for state-recovery parity:

1. Maintain a per-object `attached_explosions` linked list with (pos, radius,
   damage_rate, lifetime) and a tick-down on each main-loop iteration to expire
   entries.
2. On receiving `BC_OP_REQUEST_OBJ` for an object: enumerate its
   `attached_explosions` and send one 0x29 per explosion, targeted at the requesting
   peer's session only (unreliable flag).
3. On `BC_OP_NEW_PLAYER_IN_GAME` handshake: enumerate `attached_explosions` for every
   object the joining peer can see, send 0x29 per explosion targeted at the joiner
   only.

If OpenBC doesn't currently model attached-explosion lifetimes, Phase 3 can be
deferred; per-tick combat damage is already replicating correctly via 0x1C
StateUpdate, so the catch-up emission is needed only for late-joiner parity with
ongoing area-effect damage.

### Acceptance criteria

1. Bystander clients no longer see duplicate explosion visuals on kills.
2. Wire trace shows zero 0x29 emissions during per-tick combat (vs stock zero).
3. OBJECT_EXPLODING PythonEvent (0x06) is the sole kill-visual emission and matches
   stock format.
4. (Optional Phase 3) Late joiners see ongoing area-effect explosions on attached
   objects via catch-up 0x29 emissions, matching stock behavior.

---

## Cross-References

- **Pass 1 memo (source of truth)**:
  `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\host-event-emission-catalog-20260529.md`
  (Section 3: "Explosion / Death Path Catalog")
- Related bug: `20260529-per-handler-relay-cascade.md` — this bug supersedes the
  Phase 4 framing in that bug report
- Related bug: `20260221-self-destruct-death-pipeline.md` — anomaly #2 historically
  flagged spurious 0x14 on death; the 0x29 question here is the sibling concern
- Related bug: `20260222-collision-test-parity-gaps.md` — bug #4 (spurious Explosion
  0x29 on collision kill) is also subsumed under this report
- OpenBC doc: `docs/protocol/tgmessage-routing.md` — Per-opcode policy table (see
  Pass 1 revision)
- OpenBC doc: `docs/game-systems/ship-death-lifecycle.md` — death visual flow
