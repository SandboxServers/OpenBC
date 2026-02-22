# Player Disconnect Flow

Observed and inferred behavior of the Bridge Commander 1.1 player disconnect protocol, documented from black-box packet captures and readable Python mission scripts. Wire format details are derived from observable data; the disconnect lifecycle is reconstructed from protocol structure and script-level behavior.

**Date**: 2026-02-17
**Method**: Packet capture with decryption (AlbyRules stream cipher), Python mission script analysis
**Trace corpus**: 2,648,271 lines / 136MB combat session (3 players, 34 minutes), plus 4,343-line loopback session
**Clean room statement**: This document contains no decompiled code, no binary addresses, no internal memory offsets, and no handler tables. All formats are derived from observable wire data or readable Python source.

**Update 2026-02-19**: A graceful disconnect has been captured in a stock-dedi loopback trace. The transport disconnect type is **0x05** (DisconnectMessage), verified on the wire. See Section 7 for the complete verified disconnect sequence.

---

## Overview

Player disconnects in Bridge Commander follow a detect-cleanup-notify pattern:

1. **Detection**: The server detects that a peer is no longer reachable (timeout, graceful quit, or kick)
2. **Cleanup**: The server removes the player's game objects and internal state
3. **Notification**: The server sends cleanup messages to all remaining clients so they can update their UI and game world

Three cleanup opcodes are sent to remaining clients:
- **0x14** (DestroyObject) — removes the player's ship from the game world
- **0x17** (DeletePlayerUI) — removes the player from the scoreboard
- **0x18** (DeletePlayerAnim) — displays a "Player X has left" notification

---

## 1. Disconnect Detection

### 1.1 Peer Timeout (~45 seconds)

From the protocol specification in [protocol-reference.md](../protocol/protocol-reference.md):

> Clients that stop sending packets are disconnected after approximately 45 seconds.

The server tracks when each peer last sent data. If no packets (including keepalives) are received within the timeout window, the server considers the peer lost and initiates disconnect cleanup. This is the most common disconnect path — it covers network failures, client crashes, and ungraceful process termination.

Keepalive messages (transport type 0x00) are exchanged approximately every 12 seconds, providing 3-4 missed keepalive cycles before the timeout fires.

### 1.2 Graceful Disconnect (Transport Message 0x05)

The transport layer includes a dedicated Disconnect message type (0x05, as documented in [transport-layer.md](../protocol/transport-layer.md)). When a client cleanly exits (menu quit, ALT+F4 with clean shutdown), it sends a transport-level 0x05 DisconnectMessage. This allows the server to immediately begin cleanup rather than waiting for the timeout.

**Wire format** (verified from captured disconnect, 2026-02-19):
```
[0x05][payload: 9 bytes]
```

The disconnect message is multiplexed with other transport messages (typically stale ACKs from the ACK-outbox bug) in a single UDP packet. See Section 7 for the full captured packet.

The graceful disconnect reaches the same cleanup path as the timeout, but without the ~45-second delay.

### 1.3 Boot/Kick (Host-Initiated)

The server can forcibly disconnect a player. This is triggered by:
- **Anti-cheat**: Subsystem hash mismatch (the hash field in StateUpdate position data, see [phase1-verified-protocol.md Section 8](../protocol/protocol-reference.md#8-stateupdate-deep-dive-opcode-0x1c))
- **Host action**: Manual kick from the lobby UI

The server sends a boot message to the target peer, then performs the same cleanup as the other disconnect paths.

---

## 2. Cleanup Messages to Remaining Clients

After detecting a disconnect, the server sends three messages (all using reliable delivery) to every remaining client.

### 2.1 DestroyObject (Opcode 0x14)

Removes the disconnected player's ship from the game world.

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x14
1       4     i32     object_id     (the disconnected player's ship network ID)
```

The client receiving this message removes the specified object from its game world. If the object is a ship, it is marked as dead and removed from rendering.

**Trace note**: Opcode 0x14 was observed once in the combat trace, but for a combat-related ship destruction, not a disconnect. No disconnect-triggered 0x14 was captured.

### 2.2 DeletePlayerUI (Opcode 0x17)

Removes the disconnected player from the client's scoreboard and player list.

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x17
1       var   stream  connection_id + cleanup_data
```

The client receiving this message removes the specified player's entry from all UI elements (scoreboard, player list, team roster).

**Trace note**: Opcode 0x17 was observed 6 times in the combat trace and 1 time in the loopback trace, but **all instances occurred at join time**, not at disconnect. This suggests the opcode serves double duty — removing stale player slot entries when a slot is being reused by a new player joining.

### Hex Dump: DeletePlayerUI at Join Time (Packet #20)

```
Server -> Client (Peer#1), embedded in 39-byte packet:
  [msg 2] Reliable seq=2304: opcode 0x17 (DeletePlayerUI)
  0000: 17 66 08 00 00 F1 00 80 00 00 00 00 00 91 07 00  |.f..............|
  0010: 00 02                                              |..|

  17 bytes of player UI cleanup data
  Sent immediately after MISSION_INIT (0x35) in the same packet
```

This is the only wire-observed instance of opcode 0x17. It appears at join time, paired with the MISSION_INIT message. The same format would be sent at disconnect time to remove the departing player.

### 2.3 DeletePlayerAnim (Opcode 0x18)

Displays a floating "Player X has left" text notification on all remaining clients.

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x18
1       var   stream  player_name + animation_data
```

The client receiving this message creates a temporary floating text element displaying the player's name with a departure message. Based on the mission script resource files, the text template is loaded from the `Multiplayer.tgl` resource file under the key `"Delete_Player"`, and the animation displays for approximately 5 seconds.

**Trace note**: 0 instances of opcode 0x18 were observed in either trace, consistent with no player disconnects occurring.

---

## 3. Python Layer Cleanup

All four multiplayer mission scripts (`Mission1.py` through `Mission5.py`, excluding `Mission4.py` which doesn't exist) contain identical `DeletePlayerHandler` functions. These are readable Python source files in `scripts/Multiplayer/Episode/`:

```python
def DeletePlayerHandler(TGObject, pEvent):
    import Mission1Menus

    # We only handle this event if we're still connected.
    # If we've been disconnected, then we don't handle this
    # event since we want to preserve the score list to
    # display as the end game dialog.
    pNetwork = App.g_kUtopiaModule.GetNetwork()
    if (pNetwork):
        if (pNetwork.GetConnectStatus() == App.TGNETWORK_CONNECTED
            or pNetwork.GetConnectStatus() == App.TGNETWORK_CONNECT_IN_PROGRESS):
            # We do not remove the player from the dictionary.
            # This way, if the player rejoins, his score will
            # be preserved.

            # Rebuild the player list since a player was removed.
            Mission1Menus.RebuildPlayerList()
    return
```

### Design Decisions Visible in the Code

1. **Score preservation**: The handler intentionally does NOT remove the disconnected player from score dictionaries. The original developer comments explain this is so "if the player rejoins, his score will be preserved." A reimplementation should preserve scores on disconnect to maintain behavioral compatibility.

2. **Connection guard**: The handler only runs if the network is still connected. During game-end scenarios where the network is being torn down, it skips cleanup to preserve the final scoreboard for the end-game dialog. This prevents a race condition where the score display is corrupted during shutdown.

3. **Minimal cleanup**: Only `RebuildPlayerList()` is called. The Python layer does not need to remove game objects (the engine handles that via opcodes 0x14/0x17/0x18 before the Python handler fires).

---

## 4. Complete Disconnect Sequence

Reconstructed from protocol structure, script behavior, and transport layer observations:

### 4.1 Timeout Disconnect (Most Common)

```
Time 0s:     Player stops sending packets (network failure, crash, etc.)
             Server continues sending StateUpdates and keepalives

Time ~12s:   First missed keepalive cycle
Time ~24s:   Second missed keepalive cycle
Time ~36s:   Third missed keepalive cycle

Time ~45s:   Server detects timeout
             Server begins cleanup:

             1. Sends opcode 0x14 (DestroyObject) to all remaining clients
                - Removes the disconnected player's ship from the game world
                - Reliable delivery, requires ACK from each client

             2. Sends opcode 0x17 (DeletePlayerUI) to all remaining clients
                - Removes the player from the scoreboard and player list
                - Reliable delivery

             3. Sends opcode 0x18 (DeletePlayerAnim) to all remaining clients
                - Creates "Player X has left" floating text notification
                - Reliable delivery

             4. Python DeletePlayerHandler fires on remaining clients:
                - Calls RebuildPlayerList() to update the UI
                - Score dictionaries preserved (player can rejoin with same score)

             5. Server removes player from its internal state
                - Frees the player slot (available for new connections)
                - Cleans up network peer entry
```

### 4.2 Graceful Disconnect

Same as timeout, but the transport 0x05 DisconnectMessage triggers cleanup immediately instead of waiting ~45 seconds. See Section 7 for the verified wire sequence.

### 4.3 Boot/Kick

Same as timeout, but triggered by the host sending a boot message to the target peer. The target peer also receives notification that it was kicked.

---

## 5. Implementation Notes

### 5.1 Cleanup Opcode Ordering

The three cleanup opcodes (0x14, 0x17, 0x18) should be sent in order:
1. **First** destroy the ship (0x14) — so the game world is updated
2. **Then** remove from UI (0x17) — so the scoreboard reflects the change
3. **Last** show notification (0x18) — cosmetic feedback to remaining players

All three use reliable delivery, so each must be ACK'd by every remaining client.

### 5.2 Score Persistence on Rejoin

The original Python scripts explicitly preserve score dictionaries when a player disconnects. This means:
- When player "Alice" disconnects with 3 kills, her score entry stays in the dictionary
- When "Alice" reconnects, the `SCORE_MESSAGE` (0x37) sent to her will include her preserved score
- Other players will see her score restored on the scoreboard

A reimplementation should preserve this behavior. Do NOT clear scores on disconnect.

### 5.3 DeletePlayerUI at Join Time

Opcode 0x17 is sent at both join and disconnect times. At join time, it appears to clear any stale UI entries from the player slot being reused. An implementation should handle 0x17 gracefully regardless of whether the target player is currently displayed.

### 5.4 Timeout Value

The ~45-second timeout is observed from behavioral descriptions and is consistent with standard game networking practice (3-4 missed keepalive cycles at ~12-second intervals). The exact value may be configurable. An implementation should use a similar timeout to maintain compatibility with stock clients.

---

## 6. What's NOT on the Wire

Some aspects of the disconnect flow are handled entirely within the engine and leave no wire-observable evidence:

- **Peer array management**: The server's internal tracking of connected peers is invisible on the wire
- **Event dispatch**: The internal event system that triggers cleanup handlers is engine-internal
- **Memory cleanup**: Freeing per-player allocations, network buffers, etc.
- **GameSpy update**: The server updates its `numplayers` and `player_N` fields in the GameSpy response (observable via `\status\` queries, but no trace captures this)

---

## 7. Verified Graceful Disconnect Sequence (2026-02-19)

A graceful disconnect was captured in a stock dedicated server loopback trace on 2026-02-19. Session duration: ~91 seconds (connect at 11:37:53, disconnect at 11:39:24).

### 7.1 Disconnect Packet (Client → Server)

```
UDP payload (20 bytes, decrypted):
  0000: 02 03 05 0A C0 02 00 02 0A 0A 0A EF 01 27 00 00  |.............'..|
  0010: 01 28 00 00                                      |.(..|

Decoded:
  peer_id = 0x02 (client)
  msg_count = 3

  [msg 0] type=0x05 DisconnectMessage, payload=9 bytes
  [msg 1] type=0x01 ACK seq=39 (stale, from ACK-outbox accumulation bug)
  [msg 2] type=0x01 ACK seq=40 (stale)
```

**Key observations**:
- Transport type is **0x05**, not 0x06 (corrected from earlier documentation)
- The disconnect is multiplexed with stale ACK entries in a single UDP packet
- The disconnect payload is 9 bytes: `[0A C0 02 00 02 0A 0A 0A EF]` (content meaning TBD)

### 7.2 Server Response

The server responds with an ACK for the disconnect message:

```
UDP payload (6 bytes):
  01 01 01 02 00 02

Decoded:
  peer_id = 0x01 (server)
  msg_count = 1
  [msg 0] type=0x01 ACK seq=2 (low-type, for the disconnect message)
```

The server retransmits this ACK **7 times** at ~0.67-second intervals over 4 seconds. This is the ACK-outbox accumulation bug — the server's ACK for the disconnect is never removed from its outbox, so it retransmits until the peer cleanup removes the peer entry entirely.

### 7.3 GameSpy Notification

After ACK retransmission stops:
```
\heartbeat\0\gamename\bcommander\statechanged\1
```

Sent to the master server at 81.205.81.173:27900. The `statechanged=1` field signals that the server's player count has changed.

### 7.4 Complete Timeline

```
11:39:21.416  Last game data from server (PythonEvent seq=39, seq=40)
11:39:21.419  Client ACKs for seq=39, seq=40 (first send)
11:39:22.085  Client retransmits stale ACKs (ACK-outbox bug)
11:39:22.753  Client retransmits again
11:39:24.851  Client sends DISCONNECT (type 0x05) + 2 stale ACKs
11:39:24.854  Server ACKs disconnect (seq=2)
11:39:25.519  Server retransmits ACK (×1)
    ... 5 more retransmits at ~0.67s intervals ...
11:39:28.855  Last ACK retransmit (×7)
11:39:29.016  GameSpy heartbeat with statechanged=1
```

**Total time**: ~4.2 seconds from disconnect to GameSpy notification. The ~3.4-second gap between last game data and the disconnect message is the client's shutdown sequence.

### 7.5 Implementation Notes

- A reimplementation should handle the disconnect message arriving **multiplexed** with other transport messages in the same packet. Process all messages in the packet normally — ACKs, data messages, then the disconnect.
- The server should ACK the disconnect (it's a reliable low-type message) but does NOT need to retransmit the ACK 7 times. The stock behavior of endless retransmission is the ACK-outbox bug, not intentional protocol design.
- After processing the disconnect, trigger the same cleanup path as a timeout disconnect (destroy ship, remove from scoreboard, notify remaining clients, update GameSpy).
- The `statechanged=1` GameSpy heartbeat should be sent after cleanup completes.

---

## Summary

| Property | Value |
|----------|-------|
| Disconnect detection | Timeout (~45s), graceful (transport 0x05), or kick |
| Cleanup messages | 0x14 (DestroyObject) + 0x17 (DeletePlayerUI) + 0x18 (DeletePlayerAnim) |
| Cleanup delivery | All reliable (ACK required from each remaining client) |
| Python behavior | RebuildPlayerList() only; scores preserved for rejoin |
| Score persistence | By design — scores survive disconnect/reconnect |
| DeletePlayerUI dual use | Sent at both join time (clear stale slot) and disconnect time |
| Trace evidence | **Graceful disconnect captured** (2026-02-19, stock-dedi loopback, transport 0x05) |
| Primary source | Wire trace verification + readable Python mission scripts |
