# 8. Sovereign-Class Detailed Example


For the **local player's own ship**, Powered subsystems omit the power percentage byte (bit=0), saving ~1 byte each. The Power subsystem (Reactor) always writes both battery bytes regardless.

### Full Cycle Timing (remote ship)

With a 10-byte budget per tick at ~10 Hz:
- Tick 1 (index 0): Hull(1) + Shield(1) + Sensor(3) + Reactor(3) = 8 bytes, then Impulse (5 bytes would exceed budget)
- Tick 2 (index 4): Impulse(5), then Torpedoes (9 bytes would exceed)
- Tick 3 (index 5): Torpedoes(9)
- Tick 4 (index 6): Repair(3), then Phasers (11 bytes would exceed)
- Tick 5 (index 7): Phasers(11) — over budget but completes since it started within budget
- Tick 6 (index 8): Tractors(7), then Warp Engines (5 bytes would exceed)
- Tick 7 (index 9): Warp Engines(5) + Bridge(1) = 6 bytes, full cycle complete
- Full cycle: ~7 ticks = ~700ms

Actual timing varies because the serializer checks the budget **before** each subsystem's WriteState, not after. A subsystem that starts within budget will complete even if it exceeds the limit.

---

