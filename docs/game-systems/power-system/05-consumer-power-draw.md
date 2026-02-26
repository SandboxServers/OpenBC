# Consumer Power Draw


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

