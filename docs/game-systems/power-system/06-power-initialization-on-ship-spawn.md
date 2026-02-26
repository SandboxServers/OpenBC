# Power Initialization on Ship Spawn


Every ship spawns with all subsystems at 100% power, batteries full, and all consumers enabled. The initialization sequence establishes these defaults at three levels:

1. **Constructor defaults**: Every powered subsystem is constructed with `powerPercentageWanted = 1.0` (100%), `isOn = true`, `powerMode = 0` (main-first), `efficiency = 1.0`, and `conditionRatio = 1.0`. No explicit setter call is needed — the 100% default is baked into object construction.

2. **Property setup**: When the subsystem is configured from its hardpoint definition, it reads the already-set 100% value and computes `powerWanted = normalPowerPerSecond * 1.0`. The EPS distributor fills both batteries to their maximum capacity at this stage: `mainBatteryPower = MainBatteryLimit`, `backupBatteryPower = BackupBatteryLimit`.

3. **Ship-level safety**: After all subsystems are constructed and linked, the ship object redundantly calls `SetPowerPercentageWanted(1.0)` on every powered subsystem as a safety net.

4. **Reactor enable guard**: When the reactor subsystem is enabled, if `powerPercentageWanted <= 0.0`, it is forced to 1.0. This prevents a subsystem from remaining at 0% after being re-enabled.

**Summary**: On spawn, every slider is at 100%, every subsystem is ON, batteries are full, and all consumers use draw mode 0 (main-first). This is what the player sees on the F5 Engineering panel immediately after spawning.

---

