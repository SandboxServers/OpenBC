# Conduit Limits


Each second, the power system computes how much power is available for subsystems to draw:

```
mainConduit  = min(mainBatteryPower, MainConduitCapacity * conditionPct)
backupConduit = min(backupBatteryPower, BackupConduitCapacity)
totalAvailable = mainConduit + backupConduit
```

**Key asymmetry**: The main conduit capacity is scaled by reactor health. The backup conduit capacity is NOT health-scaled. This means a damaged reactor reduces main power throughput but backup power stays at full capacity — the backup conduit acts as an emergency reserve.

---

