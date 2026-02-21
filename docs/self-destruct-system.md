# Self-Destruct System

Behavioral specification for the player self-destruct feature in Star Trek: Bridge Commander
multiplayer, documented from the shipped game's observable behavior, keyboard configuration
files, Python scripting API, and network packet captures.

**Clean room statement**: This document describes only externally observable behavior — key
bindings, network messages, game state changes, and scoring outcomes. No binary addresses,
decompiled code, or internal data structure offsets are referenced.

---

## 1. Overview

Self-destruct allows any player to instantly destroy their own ship by pressing **Ctrl+D**.
There is no confirmation dialog, no countdown timer, and no abort mechanism — the ship
explodes immediately.

**Direction**: Client → Host (request), then normal death pipeline (Host → All Clients)
**Reliability**: Sent reliably (ACK required)
**Frequency**: Rare — typically 0-3 per match

### Use Cases

- Player is disabled and floating (no engines, no weapons) — self-destruct is the only way
  to respawn
- Player is alone in a match and wants to reset
- Tactical choice to deny kill credit (death is counted, but no kill awarded to opponent)

---

## 2. Wire Format: HostMsg (Opcode 0x13)

The self-destruct request is the **simplest possible game message** — a single byte with no
payload.

```
Offset  Size  Type    Field          Notes
------  ----  ----    -----          -----
0       1     u8      opcode         Always 0x13
```

**Total size**: 1 byte.

The sender's identity is carried in the TGMessage transport envelope (the sender's connection
ID field), not in the game-level payload. The host uses the connection ID to look up which
ship belongs to the requesting player.

### Decoded Example

```
13                    opcode = 0x13 (HostMsg / SelfDestruct)
```

That's the entire message.

---

## 3. Execution Paths

Self-destruct has three execution paths depending on the player's role:

### Path 1: Multiplayer Client (most common)

```
Player presses Ctrl+D
  → Input event: ET_INPUT_SELF_DESTRUCT
    → Create 1-byte TGMessage containing opcode 0x13
      → Send to host via SendTGMessage(hostConnectionID, msg)
```

The client does NOT apply any local damage — it only sends the request. The host is
authoritative for all damage.

### Path 2: Multiplayer Host

```
Player presses Ctrl+D
  → Input event: ET_INPUT_SELF_DESTRUCT
    → Apply lethal damage to own ship's power subsystem directly
```

No network message is sent because the host is the damage authority. The resulting ship
death is broadcast to all clients through the normal death pipeline (see Section 5).

### Path 3: Single-Player

```
Player presses Ctrl+D
  → Input event: ET_INPUT_SELF_DESTRUCT
    → Apply lethal damage to own ship's power subsystem directly
```

Gated by the current game state — self-destruct is blocked during certain menu/test states.

---

## 4. Host-Side Processing (Opcode 0x13 Handler)

When the host receives opcode 0x13:

1. **Guard**: Check that multiplayer mode is active (ignore in single-player)
2. **Identify sender**: Read the sender's connection ID from the message envelope
3. **Look up ship**: Map connection ID → player ship object
4. **Apply lethal damage**: Deal damage equal to the power subsystem's maximum HP to the
   power subsystem itself, with `force_kill = true`

The damage is applied to the **power subsystem** (the ship's reactor), NOT directly to the
hull. Destroying the reactor causes cascade failure of all powered subsystems, leading to
total ship destruction.

### Damage Gates

Two conditions can prevent self-destruct damage from being applied:

| Gate | Condition | When Active |
|------|-----------|-------------|
| God Mode | Ship has god mode enabled | Debug/cheat mode, prevents all damage |
| Damage Disabled | Ship's damage flag is off | `DisableCollisionDamage(1)` or similar |

The `force_kill` flag bypasses the "insufficient damage" check but does NOT bypass these two
gates.

---

## 5. Death Pipeline (After Damage Applied)

Once the power subsystem's HP reaches zero:

```
Power subsystem HP → 0
  → SUBSYSTEM_HIT event fires
    → Ship death handler triggers
      → OBJECT_EXPLODING event fires
        → [MP] Host forwards explosion to all clients (opcode 0x06, PythonEvent)
        → [MP] Scoring handler processes the kill
        → [All] Explosion visuals and sounds play
      → Ship object is destroyed
        → [MP] DestroyObject (opcode 0x14) sent to all clients
        → [MP] Player UI cleanup (opcode 0x17) if needed
```

This is the **exact same death pipeline** used for combat kills, collision deaths, and
explosion deaths. Self-destruct does not have a special death path — it simply applies
enough damage to trigger the normal pipeline.

---

## 6. Scoring Impact

Self-destruct has specific scoring behavior because there is no attacker:

| Scoring Field | Value | Notes |
|---------------|-------|-------|
| Attacker | NULL (no attacker) | No player caused the kill |
| Kill credit | None awarded | Attacker ID is 0/NULL, scoring skips kill credit |
| Death counted | Yes | Death always counted for the destroyed ship's owner |
| Damage score | None | No damage ledger entries exist (instant kill) |

### Team Mode Behavior

In team deathmatch (stock Mission5), self-destruct awards a team kill to the **opposing
team**:

```
If self-destructing player is on Team 0 (Attackers):
    → Team 1 (Defenders) gets +1 team kill

If self-destructing player is on Team 1 (Defenders):
    → Team 0 (Attackers) gets +1 team kill
```

This prevents players from self-destructing to deny the enemy team a kill — the opposing
team benefits regardless.

### FFA Mode

In free-for-all (Mission1), self-destruct adds +1 death to the player with no corresponding
kill for anyone. This is a pure disadvantage for the self-destructing player.

---

## 7. Keyboard Binding

All stock keyboard configurations bind self-destruct to **Ctrl+D**:

```python
App.g_kKeyboardBinding.BindKey(
    App.WC_CTRL_D,
    App.TGKeyboardEvent.KS_KEYDOWN,
    App.ET_INPUT_SELF_DESTRUCT,
    0, 0
)
```

The binding appears in all localized keyboard configuration files (English, UK English,
German, French, Spanish, Italian).

Self-destruct also appears in the keyboard configuration menu under "Special Controls",
allowing players to rebind it.

---

## 8. Alternative Destruction Methods (AI/Script)

The player self-destruct mechanism (opcode 0x13, power subsystem damage) is distinct from
two other ship destruction methods used by AI and mission scripts:

| Method | Used By | Mechanism |
|--------|---------|-----------|
| **Opcode 0x13** (this document) | Player Ctrl+D | Lethal damage to power subsystem → cascade failure |
| **DestroySystem(hull)** | AI SelfDestruct module | Instant hull destruction (bypasses power subsystem) |
| **DamageSystem(hull, maxHP)** | Mission scripts | Hull damage equal to max HP |

The AI method (`DestroySystem`) is used in campaign missions for scripted ship explosions
(e.g., a ship self-destructs as part of a cutscene). It takes a different code path than
player self-destruct — it destroys the hull directly rather than cascading through the
power subsystem.

### Python API for AI Self-Destruct

```python
pShip = App.ShipClass_Cast(pObject)
pHull = pShip.GetHull()
pShip.DestroySystem(pHull)    # Instant hull destruction

# Fallback if hull destruction fails:
pObject.SetDeleteMe(1)         # Force-delete from world
```

### Python API for Scripted Damage

```python
pShip.DamageSystem(pShip.GetHull(), pShip.GetHull().GetMaxCondition())
```

---

## 9. Relationship to Other Opcodes

| Opcode | Name | Relationship |
|--------|------|-------------|
| 0x13 | HostMsg | Self-destruct request (this document) |
| 0x06 | PythonEvent | Carries OBJECT_EXPLODING after ship death |
| 0x14 | DestroyObject | Removes destroyed ship from all clients |
| 0x17 | DeletePlayerUI | Removes player from scoreboard (if disconnecting) |
| 0x29 | Explosion | Area-of-effect explosion damage (NOT used by self-destruct) |
| 0x36 | SCORE_CHANGE | Score update broadcast after death |

---

## 10. Server Implementation Notes

### Minimal Implementation

A server that handles self-destruct needs to:

1. **Receive opcode 0x13** (1 byte, no payload)
2. **Identify the sender** from the transport envelope's connection ID
3. **Look up the sender's ship** in the player→ship mapping
4. **Apply lethal damage** to the ship's power subsystem (HP = maxHP of damage)
5. **Let the normal death pipeline handle the rest** — explosion broadcast, scoring, cleanup

### Key Details

- The message has **zero payload** — sender identity comes from the transport layer
- Damage should be applied to the **power subsystem**, not the hull directly. This triggers
  cascade failure through all powered subsystems, matching stock behavior
- The `force_kill` flag should be set to ensure the damage goes through even if the subsystem
  would normally survive
- God mode and damage-disabled checks should still be respected
- Self-destruct should NOT be possible during certain game states (e.g., ship selection,
  game-over screen)

### Scoring Integration

- When processing the death, set attacker to NULL/0
- Award no individual kill credit (attacker is NULL)
- Always count the death for the destroyed player
- In team modes, award +1 team kill to the opposing team
- No damage ledger entries are created (the damage is instant and internal)

### Respawn

After self-destruct, the player follows the normal respawn flow — ship is destroyed, player
returns to ship selection screen, picks a new ship, and re-enters the game. No special
respawn handling is needed for self-destruct.

---

## Related Documents

- **[combat-system.md](combat-system.md)** — Damage pipeline that self-destruct feeds into
- **[power-system.md](power-system.md)** — Power subsystem that receives the lethal damage
- **[pythonevent-wire-format.md](pythonevent-wire-format.md)** — OBJECT_EXPLODING event format
- **[explosion-wire-format.md](explosion-wire-format.md)** — Explosion opcode 0x29 (not used by self-destruct)
- **[disconnect-flow.md](disconnect-flow.md)** — Player removal flow (shares cleanup opcodes)
- **[gamemode-system.md](gamemode-system.md)** — Scoring rules for self-destruct in different game modes
- **[phase1-verified-protocol.md](phase1-verified-protocol.md)** — Full wire protocol reference
