# Python API Surface


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

