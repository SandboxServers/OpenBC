# Player Power Adjustment


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

