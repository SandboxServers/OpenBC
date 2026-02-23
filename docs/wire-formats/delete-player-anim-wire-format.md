# DeletePlayerAnim (Opcode 0x18) Wire Format

**Clean room statement**: All wire formats are derived from packet captures of stock BC clients and servers, readable Python scripts, and observable game behavior. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

Opcode 0x18 is a **player notification animation** message sent by the server to all remaining clients when a player joins or disconnects. It instructs the client to display a floating text notification using a localized string template loaded from a TGL resource file.

Two variants exist based on the TGL lookup key:
- **"New Player"** — displayed when a player joins the game
- **"Delete Player"** — displayed when a player leaves the game

The message carries the player's name, which is formatted into the localized template string and displayed as a floating 3D text element for approximately 5 seconds.

---

## Wire Format

```
Offset  Size  Type     Field            Notes
------  ----  ----     -----            -----
0       1     u8       opcode           Always 0x18
1       var   stream   player_name      Player name (TGBufferStream encoded)
```

The player name is encoded using the standard TGBufferStream format: a length-prefixed byte sequence containing the player's display name as it appears on the scoreboard.

---

## Client-Side Behavior

Upon receiving opcode 0x18, the client:

1. **Reads the player name** from the message stream
2. **Loads `data/TGL/Multiplayer.tgl`** — a localization resource file containing text templates for multiplayer UI elements
3. **Looks up the appropriate entry** — either `"New Player"` (join context) or `"Delete Player"` (disconnect context)
4. **Formats the notification text** by combining the template with the player name (e.g., `"PlayerName has left the game"`)
5. **Creates a floating text element** in the 3D scene:
   - Duration: approximately **5 seconds** before fading
   - Opacity: **1.25** initial alpha (bright/prominent)
   - Position: centered on screen or attached to a scene node

The text element is a temporary 3D object that fades out after the display duration.

### TGL Resource Dependency

The client **requires** the file `data/TGL/Multiplayer.tgl` to be present and valid. This file is part of the standard BC 1.1 installation and contains localized string templates for multiplayer notifications.

**Critical implementation note**: The stock client performs **no validation** on the TGL lookup result before passing it to a string conversion function. If the TGL file is missing, corrupt, or does not contain the expected entry, the stock client will crash with an access violation. An OpenBC reimplementation **must validate** that the TGL lookup succeeded before using the result.

---

## Usage Contexts

### Disconnect Context (Server → Remaining Clients)

Sent as part of the disconnect cleanup sequence, after DestroyObject (0x14) and DeletePlayerUI (0x17):

```
Disconnect cleanup order:
  1. 0x14 DestroyObject     — removes the player's ship from the game world
  2. 0x17 DeletePlayerUI    — removes the player from the scoreboard
  3. 0x18 DeletePlayerAnim  — displays "Player X has left" notification
```

All three messages use reliable delivery and require ACK from each remaining client.

### Join Context (Event-Driven, NOT from 0x18 Opcode)

The "New Player" notification is **not** triggered by opcode 0x18. Instead, it is triggered internally by an event handler when the engine processes a NewPlayerInGame (0x2A) message. The handler follows the same code path as the 0x18 handler — loading the same TGL file and looking up `"New Player"` instead of `"Delete Player"`.

This means the TGL crash vulnerability affects **both** the join path (via the event handler) and the disconnect path (via opcode 0x18). An implementation must validate TGL lookups in both code paths.

---

## Trace Evidence

- **0 instances** of opcode 0x18 observed across all available traces (34-minute combat session + 91-second loopback session)
- This is consistent with no player disconnects occurring during the traced sessions
- The disconnect cleanup sequence (0x14 + 0x17 + 0x18) has not yet been captured on the wire

---

## Implementation Notes

### TGL Lookup Validation

The stock client crashes if the TGL resource file is missing or corrupt. An OpenBC reimplementation should:

1. **Check TGL load result**: Verify the TGL file loaded successfully before attempting lookups
2. **Check entry result**: Verify the lookup returned a valid string pointer (not NULL or near-NULL)
3. **Provide fallback text**: If the TGL lookup fails, use a hardcoded fallback string (e.g., `"Player joined"` / `"Player left"`) rather than crashing
4. **Handle missing file gracefully**: The dedicated server may not have `data/TGL/` files, and the notification is cosmetic — the game should continue without it

### Relationship to DeletePlayerUI (0x17)

Opcode 0x17 and 0x18 serve different purposes:
- **0x17** (DeletePlayerUI) updates the **engine's internal player list** and **scoreboard** — this is functionally critical for correct game state
- **0x18** (DeletePlayerAnim) creates a **cosmetic floating text notification** — this is visual feedback only

If an implementation cannot display the notification (missing resources, headless server), opcode 0x18 can be safely ignored without affecting game state.

### Reliable Delivery

Like all game opcodes except StateUpdate (0x1C), opcode 0x18 uses reliable delivery. The server expects an ACK from each client for the message.

---

## Related Documents

- [delete-player-ui-wire-format.md](delete-player-ui-wire-format.md) — Opcode 0x17: player list and scoreboard management
- [../network-flows/disconnect-flow.md](../network-flows/disconnect-flow.md) — Complete disconnect cleanup sequence
- [../network-flows/join-flow.md](../network-flows/join-flow.md) — Player join flow (NewPlayerInGame → event → "New Player" notification)
