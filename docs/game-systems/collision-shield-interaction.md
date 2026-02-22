# Collision-Shield Interaction

How collision damage interacts with shields in Bridge Commander multiplayer, documented from observable behavior, network packet captures, and the game's shipped Python scripting API.

**Clean room statement**: This document describes collision-shield interaction as observable gameplay behavior. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

Collision damage does **not** bypass shields. Shields absorb collision damage directionally — the shield facing closest to the impact point absorbs damage first, and only overflow reaches hull and subsystems.

However, collision damage interacts with shields differently than weapons or explosions. The three damage paths each have distinct shield behavior.

---

## Three Damage Paths and Their Shield Behavior

### 1. Collision Damage: Directional Per-Subsystem Absorption

When a collision occurs:

1. The engine finds all subsystems (including shield facings) near the collision point
2. Each subsystem absorbs damage from its HP pool
3. Shield facings absorb damage like any other subsystem — based on spatial proximity to the impact point
4. Whatever damage shields and subsystems don't absorb flows through to hull structural damage

**Key characteristics:**
- **Directional**: A head-on collision primarily drains front shields, not all facings equally
- **No pre-filter**: 100% of collision damage enters the subsystem damage pipeline (unlike weapons which have a shield gate)
- **Power subsystem exposed**: Collisions can directly damage the reactor/power subsystem; weapons exclude it
- **Per-contact**: Multi-contact collisions apply damage once per contact point, each going through shield absorption independently

### 2. Weapon Damage: Binary Shield Gate + Per-Subsystem

When a weapon hits:

1. **Pre-filter (binary gate)**: The engine traces the weapon's ray against the shield ellipsoid. If the ray intersects and the shield facing has HP, the hit is **fully absorbed** — no damage reaches hull at all
2. About 72% of weapon hits are stopped at this gate (observed in stock 3-player combat)
3. If the weapon passes through (shield facing depleted, or angle/geometry allows penetration), damage enters the same per-subsystem pipeline as collisions
4. Weapon damage **excludes the power subsystem** from the hit list when multiple subsystems are in range

### 3. Explosion Damage: Uniform Area-Effect

When a ship explodes (area-of-effect):

1. **Uniform shield drain**: Damage is divided equally across all 6 shield facings (1/6 each)
2. Each facing independently absorbs up to its current HP for its share
3. Whatever isn't absorbed by shields flows through to subsystem/hull damage
4. A ship with 5 full facings and 1 empty facing still loses 1/6 of the damage to hull

---

## Comparison Table

| Aspect | Collision | Weapon | AoE Explosion |
|--------|-----------|--------|---------------|
| Shield check | Directional (per-subsystem) | Binary gate + per-subsystem | Uniform 1/6 per facing |
| Shield bypass rate | 0% bypass (always checked) | ~72% absorbed at gate | 0% bypass (always checked) |
| Damage reach rate | 100% enters pipeline | ~28% enters pipeline | 100% enters pipeline |
| Power subsystem hit? | Yes | No (excluded from list) | Yes (via subsystem path) |
| Facing selectivity | Impact-facing only | Hit-facing only | All 6 equally |
| Contact multiplication | Per-contact point | Single hit | Single area |

---

## Observable Consequences

### Why Collisions Feel Powerful

- **No shield gate**: Every collision enters the damage pipeline. Weapons lose 72% at the shield gate.
- **Multiple contacts**: A collision can produce 2+ contact points, each applying damage independently through shield absorption. This can overwhelm a single shield facing in one physics tick.
- **Power subsystem exposed**: Collisions can hit the warp core directly, potentially cascading into power failures.

### Shield Facing Depletion Pattern

A head-on collision at full speed:
- **Front shields** absorb the impact; other facings are unaffected
- If front shields are depleted, overflow immediately hits hull + subsystems in the forward arc
- Rear shields remain at 100% even as the ship takes serious forward damage

This is different from explosions, which drain all facings equally regardless of direction.

### Stock Trace Data (15-minute 3-player session)

| Metric | Value |
|--------|-------|
| Collision checks | 79,605 |
| Actual collision damage events | 229 (0.3% trigger rate) |
| Weapon hits | 1,939 |
| Weapon hits past shields | 536 (28% pass rate) |
| Total damage events reaching subsystems | 765 (536 weapon + 229 collision) |

---

## Server Implementation Notes

### Current Bug: Area-Effect Used for Collisions

The current OpenBC implementation uses area-effect shield absorption (damage/6 per facing) for collision damage. This is **incorrect** — it matches the explosion path, not the collision path.

**What stock does**: Directional absorption — find the shield facing(s) nearest the collision point, absorb damage from those facings only, pass overflow to hull/subsystems.

**What OpenBC does**: Uniform absorption — divide damage by 6, absorb from all facings equally. This means:
- Shields absorb ALL collision damage (total shield pool is huge when spread across 6 facings)
- Hull and subsystems never take damage from collisions (shields absorb everything)
- Client sees directional shield drain locally but server disagrees → flickering

### Required Fix

Switch collision damage from area-effect to directed shield absorption:

1. Determine which shield facing the collision impacts (from the impact direction vector)
2. Absorb damage from that facing's HP
3. If facing is depleted, overflow goes to hull and nearby subsystems
4. Do NOT distribute across all 6 facings

### Shield Facing Determination

The facing is determined by the dominant component of the impact direction in ship-local space:

| Direction | Facing |
|-----------|--------|
| +Y (forward) | Front |
| -Y (aft) | Rear |
| +Z (up) | Top |
| -Z (down) | Bottom |
| +X (starboard) | Right |
| -X (port) | Left |

This is a maximum-component test (equivalent to cube face selection), not a dot-product projection.

### Power Subsystem Exclusion

The stock game excludes the power subsystem (reactor/warp core) from the subsystem hit list for weapon damage but NOT for collision damage. This means collisions can directly damage the reactor, potentially causing cascading power failures. The `isCollision` flag controls this behavior.

---

## Wire Format Impact

Collision damage uses the same network messages as other damage types:
- **Opcode 0x15** (CollisionEffect): Client → Host, carries collision force and contact points
- **Flag 0x20** (StateUpdate): Host → Client, carries authoritative subsystem conditions
- **Opcode 0x06** (PythonEvent): Host → Client, carries ADD_TO_REPAIR_LIST events per damaged subsystem

The server must correctly apply directional shield absorption when processing opcode 0x15, then send the resulting subsystem conditions via flag 0x20 health updates and repair events via opcode 0x06.

---

## Relationship to Other Docs

- [collision-damage-event-chain.md](../bugs/collision-damage-event-chain.md) — How collision damage generates PythonEvent messages (the event chain after damage is applied)
- [collision-effect-wire-format.md](../wire-formats/collision-effect-wire-format.md) — Opcode 0x15 wire format
- [pythonevent-wire-format.md](../wire-formats/pythonevent-wire-format.md) — Opcode 0x06 wire format for subsystem damage events
