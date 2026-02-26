# AdjustPower Algorithm (Client-Side Auto-Balance)


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

