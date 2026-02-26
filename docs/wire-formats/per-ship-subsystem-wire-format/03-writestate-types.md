# WriteState Types


Three serialization formats exist for subsystem health data:

| Format | Used By | Bytes (remote ship) | Layout |
|--------|---------|---------------------|--------|
| **Base** | Hull, Shield Generator | 1 + children | `[condition:u8][child_conditions...]` |
| **Powered** | Sensors, Impulse, Warp, Phasers, Torpedoes, Tractors, Pulse Weapons, Cloak, Repair | 1 + children + 2 | `[condition:u8][child_conditions...][hasData:bit=1][powerPct:u8]` |
| **Reactor** | Power Subsystem (reactor/warp core) | 3 | `[condition:u8][mainBattery:u8][backupBattery:u8]` |

- **condition**: health percentage scaled to 0–255 (0xFF = 100%, 0x00 = destroyed)
- **powerPct**: power allocation percentage (0–100)
- **mainBattery/backupBattery**: battery charge level scaled to 0–255
- The reactor always writes battery bytes regardless of ship ownership
- Powered subsystems only write power data for remote ships (not the local player's own ship)
- Child subsystems (individual weapons, engines) always use the 1-byte Base format

