# Bug Report: Per-Handler Relay Cascade (5 Opcodes Over-Relayed)

**Date**: 2026-05-29
**Severity**: HIGH (duplicate event delivery on bystander clients)
**Status**: PARTIALLY FIXED — 0x14 (DestroyObject) and 0x15 (CollisionEffect) relay paths removed in this pass; 0x06 (PythonEvent) deferred pending server-side emission for 4 specific events; 0x1A (BeamFire) NO LONGER classified as over-relay (Pass 1 found OpenBC's behavior is correct); 0x29 (Explosion) reclassified as an over-emission bug rather than over-relay.
**Affected Systems**: TGMessage routing, event replication, host fan-out
**Verified Against**: STBC.exe live decompile (FUN_0069F880, FUN_0069FDA0, FUN_006B63A0), Pass 1 host-event-emission catalog 2026-05-29

---

## REVISED 2026-05-29 (Pass 1 host-event-emission catalog)

A Pass 1 binary-truth catalog of every host-side TGEvent emission (`host-event-emission-catalog-20260529`)
re-shaped the framing of this bug. Three of the five "over-relayed" opcodes need their
classification updated:

### 0x1A BeamFire — REMOVED from over-relay list (false positive)

The stock host **never originates 0x1A from simulation**. BeamFire is exclusively
client-input-originated: a client fires a beam, sends 0x1A, the host receives via
`FUN_0069FBB0`, **relays to the `Forward` group**, then locally applies via
`FUN_005762B0`. The host's own beam fires (when the host is a player) also call
`FUN_00575480` to broadcast. There is no host-originated 0x1A emission from `WeaponSystem`
simulation; per-tick beam damage replicates via opcode 0x1C StateUpdate (subsystem
health round-robin), not via re-emitted 0x1A.

**OpenBC's current relay behavior for 0x1A IS CORRECT.** The earlier claim that 0x1A
was over-relayed was wrong — the stock binary's per-handler `Forward`-group relay is the
intended path. Keep the relay. Phase 3 in the Fix Plan below is therefore VOID; no
server-side beam-fire generation pipeline is required.

### 0x29 Explosion — reframed as **over-emission**, not over-relay

The stock host emits opcode 0x29 **only during catch-up replay**, never as per-tick
combat damage. The two emission sites are:

1. `MultiplayerGame__RequestObjHandler @ 0x006A02A0` — sends all attached explosions to a
   peer requesting object state.
2. `NewPlayerInGameHandler @ 0x006A1E70` — sends all attached explosions to a newly
   joined peer.

Per-tick explosion damage replicates via opcode 0x1C StateUpdate (subsystem health
round-robin) and via the ObjCreate (0x02 / 0x03) initial damageable-object state. The
host never emits 0x29 from the per-tick combat / damage / death pipeline.

**Therefore the OpenBC bug isn't "remove the 0x29 relay"** — the per-handler RECEIVE-side
handler `FUN_006A0080` is already LOCAL-ONLY in stock, and that part is correct. **The
real bug is that OpenBC is over-emitting 0x29 from per-tick paths** that the stock host
never emits from. See the new bug report
`20260529-explosion-overemission.md` for the per-site audit (combat.c:698, 797, 1533
emission sites). Phase 4 in the Fix Plan below is reshaped accordingly: it's not a relay
removal, it's an emission-site audit.

### 0x06 PythonEvent — over-relay removal requires 4 specific server-side events

Pass 1 enumerates exactly which event IDs the stock host emits 0x06 PythonEvent for, and
under which gate. The complete list (subscribed to MultiplayerGame's HostEventHandler
vtable slot at `MultiplayerGame_Ctor @ 0x0069E590`, gated `DAT_0097fa8a != 0` =
IS_MULTIPLAYER) is:

| Event ID | Name | Stock emission site | Class |
|----------|------|---------------------|-------|
| 0x0080004E | OBJECT_EXPLODING | `ShipDeathHandler @ 0x005AFEA0` (immediate-MOV at 0x005AFF39) → `ObjectExplodingHandler @ 0x006A1240` → opcode 0x06 NoMe | TGEvent factory 0x101 |
| 0x00800074 | REPAIR_COMPLETED | `RepairSubsystem::Update @ 0x005652A0` (immediate-MOV at 0x00565447) → HostEventHandler `FUN_006A1150` → opcode 0x06 NoMe | TGObjPtrEvent factory 0x010C |
| 0x00800075 | REPAIR_CANNOT_BE_COMPLETED | `RepairSubsystem::Update @ 0x005652A0` (immediate-MOV at 0x005653A4 AND 0x005654E0 — two emit sites, one per failure branch) → HostEventHandler `FUN_006A1150` → opcode 0x06 NoMe | TGObjPtrEvent factory 0x010C |
| 0x008000DF | ADD_TO_REPAIR_LIST | Client emits via `AddToRepairList_MP @ 0x00565900` → host receives → HostEventHandler `FUN_006A1150` → opcode 0x06 NoMe (relay) | TGEvent factory 0x101 |

OpenBC must emit all four of these from its server-side simulation before the 0x06
over-relay can be safely removed. The `0x008000DF` AddToRepairList path is partially
built per the existing #85 fix; the other three are not currently emitted.

A separate playability-bug report (`20260529-repair-completion-silent-drop.md`) tracks
the missing REPAIR_COMPLETED / REPAIR_CANNOT_BE_COMPLETED emissions — these have
observable client-UI symptoms (stuck Engineering panel state) independent of the over-relay
question.

### What Pass 1 doesn't refute

The 0x14 (DestroyObject) and 0x15 (CollisionEffect) removal in this commit stands as
correct. Pass 1 confirms both stay LOCAL-ONLY on the stock host.

### Cross-references

- Pass 1 source memo: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\host-event-emission-catalog-20260529.md`
- Companion bug report (new): `20260529-repair-completion-silent-drop.md`
- Companion bug report (new): `20260529-explosion-overemission.md`

---

## Summary

OpenBC's spec and implementation modeled the host's game-message relay as a transport-level
"automatic" fan-out that forwards every incoming game opcode to all other peers. That model
is binary-wrong. In stock STBC.exe, relay is **per-handler**, decided **inside the dispatch
handler body** after the local effect runs, and gated on `DAT_0097fa8a` (g_IsMultiplayer).
Several opcode handlers have NO `SendToGroup` call at all and are **LOCAL-ONLY** on the
host. Implementing the spec as written produces duplicate event delivery on bystander
clients for these LOCAL-ONLY opcodes.

Five opcodes are affected: **0x06 (PythonEvent)**, **0x14 (DestroyObject)**, **0x15
(CollisionEffect)**, **0x1A (BeamFire)**, and **0x29 (Explosion)**. All five have host
handlers in stock STBC.exe that consume the message locally without re-broadcasting.

---

## Symptom

Bystander clients receive each of the affected opcodes **twice**:
1. Once from the original sender's message (relayed by OpenBC's transport-level fan-out)
2. Once again from the server's own per-opcode handler output (where applicable)

Observable consequences:
- Double-explosion visuals on bystander screens (0x29)
- Doubled "object destroyed" cleanup events (0x14)
- Phantom collision effects on bystanders (0x15)
- Doubled beam-fire visuals (0x1A)
- Duplicate Python event dispatch on remote clients (0x06)

For 0x06 specifically: client-emitted ADD_TO_REPAIR_LIST events are seen on every other
client, when in stock they would arrive only via the server's own damage-simulation
pipeline (which generates its own 0x06 events independently of any client 0x06 input).

---

## Evidence

### Stock binary anchors

Live-Ghidra spot checks against `FUN_0069F880` (PythonEvent handler, 0x0069F880) confirm:

```
FUN_0069F880: TGFactory_DeserializeObject -> FUN_006f13c0 -> FUN_006da300 (PostEvent)
              ZERO SendToGroup
              ZERO TGWinsockNetwork_SendTGMessage
              ZERO Clone calls
```

The handler decodes the event, posts it locally to the EventManager, and **does not
forward**. Both 0x06 (PythonEvent) and 0x0D (PythonEvent2) dispatch through this handler;
both are LOCAL-ONLY.

By contrast, `FUN_0069FDA0` (GenericEventForward) — used by relay-yes opcodes 0x07-0x12,
0x19, 0x1B — has EXPLICIT per-handler relay:

```
FUN_006a2fc0(s_Forward_008d94a0)               ; FindGroupByName "Forward"
TGWinsockNetwork_SendToGroup_Iterate(group, msg)  ; the relay call
gated by DAT_0097fa8a (g_IsMultiplayer) && DAT_0097fa78 (TGWinsockNetwork singleton)
vtable[6] Clone via (**(code **)(*param_1 + 0x18))() before the relay
```

Order is: receive -> dispatch -> handler.local-effect -> handler.optional-clone-and-relay.

Anchor xrefs:
- `s_Forward_008D94A0` xrefs at 0x0069E784, 0x0069E7A0 (MultiplayerGame_Ctor),
  0x0069FDF9 (FUN_0069FDA0 generic forward), 0x0069F997 (FUN_0069F930 TorpedoFire)
- `s_NoMe_008E5528` xrefs at 0x0069E6F9, 0x0069E715 (both inside MultiplayerGame_Ctor)

### Per-opcode classification (LOCAL-ONLY vs relay-yes)

| Opcode | Name | Stock handler | Relay? |
|--------|------|--------------|--------|
| 0x06 | PythonEvent | FUN_0069F880 | **LOCAL-ONLY** (no SendToGroup) |
| 0x07-0x12 | StartFiring..SetPhaserLevel | FUN_0069FDA0 | Relay (explicit per-handler) |
| 0x13 | HostMsg | FUN_006A01B0 | **LOCAL-ONLY** |
| 0x14 | DestroyObject | FUN_006a01e0 | **LOCAL-ONLY** |
| 0x15 | CollisionEffect | FUN_006a2470 | **LOCAL-ONLY** |
| 0x17 | DeletePlayerUI | FUN_006a1360 | **LOCAL-ONLY** |
| 0x18 | DeletePlayerAnim | FUN_006a1420 | **LOCAL-ONLY** |
| 0x19 | TorpedoFire | FUN_0069f930 | Relay (own SendToGroup) |
| 0x1A | BeamFire | FUN_0069fbb0 | **Relays to `Forward` group** (per Pass 1 — host re-broadcasts client-input BeamFire to other clients via `FUN_0069FBB0`) |
| 0x29 | Explosion | FUN_006a0080 | **LOCAL-ONLY on receive** (S→C generated only from catch-up paths `RequestObj 0x006A02A0` + `NewPlayerInGame 0x006A1E70`; never per-tick) |

### STBC RE memo

Full evidence chain with addresses and decompile excerpts:

`C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\networking-mid-tgmessage-cleanroom-validation-20260528.md`

This memo documents the v5 validation of the clean-room TGMessage routing spec against
`docs/protocol/tgmessage-routing.md`. Headline finding: the "Automatic Relay (C++ Layer)"
section of the clean-room spec was anchored false — relay is per-handler, not transport-level.

---

## Root Cause

The OpenBC spec (and the implementation that follows it) modeled relay as an **automatic,
unconditional, opaque** transport-level operation:

> "When the host receives ANY game message from a client via the transport layer, the host
> automatically relays it to all other connected clients. This relay is unconditional,
> opaque, and immediate."

That description is binary-wrong. In stock:
- **Not unconditional**: Opcodes 0x06, 0x0D, 0x13, 0x14, 0x15, 0x17, 0x18, 0x1A, 0x29 do
  not relay. The relay-vs-absorb decision is per-opcode.
- **Not opaque**: The relay decision happens AFTER the dispatcher decoded the opcode and
  routed to a specific handler. Clone+SendToGroup lives INSIDE the handler body.
- **Not immediate**: The handler runs the local effect first (e.g., posts to EventManager
  via FUN_006DA300), then conditionally clones and forwards.

For the relay-yes opcodes (0x07-0x12, 0x19, 0x1B), the per-handler model produces a 1:1
fan-out behaviorally indistinguishable from the "automatic" model — which is why the bug
went undetected until per-opcode wire comparison surfaced the duplicates.

For LOCAL-ONLY opcodes, the model causes over-relay.

---

## Current Status (2026-05-29, post-Pass 1)

**Fixed in this commit**:
- 0x14 (DestroyObject) relay path removed
- 0x15 (CollisionEffect) relay path removed
- Spec corrected to per-handler model

**Deferred (after Pass 1 reframing)**:
- 0x06 (PythonEvent) — still deferred, but the required server-side events are now
  enumerated (see "REVISED 2026-05-29" section above)
- 0x29 (Explosion) — re-scoped: this is no longer a relay-removal task. It's an
  emission-site audit (see new bug `20260529-explosion-overemission.md`)

**No longer in scope**:
- 0x1A (BeamFire) — Pass 1 confirmed OpenBC's relay behavior IS CORRECT. Stock host
  relays 0x1A to the `Forward` group from `FUN_0069FBB0`. There is no host-originated
  beam-fire emission to build; per-tick beam damage replicates via 0x1C StateUpdate.

### Why 0x06 is deferred

Stock binary's per-handler design for 0x06 PythonEvent relies on **server-side
event-generation pipelines** that emit the relevant events independently of any client
input. Pass 1 enumerates these as exactly four events:

1. **0x0080004E OBJECT_EXPLODING** — host posts via `ShipDeathHandler @ 0x005AFEA0`
   (immediate-MOV at 0x005AFF39); routed through `ObjectExplodingHandler @ 0x006A1240`
   to opcode 0x06 NoMe. Reliable.
2. **0x00800074 REPAIR_COMPLETED** — host posts via `RepairSubsystem::Update @ 0x005652A0`
   (immediate-MOV at 0x00565447, success branch when `currentCondition/maxCondition >= 1.0f`);
   routed through `HostEventHandler FUN_006A1150` to opcode 0x06 NoMe. Reliable.
3. **0x00800075 REPAIR_CANNOT_BE_COMPLETED** — host posts at TWO sites in the same
   `RepairSubsystem::Update @ 0x005652A0`: immediate-MOV at 0x005653A4 (in-queue branch)
   and 0x005654E0 (post-queue-scan branch). Both routed through `HostEventHandler` to
   opcode 0x06 NoMe. Reliable.
4. **0x008000DF ADD_TO_REPAIR_LIST** — host receives client emission (from
   `AddToRepairList_MP @ 0x00565900` on client side) and relays via `HostEventHandler
   FUN_006A1150` to opcode 0x06 NoMe. Partially built per the existing #85 fix.

OpenBC's server-side generation pipelines for events #1-#3 are incomplete. Removing
the 0x06 relay without first completing the generation chains causes **visible loss
of bystander state** — the bystanders stop seeing death events, repair-completion UI,
and damaged-during-repair UI for events that originated on the host's simulation.

---

## Fix Plan (Phased, Pass 1 reshaped)

### Phase 1 (DONE)
- Remove 0x14 + 0x15 relays
- Update spec to per-handler relay model
- Document the 3-mechanism routing model (per-handler relay, Python NoMe group, connect
  broadcast)

### Phase 2 — 0x06 PythonEvent (server-side emission for 4 events)
1. Emit OBJECT_EXPLODING (0x0080004E) from server-side ship-death detection
   (`bc_ship_die` or equivalent), gated host-only, reliable, target NoMe. TGEvent
   factory 0x101. Anchored to stock site 0x005AFF39.
2. Emit REPAIR_COMPLETED (0x00800074) from `bc_repair_tick` when a queued subsystem's
   `cur_condition / max_condition >= 1.0f`. TGObjPtrEvent factory 0x010C, reliable, NoMe.
   Anchored to stock site 0x00565447.
3. Emit REPAIR_CANNOT_BE_COMPLETED (0x00800075) from `bc_repair_tick` when a queued
   subsystem's `cur_condition <= 0.0f`. TGObjPtrEvent factory 0x010C, reliable, NoMe.
   Anchored to stock sites 0x005653A4 AND 0x005654E0. (Tracked separately as
   `20260529-repair-completion-silent-drop.md` for the playability impact.)
4. Verify the ADD_TO_REPAIR_LIST (0x008000DF) host-relay path under #85 is complete.
5. Remove the 0x06 relay path
6. Trace verify: stock parity for OBJECT_EXPLODING, REPAIR_*, and ADD_TO_REPAIR_LIST
   counts per scenario

### Phase 3 — 0x1A BeamFire (VOID, no action required)

Pass 1 confirmed BeamFire's per-handler relay is the intended stock behavior, not a bug.
No server-side beam-fire generation pipeline is required. OpenBC's current relay of 0x1A
is correct and should remain.

### Phase 4 — 0x29 Explosion (re-scoped to emission audit)

Re-scoped per Pass 1: this is not a relay-removal task. Stock host emits 0x29 only from
catch-up paths (`RequestObj 0x006A02A0` and `NewPlayerInGame 0x006A1E70`), never from
per-tick combat. OpenBC's emission sites at combat.c:698, 797, 1533 need a per-site
audit:

1. For each emission site, classify as catch-up (RequestObj / NewPlayer response) vs
   per-tick combat
2. Remove emission sites that fire per-tick
3. Preserve only catch-up emissions, gated on the relevant request type
4. Tracked as `20260529-explosion-overemission.md`

Cross-references:
- `20260221-self-destruct-death-pipeline.md` — already documents ObjectExplodingEvent
  generation vs Explosion 0x29 spurious-send issue
- Pass 1 memo `host-event-emission-catalog-20260529`

---

## Cross-References

- **Pass 1 memo (binary-truth catalog, source of the 2026-05-29 revisions)**:
  `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\host-event-emission-catalog-20260529.md`
- STBC memo: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\networking-mid-tgmessage-cleanroom-validation-20260528.md`
- STBC memo: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\.claude\agent-memory\game-archaeology-specialist\tgmessage-routing-validation-20260528.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\protocol\tgmessage-routing.md`
- STBC doc: `C:\Users\Steve\source\projects\STBC-Dedicated-Server\docs\networking\tgmessage-routing-cleanroom.md`
- Companion bug (new): `20260529-repair-completion-silent-drop.md` — REPAIR_COMPLETED + REPAIR_CANNOT_BE_COMPLETED not emitted (playability bug)
- Companion bug (new): `20260529-explosion-overemission.md` — 0x29 per-tick emission audit
- Related bug: `20260221-self-destruct-death-pipeline.md` (anomaly #2: spurious 0x14 on death)
- Related bug: `20260222-collision-test-parity-gaps.md` (bug #4: spurious Explosion 0x29 on collision kill)
