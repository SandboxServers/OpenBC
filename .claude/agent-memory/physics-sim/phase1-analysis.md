# Phase 1 Physics Analysis

## Summary of Findings

Phase 1 (standalone lobby server) requires NO physics simulation whatsoever.
The minimum physics deliverables for Phase 1 are data structures and component definitions
that other systems can build upon.

---

## 1. Phase 1 Minimum (Lobby Server) -- Zero Physics Required

### Does the lobby phase require ANY physics?
**No.** The lobby phase consists entirely of:
1. Connection establishment (UDP socket, peer management)
2. Checksum exchange (4 rounds of file hash verification)
3. Settings transmission (opcode 0x00: game time, settings bytes, player slot, map name)
4. Status confirmation (opcode 0x01)
5. Ship selection menus (entirely client-side UI)

None of these involve ship positions, movement, or collision.

### What happens between lobby and "in-game"?
1. Host sends MISSION_INIT_MESSAGE with system type, time/frag limits
2. Client creates the star system (SpeciesToSystem.CreateSystemFromSpecies)
3. Player selects a ship in the ship selection menu
4. Ship is created via the engine's object creation system
5. ET_OBJECT_CREATED_NOTIFY fires, relayed to all peers
6. ET_NEW_PLAYER_IN_GAME fires when ship enters the game set

This is all event-driven, no physics tick needed.

### Does the original server simulate ship positions?
**No -- the original is not server-authoritative.** Every peer runs its own full simulation.
The "server" (host) is just a relay. This is why the dedicated server hack requires the
full game executable with only rendering patched out.

---

## 2. Phase 1 Extended (Server-Authoritative Game)

If we want OpenBC's server to actually simulate the game (an improvement over the original):

### Ship Flight Model Parameters
From ImpulseEngineProperty in Hardpoints scripts:
- `MaxAccel` -- linear acceleration (BC units/s^2), range: 0.05 (mine) to 3.0 (akira)
- `MaxAngularAccel` -- angular acceleration (rad/s^2), range: 0.01 to 0.6
- `MaxAngularVelocity` -- turn rate cap (rad/s), range: 0.05 to 0.8
- `MaxSpeed` -- speed cap (BC units/s), range: 0.1 to 8.0

From ShipProperty in GlobalPropertyTemplates:
- `Mass` -- 10 (shuttle) to 1000000 (starbase)
- `RotationalInertia` -- usually 100, 10 for shuttle, 1000000 for starbases

### Tick Rate
The original runs at ~30fps (33ms timer). For a server-authoritative model:
- Fixed timestep of 30Hz would match the original feel
- 60Hz would be smoother but unnecessary for BC's flight model
- Recommendation: 30Hz fixed timestep (matches original)

### Collision Detection
Not needed for Phase 1. The original uses bounding sphere checks (ship radius is defined
in each Hardpoint script's Hull property). For a future server-authoritative model:
- Sphere-sphere for ship-to-ship and torpedo-to-ship
- Ray-sphere for phaser beams
- Simple brute force is fine for BC player counts (16 players max)

### Damage Model
Definitely Phase 2+. The damage model involves:
- 6 shield facings with per-facing HP and recharge rates
- Hull HP with subsystem damage propagation
- Power management (warp core output, conduit capacity, battery reserves)
- Subsystem degradation affecting performance
- Repair queue with priority and complexity

This is complex and tightly coupled with game logic (Python scripts handle scoring,
kill tracking, game-end conditions).

---

## 3. Phase 1 Physics Deliverables (Recommendation)

### What to Ship in Phase 1: Data Structures Only

**Do NOT implement simulation logic in Phase 1.** Instead, define the flecs ECS components
that will hold physics state. This gives other agents (flecs-ecs-architect, swig-api-compat)
something to code against without requiring a working physics tick.

### Phase 1 flecs Components

```c
// Transform -- position and orientation in 3D space
typedef struct {
    float position[3];      // x, y, z in BC world units
    float rotation[4];      // quaternion (w, x, y, z)
} Transform;

// PhysicsBody -- linear and angular dynamics state
typedef struct {
    float velocity[3];          // linear velocity (BC units/s)
    float angular_velocity[3];  // angular velocity (rad/s)
    float mass;                 // from ShipProperty
    float rotational_inertia;   // from ShipProperty
} PhysicsBody;

// ShipFlightParams -- immutable flight characteristics (from Hardpoints)
typedef struct {
    float max_speed;            // ImpulseEngines.MaxSpeed
    float max_accel;            // ImpulseEngines.MaxAccel
    float max_angular_accel;    // ImpulseEngines.MaxAngularAccel
    float max_angular_velocity; // ImpulseEngines.MaxAngularVelocity
} ShipFlightParams;

// ThrottleInput -- player/AI control input
typedef struct {
    float throttle;     // 0.0 to 1.0 (or -1.0 for reverse)
    float pitch;        // -1.0 to 1.0
    float yaw;          // -1.0 to 1.0
    // no roll -- ships auto-level in BC
} ThrottleInput;

// WarpState -- binary warp on/off with charge tracking
typedef struct {
    uint8_t active;         // 0 = normal, 1 = at warp
    uint8_t charging;       // 0 = not charging, 1 = charging up
    float charge_time;      // seconds remaining in charge-up
    float warp_factor;      // current warp factor (1-9)
} WarpState;

// CloakState
typedef struct {
    uint8_t active;         // 0 = uncloaked, 1 = cloaked
    uint8_t transitioning;  // 0 = stable, 1 = cloaking/decloaking
    float transition_time;  // seconds remaining
} CloakState;
```

### Phase 2 flecs Components (define interfaces now, implement later)

```c
// ShieldState -- per-facing shield HP
typedef struct {
    float current[6];       // FRONT, REAR, TOP, BOTTOM, LEFT, RIGHT
    float max[6];           // from ShieldProperty
    float charge_rate[6];   // from ShieldProperty
} ShieldState;

// HullState
typedef struct {
    float current_hp;
    float max_hp;           // from HullProperty.MaxCondition
} HullState;

// SubsystemState -- condition of a single subsystem
typedef struct {
    float current_condition;
    float max_condition;
    float disabled_pct;     // below this % condition, subsystem is disabled
    float repair_complexity;
    uint8_t is_critical;    // destruction kills ship
    uint8_t is_disabled;    // computed: current/max < disabled_pct
} SubsystemState;

// PowerState -- warp core / battery management
typedef struct {
    float main_battery;
    float main_battery_limit;
    float backup_battery;
    float backup_battery_limit;
    float main_conduit_capacity;
    float backup_conduit_capacity;
    float power_output;     // from PowerProperty
} PowerState;

// CollisionShape -- bounding sphere
typedef struct {
    float radius;           // from Hull.Radius
} CollisionShape;

// DamageEvent -- queued damage to be processed
typedef struct {
    uint32_t target_entity;
    uint32_t source_entity;
    float damage;
    float hit_direction[3]; // for shield facing determination
    uint8_t damage_type;    // phaser, torpedo, pulse, collision
} DamageEvent;
```

---

## 4. Phase Boundary Summary

| Feature                      | Phase 1 | Phase 2 | Phase 3+ |
|------------------------------|---------|---------|----------|
| Component structs (all)      | YES     | --      | --       |
| Ship data loading from HP    | YES     | --      | --       |
| Flight model simulation      | --      | YES     | --       |
| Collision detection          | --      | YES     | --       |
| Shield damage model          | --      | YES     | --       |
| Hull/subsystem damage        | --      | YES     | --       |
| Power management             | --      | YES     | --       |
| Weapon simulation            | --      | YES     | --       |
| Visual effects (explosions)  | --      | --      | YES      |
| Particle physics             | --      | --      | YES      |

---

## 5. Rationale

1. **Phase 1 is a lobby server.** It handles connection, checksums, settings, and game setup.
   Zero physics needed.

2. **The original server never simulated physics independently.** It was a full game client
   with rendering disabled. Making our server authoritative is a new feature.

3. **Defining data structures early is high-value, low-risk.** Other agents need these
   interfaces (flecs-ecs-architect for ECS layout, swig-api-compat for API mapping,
   network-protocol for state serialization).

4. **Premature simulation code would be wasted work.** Without the SWIG API layer to
   load ship definitions from Python scripts, there is nothing to simulate with.

5. **Ship data extraction is Phase 1 scope** because the checksum system verifies
   `scripts/ships/*.pyc` files, meaning ship definitions are integral to the protocol.
