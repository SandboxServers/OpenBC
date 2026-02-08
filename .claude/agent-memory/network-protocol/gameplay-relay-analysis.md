# Gameplay Network Relay Analysis

## Executive Summary

BC multiplayer is a **star-topology peer-to-peer relay** model. The host acts as a message
router, not a simulation server. During gameplay, TWO distinct data channels operate
simultaneously:

1. **C++ engine object state**: Automatic serialization/deserialization of game objects
   (ships, torpedoes, etc.) handled entirely by the C++ engine layer
2. **Python script messages**: Explicit `SendTGMessage` / `SendTGMessageToGroup` calls
   from mission scripts for game logic (scores, chat, game-end, mission init)

The host does NOT run authoritative game simulation. Each client simulates its own ship
locally and the engine broadcasts state updates. The host relays messages and manages
connections, but clients are trusted with their own physics.

---

## 1. Message Relay Mechanics

### TGNetwork::Update (FUN_006b4560) Host Branch

When the host runs Update (state == 2, `isHost` flag at WSN+0x10E):

```
Host Update loop:
  1. For each connected peer (param_1[0xc] peers at param_1[0xb]):
     - Skip self (peer_id == param_1[6])
     - Skip disconnecting peers (peer+0xBC != 0)
     - If time since last keepalive > threshold (DAT_0088bd58):
       -> Create keepalive message (FUN_006bdc40)
       -> Send via FUN_006b4c10(this, peer_id, msg, 0)
  2. FUN_006b55b0 - SendOutgoingPackets (serialize queues to UDP)
  3. FUN_006b5c90 - ProcessIncomingPackets (recv from socket)
  4. FUN_006b5f70 - DispatchIncomingQueue (validate, deliver)
  5. Dequeue incoming messages via FUN_006b52b0
     -> For each: create ET_NETWORK_MESSAGE_EVENT (0x60001)
     -> Post to EventManager (DAT_0097f838)
```

### FUN_006b5f70 - DispatchIncomingQueue (The Message Router)

This is the core dispatch function. After receiving and validating a message, it
reads the message TYPE via virtual call `(**(code **)*msg)()` and switches:

```
switch (message_type):
  case 0: FUN_006b63a0 -- Connection message (new peer joining)
  case 1: FUN_006b64d0 -- ACK/keepalive message
  case 3: FUN_006b6640 -- DATA message (game content)
  case 4: FUN_006b6a70 -- Disconnect message
  case 5: FUN_006b6a20 -- Peer timeout/removal
```

### FUN_006b6640 - Data Message Handler (Type 3) - THE KEY FUNCTION

This is the critical function for gameplay relay. It has two completely different
code paths based on `isHost` (WSN+0x10E):

#### Client path (isHost == 0, line 3915):
```c
if (*(char *)((int)this + 0x10e) == '\0') {
    // NOT host - this is a client receiving from host
    // Set connection state to CONNECTED (0x14 = 2)
    *(undefined4 *)((int)this + 0x14) = 2;
    // Extract sender ID from message data
    cVar2 = *pcVar8;
    *(int *)((int)this + 0x18) = (int)cVar2;
    // Update peer tracking
    FUN_006bba10(...);
    // Create event 0x60002 (hosting start / connection established)
    puVar5[4] = 0x60002;
}
```

#### Host path (isHost != 0, line 3935):
```c
else {
    // IS host - receiving data message from a client
    piVar3 = FUN_006b8530(param_1, &local_124);  // Get message payload
    local_130 = *piVar3;                          // First int = sender info
    *(int *)(param_2 + 0x20) = piVar3[1];        // Second int = data

    // Validate sender via virtual call
    cVar2 = (**(code **)(*this + 0x5c))(local_130);

    if (cVar2 == '\x01') {
        // REJECTED - send rejection (type 5) back to sender
        piVar3[0x10] = 5;
        FUN_006b4c10(this, sender_peer_id, piVar3, ...);
        return;
    }

    // Check blocked list (this + 0xE8, this + 0xE0)
    // If sender is on block list, send rejection (type 2)

    // ACCEPTED - Assign peer ID
    iVar6 = FUN_006b7540((int)this);  // Allocate new peer slot

    // Send acceptance message (type FUN_006be730) to sender
    FUN_006b4c10(this, sender_peer_id, acceptance_msg, 0);

    // === THIS IS THE RELAY BROADCAST ===
    // Iterate ALL connected peers
    piVar3 = *(int **)((int)this + 0x2c);   // peer array
    iVar1 = *(int *)((int)this + 0x30);     // peer count
    if (0 < peer_count) {
        do {
            iVar6 = *piVar3;
            // Skip the SENDER peer (don't echo back)
            if (*(int *)(iVar6 + 0x18) != sender_peer_id &&
                *(char *)(iVar6 + 0xbc) == '\0') {  // not disconnecting

                // Build info message about the new peer
                // Copy peer's address/name data
                uStack_10c = *(peer + 0x18);      // peer ID
                uStack_10b = *(peer + 0x1c);       // IP address
                // Copy additional peer data (name, etc.)
                FUN_006b84d0(msg, &data, peer->nameLen * 2 + 7);

                // Send to this peer
                FUN_006b4c10(this, this_peer_id, msg, 0);
            }
            piVar3++;
        } while (--count != 0);
    }

    // Fire ET_NETWORK_NEW_PLAYER event (0x60004) with assigned peer ID
    puVar5[4] = 0x60004;
    puVar5[10] = iVar6;  // assigned peer ID
}
```

**CRITICAL FINDING**: FUN_006b6640 is only for CONNECTION data messages (type 3),
not for gameplay state relay. This function handles a client JOINING - the host
broadcasts the new peer's info to all existing peers. It is NOT called during
normal gameplay data flow.

### FUN_006b51e0 - The Real Relay Function

This is called from FUN_006b63a0 (connection message handler) and FUN_006b6a20
(disconnect handler) when `isHost != 0`:

```c
int FUN_006b51e0(void *this, int *param_1) {
    if (*(int *)((int)this + 0x14) == 2) {  // must be host
        // Iterate ALL peers
        for each peer in peer_array:
            // Skip the HOST's own peer ID (this + 0x20)
            if (peer_id != self_id && !disconnecting):
                // Clone the message
                piVar2 = (**(code **)(*param_1 + 0x18))();  // message.Clone()
                // Enqueue to this peer's send queue
                FUN_006b5080(this, piVar2, peer);
        return result;
    }
    return 4;  // not host
}
```

**This is the automatic relay.** When the host receives a connection-level message,
it clones and forwards to all other peers. BUT this only applies to TGNetwork
internal messages (connect, disconnect, name change), NOT to game data.

---

## 2. SendTGMessage vs SendTGMessageToGroup

### SendTGMessage (FUN_006b4c10)

```python
pNetwork.SendTGMessage(iToID, pMessage)
```

Maps to `FUN_006b4c10(WSN, peer_id, message, 0)`:

- **param_1 == 0**: BROADCAST to ALL peers
  - Iterates peer array, clones message for each
  - Sets `msg[3] = self_id` (sender stamp)
  - Skips disconnecting peers (peer+0xBC == 1)
  - Last peer gets the original message (optimization)

- **param_1 == specific_id**: UNICAST to one peer
  - Binary search peer array for matching peer_id at peer+0x18
  - Enqueue via FUN_006b5080

- **param_1 == -1**: Send to connection peer by address (param_3 used)

### SendTGMessageToGroup (FUN_006b4de0)

```python
pNetwork.SendTGMessageToGroup("NoMe", pMessage)
```

Maps to `FUN_006b4de0(WSN, group_name, message)`:

- Groups are named collections of peer IDs stored at WSN+0xF4
- Binary searches the group array by name (sorted alphabetically)
- Calls FUN_006b4ec0 which iterates the group's member list
- Each member receives a clone of the message

### Groups Created During Multiplayer Setup

From MultiplayerGame constructor (line 5413), the host creates:
- **"Forward"** group: Contains forward-routed peers
- A second group (name at DAT_008e5528): Purpose unknown

From Python scripts, the key group used is:
- **"NoMe"** group: All peers EXCEPT the local player

### Usage Patterns in Scripts

| Function | Target | Pattern |
|----------|--------|---------|
| `SendTGMessage(0, msg)` | ALL peers (broadcast) | EndGame, RestartGame |
| `SendTGMessage(iToID, msg)` | One peer (unicast) | InitNetwork (send to newly joined player) |
| `SendTGMessage(hostID, msg)` | Host only | Chat from client, ship selection from client |
| `SendTGMessageToGroup("NoMe", msg)` | All except self | Score updates, chat forwarding |

---

## 3. Game Object Network Messages (C++ Automatic State Sync)

### Object Creation: FUN_0069f620 (The ReceiveMessageHandler Opcode Dispatcher)

This is the CORE gameplay message handler. When a network message event
(ET_NETWORK_MESSAGE_EVENT, 0x60001) fires, this function is called.

**Message format**:
```
byte[0]: opcode (first byte of data)
byte[1]: player_slot (which player slot sent this)
byte[2]: player_id (if param_2 is set - new player mode)
byte[3+]: serialized game object data
```

The handler calls `FUN_005a1f50(data + offset, length - offset)` which is the
**game object deserializer**. This function:
1. Creates a stream from the raw bytes
2. Reads a type identifier
3. Looks up the object factory
4. Creates the object and calls its `Unserialize()` virtual method
5. Returns a pointer to the newly created/updated object

### Host Relay of Game Object Messages

After deserialization, the host path (DAT_0097fa8a != '\0') does:

```c
// For each player slot (0x10 = 16 slots)
piVar10 = (int *)((int)this + 0x7c);  // player slot array
do {
    if ((char)piVar10[-1] != '\0') {   // slot active
        if (*piVar10 == param_1[3]) {  // this is the SENDER
            if (param_2 != '\0') {
                piVar10[1] = piVar4[1];  // update sender's object ref
            }
        }
        else if (*piVar10 != *(int *)((int)pvVar6 + 0x20)) {
            // NOT sender AND not self
            // Clone the original message
            piVar5 = (**(code **)(*param_1 + 0x18))();
            // Send to this player via TGNetwork
            FUN_006b4c10(pvVar6, *piVar10, piVar5, 0);
        }
    }
    piVar10 += 6;  // next slot (0x18 bytes per slot)
} while (--iVar9 != 0);
```

**THIS IS THE KEY RELAY MECHANISM FOR GAMEPLAY DATA.** The host:
1. Receives a game object update from Client A
2. Deserializes it locally (creates/updates the object on the host)
3. Iterates ALL 16 player slots
4. For each ACTIVE slot that is NOT the sender and NOT self:
   - Clones the original network message
   - Sends it via `FUN_006b4c10` (TGNetwork::Send)

**The host relays the RAW message bytes, not a re-serialized version.** It literally
clones and forwards the original packet payload to all other peers.

### Object State Updates: FUN_0069f930 (Position/State Handler)

This handles INCOMING state updates (NOT on the host, only on receiving clients):

```c
// Only process if sender != self
if (param_1[3] != *(int *)((int)DAT_0097fa78 + 0x18)) {
    // If host, relay to "Forward" group first
    if (DAT_0097fa8a != '\0') {
        // Forward group relay via FUN_006b4ec0
        FUN_006b4ec0(pvVar8, forward_group, message_clone);
    }

    // Then deserialize: read position, rotation, velocity from stream
    uVar5 = FUN_006cf6a0(stream);    // object type
    uVar6 = FUN_006cf540(stream);    // flags byte 1
    uVar7 = FUN_006cf540(stream);    // flags byte 2
    FUN_006d2eb0(stream, &pos, &rot, &vel);  // position/rotation/velocity

    // Find the existing object and update it
    pvVar8 = FUN_006f0ee0(uVar5);    // lookup by type
    if (pvVar8 != NULL) {
        FUN_0057d110(pvVar8, flags1, flags2, flags3, damage, ...);
    }
}
```

### Serialization Format (from FUN_005a2060 and related)

Game objects serialize their state as:
- **Object type ID** (4 bytes)
- **Object factory type** (4 bytes for class identification)
- **Position**: 3x float (x, y, z) = 12 bytes
- **Rotation**: 4x float (quaternion w, x, y, z) = 16 bytes
- **Velocity**: 3x float = 12 bytes (if position changed)
- **Ship-specific state**: shields, hull, subsystem status, etc.
- **Flags byte**: bit-packed (alive/dead, cloaked, etc.)

Approximate per-object size: ~60-100 bytes depending on state changes.

---

## 4. Bandwidth During Gameplay

### Update Rate

From TGNetwork::Update host loop (line 2189-2222):

```c
if (_DAT_0088bd58 < DAT_0099c6bc - *(float *)(peer + 0x30)) {
    // Send keepalive/state if time threshold exceeded
}
```

`_DAT_0088bd58` is stored at a fixed data address -- this is the **keepalive
interval**. It appears to be on the order of 0.5-1.0 seconds based on the float
comparison pattern.

For game objects, the update rate is driven by the game loop timer (~30fps typical
for BC), but network sends are throttled by the peer's last-send timestamp.

### Packet Structure

Each UDP packet can contain up to **255 messages** (count byte at offset 1).
Max packet size is 0x400 (1024) bytes by default.

### Bandwidth Estimate

For a 6-player game (BC's typical max):
- 5 player ships x ~80 bytes state = ~400 bytes per update
- At ~10-20 updates/sec = ~4-8 KB/s per direction
- Plus reliable messages (scores, chat, events) = ~1 KB/s
- Total per client: ~5-10 KB/s

For a 16-player game (BC's hard max):
- 15 ships x ~80 bytes = ~1200 bytes per update
- At ~10-20 updates/sec = ~12-24 KB/s per direction
- Host upstream: 15 x 12-24 = ~180-360 KB/s (significant)

### Reliable vs Unreliable

The transport layer has 3 queues per peer:
- **Unreliable** (peer+0x64): Position/state updates (latest wins)
- **Reliable** (peer+0x80): Script messages, events
- **Priority reliable** (peer+0x9C): Checksums, critical handshake

Position updates use UNRELIABLE delivery -- if a packet is lost, the next update
supersedes it. Script messages (score changes, game end, chat) use GUARANTEED
(reliable) delivery via `pMessage.SetGuaranteed(1)`.

---

## 5. The Broadcast/Relay Pattern

### How It Actually Works (Summary)

```
Client A (ship owner)                    Host                    Client B
     |                                     |                        |
     | -- Ship state update (unreliable) -> |                        |
     |                                     | -- Deserialize object   |
     |                                     | -- Clone raw message    |
     |                                     | -- Forward to B ------> |
     |                                     |                        |
     | -- Score msg (reliable) ----------> |                        |
     |                                     | (Python handles)        |
     |                                     | -- Forward to B ------> |
     |                                     |                        |
```

The host performs relay at TWO levels:

1. **C++ engine level** (FUN_0069f620): After receiving and deserializing a game
   object update, the host iterates all player slots and forwards the raw message
   to all other active players. This is AUTOMATIC and happens in the engine's
   ReceiveMessageHandler.

2. **Python script level**: Mission scripts explicitly call `SendTGMessage(0, msg)`
   or `SendTGMessageToGroup("NoMe", msg)` to broadcast game events. The host script
   receives, processes, and re-broadcasts. Chat is the canonical example:
   - Client sends chat to host
   - Host's Python ProcessMessageHandler receives it
   - Host calls `SendTGMessageToGroup("NoMe", pMessage.Copy())` to forward

### The Host Does NOT Re-broadcast at the TGNetwork Level

`FUN_006b51e0` (the network-level relay) is ONLY called for connection-management
messages (type 0 connect, type 4 disconnect). Regular game data messages (type 3)
are NOT automatically relayed by TGNetwork.

Instead, the relay happens in the MultiplayerGame C++ layer (FUN_0069f620) which
explicitly iterates player slots and calls `FUN_006b4c10` for each.

---

## 6. Opcodes During Gameplay

### MultiplayerGame ReceiveMessageHandler Opcode Dispatch

The ReceiveMessageHandler is registered for event 0x60001 (ET_NETWORK_MESSAGE_EVENT).
The first byte of the message payload determines the opcode:

#### Connection/Lobby Phase Opcodes (0x00-0x0F range)

These are handled by the MultiplayerGame C++ dispatcher at 0x0069f2a0:

| Opcode | Function | Purpose | Phase |
|--------|----------|---------|-------|
| 0x00 | Settings packet | Game settings + map name + checksum flag | Lobby |
| 0x01 | Ready signal | Player ready status | Lobby |

#### NetFile/Checksum Opcodes (0x20-0x28 range)

Handled by NetFile dispatcher (FUN_006a3cd0):

| Opcode | Purpose | Phase |
|--------|---------|-------|
| 0x20 | Checksum request | Connection |
| 0x21 | Checksum response | Connection |
| 0x22 | Checksum fail (file) | Connection |
| 0x23 | Checksum fail (ref) | Connection |
| 0x25 | File transfer | Connection |
| 0x27 | Unknown | Connection |
| 0x28 | Transfer complete | Connection |

#### Game Object Messages (engine-level, no explicit opcode byte)

These are NOT dispatched by opcode. Instead, they flow through a completely
different path:

1. TGNetwork receives a data message (type 3)
2. FUN_006b5f70 calls FUN_006b6640 for type 3 messages
3. This calls FUN_006b63a0 which fires event 0x60007 (name change) or
   FUN_006b6640 which fires 0x60004 (new player)
4. OR: the host Update loop at line 2228-2243 dequeues via FUN_006b52b0
   and fires 0x60001 events

The game object data is raw serialized bytes -- the first few bytes identify the
object type and the rest is the serialized state. There is NO opcode byte for
these messages; the deserialization function (FUN_005a1f50) reads the type ID
from the data stream itself.

#### Python Script-Level Message Types

These are defined in the mission scripts and use ordinal values above
`App.MAX_MESSAGE_TYPES + 10`:

| Value | Name | Phase |
|-------|------|-------|
| MAX+10 | MISSION_INIT_MESSAGE | Game start |
| MAX+11 | SCORE_CHANGE_MESSAGE | Gameplay |
| MAX+12 | SCORE_MESSAGE | Gameplay |
| MAX+13 | END_GAME_MESSAGE | Game end |
| MAX+14 | RESTART_GAME_MESSAGE | Post-game |
| (varies) | CHAT_MESSAGE | All phases |
| (varies) | TEAM_CHAT_MESSAGE | All phases |

These are written as the FIRST BYTE of the TGMessage payload by the Python
scripts, then read back in ProcessMessageHandler.

---

## 7. Implications for OpenBC Dedicated Server

### What the Server Must Do During Gameplay

1. **Relay game object state**: When a client sends a ship state update, the
   server must deserialize it (to validate and track state) and then forward
   the raw bytes to all other connected clients.

2. **Process Python script messages**: The server must run the mission Python
   scripts which handle scoring, game end conditions, chat forwarding, etc.

3. **Maintain the player slot table**: 16 slots at MultiplayerGame+0x74, each
   0x18 bytes. The server uses this to determine which peers to relay to.

4. **Handle keepalives**: Send periodic keepalive messages to detect disconnected
   clients (timeout detection at peer+0x2C / peer+0x30 timestamps).

5. **NOT run physics simulation**: The original BC does NOT run server-authoritative
   physics. Each client simulates its own ship and broadcasts state. The server
   just relays. (This means clients can cheat trivially, but that's the original
   design.)

### Minimum Required SWIG API for Gameplay

Beyond the ~297 lobby functions, gameplay requires:
- `App.TGMessage_Create()`, `App.TGMessage.SetGuaranteed()`, `App.TGMessage.Copy()`
- `App.TGMessage.SetDataFromStream()`, `App.TGMessage.GetBufferStream()`
- `App.TGBufferStream()` with ReadChar/WriteChar/ReadInt/WriteInt/ReadLong/WriteLong/ReadShort/WriteShort/Read/Write
- `App.g_kUtopiaModule.GetNetwork()`, `.IsHost()`, `.IsClient()`
- `App.g_kUtopiaModule.GetGameTime()`, `.GetRealTime()`
- `pNetwork.SendTGMessage(id, msg)`, `pNetwork.SendTGMessageToGroup(name, msg)`
- `pNetwork.GetHostID()`, `.GetLocalID()`, `.GetPlayerList()`
- `pPlayerList.GetPlayer(id)`, `.GetPlayerAtIndex(i)`, `.GetNumPlayers()`
- `pPlayer.GetNetID()`, `.GetName()`
- `App.MultiplayerGame_Cast()`, `.GetShipFromPlayerID()`, `.SetReadyForNewPlayers()`
- `App.ShipClass_Cast()`, `.GetName()`, `.IsPlayerShip()`, `.GetNetPlayerID()`, `.GetNetType()`
- `App.g_kEventManager.AddBroadcastPythonFuncHandler()`
- `App.IsNull()`
- Object serialization: `FUN_005a1f50` and related virtual serialize/unserialize methods

### Relay Architecture Decision

For the dedicated server, we have two options:

**Option A: Byte-perfect relay (recommended for Phase 1)**
- Receive raw UDP data messages from clients
- Parse just enough to identify sender and type
- Forward raw bytes to all other connected peers
- No deserialization needed for relay (faster, less code)
- Python scripts still need TGMessage/TGBufferStream for script-level messages

**Option B: Full deserialization relay**
- Deserialize every game object message
- Track full game state on server
- Re-serialize and send
- Required for server-authoritative validation (Phase 2+ / OpenBC protocol)
- Much more code, but enables anti-cheat

**Phase 1 should use Option A** -- byte-perfect relay with minimal parsing.
Script messages still need full processing for scoring/game-end logic.

---

## 8. Event Flow Summary During Active Gameplay

```
1. Client A's game loop:
   - Physics updates ship position
   - Engine serializes ShipClass state
   - TGNetwork queues to unreliable send queue for host

2. Host's TGNetwork::Update:
   - FUN_006b5c90: Receives UDP packet from Client A
   - FUN_006b5f70: Validates sequence, dispatches by type
   - For type 3 (data): FUN_006b6640 handles
   - Type 3 on host: processes connection-level data
   - Dequeue loop (FUN_006b52b0): fires ET_NETWORK_MESSAGE_EVENT

3. Host's EventManager:
   - Dispatches 0x60001 to MultiplayerGame::ReceiveMessageHandler
   - Handler (0x0069f620) deserializes game object
   - HOST PATH: iterates player slots, forwards raw message to all others
   - Handler also posts ET_OBJECT_CREATED_NOTIFY if new object

4. Host's Python layer:
   - ProcessMessageHandler receives script-level messages
   - Processes game logic (scoring, chat, game-end)
   - Explicitly sends responses via SendTGMessage/SendTGMessageToGroup

5. Client B receives:
   - TGNetwork receives forwarded message from host
   - Same dispatch chain: deserializes, updates local object state
   - Python ProcessMessageHandler handles script messages
```
