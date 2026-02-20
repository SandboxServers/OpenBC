# Collision Detection System

How Bridge Commander detects collisions between game objects. This covers the detection pipeline that determines two objects have collided, BEFORE any damage calculation occurs.

**Clean room statement**: This document describes collision detection behavior as observable in gameplay, from the public NetImmerse SDK (bounding volumes), and from the game's shipped Python scripting API. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

Bridge Commander uses a **three-tier collision detection system**:

1. **Broad Phase**: Sweep-and-prune (sort-and-sweep) on axis-aligned bounding boxes
2. **Mid Phase**: Hierarchical bounding sphere intersection tests
3. **Narrow Phase**: Per-type collision resolution (ship-ship, torpedo, physics)

The collision system is NOT part of NetImmerse's built-in collision infrastructure. Bridge Commander implements a completely custom collision detection pipeline at the game layer. NetImmerse's `NiCollisionSwitch` exists in the binary but is only used for toggling collision visualization, not for detection.

---

## Tier 1: Broad Phase -- Sweep-and-Prune

### Proximity Manager

Each game "Set" (scene/area) owns a **ProximityManager** that tracks all collidable objects in the set. The ProximityManager implements 3-axis sweep-and-prune, a well-known broad-phase algorithm.

### Algorithm

**Object Registration**:
When a game object enters a set, the ProximityManager:
1. Computes the object's axis-aligned bounding box (AABB) from its 3D model
2. For each of the 3 coordinate axes (X, Y, Z), inserts interval endpoints (min and max) into sorted lists
3. Maintains a sorted array per axis of all object interval boundaries

**Per-Frame Update**:
1. Recompute each object's AABB from its current world position and model bounds
2. Update endpoint values in the sorted arrays
3. Run the sweep step on each of the 3 axes
4. Process all pairs that overlap on all 3 axes

### Sweep Step

The sweep uses **bubble-sort-like** incremental updates:

```
for each axis:
    repeat until no swaps:
        for each adjacent pair of endpoints:
            if out of order:
                swap the two endpoints
                if this swap means two intervals NOW overlap:
                    increment overlap count for this pair
                    if overlap count == 3 (all axes):
                        check collision flags compatibility
                        if compatible: add to active collision pairs
                if this swap means two intervals NO LONGER overlap:
                    decrement overlap count
                    if overlap count drops below 3:
                        remove from active collision pairs
```

**Key insight**: Because objects move incrementally frame-to-frame, the sorted arrays are *nearly sorted* each frame, making the bubble-sort O(n) in practice rather than O(n^2) for naive all-pairs testing.

### AABB Computation

The bounding box comes from the NetImmerse scene graph's geometry bounds. Objects with custom extent overrides (set via Python API) can expand their AABB beyond the model geometry to create larger collision volumes.

### Collision Flags

Two objects can only collide if their collision flags are **compatible**. Each object has a flags byte encoding:
- **"Collides as" bits**: What category this object belongs to
- **"Collides with" bits**: What categories this object can hit

The compatibility check ensures both objects agree that they should interact: A's "collides with" must match B's "collides as" and vice versa. This prevents friendly projectiles from hitting their launchers, environment objects from colliding with each other, etc.

The collision flags are readable via the Python scripting API (`GetCollisionFlags()`).

The global collision enable flag is controlled via `SetPlayerCollisionsEnabled()` in the Python API.

---

## Tier 2: Hierarchical Bounding Sphere Test

After sweep-and-prune identifies overlapping AABBs, the game performs a **bounding sphere intersection test** before committing to narrow-phase resolution.

### Check Collision

```
function CheckCollision(this_object, other_object):
    // Early-out: dead or inactive
    if this_object is dead: return false
    if this_object collision is not active: return false

    // Ship-type check: skip if other ship has collision disabled
    if other is a ship with collision disabled via its damage manager:
        return false

    // Same-set check: both must be in the same game set
    if this_object.set != other_object.set:
        return false

    // Exclusion list: recently-collided objects are temporarily excluded
    // (prevents rapid-fire re-triggering when ships grind against each other)
    check event-based exclusion list

    // Cooldown timer: if still in cooldown from recent collision
    if collision_cooldown_timer > threshold:
        return true  // report collision from cooldown

    // Bounding sphere test
    if SphereIntersection(other_object):
        return true

    // Recursive: check attached sub-objects (docked ships, etc.)
    for each child object attached to this:
        if child.CheckCollision(other_object):
            return true

    // Static collision: check against terrain/static geometry
    return CheckStaticCollision(other_object)
```

### Bounding Sphere Distance Test

The core geometric test:

```
function SphereIntersection(other):
    // Same set check
    if this.set != other.set: return false

    // Get world positions from the scene graph
    pos_a = this.GetWorldTranslation()
    pos_b = other.GetWorldTranslation()

    // Euclidean distance (with optional modifiers from child bounding volumes)
    distance = sqrt((bx-ax)^2 + (by-ay)^2 + (bz-az)^2) + adjustments

    // Combined collision radius
    combined_radius = NiBound_radius * scale_factor * radius_multiplier

    // Test: spheres overlap?
    if distance < combined_radius:
        // Possible collision -- check children for more precise test
        return true (recurse into sub-objects)

    return false
```

### Collision Radius

Each object's collision radius comes from three factors:
- **NiBound radius**: The bounding sphere radius computed from the 3D model geometry (stored in the NetImmerse scene graph node)
- **Scale factor**: The object's world scale
- **Radius multiplier**: An additional multiplier (configurable per object)

If the object is dead or collision-inactive, the radius returns a sentinel value (effectively infinite), preventing any intersection.

---

## Tier 3: Narrow Phase -- Per-Type Resolution

### Collision Pair Dispatch

After sweep-and-prune and bounding sphere tests confirm proximity, the collision pair is dispatched based on object class:

| Object Types | Handler | Notes |
|-------------|---------|-------|
| Ship + Ship (or DamageableObject) | Ship-Ship Handler | Bounding sphere overlap, posts collision event |
| Torpedo + Any | Torpedo Handler | Ray/sphere or mesh intersection, time-of-impact refinement |
| Physics Object + Physics Object | Physics Handler | Full mesh intersection, velocity-based gating |

### Ship-Ship Collision

```
function HandleShipShipCollision(ship_a, ship_b):
    // Verify overlap is real (hash table lookup)
    if not confirmed overlap: return

    // Get world positions
    pos_a = ship_a.GetWorldTranslation()
    pos_b = ship_b.GetWorldTranslation()

    // Get bounding radii from model bounds
    radius_a = ship_a.GetModelBoundRadius()
    radius_b = ship_b.GetModelBoundRadius()

    // Compute gap = distance - combined_radii
    gap = euclidean_distance(pos_a, pos_b) - radius_a - radius_b

    if gap < 0:
        // Spheres overlap: post collision event
        PostCollisionEvent(ship_a, ship_b)
    else if gap > 0 and was_previously_colliding:
        // Separation: post end-collision event
        PostSeparationEvent(ship_a, ship_b)
```

**Key design decision**: Ship-to-ship collisions use ONLY bounding spheres. There is no triangle-mesh intersection test for ship-ship collisions. The NiBound radius determines the collision volume. This is why large ships with elongated shapes (like the Galaxy class) can collide before they visually touch.

### Torpedo Collision

Torpedoes use a more precise intersection method:

1. **Self-hit prevention**: Skip if the target is the torpedo's launcher
2. **Target type branch**:
   - For mesh-based targets (asteroids, stations): Full geometry intersection test with time-of-impact refinement (up to 2 iterations)
   - For simpler targets: Basic sphere-sphere intersection
3. **On hit**: The torpedo is marked as dead and triggers its destruction/explosion sequence

### Physics Object Collision

For generic physics objects (asteroids, debris):

1. **Eligibility check**: Both objects must have collision enabled
2. **Velocity threshold**: Both objects must be moving above a minimum speed. Objects at rest are excluded from physics collision to avoid constant collision events between resting objects
3. **Angular momentum check**: Rotational energy is also checked against the threshold
4. **Contact history**: Prevents re-triggering if objects are already in contact
5. **Mesh intersection**: Full detailed geometry intersection test

---

## Collision Energy and Damage Entry

When a collision is confirmed, the system computes collision energy and feeds it into the damage pipeline (see [combat-system.md](combat-system.md)):

### Per-Contact Damage Formula

```
raw_damage = (collision_force / ship_mass) / contact_count
scaled_damage = raw_damage * collision_scale + collision_offset
clamped_damage = min(scaled_damage, 0.5)    // hard cap per contact
damage_radius = 6000.0                       // fixed radius for all collision damage
```

Where:
- **collision_force**: Impulse magnitude from the physics response (depends on relative velocity and mass)
- **ship_mass**: From the ship's property definition
- **contact_count**: Number of contact points in the collision
- **collision_scale / collision_offset**: Tuning constants

Each contact point is transformed into the ship's local coordinate space and fed to the central damage function with the clamped damage and fixed radius.

### Force Computation

The `collision_force` value comes from the physics response phase, which runs after detection confirms overlap. It depends on:
- **Relative velocity** of the two objects at the contact point
- **Mass** of the objects involved
- **Coefficient of restitution** (bounce factor)

---

## Event Flow

```
ProximityManager detects AABB overlap
    |
    v
Bounding sphere test confirms proximity
    |
    v
Per-type narrow phase resolves collision
    |
    v
Posts COLLISION_EFFECT event to game event system
    |
    v
Ship collision handler catches event
    ├── Validates collision (ownership, proximity)
    ├── Forwards to clients via CollisionEffect (opcode 0x15)
    └── Applies damage locally via DoDamage pipeline
```

---

## Key Design Decisions

1. **No triangle-mesh ship-ship detection**: Ship collisions use ONLY bounding spheres. This is fast but imprecise for elongated ship shapes.

2. **Torpedoes use mesh intersection**: Unlike ships, torpedoes DO use detailed geometry tests to determine exact impact points. This makes torpedo hits feel precise.

3. **Sweep-and-prune is the workhorse**: In observed gameplay sessions, the mid-phase check runs ~80,000 times per 15-minute session. The incremental sort means most frames only need a handful of endpoint swaps.

4. **Collision cooldown timer**: A per-object timer prevents rapid-fire collision events when two ships grind against each other.

5. **Client-authoritative detection**: Collision detection runs on the **client**, not the server. Clients detect collisions locally and send CollisionEffect (opcode 0x15) to the host, which validates the reported collision (distance check) and applies damage. This means the dedicated server does NOT need to run collision detection itself.

6. **Velocity threshold for physics**: Objects at rest are excluded from physics collision to avoid constant events between resting objects (e.g., asteroids touching in an asteroid field).

7. **Custom, not engine**: Bridge Commander's collision detection is entirely game-layer code, not NetImmerse's built-in collision system. The engine's collision classes exist in the binary but serve different purposes.

---

## Python API Surface

| Function | Purpose |
|----------|---------|
| `SetPlayerCollisionsEnabled(bool)` | Global collision enable/disable |
| `GetCollisionFlags()` | Read object's collision category flags |
| `SetCollisionFlags(flags)` | Set object's collision category flags |
| `GetProximityManager()` | Access the Set's proximity manager |

---

## Implications for Reimplementation

### Server Does Not Need Collision Detection

Since collision detection is client-authoritative, a reimplemented server only needs to:
1. Receive CollisionEffect (0x15) reports from clients
2. Validate the reported collision (distance plausibility check)
3. Apply damage via the damage pipeline
4. Forward the collision to other clients

The full sweep-and-prune + bounding sphere pipeline is only needed in a client implementation.

### Client Implementation

A client reimplementation needs:
1. Sweep-and-prune broad phase (or equivalent spatial partitioning)
2. Bounding sphere mid-phase with NiBound radius from model data
3. Per-type narrow phase (ship-ship = sphere only, torpedo = mesh)
4. Collision cooldown timer to prevent event spam
5. Collision flags compatibility check

### Precision Tradeoffs

The stock game's sphere-only ship-ship detection is simple but imprecise. A reimplementation could optionally use oriented bounding boxes (OBBs) or convex hull tests for more accurate ship-ship collision without fundamentally changing the architecture.

---

## Related Documents

- **[collision-effect-wire-format.md](collision-effect-wire-format.md)** -- Opcode 0x15 wire format and host validation
- **[collision-damage-event-chain.md](collision-damage-event-chain.md)** -- How collision damage generates PythonEvent (0x06) messages
- **[combat-system.md](combat-system.md)** -- Full damage pipeline (collision -> hull/subsystem damage)
- **[server-authority.md](server-authority.md)** -- Authority model (collision is client-detected, host-validated)
