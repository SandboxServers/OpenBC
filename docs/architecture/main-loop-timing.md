# Main Loop & Timing

How Bridge Commander drives its game loop, computes frame time, and schedules updates. Documented from the NetImmerse/Gamebryo NiApplication framework (public SDK) and observable runtime behavior.

**Clean room statement**: This document describes timing behavior derived from the public Gamebryo 1.2 SDK source (NiApplication framework), observable frame rates, and the game's shipped Python scripting API. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

Bridge Commander uses a **variable-timestep busy loop** with no fixed tick rate. The game runs as fast as the CPU allows, computing a `deltaTime` each frame from the system clock. All game systems — physics, damage, repair, power, shields — multiply by `dt` to remain frame-rate-independent.

There is no fixed 30 Hz or 60 Hz server tick. The effective frame rate depends entirely on what's limiting execution: GPU rendering in the stock client, or nothing at all in the stock dedicated server.

---

## Main Loop Architecture

### The Idle Loop

The game uses the standard NetImmerse `NiApplication` main loop pattern — a Win32 `PeekMessage` idle loop:

```
while (true) {
    if (PeekMessage(&msg)) {
        if (msg == WM_QUIT) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    } else {
        OnIdle();   // runs a full game frame
    }
}
```

When no Windows messages are queued, `OnIdle()` executes immediately. There is no `Sleep()`, no `WaitMessage()`, and no explicit yield in the main loop. If game logic completes quickly, the loop starts the next frame immediately.

### OnIdle

Each call to `OnIdle()` performs the following:

1. **Update time** — reads the system clock, computes `deltaTime`
2. **Frame rate limiter check** — sets a "ready to render" flag if enough time has elapsed since the last rendered frame (1/60th of a second). This flag only gates rendering; game logic runs regardless.
3. **Sound update**
4. **Increment frame counter**
5. **Application state machine** — handles game start/end transitions

The frame rate limiter uses a minimum frame period of **1/60 second** (overriding the engine's default of 1/100). This is a soft cap on rendering only — the engine inherited a `MeasureTime()` gate from NiApplication but Bridge Commander stubs it out and uses a direct time comparison instead.

### MainTick

The core game update runs every frame with this sequence:

```
1.  Clock Update           — read system timer, compute deltaTime
2.  Game Timer Manager     — fire game-time timers (scaled by time rate)
3.  Frame Timer Manager    — fire wall-clock timers (unscaled)
4.  Event Manager          — dispatch all queued events
5.  Updateable Scheduler   — update registered game objects (budget-limited)
6.  Save/Load Processing   — handle pending save/load requests
7.  Scene Graph Update     — update scene tree, run Python OnIdle callbacks
8.  Renderer Update        — update renderer state (if renderer exists)
9.  Render Frame           — draw and present (if renderer exists)
10. Post-Frame Cleanup     — scene manager housekeeping
```

---

## Time System

### Clock

A global clock object reads the system timer once per frame:

```
deltaTime = (currentTime - previousTime) * 0.001    // milliseconds to seconds
accumulatedTime += deltaTime
frameCount++
```

The primary time source is `timeGetTime()` (Windows multimedia timer, ~1ms resolution). An optional high-resolution path using `QueryPerformanceCounter` exists but is secondary — the QPC value is read but `deltaTime` is always derived from `timeGetTime()`.

### Two Time Domains

The game maintains two separate time values, each driving its own timer manager:

| Time Domain | Advances As | Used By |
|-------------|-------------|---------|
| **Game time** | `gameTime += deltaTime * timeRate` | Game logic timers: power system (1-second intervals), repair ticks, torpedo reload, shield recharge |
| **Frame time** | `frameTime += deltaTime` (always 1:1 with wall clock) | Wall-clock timers: network timeouts, keepalive intervals, UI animations |

When `timeRate = 1.0` (the default), both domains advance identically. The Python scripting API exposes `SetTimeRate()` to scale game time — this affects all game logic timers but NOT network/wall-clock timers.

### Timer Manager

Each timer manager maintains a sorted list of pending timers. On each frame update:

1. Walk the list from earliest to latest
2. For each timer whose fire time <= current time: post the timer's event to the event manager
3. For repeating timers: reschedule by adding the interval to the fire time

This is how periodic game systems run — the power system's 1-second update, for example, is a repeating game-time timer, not a per-frame check.

---

## Updateable Scheduler (Frame Budget System)

Game objects that need per-frame updates (ships, AI, physics, network) register with a central scheduler. The scheduler uses a **time-budgeted priority system**:

### Budget Calculation

- Maintains a **16-sample ring buffer** of recent frame times
- Computes the average (excluding min/max outliers) as the per-frame time budget
- This smooths out spikes from garbage collection, disk I/O, etc.

### Priority Tiers

Objects are organized into **4 priority tiers** (0 = highest, 3 = lowest):

| Tier | Typical Contents |
|------|-----------------|
| 0 (high) | Input processing, critical game state |
| 1 | Ship updates, weapon systems |
| 2 | AI pathfinding, non-critical updates |
| 3 (low) | Background tasks |

### Scheduling Algorithm

1. Select a starting tier using a **round-robin counter** (rotates each frame)
2. Update objects in that tier until the time budget is exhausted
3. If budget remains, proceed to the next tier
4. Continue until budget is spent or all tiers are serviced

This ensures that under heavy load, no single tier (e.g., expensive AI pathfinding) starves other tiers. The round-robin rotation guarantees every tier gets first pick equally often.

### Network Updates

The network subsystem (`TGWinsockNetwork`) is registered as an updateable in this scheduler. It processes incoming UDP packets and sends outgoing messages within its allocated time budget. The network processing budget is generous (15 seconds) to prevent message queue backup.

---

## Effective Tick Rates

### Stock Client (with Renderer)

| Factor | Effect |
|--------|--------|
| Main loop | PeekMessage busy loop, no sleep |
| Frame rate limiter | 60 FPS soft cap (rendering only, not game logic) |
| GPU | Vsync or render time provides the dominant bottleneck |
| **Effective rate** | **~30-60 FPS** depending on GPU and vsync settings |
| Game logic rate | Runs every loop iteration (potentially faster than render rate) |

### Stock Dedicated Server (Headless)

| Factor | Effect |
|--------|--------|
| Main loop | Same PeekMessage busy loop |
| Renderer | Absent — no SwapBuffers/Present to block on |
| Frame rate limiter | Still 1/60, but only gates a render-ready flag nothing reads |
| Sleep calls | None in the main loop (only 4 Sleep calls in the entire binary, all outside the loop) |
| **Effective rate** | **Unbounded — 100% CPU busy loop** at thousands of iterations/sec |
| Game time | Advances correctly (deltaTime computed from real clock, just very small per frame) |

The stock dedicated server was clearly designed as a "just run the normal game headless" approach with no thought given to CPU efficiency. It works correctly — game time advances properly, all systems update — but wastes an entire CPU core spinning.

---

## Python Time API

The scripting API exposes time control:

| Function | Effect |
|----------|--------|
| `SetTimeRate(rate)` | Scale game time (1.0 = normal, 0.5 = half speed, 2.0 = double) |
| `GetGameTime()` | Returns current game time in seconds (scaled) |
| `time.sleep(seconds)` | Blocks the calling thread (NOT the main loop — only useful in background threads) |

`SetTimeRate()` affects all game-time timers (power, repair, reload, recharge) but NOT wall-clock timers (network, keepalive). Setting `timeRate = 0` effectively pauses the game simulation while network and UI continue operating.

---

## Implications for Reimplementation

### Variable Timestep is Mandatory

All stock game systems use `value += rate * dt` style updates. A reimplementation MUST either:
- Use variable timestep (matching stock behavior exactly)
- Use fixed timestep with careful conversion of all rate constants

Fixed timestep is arguably better engineering, but every rate constant in every system (power output, shield recharge, repair rate, torpedo reload, phaser charge, tractor force) was tuned for variable `dt`. Converting requires dividing each rate by the chosen fixed timestep.

### Dedicated Server Should Throttle

The stock server's 100% CPU busy loop is wasteful. A reimplementation should add explicit throttling (Sleep/yield) to maintain a target tick rate. A 30 Hz server tick is more than sufficient for Bridge Commander's gameplay — the network already sends StateUpdates at ~10 Hz and most game timers fire at 1-second intervals.

### Two Timer Domains Must Be Preserved

Game-time and wall-clock time must remain separate systems. Network timeouts must never be affected by `SetTimeRate()`. If the game is paused (timeRate = 0), network keepalives and timeout detection must continue running.

### Frame Budget Scheduler is Optional

The priority-tiered update scheduler is an optimization for maintaining responsiveness under load. A reimplementation with a fixed tick rate and known workload may not need it — a simple flat update loop may suffice. However, if AI pathfinding or other expensive operations are added, the budget system becomes valuable.

---

## Related Documents

- **[engine-reference.md](engine-reference.md)** — Engine class hierarchy and application lifecycle
- **[transport-layer.md](../protocol/transport-layer.md)** — Network update scheduling and packet processing
- **[power-system.md](../game-systems/power-system.md)** — Power system 1-second timer (game-time domain)
- **[combat-system.md](../game-systems/combat-system.md)** — All combat rate constants that depend on deltaTime
