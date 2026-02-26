# Power Generation


- The reactor generates `PowerOutput * conditionPercentage` units per second.
- `conditionPercentage` = reactor current HP / reactor max HP (0.0–1.0).
- Generation runs on a **1-second interval**. If multiple seconds have elapsed, the missed ticks are batched.
- Generated power fills the **main battery first**. If main battery is full, overflow fills the **backup battery**.
- If both batteries are full, excess power is wasted.
- **Battery recharge is HOST-ONLY in multiplayer.** Clients receive battery state via network sync, not local simulation.

---

