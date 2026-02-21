# Fix: Collision Damage Should Use Directed Shield Absorption

**Status**: Open bug
**Affects**: `src/game/combat.c`, `src/server/server_dispatch.c`
**Related**: [collision-shield-interaction.md](collision-shield-interaction.md), [collision-damage-event-chain.md](collision-damage-event-chain.md)

---

## Problem

Collision damage currently uses area-effect shield absorption (`area_effect=true`), which divides damage equally across all 6 shield facings. This is incorrect — it matches the explosion/AoE path, not the collision path.

### Observable Symptoms

1. **Ships are invulnerable to collision damage.** With a Sovereign's 55,500 total shield HP spread across 6 facings (5,500 to 11,000 each), a typical collision dealing 6,000 damage is spread as 1,000 per facing. Every facing absorbs its share completely. Zero overflow reaches hull or subsystems.

2. **Zero PythonEvent (0x06) messages after collisions.** Since no subsystems take damage, no ADD_TO_REPAIR_LIST events are generated. Clients never see repair queue updates.

3. **Shield flickering.** The client's local engine applies collision damage directionally (front shields absorb on a head-on hit). But the server reports all subsystems at full health (because no damage penetrated). Client and server disagree, causing visible shield flickering.

### Root Cause

In `server_dispatch.c`, both collision damage call sites pass `area_effect=true`:

```c
// Target ship (line ~996)
bc_combat_apply_damage(&target->ship, tcls, dmg, 6000.0f,
                       impact_dir, true);    // <-- BUG: should be false

// Source ship (line ~1124)
bc_combat_apply_damage(&source->ship, scls, sdmg,
                       6000.0f, src_impact, true);  // <-- BUG: should be false
```

The `area_effect=true` path in `bc_combat_apply_damage` divides damage by 6 and absorbs from all facings uniformly. The `area_effect=false` path finds the single impacted facing and absorbs from that facing only.

---

## Stock Game Behavior

The stock game processes collision damage in two sequential steps:

### Step 1: Subsystem Damage Distribution (includes shield absorption)

The engine finds all subsystems (including shield facings) near the collision point using the ship's subsystem linked list. Each subsystem in range absorbs damage from its HP pool. Shield facings absorb damage like any other subsystem. The total damage is **reduced in-place** — whatever shields and subsystems absorb is subtracted from the original damage amount.

### Step 2: Hull/Structural Damage

The **reduced** damage (after shield/subsystem absorption) enters the hull damage pipeline. The engine creates a damage volume from the impact point and radius, runs AABB overlap tests against subsystem bounding boxes, and distributes remaining damage to hull and overlapping subsystems.

### Key Behavioral Differences from Weapons

| Aspect | Collision | Weapon | AoE Explosion |
|--------|-----------|--------|---------------|
| Shield absorption | Directed: impact-facing only | Binary gate (72% absorbed) + directed | Uniform 1/6 per facing |
| Power subsystem | IN hit list (can be damaged) | EXCLUDED from hit list | IN hit list |
| Contact points | Per-contact (damage divided) | Single hit | Single area |

---

## Fix

### Change 1: Collision call sites — `server_dispatch.c`

Change both collision damage calls from `area_effect=true` to `area_effect=false`:

**Target ship** (in the CollisionEffect handler, where target ship takes damage):
```c
bc_combat_apply_damage(&target->ship, tcls, dmg, 6000.0f,
                       impact_dir, false);   // directed, not area-effect
```

**Source ship** (same handler, where source ship also takes damage in ship-vs-ship collisions):
```c
bc_combat_apply_damage(&source->ship, scls, sdmg,
                       6000.0f, src_impact, false);  // directed, not area-effect
```

That's it. No changes needed to `combat.c` or `combat.h`.

### Why This Works

The existing `area_effect=false` path in `bc_combat_apply_damage` already implements correct directed shield absorption:

1. **`bc_combat_shield_facing()`** determines which shield facing the impact hits, using the maximum-component test on the impact direction in ship-local space.

2. **Single-facing absorption**: Damage is absorbed from that one facing's HP. If the facing has enough HP, all damage is absorbed and the function returns (no hull/subsystem damage).

3. **Overflow**: If the facing's HP is less than the damage, it's depleted to 0 and the overflow proceeds to hull damage and subsystem AABB overlap testing.

This matches the stock game's collision behavior:
- Head-on collision drains **front shields only** (not all 6 facings)
- Front shields can be depleted while other facings remain full
- Once front shields are depleted, overflow immediately hits hull + subsystems
- Rear shields unaffected even during severe forward damage

### Verification

After the fix, observe:
- Collision damage should reduce the impacted shield facing, not all 6 equally
- When the impacted facing is depleted, hull HP should decrease
- Subsystems in the collision area should take damage (subsystem_hp values decrease)
- PythonEvent (0x06) ADD_TO_REPAIR_LIST messages should appear after collisions (~7 per ship per collision)
- No more shield flickering — server and client agree on directional shield drain

---

## Power Subsystem Exclusion (Separate Issue)

The stock game has a secondary behavioral difference for collisions: the power subsystem (reactor/warp core) stays in the hit list for collisions but is excluded for weapon damage when multiple subsystems are hit. This means collisions can directly damage the reactor, potentially causing cascading power failures.

Currently, `bc_combat_find_hit_subsystems` does not exclude any subsystem types, which is **correct for collision damage** but **incorrect for weapon damage**. The weapon exclusion is a separate bug and should be tracked independently.

If implementing power subsystem exclusion for weapons later, add a `bool is_collision` parameter to `bc_combat_apply_damage` (or `bc_combat_find_hit_subsystems`). When `false` (weapon) and more than one subsystem is in the hit list, remove the power subsystem. When `true` (collision), keep all subsystems.

---

## Summary of Changes

| File | Change | Lines |
|------|--------|-------|
| `src/server/server_dispatch.c` | `area_effect=true` -> `false` for target collision | ~996-997 |
| `src/server/server_dispatch.c` | `area_effect=true` -> `false` for source collision | ~1124-1125 |

Two lines changed. No API changes. No new functions.
