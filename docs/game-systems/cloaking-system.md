# Cloaking System — Clean Room Specification

Behavioral specification of the Bridge Commander cloaking device state machine, shield interaction, energy failure auto-decloak, and visual transparency effects.

**Clean room statement**: This specification is derived from observable behavior, network packet captures, the game's shipped Python scripting API, and hardpoint script analysis. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

The cloaking device is a PoweredSubsystem that makes a ship invisible. It uses a state machine with 4 active states and timed transitions, interacts with shields via delayed enable/disable, and controls visual transparency through scene graph alpha manipulation.

See also:
- [shield-system.md](shield-system.md) — shield disable/re-enable during cloak
- [power-system.md](power-system.md) — power draw and energy failure conditions
- [stateupdate-wire-format.md](../wire-formats/stateupdate-wire-format.md) — StateUpdate flag 0x40 (CLK) for cloak state propagation
- [event-forward-wire-format.md](../wire-formats/event-forward-wire-format.md) — opcodes 0x0E/0x0F are registered but NOT used in multiplayer

---

## State Machine

### Active States (4 states)

| Value | Name | Timer Behavior | Entered From | Exits To |
|-------|------|----------------|--------------|----------|
| 0 | DECLOAKED | Timer irrelevant | DecloakComplete | CLOAKING (2) via tick |
| 2 | CLOAKING | Timer counts UP by dt | BeginCloaking | CLOAKED (3) when timer reaches CloakTime |
| 3 | CLOAKED | Timer irrelevant | CloakComplete | DECLOAKING (5) via tick or energy failure |
| 5 | DECLOAKING | Timer counts DOWN by dt | BeginDecloaking | DECLOAKED (0) when timer reaches 0 |

### Ghost States (Dead Code)

States 1 and 4 are checked in the `IsCloaking()` and `IsDecloaking()` query functions but are **never assigned** anywhere. They are vestiges of a planned 6-state design that was collapsed to 4 active states during development.

- `IsCloaking()` returns true for states 1 OR 2
- `IsDecloaking()` returns true for states 4 OR 5

Implementations SHOULD use states 0, 2, 3, 5 (matching the original) or MAY renumber to 0-3 for simplicity, as long as the IsCloaking/IsDecloaking queries remain correct.

### State Transition Diagram

```
   StartCloaking()        timer reaches CloakTime    StopCloaking()        timer reaches 0
        |                        |                        |                     |
   DECLOAKED(0) ──→ CLOAKING(2) ──→ CLOAKED(3) ──→ DECLOAKING(5) ──→ DECLOAKED(0)
        ^                                                                       |
        └───────────────────────────────────────────────────────────────────────┘

   Also: CLOAKED(3) ──→ DECLOAKING(5) via energy failure (efficiency < threshold)
```

---

## Transition Timer

Two **class-level global** parameters control all cloaking transitions. These are shared across ALL cloaking devices in the game — they are NOT per-ship values.

| Parameter | Default | Accessor | Description |
|-----------|---------|----------|-------------|
| CloakTime | (set by scripts) | `SetCloakTime` / `GetCloakTime` | Duration of cloak/decloak transition |
| ShieldDelay | 1.0 second | `SetShieldDelay` / `GetShieldDelay` | Delay before shields disable/re-enable |

### Tick Function (Per-Frame Update)

Each frame, the cloaking subsystem updates:

```
function CloakingSubsystem_Update(dt):
    // Parent subsystem update (power draw)
    PoweredSubsystem_Update(dt)

    if state == CLOAKING(2):
        timer += dt
        progress = timer / CloakTime
        if progress >= 1.0:
            progress = 1.0
            CloakComplete()        // → state 3

    else if state == DECLOAKING(5):
        timer -= dt
        progress = timer / CloakTime
        if progress <= 0.0:
            progress = 0.0
            DecloakComplete()      // → state 0

    // Update visual transparency if transitioning
    if transitioning:
        UpdateVisibility(progress)

    // Intent processing (only if subsystem is enabled)
    if not isEnabled:
        return

    // Check if player wants to cloak but isn't yet
    if tryingToCloak AND not (state in {1, 2, 3}):
        BeginCloaking()
        return

    // Check for energy failure while cloaked
    if state == CLOAKED(3):
        if efficiency < ENERGY_THRESHOLD:
            StopCloaking()
            BeginDecloaking()
```

### Key Transition Functions

#### StartCloaking (User-Facing)
Called when the player activates cloak:
1. Sets `tryingToCloak = true`
2. The actual state transition happens on the next tick via the intent check

#### StopCloaking (User-Facing)
Called when the player deactivates cloak:
1. If currently in CLOAKING (1 or 2) OR `tryingToCloak` is set: begin decloaking
2. Sets `tryingToCloak = false`

#### BeginCloaking (Internal)
1. Check energy via recursive power check — if insufficient, abort
2. Create cloak animation/sound effects
3. If ship has shields: schedule delayed shield disable (delay = ShieldDelay)
4. Set state = CLOAKING(2), timer = 0

#### CloakComplete (Timer Finished)
1. Set state = CLOAKED(3)
2. Post ET_CLOAK_BEGINNING event
3. Set `isFullyCloaked = true`
4. Make ship invisible in scene graph

#### BeginDecloaking (Internal)
1. Post ET_DECLOAK_BEGINNING event
2. If not currently mid-cloak: set timer = CloakTime (count down from full)
3. Set state = DECLOAKING(5)
4. Play uncloak animation

#### DecloakComplete (Timer Finished)
1. Set state = DECLOAKED(0)
2. Post ET_DECLOAK_COMPLETED event
3. If ship has shields: schedule delayed shield re-enable (delay = ShieldDelay)
4. If any shield facing was at 0 HP: reset to 1.0 HP
5. Restore ship visibility in scene graph

---

## Shield Interaction

Shield behavior is managed entirely by the cloaking code — the shield subsystem itself does NOT check cloak state.

### Cloaking (Shields Go Down)

1. When cloaking begins, a **delayed event** is scheduled (delay = ShieldDelay)
2. During the delay: shields are still active (brief vulnerability window)
3. After the delay: shield subsystem is **disabled** (no absorption, no recharge, hidden visually)
4. **Shield HP values are preserved** — NOT zeroed

### Decloaking (Shields Come Back)

1. When decloaking completes (state → DECLOAKED): a **delayed event** is scheduled (delay = ShieldDelay)
2. During the delay: shields remain disabled
3. After the delay: shield subsystem re-enables, begins recharging normally
4. If any facing had 0 HP: reset to 1.0 HP minimum

### Timeline Example

```
Time  Event                    Shields    Visible
----  -----                    -------    -------
0.0   Player activates cloak   ON         Yes
0.0   BeginCloaking fires      ON (delay) Fading...
1.0   ShieldDelay expires      OFF        Fading...
3.0   CloakTime expires        OFF        No (invisible)
...   Player deactivates cloak OFF        Appearing...
+3.0  CloakTime timer expires  OFF        Yes (visible)
+3.0  DecloakComplete fires    OFF (delay) Yes
+4.0  ShieldDelay expires      ON         Yes
```

(Assuming CloakTime = 3.0s and ShieldDelay = 1.0s)

---

## Energy Failure Auto-Decloak

If a cloaked ship's power grid cannot sustain the cloaking device, the cloak automatically fails:

```
if state == CLOAKED(3):
    efficiency = actualPower / maxPower
    if efficiency < ENERGY_THRESHOLD:
        StopCloaking()
        BeginDecloaking()
```

The `efficiency` field is computed by the PoweredSubsystem base class each tick as the ratio of actual power received to power requested. If the ship's reactor is damaged or the power grid is overloaded, efficiency drops and the cloak fails.

This creates the classic Star Trek mechanic: damaging a cloaked ship's power systems can force it to decloak.

---

## Weapon Interaction

Weapon firing is **not directly gated by cloak state** in the weapon CanFire code. Instead, the gating happens through the subsystem disable mechanism:

1. When cloaking begins, `ET_SUBSYSTEM_STATUS` events disable weapon subsystems
2. Disabled subsystems cannot fire (PoweredSubsystem `isEnabled` check)
3. The AI and Python scripting layer additionally checks `IsCloaked()` before initiating fire

### IsCloaked() Query

Returns `true` ONLY when `isFullyCloaked` is set (state == CLOAKED(3)). Returns `false` during CLOAKING and DECLOAKING transitions. This means:
- A ship in CLOAKING state is NOT considered "cloaked" for targeting purposes
- A ship in DECLOAKING state is also NOT considered "cloaked"
- Only fully cloaked ships are invisible to targeting

---

## Visual Effect System

### Transparency During Transitions

During CLOAKING and DECLOAKING, the ship's visual transparency is updated each frame based on `progress`:

- **progress = 0.0**: Fully visible (DECLOAKED)
- **progress = 1.0**: Fully invisible (CLOAKED)
- **Intermediate values**: Semi-transparent with shimmer effect

### Shimmer Effect

The visual transition includes a randomized shimmer:
- During cloaking: alpha = random_factor * progress (ripple effect)
- During decloaking: alpha uses a different scale with random offsets
- `rand()` is called per scene graph node to create non-uniform shimmer across the ship's surface

The shimmer makes ships briefly visible in a shimmering outline during transitions, matching the Star Trek visual language for cloaking devices.

### Owner vs Other Ships

- **Owner's ship**: The cloak effect node receives a special alpha value for the "looking out from inside" perspective
- **Other ships**: Scene graph alpha is modified recursively across all child nodes

---

## Network Serialization

### Cloak Propagation: StateUpdate CLK Flag ONLY

**Correction (Feb 2026)**: In multiplayer, cloaking does NOT use dedicated network opcodes.
A stock dedicated server trace with active cloaking (both Bird of Prey and Warbird) showed
**zero** StartCloak (0x0E) or StopCloak (0x0F) messages across all four trace files (server
packet trace, client packet trace, server message trace, client message trace). Zero instances
of event codes ET_START_CLOAKING or ET_STOP_CLOAKING were observed on the wire.

Cloak state propagates **entirely via StateUpdate flag 0x40** (the CLK flag):

```
if flag 0x40 is set:
    WriteBit(isEnabled)    // 1 = shields on (uncloaked), 0 = shields off (cloaked)
```

**Important**: The network transmits the `isEnabled` boolean, NOT the state machine value.
Clients receive "cloak on" or "cloak off" and run their own local state machine transitions,
including visual effects and timers.

### Observed Behavior

In a session with both BoP and Warbird actively cloaking:
- 119 cloak=ON state transitions observed in StateUpdate
- 484 cloak=OFF state transitions observed in StateUpdate
- Both ships spawned with cloak initially ON (first StateUpdate has CLK flag set)
- Zero 0x0E or 0x0F opcodes in any direction

### Opcodes 0x0E and 0x0F

These opcodes are registered in the generic event forward handler table (see
[event-forward-wire-format.md](../wire-formats/event-forward-wire-format.md)) and appear
in the opcode dispatch tables, but they are **dead code in multiplayer**. The cloak toggle
event is handled locally on the originating client, and only the resulting boolean state
(cloaked or not) crosses the wire via the periodic StateUpdate.

This is consistent with the design pattern: discrete toggles that change infrequently
(SubsystemStatus 0x0A for on/off) get dedicated opcodes for immediate notification, while
the cloak state — which transitions through multiple intermediate states (CLOAKING →
CLOAKED → DECLOAKING → DECLOAKED) — relies on the state replication system instead.

### Event Registration (Local Only in MP)

The cloaking subsystem registers for TWO pairs of events, but in multiplayer only the
local/subsystem-level pair fires:

**Subsystem-level** (actual cloak handler):
- ET_START_CLOAKING_NOTIFY → StartCloaking
- ET_STOP_CLOAKING_NOTIFY → StopCloaking

**MultiplayerGame-level** (registered but NOT observed on wire):
- ET_START_CLOAKING → would serialize to opcode 0x0E (but never fires in MP)
- ET_STOP_CLOAKING → would serialize to opcode 0x0F (but never fires in MP)

The _NOTIFY variants are offset by +1 from the request variants.

---

## Collision While Cloaked

An event constant for "cloaked collision" exists in the game's string table but has **zero references** — it is dead/unused content. Collisions while cloaked are handled through the normal collision damage pipeline with no special cloaked-collision logic.

A collision against a cloaked ship will deal normal damage and may trigger energy failure auto-decloak if the power subsystem is damaged enough.

---

## Implementation Requirements

An OpenBC server implementation SHALL:

1. **Implement the 4-state state machine** (DECLOAKED, CLOAKING, CLOAKED, DECLOAKING) with timer-driven transitions
2. **Use class-level globals** for CloakTime and ShieldDelay (shared across all ships)
3. **Gate cloaking on energy** — refuse to cloak if power check fails
4. **Auto-decloak on energy failure** — efficiency below threshold forces decloak
5. **Disable shields with ShieldDelay** — delayed disable on cloak, delayed re-enable on decloak
6. **Preserve shield HP** through cloak/decloak cycles
7. **Reset 0 HP shield facings to 1.0** on decloak
8. **Disable weapon subsystems** when cloaking begins (via subsystem status events)
9. **Serialize cloak state** via StateUpdate flag 0x40 as a single bit
10. **Do NOT implement opcodes 0x0E/0x0F for cloak/decloak** — these are dead code in multiplayer. Cloak state propagates exclusively via the StateUpdate CLK flag. If desired, register them as no-ops for forward compatibility.
11. **Support IsCloaking/IsDecloaking queries** that include the ghost states (1 and 4) for API compatibility
12. **Run visual transparency** on clients locally (server does not need to compute shimmer)

---

## Constants

| Name | Value | Description |
|------|-------|-------------|
| STATE_DECLOAKED | 0 | Fully visible |
| STATE_CLOAKING | 2 | Transition to invisible (timer counting up) |
| STATE_CLOAKED | 3 | Fully invisible |
| STATE_DECLOAKING | 5 | Transition to visible (timer counting down) |
| GHOST_STATE_CLOAKING | 1 | Checked in IsCloaking, never assigned |
| GHOST_STATE_DECLOAKING | 4 | Checked in IsDecloaking, never assigned |
| DEFAULT_SHIELD_DELAY | 1.0 | Default ShieldDelay in seconds |
| ENERGY_THRESHOLD | (configurable) | Efficiency ratio below which auto-decloak triggers |

---

## Event Types

| Event | Name | Fired When |
|-------|------|------------|
| ET_START_CLOAKING | Request | Player activates cloak (local only — opcode 0x0E NOT used in MP) |
| ET_START_CLOAKING_NOTIFY | Forwarded | Subsystem receives cloak command |
| ET_STOP_CLOAKING | Request | Player deactivates cloak (local only — opcode 0x0F NOT used in MP) |
| ET_STOP_CLOAKING_NOTIFY | Forwarded | Subsystem receives decloak command |
| ET_CLOAK_BEGINNING | Notification | CloakComplete: state → CLOAKED(3) |
| ET_DECLOAK_BEGINNING | Notification | BeginDecloaking: state → DECLOAKING(5) |
| ET_DECLOAK_COMPLETED | Notification | DecloakComplete: state → DECLOAKED(0) |
| ET_SUBSYSTEM_STATUS | System | Subsystem enabled/disabled (used to disable weapons) |
