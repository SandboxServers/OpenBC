# Server Authority Analysis

How much game authority can the server take from clients, and what are the limits?

## Background

Bridge Commander multiplayer uses a **split authority model**, not a purely client-authoritative one. Different systems have different authority owners. Understanding this split — verified through extensive reverse engineering of the shipping binary and 90MB+ of packet traces — is the foundation for any server authority work.

## Stock Authority Model (Verified)

### Already HOST-authoritative

These systems are controlled by the host in the stock game:

| System | How | Evidence |
|--------|-----|----------|
| **Collision damage** | Host runs `HostCollisionEffectHandler` (`FUN_005afad0`) and applies damage to ALL ships. Clients apply collision damage only to OTHER players' ships — each client's own ship reports as "invulnerable" to local collision processing via the check at `FUN_005ae140`. Two distinct collision events: `0x00800050` (client-detected, sent to host via opcode `0x0C`) and `0x008000fc` (host-validated, triggers actual damage). | Verified from decompiled code + stock dedi traces |
| **Object lifecycle** | ObjCreate (`0x02`), ObjCreateTeam (`0x03`), DestroyObject (`0x14`), and Explosion (`0x29`) all flow server-to-client only. Object IDs allocated per-player: `0x3FFFFFFF + N * 0x40000`. | Verified from jump table + packet traces |
| **Subsystem health** | Server sends authoritative subsystem health via StateUpdate flag `0x20`. Clients send weapon status via flag `0x80`. These flags are mutually exclusive by direction — verified across 30,000+ packets. The server's `0x20` data overrides any client-local health values on all other clients. | Verified from wire format analysis |
| **Anti-cheat hash** | StateUpdate flag `0x01` includes an XOR-folded 32-bit subsystem hash. Mismatch between client-reported and server-computed hash triggers `ET_BOOT_PLAYER`. | Verified from decompiled code |
| **Game flow** | Settings (`0x00`), GameInit (`0x01`), NewPlayerInGame (`0x2A`), EndGame (`0x38`), RestartGame (`0x39`) — all server-to-client. | Verified from opcode table |
| **Scoring** | SCORE_CHANGE_MESSAGE (`0x36`) and SCORE_MESSAGE (`0x37`) originate from the host. The ObjectKilledHandler runs on the host. | Verified from Python scripts + packet traces |

### OWNER-authoritative (each client controls their own ship)

| System | How | Wire Format |
|--------|-----|-------------|
| **Movement/position** | Each client writes its own ship's StateUpdate (`0x1C`) via `FUN_005b17f0` at ~10Hz. Sends POSITIONS, not inputs: absolute position (flag `0x01`), position delta (flag `0x02`), forward vector (flag `0x04`), up vector (flag `0x08`), speed (flag `0x10`). The host relays but does not validate. | Verified from decompiled serializer + wire format spec |
| **Weapon fire** | TorpedoFire (`0x19`) and BeamFire (`0x1A`) sent by the firing client. Host relays to all peers without validation. | Verified from opcode table |
| **Event forwarding** | Opcodes `0x06`-`0x12` and `0x1B` (StartFiring, StopFiring, StartCloak, StopCloak, StartWarp, SetPhaserLevel, etc.) are "I did a thing, tell everyone" messages. The originating client is authoritative. | Verified from handler analysis — all route through `FUN_0069fda0` |

### RECEIVER-LOCAL (each client computes independently)

| System | How |
|--------|-----|
| **Weapon damage** | When a beam/torpedo hits, the full damage pipeline runs on each receiving client independently: `WeaponHitHandler` (`0x005AF010`) -> `ApplyWeaponDamage` (`0x005AF420`) -> `DoDamage` (`0x00594020`) -> `ProcessDamage` (`0x00593E50`). No server validation. Each client computes hit detection against its own local copy of the target's scene graph. |
| **Physics response** | Bouncing/deflection from collisions runs locally via ProximityManager. Only the DAMAGE from collisions is host-authoritative; the physical response is local. |

## The Fundamental Constraint

Stock clients send **computed positions**, not **player inputs**. StateUpdate (`0x1C`) contains world position, orientation vectors, and speed — the results of the client's local physics simulation, not the throttle/rudder commands that produced them.

There is no code path in stock clients for:
- "Server says I'm at position X" (no position reconciliation)
- "Server says my torpedo didn't fire" (no weapon denial)

Clients will always override any server-sent position on the next frame by writing their own StateUpdate.

**However**, clients DO accept certain authoritative data from the host:
- StateUpdate flag `0x20` (subsystem health) from the server overrides local values
- DestroyObject (`0x14`) kills a ship regardless of local state
- Explosion (`0x29`) applies damage as received
- Collision damage from the host collision path is accepted

This means the server has more leverage than "pure relay" — it already controls damage and health for some paths.

## What's Feasible with Stock Clients

### Tier 1: Bounds Checking and Rate Limiting (Days of work)

The server inspects incoming messages for impossible values and drops or corrects them. No simulation, no protocol changes, no client modifications.

| Check | How | What It Catches |
|-------|-----|-----------------|
| **Weapon fire rate** | Track last fire time per weapon subsystem. Drop TorpedoFire (`0x19`) / BeamFire (`0x1A`) that arrive faster than the weapon's known reload time. Weapon types identified by subsystem vtable: PhaserEmitter at `0x00893194`, TorpedoTube at `0x00893630`. | Rapid fire cheats |
| **Damage bounds** | Explosion (`0x29`) carries CompressedFloat16 damage/radius. Validate against known weapon maximums. CollisionEffect (`0x15`) carries collision parameters — validate against plausible physics given known ship masses and velocities. | Damage multiplication |
| **Health floor/ceiling** | Server has ship objects with subsystem data (populated by DeferredInitObject). When receiving StateUpdate with weapon health (flag `0x80`), validate values are within `[0.0, max_health]`. Server's flag `0x20` updates override on all other clients anyway. | Health manipulation |
| **Speed validation** | Reject StateUpdate with velocity exceeding ship class maximum. | Speed hacks |
| **Subsystem integrity** | Reject weapon fire from a destroyed subsystem. Server tracks subsystem health via flag `0x20`. | Firing destroyed weapons |
| **Cloak eligibility** | Reject StartCloak (`0x0E`) from a ship with no functional cloak subsystem or insufficient power. | Invalid cloak |

The server cannot correct the cheating client's local display, but it can:
- **Drop the invalid message** so other clients never see it
- **Kick the player** after repeated violations
- **Log the violation** for server operators

### Tier 2: Plausibility-Based Damage Validation (1-2 weeks)

The server checks whether weapon hits are geometrically plausible given known positions.

When the server receives BeamFire (`0x1A`), it knows:
- Firing ship's last position/orientation (from its StateUpdate)
- Target ship's last position (from its StateUpdate)
- Weapon type and maximum range (from property set data loaded during ship creation)

The server computes: Is the target within weapon range? Is it within a reasonable firing arc? Is the firing ship alive and uncloaked?

**The position lag problem**: The server's position data lags behind each client's true position by at least one StateUpdate interval (~100ms) plus network latency. Tolerance margins are required — e.g., weapon range + (target speed * 0.5s). This catches gross violations (hitting a target across the map) but not subtle ones.

### Tier 3: Server-Side Damage Computation (1-2 weeks, hybrid approach)

**Recommended approach**: Trust the client's hit report (which target was hit), but have the server compute the damage amount. This avoids the need for hit geometry or lag compensation.

1. Client fires beam, computes hit locally, sends BeamFire (`0x1A`) with target ID
2. Server receives BeamFire, runs Tier 2 plausibility checks
3. Server looks up weapon's damage value from the ship's property set (loaded during DeferredInitObject)
4. Server calls `DoDamage` (`0x00594020`) on the target with the correct damage value — this function already works on the server (collision damage proves the pipeline is functional)
5. Server's StateUpdate flag `0x20` sends authoritative subsystem health to all clients, overriding any local computation

**What this gives you**:
- Server decides actual damage amounts — no damage multiplication cheats
- Server decides who dies (DestroyObject `0x14` + Explosion `0x29` are server-to-client)
- Consistent health state across all clients (flag `0x20` is already authoritative)
- No client-side prediction or reconciliation needed

**What this does NOT solve**:
- Client can still claim hits that didn't happen (handled by Tier 2 plausibility)
- Client's local damage display may briefly disagree with server (corrected on next `0x20` update)
- Subtle cheats like slightly increased fire rate below the Tier 1 threshold

**DoDamage prerequisites on the server** (all verified working):
- `ship+0x18` (NiNode) must be non-NULL — populated by DeferredInitObject
- `ship+0x140` (damage target) must be non-NULL — needs verification
- `ship+0x128/+0x130` (damage handler array) must be populated — populated by SetupProperties during DeferredInitObject (creates 33 subsystems)
- NiNode bounding sphere at `node+0x94` must be valid — used for damage volume scaling
- Rotation matrix at `node+0x64` — used for coordinate transforms. Server's copy gets stamped from StateUpdate data but may not be continuously updated like on clients

### Tier 4: Server-Authoritative Movement (Months, requires new client)

True server-authoritative movement requires:

1. **New input message type** — clients send throttle, heading, weapon commands instead of positions
2. **Server runs physics** — processes all ship inputs, integrates physics, detects collisions
3. **Client-side prediction** — client predicts locally for responsiveness, reconciles when server state arrives
4. **Input history and replay** — client buffers inputs and can re-simulate from server correction point

**Why stock clients can't do this**: The StateUpdate serializer (`FUN_005b17f0`) reads position directly from the ship's NiAVObject scene graph node. There is no distinction between "locally computed state" and "received network state" — the ship has one set of values. The client will overwrite any server-sent position on the next physics tick.

**Protocol-breaking**: This changes the wire format. Stock BC clients could not connect. Requires a new client build.

**Additional complications**:
- The NiNode transform pipeline (`NiAVObject::UpdateWorldTransform`) was not designed for server-side execution without a renderer
- The physics tick at `0x005857FF` is tightly coupled to the frame loop
- Python mission scripts (1,228 files in the stock game) assume local state is ground truth
- No entity interpolation timeline, no historical state snapshots, no reconciliation loop exists in the engine

### Tier 5: Full Server Simulation (Do not attempt)

Server runs everything — physics, AI, damage, mission scripts, environmental effects. Clients are thin renderers.

BC's Python scripts use SWIG bindings that read/write ship state directly (`App.g_kTacticalControlledShip.GetHull()`). Making the server authoritative over all Python state would require either running all scripts server-only (rewrite the entire UI) or running them on both sides with reconciliation (solve prediction for arbitrary Python code).

This is effectively "write a new game."

## Summary

| Tier | What | Effort | Client Mod? | Anti-Cheat Value |
|------|------|--------|-------------|-----------------|
| 0 | Stock host (relay + collision damage authority) | Done | No | Low |
| 1 | Bounds checking + rate limiting | Days | No | High for obvious cheats |
| 2 | Plausibility validation (range/arc checks) | 1-2 weeks | No | Moderate |
| 3 | Hybrid damage authority (trust hit, compute damage) | 1-2 weeks | No | High for damage cheats |
| 4 | Server-authoritative movement | Months | **Yes** | Full movement anti-cheat |
| 5 | Full server simulation | Many months | **Yes** | Complete |

**Recommendation**: Tiers 1-3 give the vast majority of anti-cheat value without protocol changes or client modifications. The server already has the DoDamage pipeline working (collision damage proves it), subsystem health is already server-authoritative (flag `0x20`), and object lifecycle is already server-controlled. The gap between "pure relay" and "meaningful authority" is smaller than it appears — the stock host already has more authority than a naive reading of the protocol suggests.

Tier 4+ requires a new client and is a fundamentally different project scope. The architecture was not designed for it, and retrofitting input-based networking into an engine built around position-based replication is a substantial undertaking.
