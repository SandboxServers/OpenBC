# Power & Reactor System — Clean Room Specification

Behavioral specification of the Bridge Commander power system, described purely in terms of observable behavior. No binary addresses, decompiled code, or implementation details. Suitable for clean-room reimplementation.

Derived from behavioral observation and publicly available game scripts. The reverse engineering
analysis (with binary addresses) is maintained separately in the STBC-Dedicated-Server repository.

---

## Overview

Bridge Commander ships have a reactor/power system with three conceptual layers:

1. **Reactor** — generates power per second, health-scalable
2. **Batteries** — stores power (main battery + backup battery)
3. **Conduits** — limits how much stored power can flow to subsystems per second (main conduit + backup conduit)
4. **Consumers** — powered subsystems that draw from conduits each frame

Five hardpoint parameters fully define a ship's power plant:

| Parameter | Meaning |
|-----------|---------|
| PowerOutput | Units of power generated per second (reactor rate) |
| MainBatteryLimit | Maximum capacity of the main battery |
| BackupBatteryLimit | Maximum capacity of the backup battery |
| MainConduitCapacity | Maximum power that can flow through the main conduit per second |
| BackupConduitCapacity | Maximum power that can flow through the backup conduit per second |

---

## Conceptual Architecture

```
                    ┌──────────────┐
                    │   Reactor    │ ← Has its own HP. Output scaled by health.
                    │  (generates) │
                    └──────┬───────┘
                           │ PowerOutput * conditionPct  (per second)
                           ▼
              ┌────────────────────────┐
              │    Main Battery        │ ← Fills first. Overflow goes to backup.
              │  (up to MainBatLimit)  │
              └────────────┬───────────┘
                           │                    ┌────────────────────────┐
                           │    overflow ──────►│    Backup Battery      │
                           │                    │ (up to BackupBatLimit) │
                           │                    └────────────┬───────────┘
                           ▼                                 ▼
              ┌─────────────────┐               ┌─────────────────┐
              │  Main Conduit   │               │ Backup Conduit  │
              │ (health-scaled) │               │  (NOT scaled)   │
              └────────┬────────┘               └────────┬────────┘
                       │                                  │
                       └──────────┬───────────────────────┘
                                  ▼
                    ┌──────────────────────────┐
                    │   Consumer Subsystems    │
                    │  (shields, engines, ...)  │
                    └──────────────────────────┘
```

---

## Power Generation

- The reactor generates `PowerOutput * conditionPercentage` units per second.
- `conditionPercentage` = reactor current HP / reactor max HP (0.0–1.0).
- Generation runs on a **1-second interval**. If multiple seconds have elapsed, the missed ticks are batched.
- Generated power fills the **main battery first**. If main battery is full, overflow fills the **backup battery**.
- If both batteries are full, excess power is wasted.
- **Battery recharge is HOST-ONLY in multiplayer.** Clients receive battery state via network sync, not local simulation.

---

## Conduit Limits

Each second, the power system computes how much power is available for subsystems to draw:

```
mainConduit  = min(mainBatteryPower, MainConduitCapacity * conditionPct)
backupConduit = min(backupBatteryPower, BackupConduitCapacity)
totalAvailable = mainConduit + backupConduit
```

**Key asymmetry**: The main conduit capacity is scaled by reactor health. The backup conduit capacity is NOT health-scaled. This means a damaged reactor reduces main power throughput but backup power stays at full capacity — the backup conduit acts as an emergency reserve.

---

## Consumer Power Draw

Every powered subsystem (shields, engines, weapons, sensors, repair, tractors, cloaking device) inherits from a common base class that handles power consumption.

### Per-Frame Draw

Each frame, every powered consumer:

1. **Computes demand**: `normalPowerPerSecond * powerPercentageWanted * deltaTime`
   - `normalPowerPerSecond` — base requirement set by the hardpoint script
   - `powerPercentageWanted` — user-controlled slider (0.0–1.0), defaults to 1.0
   - `deltaTime` — frame time in seconds

2. **Requests power** from the central distributor using one of three modes:

| Mode | Name | Behavior |
|------|------|----------|
| 0 | Main-first | Draw from main conduit; if insufficient, fall back to backup conduit |
| 1 | Backup-first | Draw from backup conduit; if insufficient, fall back to main conduit |
| 2 | Backup-only | Draw only from backup conduit; never touches main battery |

3. **Receives power**: May receive less than requested if conduits are depleted.

4. **Computes efficiency**: `efficiency = powerReceived / powerWanted` (0.0–1.0)

### Efficiency Effects

The efficiency ratio scales subsystem performance:
- **Shields**: Recharge rate scaled by efficiency
- **Weapons**: Charge rate / fire rate scaled by efficiency
- **Engines**: Acceleration/speed scaled by efficiency
- **No hard cutoff**: Subsystems don't turn off. At efficiency 0.0, they simply provide zero capability. Gradually increasing power gradually restores functionality.

### Per-Subsystem Power Mode Assignments

Each subsystem type has a fixed power draw mode assigned at construction time:

| Subsystem | Power Mode | Behavior |
|-----------|-----------|----------|
| Shields | 0 (main-first) | Standard draw |
| Sensors | 0 (main-first) | Standard draw |
| Impulse engines | 0 (main-first) | Standard draw |
| Warp engines | 0 (main-first) | Standard draw |
| Phasers | 0 (main-first) | Standard draw |
| Torpedoes | 0 (main-first) | Standard draw |
| Pulse weapons | 0 (main-first) | Standard draw |
| Repair | 0 (main-first) | Standard draw |
| **Tractor beams** | **1 (backup-first)** | Draws from backup battery first, then main as fallback |
| **Cloaking device** | **2 (backup-only)** | Draws exclusively from backup battery; never touches main |

Only **2 of 11** subsystem types override the default power mode. This creates a power priority hierarchy:

1. **Main battery** (mode 0): Reserved for combat-critical systems (weapons, shields, engines, sensors). These get first access to the primary power reservoir.
2. **Backup battery** (mode 1): Tractor beams draw from backup first. This prevents tractor operations from draining combat power unless the backup battery is exhausted.
3. **Backup-only** (mode 2): The cloaking device is completely isolated from the main power grid. It can only operate while the backup battery has charge, creating a natural time limit on cloak duration and making it impossible for cloaking to starve combat systems.

This is consistent with Star Trek engineering: backup/auxiliary power for stealth and utility systems, primary power for weapons and shields.

### Shield Recharge Special Path

When the shield subsystem itself is destroyed (condition = 0), individual shield facings that are still intact need recharge power. In this scenario, the shield system draws directly from the **backup battery** using a hardcoded path that bypasses the normal power mode switch.

This is NOT driven by the shield's power mode (which remains mode 0). It is a separate code path specifically for the dead-shield recovery state:

```
Shield Update:
  if shield subsystem is alive AND enabled:
    → Normal path: recharge facings using power from mode 0 (main-first)
  else (shield subsystem destroyed OR disabled):
    → Recovery path: for each surviving facing, draw from backup battery directly
    → Excess power returned to backup battery after recharge
```

This design means damaged shields preferentially consume backup batteries during recovery, preserving main battery charge for active combat systems during the vulnerable period when shields are down.

---

## Power Initialization on Ship Spawn

Every ship spawns with all subsystems at 100% power, batteries full, and all consumers enabled. The initialization sequence establishes these defaults at three levels:

1. **Constructor defaults**: Every powered subsystem is constructed with `powerPercentageWanted = 1.0` (100%), `isOn = true`, `powerMode = 0` (main-first), `efficiency = 1.0`, and `conditionRatio = 1.0`. No explicit setter call is needed — the 100% default is baked into object construction.

2. **Property setup**: When the subsystem is configured from its hardpoint definition, it reads the already-set 100% value and computes `powerWanted = normalPowerPerSecond * 1.0`. The EPS distributor fills both batteries to their maximum capacity at this stage: `mainBatteryPower = MainBatteryLimit`, `backupBatteryPower = BackupBatteryLimit`.

3. **Ship-level safety**: After all subsystems are constructed and linked, the ship object redundantly calls `SetPowerPercentageWanted(1.0)` on every powered subsystem as a safety net.

4. **Reactor enable guard**: When the reactor subsystem is enabled, if `powerPercentageWanted <= 0.0`, it is forced to 1.0. This prevents a subsystem from remaining at 0% after being re-enabled.

**Summary**: On spawn, every slider is at 100%, every subsystem is ON, batteries are full, and all consumers use draw mode 0 (main-first). This is what the player sees on the F5 Engineering panel immediately after spawning.

---

## Player Power Adjustment

Players adjust power distribution via the F5 Engineering panel (Power Transmission Grid). Two input paths converge on the same setter method.

### Input Path A: Mouse Slider

The drag-adjustable slider bars in the F5 panel map directly to `SetPowerPercentageWanted(newValue)` for the target subsystem. When weapons or engines are adjusted, all subsystems in that group are synced to the same value (phasers + torpedoes + pulse weapons, or impulse + warp).

### Input Path B: Keyboard Hotkeys

Keyboard shortcuts fire a `MANAGE_POWER` event. The event's integer parameter encodes both the subsystem group and the adjustment direction:

| Key Event Int | Group | Direction |
|---------------|-------|-----------|
| 0 | Weapons | Decrease (−25%) |
| 1 | Weapons | Increase (+25%) |
| 2 | Engines | Decrease (−25%) |
| 3 | Engines | Increase (+25%) |
| 4 | Sensors | Decrease (−25%) |
| 5 | Sensors | Increase (+25%) |
| 6 | Shields | Decrease (−25%) |
| 7 | Shields | Increase (+25%) |

**Encoding**: `int / 2` = group (0=weapons, 1=engines, 2=sensors, 3=shields), `int % 2` = direction (0=decrease, 1=increase). Values ≥ 8 are ignored.

### Valid Range: 0%–125%

The power percentage range is **0.0 to 1.25** (0% to 125%):

- **Keyboard**: Explicit clamping at 0.0 and 1.25
- **Slider**: C++ widget validates against the 1.25 maximum
- **Network**: Power byte encodes as `(int)(pct * 100)`, practical range 0–125
- **Server**: Does **NOT** enforce bounds — applies whatever the client sends

The 125% overload mechanic allows "overclocking" a subsystem beyond its rated power consumption. This increases demand above the ship's normal power budget, accelerating battery drain. It is visible in the F5 panel as a zone past the 100% mark.

### Subsystem Grouping

- **Weapons** (group 0): Phasers, torpedoes, and pulse weapons are all set to the same percentage simultaneously
- **Engines** (group 1): Impulse and warp engines are set together
- **Sensors** (group 2): Standalone
- **Shields** (group 3): Standalone

### On/Off Boundary Behavior

When the power percentage reaches boundaries, the subsystem's on/off state changes:

- **Setting to 0%**: The subsystem is turned off. This fires a `SubsystemStatus` network message (immediate reliable delivery), so all peers are notified instantly.
- **Setting to >0% on a disabled subsystem**: The subsystem is turned back on. Again fires an immediate `SubsystemStatus` message.
- **Setting between 0% and 125% on an already-enabled subsystem**: No on/off change, power percentage propagates via the periodic state replication (not immediately).

---

## Python API Surface

### Power Subsystem (reactor/battery manager)

**Query methods:**
- `GetPowerOutput()` — Current generation rate (health-scaled)
- `GetMainBatteryPower()` / `GetBackupBatteryPower()` — Current charge levels
- `GetMainBatteryLimit()` / `GetBackupBatteryLimit()` — Maximum capacities
- `GetMaxMainConduitCapacity()` — Raw main conduit limit (not health-scaled)
- `GetMainConduitCapacity()` / `GetBackupConduitCapacity()` — Current remaining capacity this interval
- `GetAvailablePower()` — Total available (main + backup)
- `GetPowerWanted()` — Total power demanded by all consumers
- `GetPowerDispensed()` — Total power delivered this interval
- `GetConditionPercentage()` — Reactor health (0.0–1.0)

**Manipulation methods:**
- `SetMainBatteryPower(float)` / `SetBackupBatteryPower(float)` — Set battery levels directly
- `AddPower(float)` — Add power to main battery
- `DeductPower(float)` — Remove power from system
- `StealPower(float)` — Drain from main battery
- `StealPowerFromReserve(float)` — Drain from backup battery

**Watchers:**
- `GetMainBatteryWatcher()` / `GetBackupBatteryWatcher()` — Event triggers on level changes

### Powered Subsystem (all consumers)

- `GetNormalPowerWanted()` — Base power requirement from hardpoint
- `GetPowerPercentageWanted()` — Current user slider value
- `SetPowerPercentageWanted(float)` — Set user slider (0.0–1.0+)

### Power Property (hardpoint template)

- `Get/SetMainBatteryLimit(float)`
- `Get/SetBackupBatteryLimit(float)`
- `Get/SetMainConduitCapacity(float)`
- `Get/SetBackupConduitCapacity(float)`
- `Get/SetPowerOutput(float)`

---

## AdjustPower Algorithm (Client-Side Auto-Balance)

The game's power display UI runs an auto-balance algorithm when total demand exceeds supply:

1. **Compute each subsystem's share** of total normal power (as a percentage).
2. **Check for deficit**: `totalDemand - (mainConduit + backupConduit)`.
3. **If deficit > 1% of total demand**:
   - Reduce each subsystem's `powerPercentageWanted` proportionally to its share of the deficit.
   - Never reduce below **20%** or the user's current desired setting (whichever is lower).
4. **Sync weapon types**: All weapons (phasers, torpedoes, disruptors) are set to the same percentage.
5. **Sync engine types**: Warp engines match impulse engines' percentage.

This runs client-side only (UI layer). The C++ power sim itself does not auto-balance — it simply delivers what's available, first-come-first-served from the consumer update order.

---

## Ship Power Parameters

### Playable Ships

| Ship | Faction | MainBattery | BackupBattery | MainConduit | BackupConduit | Output |
|------|---------|-------------|---------------|-------------|---------------|--------|
| Enterprise-E | Federation | 300,000 | 120,000 | 1,900 | 300 | 1,600 |
| Galaxy | Federation | 250,000 | 80,000 | 1,200 | 200 | 1,000 |
| Sovereign | Federation | 200,000 | 100,000 | 1,450 | 250 | 1,200 |
| Geronimo | Federation | 240,000 | 80,000 | 1,200 | 200 | 1,000 |
| Nebula | Federation | 100,000 | 150,000 | 1,000 | 200 | 800 |
| Akira | Federation | 150,000 | 50,000 | 900 | 100 | 800 |
| Ambassador | Federation | 200,000 | 50,000 | 700 | 100 | 600 |
| Peregrine | Federation | 50,000 | 200,000 | 900 | 100 | 800 |
| Shuttle | Federation | 20,000 | 10,000 | 140 | 40 | 100 |
| Warbird | Romulan | 100,000 | 200,000 | 1,700 | 300 | 1,500 |
| Vor'cha | Klingon | 100,000 | 100,000 | 900 | 200 | 800 |
| Bird of Prey | Klingon | 80,000 | 40,000 | 470 | 70 | 400 |
| Keldon | Cardassian | 140,000 | 50,000 | 700 | 100 | 600 |
| Galor | Cardassian | 120,000 | 50,000 | 550 | 150 | 500 |
| Matan Keldon | Cardassian | 160,000 | 50,000 | 1,200 | 600 | 900 |
| Cardassian Hybrid | Cardassian | 160,000 | 50,000 | 1,100 | 100 | 1,000 |
| Kessok Heavy | Kessok | 100,000 | 100,000 | 1,500 | 100 | 1,400 |
| Kessok Light | Kessok | 120,000 | 80,000 | 1,000 | 50 | 900 |
| Marauder | Ferengi | 140,000 | 100,000 | 900 | 200 | 700 |
| Sunbuster | — | 200,000 | 50,000 | 1,550 | 100 | 1,500 |
| Transport | — | 120,000 | 50,000 | 800 | 100 | 700 |
| Freighter | — | 70,000 | 40,000 | 650 | 400 | 600 |
| Card. Freighter | Cardassian | 50,000 | 10,000 | 400 | 200 | 400 |
| Escape Pod | — | 50,000 | 20,000 | 200 | 100 | 100 |
| Probe | — | 8,000 | 4,000 | 100 | 100 | 15 |

### Stations

| Station | MainBattery | BackupBattery | MainConduit | BackupConduit | Output |
|---------|-------------|---------------|-------------|---------------|--------|
| Fed Starbase | 800,000 | 200,000 | 5,500 | 500 | 5,000 |
| Fed Outpost | 100,000 | 20,000 | 1,700 | 200 | 1,500 |
| Card. Starbase | 200,000 | 200,000 | 2,500 | 500 | 2,000 |
| Card. Station | 150,000 | 150,000 | 1,300 | 300 | 1,000 |
| Card. Outpost | 50,000 | 100,000 | 1,600 | 200 | 1,500 |
| Card. Facility | 400,000 | 50,000 | 1,000 | 600 | 1,500 |
| Space Facility | 400,000 | 200,000 | 3,000 | 1,500 | 2,000 |
| Drydock | 50,000 | 5,000 | 650 | 50 | 600 |
| Comm Array | 10,000 | 5,000 | 700 | 200 | 600 |
| Comm Light | 180,000 | 5,000 | 1,000 | 400 | 600 |
| Kessok Mine | 40,000 | 20,000 | 350 | 50 | 300 |

---

## Subsystem Power Consumption

### Federation

| Subsystem | Sovereign | Enterprise | Galaxy | Nebula | Akira | Ambassador |
|-----------|-----------|-----------|--------|--------|-------|------------|
| Shields | 450 | 300 | 400 | 250 | 300 | 200 |
| Sensors | 150 | — | 100 | 100 | 150 | 50 |
| Impulse | 200 | — | 150 | 100 | 50 | 100 |
| Phasers | 400 | — | 300 | 200 | 200 | 150 |
| Torpedoes | 150 | — | 100 | 150 | 100 | 100 |
| Tractors | 700 | — | 600 | 400 | 600 | 600 |
| Repair | 1 | 1 | 1 | 1 | 1 | 1 |
| Warp | 0 | — | 0 | 0 | 0 | 0 |
| **Total** | **2,051** | — | **1,651** | **1,201** | **1,301** | **1,101** |

### Klingon

| Subsystem | Vor'cha | Bird of Prey |
|-----------|---------|-------------|
| Shields | 250 | 180 |
| Sensors | 100 | 50 |
| Impulse | 100 | 50 |
| Disruptor Beams | 50 | — |
| Disruptor Cannons | 150 | 80 |
| Torpedoes | 150 | 50 |
| Tractors | 700 | — |
| Cloak | 700 | 380 |
| Repair | 1 | 1 |
| Warp | 0 | 0 |
| **Total (uncloaked)** | **1,301** | **411** |
| **Total (cloaked)** | **2,001** | **791** |

### Romulan

| Subsystem | Warbird |
|-----------|---------|
| Shields | 400 |
| Sensors | 200 |
| Impulse | 300 |
| Disruptor Beams | 100 |
| Disruptor Cannons | 200 |
| Torpedoes | 150 |
| Tractors | 800 |
| Cloak | 1,000 |
| Repair | 1 |
| Warp | 0 |
| **Total (uncloaked)** | **2,151** |
| **Total (cloaked)** | **3,151** |

### Cardassian

| Subsystem | Keldon | Galor |
|-----------|--------|-------|
| Shields | 200 | 200 |
| Sensors | 50 | 50 |
| Impulse | 70 | 50 |
| Torpedoes | 70 | 50 |
| Compressors | 200 | 150 |
| Tractors | 400 | — |
| Repair | 1 | 1 |
| Warp | 0 | 0 |
| **Total** | **991** | **501** |

### Other Factions

| Subsystem | Marauder (Ferengi) | Kessok Heavy |
|-----------|-------------------|-------------|
| Shields | 200 | 500 |
| Sensors | 100 | 200 |
| Impulse | 50 | 200 |
| Weapons | 300 (Phasers+Plasma) | 400 (Positron+Torp) |
| Tractors | 2,000 | — |
| Cloak | — | 1,300 |
| Repair | 1 | 50 |
| Warp | 0 | 0 |
| **Total** | **2,651** | **1,350/2,650** |

---

## Power Budget Analysis

Ships are designed to run at a power deficit under full combat load:

| Ship | Output/sec | Full Draw/sec | Deficit/sec | Time to Drain Main Battery |
|------|-----------|--------------|------------|---------------------------|
| Sovereign | 1,200 | 2,051 | -851 | ~3m 55s |
| Enterprise-E | 1,600 | ~2,051 | -451 | ~11m 5s |
| Galaxy | 1,000 | 1,651 | -651 | ~6m 24s |
| Warbird | 1,500 | 2,151 | -651 | ~2m 34s |
| Warbird (cloaked) | 1,500 | 3,151 | -1,651 | ~1m 1s |
| Vor'cha | 800 | 1,301 | -501 | ~3m 20s |
| Bird of Prey | 400 | 411 | -11 | ~2 hours |
| Keldon | 600 | 991 | -391 | ~6m 0s |
| Marauder | 700 | 2,651 | -1,951 | ~1m 12s |

In practice, drain is slower because some subsystems are not always active (tractors, torpedoes during reload, etc.).

---

## Multiplayer Network Propagation

### Power Distribution Has No Dedicated Network Message

When a player adjusts the 4 engineering sliders (weapons, shields, engines, sensors), the
change to `powerPercentageWanted` is **not sent as a dedicated network message**. There is
no event-forwarding opcode, no Python-level TGMessage, and no immediate network send call.

Instead, power distribution percentages propagate **exclusively through the StateUpdate
subsystem health round-robin** (flag 0x20 block). Each client sets power locally, the
periodic state replication includes the current power percentages, and remote peers apply
them on receipt.

### Why No Dedicated Message?

The engine's event forwarding system selectively registers certain subsystem events for
network broadcast (weapon firing, cloak toggle, phaser intensity, etc.). The
"subsystem power changed" event is deliberately **not registered** for network forwarding.
It fires locally (for UI update) but never reaches the network layer.

The complete list of events that ARE network-forwarded:

| Event | Network Opcode | Description |
|-------|---------------|-------------|
| StartFiring | 0x07 | Weapon begins firing |
| StopFiring | 0x08 | Weapon stops firing |
| StopFiringAtTarget | 0x09 | Beam stops tracking |
| SubsystemStatus | 0x0A | Subsystem on/off toggle |
| RepairListPriority | 0x11 | Repair queue ordering |
| SetPhaserLevel | 0x12 | Phaser intensity (LOW/MED/HIGH) |
| StartCloaking | 0x0E | Cloak activated |
| StopCloaking | 0x0F | Cloak deactivated |
| StartWarp | 0x10 | Warp engaged |
| TorpedoTypeChange | 0x1B | Torpedo type selection |

**Power slider changes are absent from this list.**

### How Power Percentages Travel Over the Network

Power percentages are serialized inside the StateUpdate subsystem health block. Each
powered subsystem writes its current `powerPercentageWanted` as part of its periodic
state replication:

**Encoding (sender):**
```
[condition: byte]            // subsystem health 0-255
[children: recursive]        // child subsystem conditions
[hasData: 1 bit]             // 1 = remote ship, 0 = own ship
[if hasData=1:]
  [powerPctWanted: byte]     // (int)(powerPercentageWanted * 100.0)
```

**Decoding (receiver):**
```
if hasData bit is set:
    pctByte = read byte
    if timestamp is newer than last update:
        powerPercentageWanted = pctByte * 0.01
```

**Precision**: 1% steps (0-100 for normal range, up to 125 for overclocked subsystems).

### Own-Ship Skip

When the host sends a StateUpdate about ship X back to the player who owns ship X, the
power data is **omitted** (hasData = 0). This prevents the host from overwriting the
owner's local slider settings with stale network data.

When the host sends ship X's state to any other player, the power data IS included
(hasData = 1).

### Sign Bit for On/Off State

An alternate deserialization path packs the subsystem on/off state into the power byte:
- **Positive byte** → subsystem is ON, percentage = byte * 0.01
- **Negative byte** (bit 7 set) → subsystem is OFF, percentage = (-byte) * 0.01

This allows the on/off toggle to propagate via StateUpdate as well as through the
dedicated SubsystemStatus opcode (0x0A). The dedicated opcode provides immediate
notification; the StateUpdate provides eventual consistency.

### Data Flow

```
Client A adjusts engineering slider
  → SetPowerPercentageWanted(newValue) applied locally
  → No network message sent

Client A's next StateUpdate cycle (every ~100ms per subsystem)
  → PoweredSubsystem::WriteState includes powerPctWanted byte
  → Sent to host via opcode 0x1C

Host receives StateUpdate
  → Applies power percentage to its copy of Client A's ship
  → Rebroadcasts in next StateUpdate to Client B (hasData=1)
  → Sends back to Client A with hasData=0 (no overwrite)

Client B receives StateUpdate
  → Applies Client A's power percentages to its local copy
```

### Timing and Latency

- Power changes propagate at the StateUpdate round-robin rate (~10Hz per ship)
- With ~11 top-level subsystems on a Sovereign-class ship, a full cycle takes 1-2 seconds
- This means a slider change may take up to 1-2 seconds to reach all peers
- This is acceptable because power distribution is a gradual management activity, not a
  frame-critical action like weapon firing

### Server Authority

The host does **not** validate or enforce power percentages. It applies whatever the client
sends. A client could set `powerPercentageWanted` to 200% and the host would accept it.

### Auto-Balance is Client-Side Only

The `AdjustPower` algorithm (see above) runs locally on the adjusting client's UI layer.
Other peers see only the final balanced percentages via StateUpdate, not the intermediate
auto-balancing steps.

### Contrast: What IS Immediately Forwarded

| Action | Network Mechanism | Latency |
|--------|-------------------|---------|
| Subsystem on/off toggle | Dedicated opcode (0x0A) | Immediate (reliable) |
| Phaser intensity (LOW/MED/HIGH) | Dedicated opcode (0x12) | Immediate (reliable) |
| Power slider adjustment | StateUpdate (0x1C) piggyback | 1-2 seconds (round-robin) |
| Torpedo type change | Dedicated opcode (0x1B) | Immediate (reliable) |

The design rationale: discrete toggles (on/off, LOW/MED/HIGH, torpedo type) use immediate
reliable messages. Continuous values (power percentages) use the existing state replication,
which is more bandwidth-efficient for frequently-changing values.

---

## Power State Wire Format

This section describes the exact wire encoding and update algorithm for power state replication, providing the detail needed for a server to match client expectations.

### Two Serialization Interfaces

Power state uses two distinct serialization interfaces depending on context:

#### Interface A: Round-Robin (StateUpdate flag 0x20)

Used during **ongoing gameplay** in the subsystem health round-robin. Each powered subsystem conditionally includes a power byte:

**Sender:**
```
[condition: byte]            // subsystem health: (currentHP / maxHP) * 255, truncated
[children: recursive]        // each child subsystem writes its own condition byte
[hasData: 1 bit]             // 1 = remote ship (include power), 0 = own ship (skip)
[if hasData=1:]
  [powerPctWanted: byte]     // (int)(powerPercentageWanted * 100.0), range 0-125
```

**Receiver:**
```
read condition byte + children (recursive)
hasData = read 1 bit
if hasData:
    pctByte = read byte
    if previousTimestamp < packetTimestamp:
        SetPowerPercentageWanted(pctByte * 0.01)
```

The `hasData` bit allows the sender to skip power data when the recipient owns the ship, preventing stale server data from overwriting the player's local slider settings.

#### Interface B: ObjCreate / Full Snapshot

Used during **initial object creation** (ObjCreate opcode) and weapon round-robin. This path uses sign-bit encoding to pack the on/off state into the power byte:

**Sender:**
```
[condition: byte]            // health
[children: recursive]
[powerByte: signed byte]     // positive = ON, negative = OFF
                             // absolute value = (int)(powerPercentageWanted * 100.0)
```

**Receiver:**
```
read condition byte + children
powerByte = read signed byte
if powerByte < 1:            // 0 or negative
    isOn = false
    powerPercentageWanted = (-powerByte) * 0.01
else:
    isOn = true
    powerPercentageWanted = powerByte * 0.01
```

**Sign-bit encoding rationale**: During ObjCreate, the receiver needs both the power percentage AND the on/off state. Packing them into one byte avoids an extra bit/byte for the toggle. A subsystem at 50% and OFF encodes as `-50`; the receiver restores both the slider position (50%) and the disabled state. This allows a player's pre-disable slider position to survive across the network.

### Round-Robin Algorithm

The subsystem health round-robin uses a **persistent cursor** per peer per ship, allowing it to resume where it left off each tick:

1. **Per-peer state**: The server maintains a cursor (linked list node pointer) and an index counter for each ship being sent to each peer. These persist across ticks.
2. **Start index**: Each flag 0x20 block begins with a `startIndex` byte telling the receiver which subsystem index the data starts from.
3. **10-byte budget**: The serializer writes subsystem data until either 10 bytes of stream space are consumed, or the cursor wraps back to its starting position (full cycle complete).
4. **Wrap detection**: When the cursor reaches the end of the subsystem linked list, it wraps to the beginning and resets the index to 0. If it reaches its starting position, the full cycle is complete and it stops.

```
[startIndex: byte]           // which subsystem index the round-robin starts from
[subsystem_0: WriteState]    // subsystem at startIndex position
[subsystem_1: WriteState]    // next subsystem (if budget allows)
...                          // continues until 10-byte budget exhausted or full wrap
```

### Power Byte Encoding

| Direction | Formula | Range | Precision |
|-----------|---------|-------|-----------|
| Send | `(int)(powerPercentageWanted * 100.0)` | 0–125 | Truncation toward zero |
| Receive | `(float)byte * 0.01` | 0.00–1.25 | 1% steps |

Maximum precision loss: 0.009 (e.g., 0.256 → byte 25 → 0.25).

### Own-Ship Skip

When sending state about ship X to the player who owns ship X:

- **hasData = 0** (Interface A): Power byte is omitted. The client's local slider state is authoritative for its own ship.
- **Interface B** (ObjCreate): Always includes power data, including for own ship.

**Own-ship determination**: The sender compares the ship's object ID against the target peer's assigned ship object ID. If they match (and it's a multiplayer game), the ship is "own-ship" for that peer.

**Server MUST implement this**: If the server sends power data (hasData=1) to the ship's owner, the server will continuously overwrite the player's local engineering slider positions with stale data, making the F5 panel unusable.

### Timestamp Ordering

The receiver only applies power data from Interface A if the packet timestamp is newer than the last known update timestamp. This prevents stale data from overwriting newer state in cases of packet reordering. The receiver saves the previous timestamp BEFORE processing the base class data (which updates the stored timestamp), then compares the incoming packet timestamp against the saved value.

### Update Timing

- **StateUpdate rate**: ~10Hz per ship (one StateUpdate packet every ~100ms)
- **Per-tick budget**: 10 bytes for flag 0x20 (subsystem health)
- **Sovereign example**: 11 top-level subsystems, variable bytes per subsystem (1–11 bytes depending on children and power data). Full cycle takes ~3-5 ticks.
- **Full cycle time**: ~0.3–0.5 seconds at 10Hz for all subsystem power percentages to transmit
- **Server must send StateUpdates at this rate** for clients to see smooth power bar updates on remote ships

### Server Implementation Requirements

For an OpenBC server to correctly replicate power state:

1. **Track per-peer write cursor**: Maintain a subsystem list node pointer and index counter for each ship being sent to each peer. Persist across ticks.
2. **Respect 10-byte budget**: Stop writing subsystem data when 10 bytes of stream space are consumed in the flag 0x20 block.
3. **Send startIndex byte**: Write the current index counter at the start of each flag 0x20 block so the receiver knows which subsystem the data starts from.
4. **Skip power data for own-ship**: When sending to the peer who owns the ship, set `hasData = 0` (omit power byte). This is critical — failing to do this makes the F5 panel unresponsive.
5. **Include power data for remote ships**: For all other peers, set `hasData = 1` and encode power as `(int)(powerPercentageWanted * 100.0)`.
6. **Use sign-bit encoding on ObjCreate**: When sending initial object state, pack on/off into the power byte (positive = ON, negative = OFF). This gives the client both the slider position and the subsystem's enable state.
7. **Apply received power only if timestamp is newer**: When receiving power state from clients, compare the packet timestamp against the saved previous timestamp. Only apply if newer.
8. **Do NOT validate power values**: The stock server does not clamp or reject power percentages. A client sending 125% (or even higher via a mod) should be accepted as-is.

---

## Design Observations for Reimplementation

1. **Reactor and EPS are separate objects.** The reactor has HP and can be damaged (reducing power output). The EPS distributor manages batteries, conduits, and consumer registration. Both are created from the same hardpoint definition.

2. **1-second interval is deliberate.** Power generation and conduit recomputation happen once per second. Consumer draw happens every frame. This creates a "budget pool" that depletes over the second, naturally implementing first-come-first-served priority.

3. **No explicit priority system.** Subsystem draw priority is implicitly determined by the order they run their per-frame updates. There is no explicit priority queue.

4. **Backup conduit is the emergency reserve.** It is deliberately NOT health-scaled, ensuring damaged ships always have at least some power throughput for critical systems.

5. **Warp engines draw zero power.** Warp is gated by other mechanics (warp drive subsystem), not by power consumption. This is true across all ship classes.

6. **Cloaking is extremely power-hungry.** The Warbird's cloak (1,000/sec) exceeds any other single subsystem draw, and pushes total demand well above reactor capacity, creating mandatory battery drain while cloaked.

7. **Graceful degradation by design.** The efficiency ratio (0.0–1.0) means there's no cliff — subsystems degrade smoothly as power drops, giving the player time to manage.

8. **Faction design philosophy**:
   - **Federation**: High batteries, moderate output, balanced consumers
   - **Romulan**: Huge backup battery (200K), high output (1,500), but cloak creates massive drain
   - **Klingon**: Moderate across the board; Bird of Prey nearly power-neutral
   - **Cardassian**: Efficient consumers, moderate batteries; Matan Keldon has unusually high backup conduit (600)
   - **Ferengi**: Extreme tractor draw (2,000/sec) — designed around tractor beam gameplay
   - **Kessok**: High output (1,400), but hidden cloak draws 1,300/sec

9. **Battery refill** is available via `SetMainBatteryPower(GetMainBatteryLimit())` — used in docking and mission scripting.
