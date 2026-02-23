# Bug Report: Per-Species Power Initialization Failures + Missing Collision Rate Limiting

**Date**: 2026-02-22
**Severity**: HIGH (4 HIGH, 1 MEDIUM)
**Status**: OPEN
**Affected Systems**: Power initialization, collision rate limiting
**Verified Against**: Live OpenBC server testing, stock BC power data files cross-referenced

---

## Summary

Testing 13 of 16 flyable ship species revealed two categories of bugs:

1. **Per-species power initialization failures** (species 10-13): Cardassian and Kessok ships
   have broken power systems ranging from zero effective power (Galor) to partially-initialized
   warp cores (CardHybrid, KessokHeavy). Federation (species 1-5) and Klingon/Romulan (species
   6-8) ships all work correctly.

2. **Missing collision rate limiting**: The client sends CollisionEffect every ~30ms when a ship
   grinds against an object, resulting in 28,504 packets in 11 minutes (1,033x stock rate). Stock
   BC has collision cooldown logic that caps this.

Bug numbers continue from
[20260222-collision-test-parity-gaps.md](20260222-collision-test-parity-gaps.md) (bugs 1-6).

---

## Bug 7: Galor (Species 10) — Zero Effective Power {#bug-7}

**Severity**: HIGH
**Issue**: Galor has zero effective power output; engines are non-functional

The Galor spawns but cannot move. The warp core/reactor does not register any power output.
Engines receive no power and remain non-functional. The ship is completely unplayable.

**Observed**:
- Ship spawns successfully (ObjCreateTeam works)
- Zero collisions logged (ship cannot reach any obstacle)
- Engines non-functional — ship does not respond to movement input

**Data file verification**: The Galor's ship data specifies `power_output: 500` — the data is
correct. The bug is in code, not data.

**Expected**: Galor should have 500 power output and fully functional engines, matching the data
file specification.

**Reproduction**:
1. Start OpenBC server
2. Connect client, select Galor
3. Attempt to fly — ship does not move

---

## Bug 8: Keldon (Species 11) — Uncharged Batteries {#bug-8}

**Severity**: HIGH
**Issue**: Keldon batteries and power reserves are not fully charged at spawn

The Keldon spawns with uncharged batteries/reserves. Power output exists but is insufficient for
normal operation. A single collision (11,187 damage) destroys the ship in one hit, suggesting
shields have no energy to absorb damage.

**Observed**:
- Ship can move (engines have some power)
- 1 collision with 11,187 damage → instant death (hull is 6,000)
- No shield absorption — the full collision energy appears to reach the hull
- Battery/reserve power not fully charged

**Expected**: Keldon should spawn with fully charged batteries and reserves. Shields should
absorb a portion of collision damage.

**Reproduction**:
1. Start OpenBC server
2. Connect client, select Keldon
3. Check power display — batteries show uncharged
4. Collide with any object — ship dies in one hit regardless of damage

---

## Bug 9: CardHybrid (Species 12) — Warp Core at ~85% {#bug-9}

**Severity**: HIGH
**Issue**: CardHybrid warp core initializes at ~85% instead of 100%

The CardHybrid spawns with its warp core/reactor at approximately 85% capacity. This reduces
total power available to all subsystems. Forward shields did appear to take damage from
collisions (partial power reaching shields), but the reduced power budget degrades overall
ship performance.

**Observed**:
- Warp core display shows ~85% at spawn
- ~12 collisions before death (ship has 11,000 hull)
- Collision flood behavior observed (grinding against asteroid)
- Forward shield absorbed some damage (partial functionality)

**Expected**: Warp core should initialize at 100%.

**Reproduction**:
1. Start OpenBC server
2. Connect client, select CardHybrid
3. Check power display — warp core shows ~85%

---

## Bug 10: KessokHeavy (Species 13) — Warp Core at ~87% {#bug-10}

**Severity**: HIGH
**Issue**: KessokHeavy warp core initializes at ~87% instead of 100%

The KessokHeavy spawns with its warp core at approximately 87%. Combined with the collision
rate limiting bug (Bug 11), this created a respawn loop: the ship would spawn, immediately
collide with a nearby asteroid, take massive damage, die, respawn at the same location, and
repeat. 26 respawn cycles observed in approximately 2 minutes.

**Observed**:
- Warp core at ~87% at spawn
- 26 respawns from collision flood (spawn → collide → die → repeat)
- Massive collision packet flood during respawn loop
- 5,116 collision ownership failures (stale object IDs from previous ship spawns)

**Expected**: Warp core should initialize at 100%.

**Reproduction**:
1. Start OpenBC server
2. Connect client, select KessokHeavy
3. Check power display — warp core shows ~87%
4. Fly into asteroid — collision flood + rapid death/respawn cycle

---

## Bug 11: Collision Rate Limiting Absent {#bug-11}

**Severity**: MEDIUM
**Issue**: No collision cooldown between repeated CollisionEffect packets

When a ship grinds against an object (asteroid, station), the client sends a CollisionEffect
(opcode 0x15) every ~30ms (~33/second). Stock BC has collision cooldown logic that prevents
this flood. OpenBC processes and relays every collision without rate limiting.

**Observed**:
- Session 2: **28,504 CollisionEffect packets** in ~11 minutes
- Rate: ~43/sec (2,591 per minute)
- Stock reference: 84 CollisionEffect in 33.5 minutes (~0.04/sec)
- **OpenBC rate is 1,033x higher than stock**

**Impact**:
- Network bandwidth waste (each CollisionEffect is a UDP packet)
- Rapid death/respawn loops when ships spawn near obstacles
- KessokHeavy respawned 26 times in ~2 minutes from collision flood
- Server processes thousands of unnecessary damage calculations

**Expected behavior**: Stock BC applies some form of collision rate limiting (cooldown timer,
minimum velocity threshold, or contact deduplication). The exact mechanism hasn't been
reverse-engineered, but the cap is approximately 0.04 CollisionEffect/sec in normal gameplay.

**Reproduction**:
1. Start OpenBC server
2. Connect client, select any ship
3. Fly into asteroid at low speed and maintain contact (grind along surface)
4. Observe packet_trace.log — CollisionEffect packets arrive every ~30ms

---

## Confirmed Working Species

These species were tested and have fully functional power systems:

| Species | Ship | Power | Shields | Hull Tracking |
|---------|------|-------|---------|---------------|
| 1 | Akira | OK | — | OK (one-shot at 12,651 vs 9,000 hull) |
| 2 | Ambassador | OK | Works | OK |
| 3 | Galaxy | OK | Works | OK |
| 5 | Sovereign | OK | — | OK (4 hits, ~29,500 total vs 12,000 hull) |
| 7 | Vorcha | OK | **Absorbing correctly** | OK (survived 25,523 vs 18,000 hull) |
| 8 | Warbird | OK | — | OK (prior session) |

The Vorcha result is particularly notable: two hits totaling 25,523 collision energy against
18,000 hull, yet the ship survived because shields absorbed a portion of each hit with
recharge between them. This confirms the collision-shield absorption pipeline works correctly.

---

## Untested Species

| Species | Ship | Notes |
|---------|------|-------|
| 4 | Nebula | F5 tactical showed damage at death only |
| 6 | Bird of Prey | One-shot killed (4,000 hull vs 11,644 hit), power appeared OK |
| 9 | Marauder | Not tested |
| 14 | KessokLight | Not tested |
| 15 | Shuttle | Not tested |
| 16 | CardFreighter | Not tested |

---

## Related Documents

- [20260222-collision-test-parity-gaps.md](20260222-collision-test-parity-gaps.md) — Bugs 1-6 from same-day testing
- [collision-damage-event-chain.md](../collision-damage-event-chain.md) — Collision damage event chain
