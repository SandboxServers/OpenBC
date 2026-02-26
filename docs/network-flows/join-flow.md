# Connection & Join Flow

This document describes the complete lifecycle of a client connecting to a Bridge Commander multiplayer server, from the initial UDP packet through active gameplay.

**Clean room statement**: All wire formats and timing data are derived from packet captures of stock BC clients and servers. No binary addresses, memory offsets, or decompiled code are referenced.

---

## 1. Peer State Machine

Each client progresses through a fixed sequence of states:

```
EMPTY → CONNECTING → CHECKSUMMING → CHECKSUMMING_FINAL → LOBBY → IN_GAME
                                                                      │
                                                                      ▼
                                                                  (disconnect)
```

| State | Description | Duration |
|-------|-------------|----------|
| EMPTY | Slot is available | -- |
| CONNECTING | Connect packet received, slot assigned | ~4ms |
| CHECKSUMMING | Running 4-round checksum validation (rounds 0-3) | ~60ms |
| CHECKSUMMING_FINAL | Waiting for final checksum round (0xFF) | ~30ms |
| LOBBY | In lobby, waiting for ship selection | until player acts |
| IN_GAME | Ship selected, actively playing | until disconnect |

---

## 2. Connection Handshake

### Step 1: Client Connect

The client sends a Connect packet (transport type 0x03) to the server's game port:

```
[dir=0xFF][count=1][0x03][totalLen=0x0F][0xC0][0x00][0x00][ip:4][port:2][pad:4]
```

The direction byte `0xFF` identifies this as an initial handshake (no assigned slot yet).

### Step 2: Server ConnectAck + First Checksum

The server responds with a multi-message packet containing:
1. **ACK** for the connect (transport type 0x01)
2. **Connect response** (transport type 0x03): `[0x03][0x06][0xC0][0x00][0x00][slot]`
3. **ConnectAck** (transport type 0x05): `[0x05][0x0A][0xC0][status][0x00][slot][ip:4]`
   - `status = 0x02` = accept connection
   - `slot` = 1-based player index
4. **First ChecksumReq** (game opcode 0x20, round 0)

These are typically bundled into a single UDP packet with 3-4 transport messages.

### Step 3: Peer Transitions to CONNECTING → CHECKSUMMING

The server assigns a slot, creates a peer entry, and immediately begins the checksum exchange.

---

## 3. Checksum Exchange

The server validates that the client has matching game files by exchanging file hashes across 5 rounds. See [checksum-handshake-protocol/](../wire-formats/checksum-handshake-protocol/) for full hash algorithm details.

### Round Definitions

| Round | Index | Directory | Filter | Recursive |
|-------|-------|-----------|--------|-----------|
| 0 | `0x00` | `scripts/` | `App.pyc` | No |
| 1 | `0x01` | `scripts/` | `Autoexec.pyc` | No |
| 2 | `0x02` | `scripts/ships` | `*.pyc` | Yes |
| 3 | `0x03` | `scripts/mainmenu` | `*.pyc` | No |
| Final | `0xFF` | `Scripts/Multiplayer` | `*.pyc` | Yes |

Note: Round 0xFF uses a capital `S` in `Scripts/` (differs from rounds 0-3). The `scripts/Custom/` directory is exempt from checksums, allowing server-side mods.

### Checksum Request Wire Format

```
[0x20][index:u8][dirLen:u16le][directory:bytes][filterLen:u16le][filter:bytes][recursive:bit]
```

The `recursive` field is a bit-packed boolean (0x20 = false, 0x21 = true).

### Checksum Response Wire Format

```
[0x21][index:u8][ref_hash:u32][dir_hash:u32][file_tree...]

File tree:
  [file_count:u16le][{name_hash:u32, content_hash:u32}...]
  If recursive: [subdir_count:u16le][{name_hash:u32, file_tree...}...]
```

### Checksum Complete Signal

After all 5 rounds pass validation, the server sends opcode `0x28` (no game payload) to signal that checksums are complete. This is a transport-level signal, not a game command.

### Validation Failure

If any round fails, the server sends a BootPlayer (opcode 0x04) with reason code 4 (checksum validation failed) and disconnects the peer.

---

## 4. Post-Checksum Delivery

After checksums complete, the server sends a burst of configuration messages to the client. These are typically bundled into one or two UDP packets.

### Settings (Opcode 0x00)

```
[0x00][gameTime:f32le][collision:bit][friendlyFire:bit][playerSlot:u8][mapLen:u16le][mapName:bytes][checksumFlag:bit]
```

- `gameTime`: Current game clock in seconds
- `collision/friendlyFire`: Bit-packed booleans (see verified-protocol.md Section 7)
- `playerSlot`: 0-based player index
- `mapName`: Mission script path (e.g., `Multiplayer.Episode.Mission1.Mission1`)
- `checksumFlag`: If set, checksum correction data follows (rarely used)

### GameInit (Opcode 0x01)

Single byte, no payload. Triggers the client to transition to the ship selection screen.

### Delivery Order — MUST BE BUNDLED

The server sends three messages in a **single UDP datagram** after checksums:

```
[0x28 ChecksumComplete] → [0x00 Settings] → [0x01 GameInit]
```

**This bundling is a hard protocol requirement.** Verified 3/3 player joins in a 33.5-minute stock trace — every join shows identical bundling. Each message has its own reliable transport header and sequence number, but all three share one UDP packet.

**Why bundling matters**:
- If sent as separate packets, any one could be lost or reordered
- Client receiving GameInit (0x01) before Settings (0x00) would break initialization
- Client may time out waiting for the next message in the sequence
- Bundling ensures atomicity: the client either receives all three or none

Peer transitions from CHECKSUMMING_FINAL → LOBBY.

---

## 5. Player Join

### Client NewPlayerInGame (Opcode 0x2A)

After receiving GameInit, the client sends back a NewPlayerInGame message:

```
[0x2A][bitpacked boolean: 0x20 (false)]
```

This signals the server that the client is ready to participate.

### Server Response

The server responds with:

1. **MISSION_INIT (0x35)**: Game configuration
   ```
   [0x35][current_player_count:u8][system_index:u8][time_limit:u8][end_time:i32 if time_limit != 0xFF][frag_limit:u8]
   ```
   The first payload byte is `current_player_count` — the number of players currently
   connected (dynamic, updates with each join), NOT a fixed player limit. Stock servers
   send 0x01 for 2-player sessions and 0x03 for 3-player sessions.

2. **DeletePlayerUI (0x17)**: Adds the joining player to the engine's internal player list.
   This message carries a serialized event structure — see
   [delete-player-ui-wire-format.md](../wire-formats/delete-player-ui-wire-format.md)
   for the full wire format.
   ```
   [0x17][factory_class_id:u32le][event_code:u32le][src_obj_id:u32le][tgt_obj_id:u32le][wire_peer_id:u8]
   ```
   Total: 18 bytes (1 opcode + 17 payload).

   At join time, `event_code` carries a "new player" notification (`0x008000F1`).
   At disconnect time, the same opcode carries a "remove player" notification (`0x00060005`).
   The `factory_class_id` is always `0x00000866` (base event class).

   **This message is critical for the scoreboard.** The engine's internal player list
   is populated by this event. Without it, `GetPlayerList()` returns no entries and
   the scoreboard UI has no players to display, even if score data exists.

3. **Score (0x37)**: Current scores for all players (zeros for new game)
   ```
   [0x37][player_id:i32][kills:i32][deaths:i32][score:i32]
   ```
   One `0x37` message is sent per player (not batched). `player_id` is the
   network player ID (`GetNetID()` / wire slot), not an object ID.

4. **ObjCreateTeam (0x03)**: One per already-spawned ship (for late joiners)
   ```
   [0x03][owner:u8][team:u8][serialized ship data...]
   ```

### Late-Join Data

When a player joins an in-progress game, the server sends the full game state:
- MissionInit (0x35) with current player count
- DeletePlayerUI (0x17) for the joining player (adds to engine player list)
- Score message(s) (0x37), one per player, with current kills/deaths/score
- Object replication: one message per already-existing game object

This allows the late joiner to see all existing ships and scores immediately.

### Late-Join Object Replication: 0x02 vs 0x03

The server replicates existing objects to the joining player using two opcodes:

| Opcode | Name | When Used |
|--------|------|-----------|
| 0x02 | ObjCreate (non-team) | Objects without a team assignment OR non-ship objects |
| 0x03 | ObjCreateTeam | Player ships with team data |

**Decision logic** (per existing game object):

```
For each existing game object:
    if object is a ship AND has team info:
        send as 0x03 (ObjCreateTeam) — includes team_id byte
    else:
        send as 0x02 (ObjCreate) — no team_id
```

**Behavioral difference**: Opcode 0x03 updates the player slot's base object ID and triggers scene entry on the host. Opcode 0x02 does neither — it creates the object without binding it to a player slot.

**Non-team objects** include: the host's dummy ship (if applicable), environmental objects (asteroids, stations), torpedoes in flight, and AI-spawned ships in cooperative mode.

Observed in a 3-player trace: 3 instances of 0x02 (all server-owned, low object IDs) sent to the 3rd joining player during late-join replication. These represent non-player objects that exist in the game world but are not associated with a specific team.

See [objcreate-wire-format/](../wire-formats/objcreate-wire-format/) for detailed wire formats of both opcodes.

---

## 6. Ship Selection

### Client Sends ObjCreateTeam (Opcode 0x03)

When the player selects a ship, the client sends an ObjCreateTeam message containing the full ship state:

```
[0x03][owner:u8][team:u8][ship blob]

Ship blob:
  [object_id:i32][position:3×f32][quaternion:4×f32][velocity:f32]
  [nameLen:u8][playerName:bytes][classLen:u8][shipClass:bytes]
  [subsystem health array: one byte per subsystem, 0xFF = 100%]
```

### Server Processing

1. Server caches the ObjCreateTeam payload for late-join forwarding
2. Server relays the ObjCreateTeam to all other connected peers
3. Server begins sending StateUpdate (0x1C) for this ship
4. Peer transitions to IN_GAME

### Ship Change

If the player returns to ship selection and picks a different ship, the client sends a new ObjCreateTeam. The server:
1. Sends DestroyObject (0x14) for the old ship to all peers
2. Caches the new ObjCreateTeam
3. Relays the new ObjCreateTeam to all peers

---

## 7. Active Gameplay

Once in the IN_GAME state, the following message patterns are active:

### Continuous (per tick, ~10 Hz)
- **StateUpdate (0x1C)**: Position, orientation, speed (client→server, unreliable)
- **StateUpdate (0x1C)**: Subsystem health (server→clients, unreliable)

### Event-Driven (reliable)
- **StartFiring (0x07) / StopFiring (0x08)**: Weapon fire events
- **TorpedoFire (0x19)**: Torpedo launch
- **BeamFire (0x1A)**: Beam weapon hit
- **PythonEvent (0x06)**: Script events (most frequent reliable message)
- **CollisionEffect (0x15)**: Collision damage reports
- **SubsysStatus (0x0A)**: Subsystem toggles (shields, etc.)
- **Chat (0x2C/0x2D)**: Player chat messages

### Periodic
- **Keepalive**: Client sends identity data, server echoes (~1 second interval)

---

## 8. Keepalive

The client periodically sends its identity data as a keepalive. The server caches this data and echoes it back.

### Client Keepalive Payload

```
[0x00][totalLen][0x80][0x00][0x00][slot:u8][ip:4][playerName:utf16le...]
```

Transport type 0x00, with flags byte 0x80.

### Server Echo

The server stores the client's keepalive payload and re-sends it at approximately 1-second intervals. This confirms liveness to the client.

### Timeout

If the server receives no packets from a client for approximately 30 seconds, the client is considered disconnected. The server:
1. Sends DestroyObject (0x14) for the player's ship to all remaining peers
2. Sends DeletePlayerUI (0x17) to all remaining peers
3. Sends DeletePlayerAnim (0x18) with the player's name
4. Removes the peer from the slot array

---

## 9. Disconnect

Three disconnect paths exist:

### Graceful Disconnect
The client sends a Disconnect message (transport type 0x06). The server immediately cleans up.

### Timeout Disconnect
No packets received for ~30 seconds. The server removes the peer.

### Kick (BootPlayer)
The server sends BootPlayer (opcode 0x04) with a reason code, then removes the peer.

### Server Shutdown
The server sends a ConnectAck (transport type 0x05) with `status = 0x00` to each connected peer, then sends a final GameSpy heartbeat with `\final\` to all master servers.

---

## 10. Timing Summary

Observed timing from packet captures of a stock dedicated server:

| Phase | Duration | Notes |
|-------|----------|-------|
| Connect → ConnectAck | ~4ms | Server bundles with first checksum |
| Checksum exchange (5 rounds) | ~90ms | Round 2 is largest (may fragment) |
| ChecksumComplete → Settings → GameInit | ~0ms | All in same packet |
| Client receives GameInit → sends 0x2A | ~26ms | Client processing time |
| Server sends MISSION_INIT + cleanup | ~6ms | |
| **Total connect → lobby** | **~130ms** | Network + processing |
| Ship selection (human) | ~4.5 seconds | Player choosing |
| Ship creation → first StateUpdate | ~150ms | Server processing |
| **Total connect → gameplay** | **~5 seconds** | Dominated by human choice |

---

## Related Documents

- **[protocol-reference.md](../protocol/protocol-reference.md)** -- Wire format details and hex dumps
- **[checksum-handshake-protocol/](../wire-formats/checksum-handshake-protocol/)** -- Hash algorithms and round details
- **[gamespy-protocol.md](../protocol/gamespy-protocol.md)** -- Server discovery (runs before connect)
- **[disconnect-flow.md](disconnect-flow.md)** -- Detailed disconnect cleanup
