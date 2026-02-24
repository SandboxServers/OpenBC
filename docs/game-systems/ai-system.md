# AI System Specification

This document describes Bridge Commander's ship AI architecture. Relevant to Quick Battle (AI opponents) and single-player campaign (enemy ships, wingmen, fleet commands).

**Clean room statement**: This document describes AI behavior as observable in-game and readable from shipped Python scripts (`AI/`, `Conditions/`). No binary addresses, memory offsets, or decompiled code are referenced.

---

## 1. Architecture Overview

Bridge Commander's AI uses a **hierarchical behavior tree** with composable nodes. Each AI node is a C++ object created via the SWIG API (`App.*AI_Create`), configured from Python scripts. The tree is evaluated every AI tick; each node returns a state (ACTIVE, DORMANT, or DONE) that drives its parent's logic.

```
Ship AI Root
  └── ConditionalAI (check: is target alive?)
        └── PreprocessingAI (preprocessor: fire weapons)
              └── PriorityListAI
                    ├── [Priority 1] PlainAI: TorpedoRun
                    ├── [Priority 2] PlainAI: CircleObject
                    └── [Priority 3] PlainAI: Flee
```

### Node States

Each AI node returns one of three states on each tick:

| State | Meaning |
|-------|---------|
| `US_ACTIVE` | Currently executing — keep running this behavior |
| `US_DORMANT` | Temporarily inactive — parent should try alternatives |
| `US_DONE` | Completed or failed — remove from consideration |

---

## 2. AI Node Types (C++ Classes)

### PlainAI

Leaf node. Wraps a Python script class (derived from `BaseAI`) that implements actual ship behavior — movement, targeting, maneuvering.

**Creation**: `App.PlainAI_Create(pShip, "Name")`

**Script interface**:
- Constructor receives the C++ AI handle (`pCodeAI`)
- `Activate()` — called when the node becomes active
- The script controls the ship through the SWIG API (set speed, heading, etc.)

**Examples**: `CircleObject`, `Intercept`, `Flee`, `FollowObject`, `TorpedoRun`, `Stay`, `Ram`, `GoForward`, `PhaserSweep`

### ConditionalAI

Evaluation node. Contains one child AI and one or more conditions. The evaluation function receives condition results and returns a state that controls whether the child runs.

**Creation**: `App.ConditionalAI_Create(pShip, "Name")`

**Configuration**:
- `SetContainedAI(pChildAI)` — set the child behavior
- `AddCondition(pConditionScript)` — add a condition to evaluate
- `SetEvaluationFunction(func)` — Python callback that maps condition results to states
- `SetInterruptable(bool)` — whether a higher-priority sibling can interrupt this node

**Logic**: Each tick, all conditions are evaluated. Their boolean results are passed to the evaluation function. If the function returns ACTIVE, the child AI runs. If DORMANT or DONE, the child is suspended or terminated.

### PreprocessingAI

Wrapper node. Runs a preprocessor function before delegating to its contained AI. Used for cross-cutting concerns like weapon firing, obstacle avoidance, and evasive maneuvers.

**Creation**: `App.PreprocessingAI_Create(pShip, "Name")`

**Configuration**:
- `SetContainedAI(pChildAI)` — set the wrapped behavior
- `SetPreprocessingMethod(pScript, "MethodName")` — set the preprocessor
- `SetInterruptable(bool)`

**Key preprocessors** (from `AI/Preprocessors.py`):
| Preprocessor | Purpose |
|-------------|---------|
| `FireScript` | Auto-fire weapons at target (phaser sweep, torpedo launch, tractor beam) |
| `AvoidObstacles` | Steer away from nearby objects |
| `EvadeTorps` | Dodge incoming torpedoes |
| `ChooseTarget` | Select best target from an object group |
| `ShieldManager` | Dynamically adjust shield facing priorities |
| `WarpBeforeDeath` | Emergency warp when hull is critical |

### PriorityListAI

Composite node. Contains an ordered list of child AIs. Evaluates children from highest to lowest priority. The first child that returns ACTIVE is executed; lower-priority children are suspended.

**Creation**: `App.PriorityListAI_Create(pShip, "Name")`

**Configuration**:
- `AddAI(pChildAI, priority)` — add child with priority (higher = first)

**Behavior**: On each tick, iterate children by priority. If a higher-priority child becomes ACTIVE, lower-priority children that were previously running are interrupted (if interruptable).

### SequenceAI

Composite node. Executes children in sequence. When one child completes (DONE), the next child starts.

**Creation**: `App.SequenceAI_Create(pShip, "Name")`

**Behavior**: Run first child until DONE, then second child, etc. If any child returns DORMANT, the sequence pauses. Useful for multi-step behaviors (approach → attack → retreat).

### RandomAI

Composite node. Randomly selects one child to execute from its list.

**Creation**: `App.RandomAI_Create(pShip, "Name")`

**Behavior**: On activation, randomly pick one child and run it. When that child completes, pick another randomly.

### BuilderAI

Meta-node used by compound AI scripts. A declarative builder that assembles complex behavior trees from named blocks with dependency relationships. The FedAttack and NonFedAttack compound AIs use this to construct ~30+ node trees programmatically.

**Creation**: `App.BuilderAI_Create(pShip, "Name", moduleName)`

**Configuration**:
- `AddAIBlock(blockName, creatorFunction)` — register a named subtree
- `AddDependency(blockName, dependsOnBlock)` — declare ordering
- `AddDependencyObject(blockName, varName, value)` — pass data to blocks

---

## 3. Condition System

Conditions are boolean tests evaluated by ConditionalAI nodes. Each condition is a Python script implementing a check.

**Creation**: `App.ConditionScript_Create("Conditions.ModuleName", "ConditionName", ...args)`

### Shipped Conditions

| Condition | Tests |
|-----------|-------|
| `ConditionInRange` | Target within specified distance |
| `ConditionFacingToward` | Ship facing within angle threshold of target |
| `ConditionAttacked` | Ship was recently attacked |
| `ConditionAttackedBy` | Ship attacked by specific attacker |
| `ConditionSystemBelow` | Any subsystem HP below threshold |
| `ConditionCriticalSystemBelow` | Critical subsystem HP below threshold |
| `ConditionSystemDestroyed` | Specific subsystem at 0 HP |
| `ConditionSystemDisabled` | Specific subsystem disabled |
| `ConditionDestroyed` | Target ship is dead |
| `ConditionExists` | Target object exists in current set |
| `ConditionAllInSameSet` | All specified objects in same game set |
| `ConditionAnyInSameSet` | Any specified object in same set |
| `ConditionInLineOfSight` | Clear line of sight to target |
| `ConditionInNebula` | Ship is inside a nebula |
| `ConditionInPhaserFiringArc` | Target in phaser firing arc |
| `ConditionTorpsReady` | Torpedo tubes loaded and ready |
| `ConditionPulseReady` | Pulse weapon charged |
| `ConditionShipDisabled` | Ship disabled (critical systems dead) |
| `ConditionSingleShieldBelow` | Specific shield facing below threshold |
| `ConditionPowerBelow` | Power output below threshold |
| `ConditionIncomingTorps` | Torpedoes incoming toward ship |
| `ConditionFiringTractorBeam` | Ship has active tractor beam |
| `ConditionTimer` | Time-based trigger |
| `ConditionFlagSet` | Named mission flag is set |
| `ConditionMissionEvent` | Specific mission event occurred |
| `ConditionWarpingToSet` | Ship warping to specific set |
| `ConditionReachedWaypoint` | Ship reached a waypoint |
| `ConditionDifficultyAt` | Game difficulty matches level |
| `ConditionUsingWeapon` | Ship currently firing specific weapon type |
| `ConditionPlayerOrbitting` | Player in orbit around planet |

---

## 4. Compound AI Behaviors

Compound AIs are pre-built behavior trees assembled from PlainAI, ConditionalAI, PreprocessingAI, and PriorityListAI nodes. They represent complete tactical strategies.

### BasicAttack

Entry point for all combat AI. Selects attack strategy based on ship faction:
- **Federation ships** → `FedAttack` (uses torpedo runs + phaser sweeps, respects front shield)
- **Non-Federation ships** → `NonFedAttack` (more aggressive, different maneuvering)
- **Ships with cloaking device** → `CloakAttackWrapper` (hit-and-run with cloak)

Each strategy is a ~30-node behavior tree with:
- Torpedo run approaches (attack from optimal torpedo angle)
- Phaser sweep patterns (maintain phaser firing arc)
- Shield management (turn damaged shield away from enemy)
- Range management (keep in optimal weapon range)
- Evasive maneuvers (dodge torpedoes, avoid obstacles)

### Difficulty Scaling

AI difficulty is a 0.0-1.0 float that enables/disables behavior flags:

| Flag | Effect |
|------|--------|
| `UseCloaking` | Use cloak for hit-and-run |
| `SmartTorpSelection` | Choose torpedo type intelligently |
| `SmartPhasers` | Optimize phaser firing patterns |
| `ChooseSubsystemTargets` | Target specific subsystems |
| `DisableBeforeDestroy` | Disable target before killing |
| `EvadeTorps` | Actively dodge incoming torpedoes |
| `SweepPhasers` | Phaser sweep across target hull |

Three difficulty presets (Easy_, default, Hard_) set these flags at different thresholds. Scripts can override any flag explicitly.

### Other Compound Behaviors

| Compound AI | Purpose |
|------------|---------|
| `Defend` | Protect a target (follow + engage attackers) |
| `CloakAttack` | Cloak → approach → decloak → alpha strike → recloak |
| `DockWithStarbase` | Full docking sequence (approach, cutscene, repair/rearm, undock) |
| `StarbaseAttack` | Attack a stationary target (different approach angles) |
| `ChainFollow` | Follow a leader ship in formation |
| `ChainFollowThroughWarp` | Follow leader through warp transitions |
| `TractorDockTargets` | Tractor beam docking behavior |
| `CallDamageAI` | Switch to damage-appropriate AI when hit |

---

## 5. PlainAI Behaviors (Leaf Nodes)

| PlainAI | Ship Behavior |
|---------|---------------|
| `CircleObject` | Orbit target at configurable range and speed |
| `IntelligentCircleObject` | Orbit with shield-aware facing |
| `Intercept` | Move to intercept a moving target |
| `Flee` | Run away from target (disengage) |
| `FollowObject` | Follow a target maintaining formation distance |
| `FollowThroughWarp` | Follow through warp transitions between sets |
| `FollowWaypoints` | Follow a waypoint path with per-waypoint speed |
| `GoForward` | Fly straight ahead at set speed |
| `Stay` | Hold position (zero throttle) |
| `TorpedoRun` | Approach from optimal torpedo firing angle, fire, break away |
| `PhaserSweep` | Maintain phaser firing arc, sweep beam across target |
| `Ram` | Collision course with target |
| `StationaryAttack` | Attack without moving (turret mode) |
| `StarbaseAttack` | Attack approach optimized for large stationary targets |
| `Defensive` | Defensive maneuvering (shield management priority) |
| `ManeuverLoop` | Execute a pre-defined maneuver pattern |
| `MoveToObjectSide` | Position on specific side of target |
| `TurnToOrientation` | Rotate to face specific direction |
| `Warp` | Engage warp drive to destination set |
| `SelfDestruct` | AI-triggered self-destruct (emergency) |
| `TriggerEvent` | Fire a game event (scripted trigger) |
| `RunAction` | Execute a timed action sequence |
| `RunScript` | Run arbitrary Python script as AI behavior |
| `EvilShuttleDocking` | Hostile shuttle docking approach |

---

## 6. Fleet Command System

Fleet commands allow ordering AI-controlled wingmen. Each command creates an appropriate compound AI on the target ship.

| Command | Module | Behavior |
|---------|--------|----------|
| `DestroyTarget` | `AI.Fleet.DestroyTarget` | Full attack AI (BasicAttack wrapped with conditions) |
| `DisableTarget` | `AI.Fleet.DisableTarget` | Attack subsystems only (disable, don't kill) |
| `DefendTarget` | `AI.Fleet.DefendTarget` | Protect specified ship (engage attackers) |
| `HelpMe` | `AI.Fleet.HelpMe` | Distress call — come to player's aid |
| `DockStarbase` | `AI.Fleet.DockStarbase` | Order wingman to dock for repair |

Each fleet command:
1. Creates the appropriate compound AI
2. Wraps it in a ConditionalAI that checks the target still exists and is in the same set
3. Sets the AI as interruptable (can be overridden by new orders)

**Not used in shipped multiplayer** — fleet commands are only used indirectly through campaign mission scripts. Could be exposed for cooperative multiplayer modes.

---

## 7. AI Preloading

On game initialization (when `CreateMultiplayerGame` or equivalent fires), `AI.Setup.GameInit()` is called. This pre-imports all 73 AI module files to prevent hitching during gameplay when AIs are first created:
- 27 PlainAI scripts
- 15 Compound AI scripts (+ 5 Parts sub-modules)
- 5 Fleet command scripts
- 36 Condition scripts

---

## 8. Player AI

The `AI/Player/` directory contains behavior trees for human player actions (auto-piloting). These are used when the player issues high-level commands from the tactical interface:

| Player AI | Action |
|-----------|--------|
| `DestroyFreely` | Attack nearest enemy (auto-target) |
| `DestroyFore` | Attack from forward angle |
| `DestroyAft` | Attack from rear angle |
| `DestroyFromSide` | Attack from broadside |
| `DisableFreely` | Disable nearest enemy |
| `DisableFore` | Disable from forward |
| `Defense` | Defensive posture (shield management) |
| `InterceptTarget` | Intercept selected target |
| `OrbitPlanet` | Orbit a planet object |
| `Stay` | Hold position |
| `FlyForward` | Fly straight ahead |
| `PlayerWarp` | Warp to destination |

Each has "Close", "Maintain", and "Separate" variants controlling engagement range.

---

## 9. Implementation Notes for OpenBC

### Quick Battle (AI Opponents)

For Quick Battle, OpenBC needs:
1. **AI tree construction** — Replicate the `App.*AI_Create` factory methods
2. **Tick evaluation** — Each AI tick, walk the tree top-down, evaluate conditions, update ship controls
3. **Preprocessor execution** — Fire weapons, dodge torpedoes, avoid obstacles as cross-cutting concerns
4. **Difficulty system** — Map 0.0-1.0 difficulty to flag sets that enable/disable behaviors

The AI scripts themselves (Python) can be loaded directly. The C++ runtime just needs to:
- Create/manage the tree node objects
- Call `Activate()` on script instances when nodes become active
- Evaluate conditions each tick
- Pass ship control outputs to the physics simulation

### Single-Player Campaign

Campaign AI additionally requires:
- Fleet command dispatch (player issues orders to wingmen)
- Waypoint following (mission-scripted paths)
- Set/warp transitions (AI follows through warp)
- Docking behaviors (starbase approach, dock, repair, undock)
- Event-triggered AI changes (mission script swaps AI on events)
- Save/load of AI state (BaseAI `__getstate__`/`__setstate__`)

### Multiplayer AI (Future)

For cooperative modes with AI wingmen:
- Fleet commands need network synchronization (currently local-only)
- AI state could be replicated as an opaque blob or via command messages
- The `ObjectGroupWithInfo` class provides dynamic team grouping for fleet coordination

---

## Related Documents

- **[combat-system.md](combat-system.md)** — Damage, weapons, shields that AI behaviors interact with
- **[ship-subsystems.md](ship-subsystems.md)** — Subsystem types that conditions test against
- **[../planning/cut-content-analysis.md](../planning/cut-content-analysis.md)** — Fleet command restoration feasibility
- **[../planning/gamemode-system.md](../planning/gamemode-system.md)** — Cooperative modes that would use fleet AI
