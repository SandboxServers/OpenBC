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

### TGNetwork::Update Host Branch

When the host runs Update (state == 2, isHost flag set):

```
Host Update loop:
  1. For each connected peer (skip self, skip disconnecting):
     - If time since last keepalive > threshold:
       -> Create keepalive message
       -> Send to that peer
  2. SendOutgoingPackets - serialize queues to UDP
  3. ProcessIncomingPackets - recv from socket
  4. DispatchIncomingQueue - validate, deliver
  5. Dequeue incoming messages
     -> For each: create ET_NETWORK_MESSAGE_EVENT
     -> Post to EventManager
```

### DispatchIncomingQueue (The Message Router)

This is the core dispatch function. After receiving and validating a message, it
reads the message TYPE via virtual call and switches:

```
switch (message_type):
  case 0: Connection message (new peer joining)
  case 1: ACK/keepalive message
  case 3: DATA message (game content)
  case 4: Disconnect message
  case 5: Peer timeout/removal
```

### Data Message Handler (Type 3) - THE KEY FUNCTION

This is the critical function for gameplay relay. It has two completely different
code paths based on `isHost`:

#### Client path (isHost == 0):
- Set connection state to CONNECTED (state = 2)
- Extract sender ID from message data
- Update peer tracking
- Create "hosting start / connection established" event

#### Host path (isHost != 0):
- Get message payload (sender info + data)
- Validate sender via virtual call
- If REJECTED: send rejection (type 5) back to sender, return
- Check blocked list; if sender is blocked, send rejection (type 2)
- ACCEPTED: Assign peer ID, allocate new peer slot
- Send acceptance message to sender
- **THIS IS THE RELAY BROADCAST**: Iterate ALL connected peers, skip the sender
  and disconnecting peers, build info message about the new peer, send to each

**CRITICAL FINDING**: The data message handler for type 3 is only for CONNECTION data
messages, not for gameplay state relay. This function handles a client JOINING - the host
broadcasts the new peer's info to all existing peers. It is NOT called during
normal gameplay data flow.

### The Real Relay Function (Network-Level)

Called from connection message handler and disconnect handler when `isHost != 0`:

```c
// Pseudocode
if (connState == 2) {  // must be host
    for each peer in peer_array:
        if (peer_id != self_id && !disconnecting):
            clone the message
            enqueue to this peer's send queue
    return result;
}
return 4;  // not host
```

**This is the automatic relay.** When the host receives a connection-level message,
it clones and forwards to all other peers. BUT this only applies to TGNetwork
internal messages (connect, disconnect, name change), NOT to game data.

---

## 2. SendTGMessage vs SendTGMessageToGroup

### SendTGMessage

```python
pNetwork.SendTGMessage(iToID, pMessage)
```

Maps to TGNetwork::Send:

- **param == 0**: BROADCAST to ALL peers
  - Iterates peer array, clones message for each
  - Sets sender stamp on message
  - Skips disconnecting peers
  - Last peer gets the original message (optimization)

- **param == specific_id**: UNICAST to one peer
  - Binary search peer array for matching peer_id
  - Enqueue to that peer's send queue

- **param == -1**: Send to connection peer by address

### SendTGMessageToGroup

```python
pNetwork.SendTGMessageToGroup("NoMe", pMessage)
```

Maps to TGNetwork::SendToGroup:

- Groups are named collections of peer IDs stored in the network object
- Binary searches the group array by name (sorted alphabetically)
- Iterates the group's member list
- Each member receives a clone of the message

### Groups Created During Multiplayer Setup

From MultiplayerGame constructor, the host creates:
- **"Forward"** group: Contains forward-routed peers
- A second group: Purpose unknown

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

### Object Creation: The ReceiveMessageHandler Opcode Dispatcher

This is the CORE gameplay message handler. When a network message event
(ET_NETWORK_MESSAGE_EVENT) fires, this function is called.

**Message format**:
```
byte[0]: opcode (first byte of data)
byte[1]: player_slot (which player slot sent this)
byte[2]: player_id (if param_2 is set - new player mode)
byte[3+]: serialized game object data
```

The handler calls the **game object deserializer** which:
1. Creates a stream from the raw bytes
2. Reads a type identifier
3. Looks up the object factory
4. Creates the object and calls its Unserialize() virtual method
5. Returns a pointer to the newly created/updated object

### Host Relay of Game Object Messages

After deserialization, the host path (IsMultiplayer flag set) does:

```
// For each player slot (16 slots)
for each slot in player_array:
    if slot is active:
        if this is the SENDER:
            update sender's object ref (if new player mode)
        else if not self:
            // NOT sender AND not self
            clone the original message
            send to this player via TGNetwork::Send
```

**THIS IS THE KEY RELAY MECHANISM FOR GAMEPLAY DATA.** The host:
1. Receives a game object update from Client A
2. Deserializes it locally (creates/updates the object on the host)
3. Iterates ALL 16 player slots
4. For each ACTIVE slot that is NOT the sender and NOT self:
   - Clones the original network message
   - Sends it via TGNetwork::Send

**The host relays the RAW message bytes, not a re-serialized version.** It literally
clones and forwards the original packet payload to all other peers.

### Object State Updates Handler

This handles INCOMING state updates (NOT on the host, only on receiving clients):

```
// Only process if sender != self
if (sender_id != local_peer_id):
    // If host, relay to "Forward" group first
    if (IsMultiplayer):
        forward group relay

    // Then deserialize: read position, rotation, velocity from stream
    // Find the existing object and update it
```

### Serialization Format

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

The keepalive/state interval appears to be on the order of 0.5-1.0 seconds based on
the float comparison pattern in the host update loop.

For game objects, the update rate is driven by the game loop timer (~30fps typical
for BC), but network sends are throttled by the peer's last-send timestamp.

### Packet Structure

Each UDP packet can contain up to **255 messages** (count byte at offset 1).
Max packet size is 1024 bytes by default.

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
- **Unreliable**: Position/state updates (latest wins)
- **Reliable**: Script messages, events
- **Priority reliable**: Checksums, critical handshake

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

1. **C++ engine level**: After receiving and deserializing a game object update,
   the host iterates all player slots and forwards the raw message to all other
   active players. This is AUTOMATIC and happens in the engine's ReceiveMessageHandler.

2. **Python script level**: Mission scripts explicitly call `SendTGMessage(0, msg)`
   or `SendTGMessageToGroup("NoMe", msg)` to broadcast game events. The host script
   receives, processes, and re-broadcasts. Chat is the canonical example:
   - Client sends chat to host
   - Host's Python ProcessMessageHandler receives it
   - Host calls `SendTGMessageToGroup("NoMe", pMessage.Copy())` to forward

### The Host Does NOT Re-broadcast at the TGNetwork Level

The network-level relay is ONLY called for connection-management messages (type 0
connect, type 4 disconnect). Regular game data messages (type 3) are NOT automatically
relayed by TGNetwork.

Instead, the relay happens in the MultiplayerGame C++ layer which explicitly iterates
player slots and calls TGNetwork::Send for each.

---

## 6. Opcodes During Gameplay

### MultiplayerGame ReceiveMessageHandler Opcode Dispatch

The ReceiveMessageHandler is registered for ET_NETWORK_MESSAGE_EVENT.
The first byte of the message payload determines the opcode:

#### Connection/Lobby Phase Opcodes

These are handled by the MultiplayerGame C++ dispatcher:

| Opcode | Purpose | Phase |
|--------|---------|-------|
| 0x00 | Settings packet (game settings + map name + checksum flag) | Lobby |
| 0x01 | Ready signal (player ready status) | Lobby |

#### NetFile/Checksum Opcodes (0x20-0x28 range)

Handled by NetFile dispatcher:

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
2. Dispatch validates sequence and delivers
3. The host Update loop dequeues and fires ET_NETWORK_MESSAGE_EVENT events

The game object data is raw serialized bytes -- the first few bytes identify the
object type and the rest is the serialized state. There is NO opcode byte for
these messages; the deserialization function reads the type ID from the data stream itself.

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

3. **Maintain the player slot table**: 16 slots, each 24 bytes. The server
   uses this to determine which peers to relay to.

4. **Handle keepalives**: Send periodic keepalive messages to detect disconnected
   clients (timeout detection via peer timestamps).

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
- Object serialization: virtual serialize/unserialize methods on game objects

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
   - ProcessIncomingPackets: Receives UDP packet from Client A
   - DispatchIncomingQueue: Validates sequence, dispatches by type
   - For type 3 (data): data message handler processes
   - Dequeue loop: fires ET_NETWORK_MESSAGE_EVENT events

3. Host's EventManager:
   - Dispatches to MultiplayerGame::ReceiveMessageHandler
   - Handler deserializes game object
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
