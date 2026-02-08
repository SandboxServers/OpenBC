# Physics-Sim Agent Memory

## Key Files
- [ship-class-data.md](ship-class-data.md) - Extracted ship parameters from BC scripts
- [phase1-analysis.md](phase1-analysis.md) - Phase 1 vs Phase 2 physics scope analysis
- [original-server-model.md](original-server-model.md) - How the original BC server works
- [gameplay-physics-analysis.md](gameplay-physics-analysis.md) - Phase 1 relay server analysis (what state to track, what to stub)

## Critical Findings

### Original Server Architecture
- BC multiplayer is **peer-to-peer with host relay**, NOT server-authoritative
- The host runs the full game simulation (engine + Python scripts)
- Other clients also run the full simulation locally
- The host relays events (firing, warp, cloak, damage) between clients
- Scoring/kill tracking is host-side only (Python mission scripts)
- Ship physics runs on EVERY client independently -- no server validation

### Ship Flight Parameters (from Hardpoints scripts)
- ImpulseEngineProperty has 4 key values: MaxAccel, MaxAngularAccel, MaxAngularVelocity, MaxSpeed
- ShipProperty (GlobalPropertyTemplates) has: Mass, RotationalInertia
- These are set per-ship in `scripts/ships/Hardpoints/*.py`
- Angular values appear to be in radians/second
- Speed values are in BC internal units (not real-world units)

### Shield System
- 6 shield facings: FRONT, REAR, TOP, BOTTOM, LEFT, RIGHT
- Each facing has: MaxShields (HP), ShieldChargePerSecond
- ShieldGenerator subsystem has: MaxCondition, NormalPowerPerSecond

### Subsystem Model
- Each subsystem is a "Property" with: MaxCondition, DisabledPercentage, RepairComplexity, Radius, Position
- Critical flag marks subsystems whose destruction kills the ship (Hull, WarpCore)
- Subsystem types: Hull, ShieldGenerator, ImpulseEngines, WarpCore (PowerProperty),
  WarpEngines, SensorArray, Phasers (WeaponSystem), Torpedoes (WeaponSystem), Engineering

### Phase 1 Recommendation (UPDATED for Playable Server)
- Phase 1 relay server needs ZERO physics simulation
- Phase 1 DOES need lightweight ship state tracking for Python scoring
- Server must reconstruct WeaponHitEvent and ObjectExplodingEvent from client messages
- Scoring data (damage, kills, deaths, scores) is pure Python dict state -- no physics
- Position tracking NOT needed: scoring scripts NEVER query ship position
- Collision damage is purely client-side in relay model (no server detection)
- ~70 App.* functions need working stubs for scoring scripts (see gameplay-physics-analysis.md)
- Phase 2 adds server-authoritative simulation (flight model, damage calc, collision)
- See phase1-analysis.md for original analysis, gameplay-physics-analysis.md for relay update

### Game Loop
- Original runs at ~30fps (33ms timer in dedicated server)
- SimulationPipelineTick at 0x00451ac0 drives physics + network
- Game logic is Category 5: 0x0052-0x005A (2,073 functions)
