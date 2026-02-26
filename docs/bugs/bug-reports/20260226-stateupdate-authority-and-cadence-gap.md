# Bug Report: StateUpdate Authority/Cadence Gap (Shield Flicker, Subsystem Drift, Hull Disagreement)

**Date**: 2026-02-26  
**Type**: Multiplayer replication correctness  
**Impact**: High (visible gameplay desync)

---

## Summary

Observed multiplayer behavior indicates a replication boundary mismatch:

- Clients send owner-state updates upstream (`0x1C` with mostly `0x8x/0x9x` flags).
- Stock server broadcasts downstream with subsystem-bearing packets (`0x20/0x3x` patterns).
- OpenBC currently shows a mixed pattern where client-style packets are forwarded downstream while separate pure `0x20` packets are also emitted.

This mismatch lines up with reported symptoms:

- Flickering shields/subsystems
- Hull value disagreements
- Power slider/state disagreement (including non-100% defaults on remote views)

---

## Evidence (Wire-Level, Behavioral)

### Expected from stock captures

- Upstream (`C->S`) `0x1C` traffic is dominated by `0x8x/0x9x` flag families.
- Downstream (`S->C`) `0x1C` traffic is dominated by `0x20` and `0x3x` (subsystem-bearing) families.
- Downstream subsystem updates are paced near 10 Hz per active ship lane.

### OpenBC comparison capture

From `build/openbc-20260225-210656.log`:

- Incoming `0x1C` traffic is client-style (`0x8x/0x9x`) as expected.
- Outgoing subsystem traffic is present, but packet shape/cadence differs from stock:
  - Outgoing `0x20` cadence is closer to ~7 Hz in the sampled session.
  - Byte distribution inside outgoing `0x20` blocks is notably different from stock/baseline traces.

Interpretation:

- Downstream state appears to combine forwarded owner packets plus separate subsystem packets instead of a stock-like server-shaped downstream stream.

---

## Why This Produces the Symptoms

1. **Shield/subsystem flicker**: client-local simulation and server packets disagree in value/timing; HUD values bounce between sources.
2. **Hull disagreements**: peers are not converging to one consistent downstream authority stream.
3. **Power grid mismatch**: engineering state is transported inside subsystem replication; if content/cadence/ordering diverges, slider and enabled-state views diverge.

---

## Suggested Implementation Fix (Clean-Room)

### 1) Treat upstream `0x1C` as input, not as final downstream payload

- Ingest owner updates to refresh server-side ship input state (movement/orientation/speed/weapon indicators as needed).
- Do not forward incoming owner payloads downstream as-is.

### 2) Build downstream `0x1C` on the server at stock-like cadence

- Emit one server-shaped downstream stream per active ship lane, target ~10 Hz.
- Include subsystem-bearing payloads in the same downstream family used by stock (`0x20` and mixed `0x3x` as applicable).
- Keep round-robin cursor behavior stable and deterministic.

### 3) Keep subsystem/power bytes and timing stock-like

- Preserve expected engineering payload structure and ordering semantics.
- Maintain per-ship cadence even under load; avoid skipping long windows.

### 4) Apply client engineering intent before downstream broadcast

- Ensure on/off and power-percentage intent received from clients is reflected in the server-side ship model prior to subsystem broadcast.

---

## Acceptance Criteria

1. In multiplayer traces, downstream `0x1C` is predominantly subsystem-bearing (`0x20/0x3x`) with stock-like frequency.
2. Outgoing subsystem cadence is near 10 Hz per active ship lane (within normal jitter bounds).
3. Subsystem/power byte distributions are qualitatively aligned with stock baseline sessions.
4. Repro symptoms disappear or materially reduce:
   - no shield/subsystem HUD flicker
   - consistent hull values across peers
   - power defaults and slider states converge correctly

