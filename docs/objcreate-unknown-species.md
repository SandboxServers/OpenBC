# ObjCreate Behavior with Unknown Species IDs

How Bridge Commander's stock game handles ObjectCreateTeam (opcode 0x03) messages containing species IDs that the server doesn't recognize — documented from behavioral observation, packet captures, and the game's shipped Python scripting API.

## Summary

| Question | Answer |
|----------|--------|
| Does the server relay the message to other clients? | **Yes**, always (unless duplicate object ID). |
| Is relay before or after local processing? | **After.** Local object creation runs first, then relay. |
| Does the server create state for unknown species? | **Yes.** A C++ ship object and network tracker are created regardless. |
| Does the server reject/drop unknown species? | **No.** The message is never dropped for species failure. |
| Does the stock dedi load hardpoint files? | **Yes**, through the Python `SpeciesToShip.InitObject` pipeline. |

## Background: Species Resolution Pipeline

When a peer receives an ObjCreateTeam message, it:

1. Reads the envelope (opcode, owner slot, team)
2. Creates a C++ ship object via the engine's factory system
3. Reads the species byte from the serialized stream
4. Calls `Multiplayer.SpeciesToShip.InitObject(ship, species_type)` in Python
5. That function calls `GetShipFromSpecies(species_type)` which:
   - Bounds-checks against `MAX_SHIPS` (46)
   - Loads the ship script module (e.g., `ships.Sovereign`)
   - Loads the hardpoint file (e.g., `ships.Hardpoints.sovereign`)
   - Creates subsystems via `SetupProperties()`
6. Continues reading position, orientation, name, and subsystem state from the stream
7. Relays the original message to other connected peers
8. Creates a network position/velocity tracker

## Three Failure Scenarios

### Scenario A: Species ID Out of Range (mod ship, species >= 46)

```python
# From Multiplayer/SpeciesToShip.py (shipped with game):
def GetShipFromSpecies(iSpecies):
    if iSpecies <= 0 or iSpecies >= MAX_SHIPS:   # MAX_SHIPS = 46
        return None
```

When `species_type >= 46` (a mod ship not in the standard table):

1. `GetShipFromSpecies()` returns `None` (no exception raised)
2. `InitObject()` checks for `None`, returns 0 (success code = 0, not an error)
3. The C++ caller does **not** treat return value 0 as failure
4. The ship C++ object exists but has:
   - **No model** (no NIF geometry loaded)
   - **No subsystems** (no shields, weapons, engines, etc.)
   - **No damage handling** capability
5. The species byte IS stored on the object (it was read before Python ran)
6. The remaining stream data (position, orientation, name) is still consumed
7. **The message is relayed to all other clients verbatim**
8. **A network tracker is created and attached**

### Scenario B: Valid Species Range but Ship Script Missing

When `species_type` is 1-45 but the corresponding ship script file doesn't exist (e.g., `ships/Akira.py` deleted):

1. `GetShipFromSpecies()` attempts `__import__("ships.Akira")`
2. Python raises `ImportError`
3. The exception propagates back to C++, which prints a traceback to stderr
4. Same result as Scenario A: empty ship hull with no model or subsystems

### Scenario C: Valid Species but Missing Hardpoint File

When the ship script exists but the hardpoint file doesn't (e.g., `ships/Hardpoints/akira.py` missing):

1. Ship model loads successfully (NIF geometry is present)
2. Hardpoint import raises `ImportError`
3. Exception propagates back to C++
4. **Partial initialization**: ship has a visible model but NO subsystems
5. The ship appears visually but cannot take or deal damage

## The "Empty Hull" Problem

In all three failure scenarios, the C++ ship object is created and persists:

| Component | State | Impact |
|-----------|-------|--------|
| C++ object | Created, in object table | Participates in lookups, state updates |
| Species byte | Stored on object | Preserved from the network stream |
| Visual model | NULL (scenarios A/B) or loaded (C) | No rendering / partial rendering |
| Subsystems | None created | No shields, weapons, engines |
| Damage handling | Not initialized | Ship cannot take damage |
| Network tracker | Created (position/velocity) | Reports default/zero position |
| Team assignment | Set correctly | Team is assigned regardless of species validity |

### Why Does This Happen?

The deserialization function that creates ship objects only returns NULL (causing the handler to skip relay and cleanup) when the **object ID is a duplicate** (an object with that ID already exists). Species failure does NOT cause a NULL return — the function always returns a valid ship pointer regardless of whether the Python species initialization succeeded.

The return value from the Python `InitObject` call is **not checked** by the C++ deserialization code. The C++ only checks for Python exceptions (which only occur in scenarios B and C, not A).

## Relay Behavior

The relay sends a **clone of the original raw message** — it does NOT re-serialize the locally created object. Every receiving client gets the exact same bytes and runs the same deserialization pipeline independently.

```
Timeline:
1. Parse envelope
2. Create object locally ← species lookup happens here (may fail)
3. Assign team
4. Relay original message to all other peers ← always happens
5. Create network tracker ← always happens
```

This means that if the server doesn't have a mod ship's files but a client does, the client will successfully create the modded ship from the same bytes that failed on the server. The relay is format-preserving.

## Host vs Client Processing

The relay loop executes on all multiplayer participants, but natural filtering ensures only the host actually sends packets:

- Clients only know about the host in their peer table
- For relayed messages, the sender is the host, which is filtered out
- The self-check filters the client's own connection

After relay, tracker creation differs by role:

| Role | Tracker for ObjCreate (0x02) | Tracker for ObjCreateTeam (0x03) |
|------|------------------------------|----------------------------------|
| Host | Not created (skipped) | Created for all ships (not torpedoes) |
| Client | Not created (skipped) | Created for OTHER players' ships only |

## Implications for Server Implementations

### Mod Ship Support

A dedicated server that wants to support mod ships (species >= 46) has two options:

**Option 1: Relay-only (no server-side state)**
- Parse the envelope and species byte
- Relay the message verbatim to other clients
- Do NOT attempt to create a local ship object
- Skip damage tracking for this ship (let clients handle it)
- Pro: Works with any mod; Con: No server-authority for this ship

**Option 2: Extended species table**
- Extend the `SpeciesToShip` table to include mod ship definitions
- Load mod ship scripts and hardpoints on the server
- Full server-side state for mod ships
- Pro: Full authority; Con: Server must have mod files installed

### Crash Prevention

Empty hull ships (failed species init) are a crash risk because:

1. **Bounding queries**: Code that queries a ship's bounding box may dereference the NULL model pointer
2. **State serialization**: The subsystem state serializer may encounter an empty subsystem list
3. **Collision detection**: The proximity system may query bounds of a model-less ship

A robust server should either:
- Guard all bounding/model access with NULL checks
- Refuse to create server-side objects for unknown species (relay the message but don't create local state)
- Implement a cleanup mechanism to remove empty hull objects

### Species ID Ranges

| Range | Category | Notes |
|-------|----------|-------|
| 0 | UNKNOWN | Reserved, always returns None |
| 1-15 | Playable ships | Standard multiplayer ships |
| 16-45 | Non-playable objects | NPCs, stations, asteroids |
| 46+ | Out of range | Mod ships, always fails stock lookup |

The full species table is documented in [objcreate-wire-format.md](objcreate-wire-format.md).

## Observed from Packet Captures

All observations are from BC 1.1 stock dedicated server + stock client sessions:

- Species 1-15 always succeed in stock installations
- The relay loop sends to all connected peers (verified via packet trace: one ObjCreateTeam from client produces N-1 relayed copies)
- The relay preserves the original message byte-for-byte (verified by comparing relay to original)
- Network tracker creation is confirmed by the appearance of StateUpdate packets for the new object within ~100ms of creation
- No stock captures of species >= 46 exist (requires modded client), but the Python source code path is clear
