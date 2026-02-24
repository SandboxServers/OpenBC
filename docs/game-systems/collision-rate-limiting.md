# Collision Rate Limiting

How the stock Bridge Commander game limits collision event frequency to prevent network flooding. Without this system, colliding ships generate thousands of collision events per minute instead of a handful.

**Clean room statement**: This document describes collision rate limiting behavior as observable through gameplay testing, packet frequency analysis, and the game's shipped Python scripting API. No binary addresses, memory offsets, or decompiled code are referenced. Rate limiting constants are derived from behavioral observation and protocol analysis.

---

## The Problem

When two ships collide in Bridge Commander, they can grind against each other for extended periods. Without rate limiting, the collision detection system fires a CollisionEffect (opcode 0x15) message for every frame the ships overlap — at 30+ FPS, this produces thousands of packets per minute.

**Observed impact**: A test session without rate limiting produced **28,504 CollisionEffect packets in 11 minutes** (43 packets/second average). A stock dedicated server session produced only **84 CollisionEffect packets in 33.5 minutes** (0.042 packets/second) — a 269x difference.

---

## Rate Limiting Architecture

The stock game uses a **distance-adaptive, player-count-scaled, per-pair** cooldown system. This is NOT a simple global timer.

### Three Components

1. **Per-pair tracking**: Each unique pair of colliding objects has its own cooldown timer
2. **Distance-adaptive cooldown**: The cooldown duration scales with the distance between objects
3. **Player-count scaling**: More players in the game increases the cooldown (reduces network load)

### Additionally

4. **Velocity gate**: Objects must be moving above a minimum speed threshold to generate collision events. Stationary ships never produce collision events, even when overlapping.

---

## Per-Pair Collision Tracking

Each ship maintains a collision history table that tracks recent collision partners. When a collision is detected between ship A and ship B:

1. Look up the pair (A, B) in the collision history
2. If found AND the time since last collision is less than the cooldown: **suppress** the event
3. If not found OR cooldown has elapsed: **allow** the event and update the timestamp

This means two ships grinding together produce events at the cooldown rate, not at the frame rate.

---

## Base Cooldown by Player Count

The base cooldown interval varies by how many players are in the game:

| Condition | Cooldown (seconds) |
|-----------|-------------------|
| Default / low player count | 0.100 |
| 3+ players, collision enabled | 0.167 |
| 4+ players | 0.250 |
| 5+ players (multiplayer) | 0.125 |
| High player count bracket | 0.500 |

The game selects a base cooldown from a tiered bracket system based on the current player count and game mode (singleplayer vs multiplayer).

---

## Distance-Adaptive Scaling

The base cooldown is multiplied by a factor that depends on the **bounding sphere gap** between the two objects (distance between centers minus both radii):

### Standard Mode (non-camera, singleplayer or basic MP)

| Distance (game units) | Multiplier | Effect |
|-----------------------|------------|--------|
| < 114.0 | 1x (base cooldown) | Normal rate |
| 114.0 – 228.0 | 2x | Reduced rate |
| 228.0 – 343.0 | 5x | Heavy throttle |
| >= 343.0 | **BLOCKED** (infinite) | No events at all |

### Camera/MP-Flag Mode (multiplayer with high player counts)

| Distance (game units) | Multiplier | Effect |
|-----------------------|------------|--------|
| < 114.0 | 1x (base cooldown) | Normal rate |
| 114.0 – 170.0 | 2x | Reduced rate |
| >= 170.0 | **BLOCKED** (infinite) | No events at all |

The camera/MP-flag mode uses a more aggressive distance gate — ships are completely blocked from generating collision events at a shorter range (170 vs 343 units). This significantly reduces collision traffic in crowded multiplayer games.

**Design rationale**: Ships that are barely touching (small gap) get frequent collision updates for responsive damage. Ships that are far apart but still technically in bounding sphere overlap get heavily throttled or blocked entirely.

### Targeting Awareness Multiplier

When the two colliding ships have a targeting relationship (one is targeting the other), an additional 1.333x multiplier is applied to the cooldown. This further reduces collision traffic during active combat where players are already aware of each other. The multiplier is only applied if it won't result in effectively blocking all collisions (cooldown × 1.333 must remain ≤ 1.0 seconds).

---

## Velocity Gate

Both objects involved in a collision must have velocity above a minimum threshold:

- **Threshold**: velocity-squared > ~1e-7 (speed > ~0.000316 units/second)
- **Effect**: Objects that are effectively stationary are excluded from collision events entirely
- **Purpose**: Prevents constant collision events between resting objects (e.g., a ship that bumped another and both came to rest while overlapping)

This gate is checked BEFORE the collision event is generated, so it prevents the event entirely rather than suppressing it after generation.

---

## Server Validation Gate

In addition to the client-side rate limiting, the server performs its own validation when receiving CollisionEffect (0x15) messages:

- **Distance check**: The bounding sphere gap between the two reported ships must be less than approximately 26 game units
- **Effect**: Rejects fabricated collision reports from ships that are far apart
- **Purpose**: Anti-cheat — prevents clients from claiming collisions between distant ships

This is separate from the rate limiting system and applies even to collisions that pass the cooldown check.

---

## Combined Effect

For a typical 3-player FFA match:

```
Frame-rate collision detection (~30Hz)
    │
    ├── Velocity gate: Skip if both ships stationary
    │
    ├── Per-pair cooldown lookup
    │   ├── Within cooldown window: SUPPRESS
    │   └── Cooldown elapsed: ALLOW
    │       │
    │       ├── Compute distance-adaptive cooldown
    │       ├── Apply player-count multiplier
    │       └── Record timestamp for this pair
    │
    └── Generate CollisionEffect (0x15) message
        │
        └── Server receives:
            ├── Distance validation (< 26 units): ACCEPT
            └── Distance validation (>= 26 units): REJECT
```

The result: instead of 30+ events/second, ships produce roughly 2-10 events/second during active grinding, tapering off as they separate.

---

## Implementation Requirements

A reimplementation must include collision rate limiting to avoid flooding the network. The key components:

### Required

1. **Per-pair tracking**: Hash table mapping object-pair → last-collision-timestamp
2. **Distance-adaptive cooldown**: Scale cooldown based on bounding sphere gap
3. **Velocity gate**: Skip collisions between stationary objects

### Recommended

4. **Player-count scaling**: Increase cooldowns with more players
5. **Server-side distance validation**: Reject implausible collision reports

### Constants

| Parameter | Value |
|-----------|-------|
| Minimum cooldown | 0.100 seconds |
| Standard MP cooldown (≥3 players) | 0.500 seconds |
| Distance bracket: near | 114.0 game units |
| Distance bracket: medium (standard) | 228.0 game units |
| Distance bracket: far (standard) | 343.0 game units |
| Distance bracket: far (camera/MP) | 170.0 game units |
| Near-to-medium multiplier | 2x |
| Medium-to-far multiplier (standard) | 5x |
| Beyond-far behavior | BLOCKED (no events) |
| Targeting awareness multiplier | 1.333x |
| Velocity-squared threshold | ~1e-7 |
| Server distance validation | 26 game units (bounding sphere gap) |

---

## Related Documents

- **[collision-detection-system.md](collision-detection-system.md)** — Three-tier collision detection pipeline
- **[collision-shield-interaction.md](collision-shield-interaction.md)** — How collision damage interacts with shields
- **[../wire-formats/collision-effect-wire-format.md](../wire-formats/collision-effect-wire-format.md)** — CollisionEffect (0x15) wire format
- **[../architecture/server-authority.md](../architecture/server-authority.md)** — Collision damage authority model
- **[../architecture/server-computation-model.md](../architecture/server-computation-model.md)** — Server validates + recomputes collision damage
