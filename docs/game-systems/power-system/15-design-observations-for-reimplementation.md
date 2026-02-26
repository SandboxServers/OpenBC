# Design Observations for Reimplementation


1. **Reactor and EPS are separate objects.** The reactor has HP and can be damaged (reducing power output). The EPS distributor manages batteries, conduits, and consumer registration. Both are created from the same hardpoint definition.

2. **1-second interval is deliberate.** Power generation and conduit recomputation happen once per second. Consumer draw happens every frame. This creates a "budget pool" that depletes over the second, naturally implementing first-come-first-served priority.

3. **No explicit priority system.** Subsystem draw priority is implicitly determined by the order they run their per-frame updates. There is no explicit priority queue.

4. **Backup conduit is the emergency reserve.** It is deliberately NOT health-scaled, ensuring damaged ships always have at least some power throughput for critical systems.

5. **Warp engines draw zero power.** Warp is gated by other mechanics (warp drive subsystem), not by power consumption. This is true across all ship classes.

6. **Cloaking is extremely power-hungry.** The Warbird's cloak (1,000/sec) exceeds any other single subsystem draw, and pushes total demand well above reactor capacity, creating mandatory battery drain while cloaked.

7. **Graceful degradation by design.** The efficiency ratio (0.0–1.0) means there's no cliff — subsystems degrade smoothly as power drops, giving the player time to manage.

8. **Faction design philosophy**:
   - **Federation**: High batteries, moderate output, balanced consumers
   - **Romulan**: Huge backup battery (200K), high output (1,500), but cloak creates massive drain
   - **Klingon**: Moderate across the board; Bird of Prey nearly power-neutral
   - **Cardassian**: Efficient consumers, moderate batteries; Matan Keldon has unusually high backup conduit (600)
   - **Ferengi**: Extreme tractor draw (2,000/sec) — designed around tractor beam gameplay
   - **Kessok**: High output (1,400), but hidden cloak draws 1,300/sec

9. **Battery refill** is available via `SetMainBatteryPower(GetMainBatteryLimit())` — used in docking and mission scripting.
