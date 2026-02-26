# Overview


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

