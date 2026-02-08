# Gameplay Relay Analysis for Phase 1 Dedicated Server

## Executive Summary

BC multiplayer uses a **hybrid relay architecture**. The host (or dedicated server) does NOT simulate physics, AI, or combat. Instead:

1. **Native game messages (opcodes 0x02-0x1F)**: The host's C++ engine receives these, **deserializes them into game objects** on the host side, then **re-serializes and forwards** to all other connected peers. The host DOES process these messages locally (creating objects, updating state), but only for bookkeeping -- the actual simulation runs on each client independently.

2. **Python script messages (opcodes >= MAX_MESSAGE_TYPES)**: These are higher-level protocol messages (chat, mission init, scores, end game, restart) sent/received entirely in Python scripts. The host's Python scripts explicitly forward them using `SendTGMessage` or `SendTGMessageToGroup`.

3. **The server implementation for Phase 1 needs to handle BOTH levels**, but can largely treat native messages as opaque relay and focus Python effort on the script-level messages.

---

## Question 1: Game Message Opcodes (0x02+)

### Complete Message Type Enumeration

From the SWIG API surface (App.py constants), correlated with handler registrations in FUN_0069e590 and FUN_0069efe0, here is the complete native message type table:

```
Opcode  SWIG Constant                    Event Type        Handler Name
------  ---------------------------------  ----------------  ---------------------------
0x00    (verification/settings)            (direct)          (inline in ChecksumCompleteHandler)
0x01    (status byte)                      (direct)          (inline in ChecksumCompleteHandler)
0x02    GAME_INITIALIZE_MESSAGE            (internal)        (game init setup)
0x03    GAME_INITIALIZE_DONE_MESSAGE       (internal)        (game init complete)
0x04    CREATE_OBJECT_MESSAGE              ET_OBJECT_CREATED ObjectCreatedHandler
0x05    CREATE_PLAYER_OBJECT_MESSAGE       ET_OBJECT_CREATED ObjectCreatedHandler
0x06    DESTROY_OBJECT_MESSAGE             (internal)        DeleteObjectHandler
0x07    TORPEDO_POSITION_MESSAGE           (internal)        (position update)
0x08    HOST_EVENT_MESSAGE                 ET_HOST_EVENT     HostEventHandler
0x09    START_FIRING_MESSAGE               ET_START_FIRING   StartFiringHandler
0x0A    STOP_FIRING_MESSAGE                ET_STOP_FIRING    StopFiringHandler
0x0B    STOP_FIRING_AT_TARGET_MESSAGE      ET_STOP_FIRING_AT StopFiringAtTargetHandler
0x0C    SUBSYSTEM_STATE_CHANGED_MESSAGE    ET_SUBSYSTEM_...  SubsystemStateChangedHandler
0x0D    ADD_TO_REPAIR_LIST_MESSAGE         (internal)        AddToRepairListHandler
0x0E    CLIENT_EVENT_MESSAGE               (internal)        ClientEventHandler
0x0F    CHANGED_TARGET_MESSAGE             ET_CHANGED_TARGET ChangedTargetHandler
0x10    START_CLOAKING_MESSAGE             ET_START_CLOAKING StartCloakingHandler
0x11    STOP_CLOAKING_MESSAGE              ET_STOP_CLOAKING  StopCloakingHandler
0x12    START_WARP_MESSAGE                 ET_START_WARP     StartWarpHandler
0x13    REPAIR_LIST_PRIORITY_MESSAGE       ET_REPAIR_LIST_.. RepairListPriorityHandler
0x14    SET_PHASER_LEVEL_MESSAGE           ET_SET_PHASER_... SetPhaserLevelHandler
0x15    SELF_DESTRUCT_REQUEST_MESSAGE      (internal)        (self-destruct)
0x16    DELETE_OBJECT_FROM_GAME_MESSAGE    (internal)        DeleteObjectFromGameHandler
0x17    CLIENT_COLLISION_MESSAGE           (internal)        (collision)
0x18    COLLISION_ENABLED_MESSAGE          (internal)        (collision toggle)
0x19    NEW_PLAYER_IN_GAME_MESSAGE         ET_NEW_PLAYER_IN  NewPlayerInGameHandler
0x1A    DELETE_PLAYER_FROM_GAME_MESSAGE    (internal)        (player removal)
0x1B    CREATE_TORP_MESSAGE                (internal)        (torpedo creation)
0x1C    CREATE_PULSE_MESSAGE               (internal)        (pulse weapon)
0x1D    TORPEDO_TYPE_CHANGED_MESSAGE       ET_TORPEDO_TYPE.. TorpedoTypeChangedHandler
0x1E    SHIP_UPDATE_MESSAGE                (internal)        (position/state update)
0x1F    VERIFY_ENTER_SET_MESSAGE           ET_ENTER_SET      EnterSetHandler
0x20    DO_CHECKSUM_MESSAGE                (NetFile opcode)  NetFile::ReceiveMessageHandler
0x21    CHECKSUM_MESSAGE                   (NetFile opcode)  NetFile::ReceiveMessageHandler
0x22-27 (NetFile transfer opcodes)         (NetFile opcode)  NetFile::ReceiveMessageHandler
0x28    SEND_OBJECT_MESSAGE                (internal)        (object sync)
0x29    VERIFY_EXITED_WARP_MESSAGE         ET_EXITED_WARP    ExitedWarpHandler
0x2A    DAMAGE_VOLUME_MESSAGE              (internal)        (damage)
0x2B    CLIENT_READY_MESSAGE               (internal)        (client ready)
0x2C    MAX_MESSAGE_TYPES                  --                --
```

**IMPORTANT: MAX_MESSAGE_TYPES = 0x2C (44 decimal)**

Python script messages start at `MAX_MESSAGE_TYPES + N`:
- `0x2D` (MAX+1): CHAT_MESSAGE
- `0x2E` (MAX+2): TEAM_CHAT_MESSAGE
- `0x36` (MAX+10): MISSION_INIT_MESSAGE
- `0x37` (MAX+11): SCORE_CHANGE_MESSAGE
- `0x38` (MAX+12): SCORE_MESSAGE
- `0x39` (MAX+13): END_GAME_MESSAGE
- `0x3A` (MAX+14): RESTART_GAME_MESSAGE
- `0x40` (MAX+20): SCORE_INIT_MESSAGE (team modes)
- `0x41` (MAX+21): TEAM_SCORE_MESSAGE
- `0x42` (MAX+22): TEAM_MESSAGE

### Which opcodes does the server UNDERSTAND vs simply FORWARD?

**The server MUST understand (parse content):**
- 0x00: Verification/settings -- server constructs this, never receives it
- 0x01: Status -- server constructs this, never receives it
- 0x03: Reject message (type=3 at piVar7[0x10]) -- server constructs for full-server rejection

**The server acts as SMART RELAY for native opcodes (0x02-0x1F+):**
- The server receives the raw network message
- Calls `FUN_006b8530` to get the payload
- Reads byte[1] as the sender's player slot index
- Optionally reads byte[2] as additional data (when `param_2 != 0`)
- Calls `FUN_005a1f50` to **deserialize the payload into a game object** on the host
- If deserialization succeeds, the host then:
  - **If host (DAT_0097fa8a != '\0')**: Iterates all 16 player slots, clones the message via vtable+0x18, and sends to each active peer EXCEPT the sender and the host itself
  - **If client (DAT_0097fa8a == '\0')**: Processes locally only

**Confidence: HIGH** -- Verified from FUN_0069f620 decompiled code analysis.

---

## Question 2: Ship Creation Network Messages

### Ship Selection is NOT a Network Message

Ship selection happens entirely locally on each client. There is no "ship selection" network message. Each client:
1. Shows the ship selection UI (MissionMenusShared.py)
2. Player picks a species/ship type
3. Clicks "Start" button in the mission pane (which is mission-specific)

### Ship Creation Flow

When the host's mission script starts the game (after all players are in the lobby):

1. **Host Python script** calls `Multiplayer.MissionMenusShared.CreateShip(iType)` which calls `Multiplayer.SpeciesToShip.CreateShip(iType)` to create the ship object locally.

2. The engine fires **ET_OBJECT_CREATED (0x8000C8)** internally.

3. **MultiplayerGame::ObjectCreatedHandler (LAB_006a0f90)** catches this event. This handler:
   - Is registered for event type 0x8000C8
   - Serializes the created object into a network message
   - The message format uses opcode `CREATE_OBJECT_MESSAGE (0x04)` or `CREATE_PLAYER_OBJECT_MESSAGE (0x05)`
   - Sends it to all connected peers via the "Forward" group or directly

4. **Each client** receives the CREATE_OBJECT/CREATE_PLAYER_OBJECT message, deserializes it, and creates the ship object locally.

### NewPlayerInGameHandler (LAB_006a1590)

This handler fires when event **ET_NEW_PLAYER_IN_GAME (0x8000F1)** is posted. It is registered for both host and client. In the constructor:

```c
// Line 5482: Register for ET_NEW_PLAYER_IN_GAME
FUN_006db380(&DAT_0097f864, &DAT_008000f1, this,
    s_MultiplayerGame____NewPlayerInGa_0095a028, '\x01', '\x01', DAT_0095adf8);
```

And at lines 5509-5523, when the host constructs MultiplayerGame, it fires ET_NEW_PLAYER_IN_GAME for ITSELF:
```c
if (DAT_0097fa8a != '\0' && DAT_0097fa88 != '\0') {
    // Create event with type ET_NEW_PLAYER_IN_GAME (0x8000F1)
    puVar2[4] = &DAT_008000f1;
    puVar2[10] = *(WSN + 0x20);  // host's own peer ID
    // Post event
    FUN_006da2a0(&DAT_0097f838, puVar2);
    // Set first slot active with host's peer ID
    *(this + 0x78) = 1;   // slot[0].active = 1
    *(this + 0x7c) = *(WSN + 0x20);  // slot[0].peerID = hostPeerID
}
```

The Python mission script then receives this event and initializes the player (adds to kill/death dictionaries, rebuilds player list UI).

### What the Server Does When Ship is Created

The server does not need to validate ship selection. The engine automatically handles:
1. Object creation locally
2. Serialization into network message
3. Broadcasting to peers

For a **dedicated server**, since `DAT_0097fa88 == '\0'` (IsHost=true but IsClient=false), the host itself doesn't create a ship. It only relays ship creation messages from clients to other clients.

**Confidence: HIGH** -- from constructor code, handler registration, and Python script analysis.

---

## Question 3: Game Start/End Transition

### Game Start Flow

1. **Host clicks "Start Game"** in the multiplayer lobby UI
2. Python `HandleHostStartClicked` fires, which:
   - Sets multiplayer and host flags
   - Posts `ET_START (0x800053)` event
3. `HandleStartGame` handler runs, calling the window's built-in handler first
4. The built-in handler (C++ MultiplayerWindow) transitions from lobby to game state
5. The **mission script loads** (e.g., `Multiplayer.Episode.Mission1.Mission1`)
6. Mission script `Initialize()` sets up the game world:
   - Creates the starting set (star system)
   - Calls `SetReadyForNewPlayers(1)` on the MultiplayerGame
   - Posts `ET_NEW_PLAYER_IN_GAME` for each connected player
7. **Host mission script sends MISSION_INIT_MESSAGE** to all clients with:
   ```
   [chr(MISSION_INIT_MESSAGE)]  -- opcode byte (MAX_MESSAGE_TYPES + 10)
   [chr(playerLimit)]           -- max players
   [chr(systemSpecies)]         -- star system type
   [chr(timeLimit or 255)]      -- time limit (255 = none)
   [int(endTime)]               -- if time limit, absolute end time
   [chr(fragLimit or 255)]      -- frag limit (255 = none)
   ```
   Sent via `pNetwork.SendTGMessage(iToID, pMessage)` with `SetGuaranteed(1)`

8. Each client receives MISSION_INIT_MESSAGE in `ProcessMessageHandler`, sets up local game state

### Game End Flow

Game end is triggered by the host's Python mission script when conditions are met:

1. **Host detects end condition** (time up, frag limit, starbase destroyed, etc.)
2. Host calls `MissionShared.EndGame(iReason)` which:
   ```python
   pMessage = App.TGMessage_Create()
   pMessage.SetGuaranteed(1)
   kStream = App.TGBufferStream()
   kStream.OpenBuffer(256)
   kStream.WriteChar(chr(END_GAME_MESSAGE))   # MAX_MESSAGE_TYPES + 13
   kStream.WriteInt(iReason)                   # END_TIME_UP=1, END_NUM_FRAGS_REACHED=2, etc.
   pMessage.SetDataFromStream(kStream)
   pNetwork.SendTGMessage(0, pMessage)         # 0 = broadcast to all
   ```
3. Also calls `pMultGame.SetReadyForNewPlayers(0)` to stop accepting new connections
4. Clients receive END_GAME_MESSAGE, set `g_bGameOver = 1`, call `ClearShips()`, show end dialog

### Restart Flow

1. Host's end game dialog has a "Restart" button
2. Clicking it fires `ET_RESTART_GAME` event
3. Mission script's `RestartGameHandler` sends `RESTART_GAME_MESSAGE`:
   ```python
   kStream.WriteChar(chr(RESTART_GAME_MESSAGE))  # MAX_MESSAGE_TYPES + 14
   pNetwork.SendTGMessage(0, pMessage)            # broadcast
   ```
4. Clients receive it and call `RestartGame()` which:
   - Resets scores
   - Clears ships
   - Returns to ship select screen via `ShowShipSelectScreen()`
   - Re-enables `SetReadyForNewPlayers(1)`

**Confidence: VERIFIED** -- directly from Python mission script source code.

---

## Question 4: The ReceiveMessageHandler Relay Pattern (THE KEY QUESTION)

### Analysis of FUN_0069f620 (ProcessGameMessage)

This is the core relay function. Here is the reconstructed logic:

```c
void MultiplayerGame::ProcessGameMessage(TGMessage* msg, bool hasPlayerSlot) {
    // 1. Get message payload
    char* payload = msg->GetData(&payloadSize);

    // 2. Save/restore current player slot context
    int savedSlot = g_currentPlayerSlot;  // _DAT_0097fa84

    // 3. Read sender's player slot from byte[1] of payload
    char senderSlot = payload[1];
    int headerSize = 2;

    // 4. If hasPlayerSlot flag, also read byte[2] as extra data
    int extraData;
    if (hasPlayerSlot) {
        extraData = payload[2];
        headerSize = 3;
    }

    // 5. Swap player slot context (sets current slot to sender's slot)
    // This is a slot-swap operation for the game object system
    this->playerSlots[g_currentPlayerSlot].objectID = g_currentObjectID;
    g_currentObjectID = this->playerSlots[senderSlot].objectID;
    g_currentPlayerSlot = senderSlot;

    // 6. DESERIALIZE the remaining payload into a game object
    //    FUN_005a1f50 reads [objectTypeID:int][objectClassID:int] then
    //    creates the appropriate game object and deserializes its state
    TGObject* gameObj = DeserializeGameObject(payload + headerSize, payloadSize - headerSize);

    // 7. Restore slot context
    this->playerSlots[senderSlot].objectID = g_currentObjectID;
    g_currentObjectID = savedObjectID;
    g_currentPlayerSlot = savedSlot;

    if (gameObj == NULL) return;

    // 8. If hasPlayerSlot, store extra data on the object
    if (hasPlayerSlot) {
        gameObj->field_0x2E4 = extraData;  // piVar4[0xb9] = local_10
    }

    // 9. Check network state
    TGNetwork* wsn = g_pNetwork;  // DAT_0097fa78
    if (wsn == NULL) return;

    // ============ THE RELAY LOGIC ============

    if (!g_isMultiplayer) {  // DAT_0097fa8a == '\0' -- single player or client
        // CLIENT PATH:
        // Check if object type is 0x8009 (some skip type)
        if (gameObj->GetType() == 0x8009) return;
        // Allocate a 0x58-byte ShipObject wrapper
        // Fall through to object creation below
    }
    else {
        // HOST PATH: RELAY TO ALL OTHER PEERS
        for (int i = 0; i < 16; i++) {
            int* slot = &this->playerSlots[i];  // this + 0x7c + i*0x18
            if (slot[-1] == 0) continue;  // slot not active

            if (slot[0] == msg->senderPeerID) {
                // This is the SENDER's slot -- don't relay back
                // But if hasPlayerSlot, update slot's objectID
                if (hasPlayerSlot) {
                    slot[1] = gameObj->field_0x04;  // objectID
                }
            }
            else if (slot[0] != wsn->localPeerID) {
                // This is ANOTHER PEER (not sender, not host)
                // CLONE the original message and SEND to this peer
                TGMessage* clone = msg->Clone();  // vtable+0x18
                TGNetwork::Send(wsn, slot[0], clone, 0);
            }
            // If slot[0] == wsn->localPeerID, this is the host itself -- skip
        }

        // After relay, check if host should also process locally
        if (!g_isHost) {  // DAT_0097fa88 == '\0' -- dedicated server
            if (!hasPlayerSlot) return;  // no local processing for non-player msgs
            // Check skip type
            if (gameObj->GetType() == 0x8009) return;
            // Allocate wrapper
        }
        else {
            // Host IS a client too (non-dedicated)
            if (!hasPlayerSlot) return;
            // Skip if object belongs to host's own slot
            if (gameObj->field_0x04 == this->playerSlots[0].objectID) return;
            // Check skip type
            if (gameObj->GetType() == 0x8009) return;
            // Allocate wrapper
        }
    }

    // 10. Create a ShipObject wrapper (FUN_0047dab0) and add to game
    ShipObject* shipObj = new ShipObject(wrapper, gameObj, "Network");

    // 11. Fire the object's update handler
    gameObj->vtable[0x134/4](shipObj, 1, 1);  // AddToWorld(shipObj, true, true)
    gameObj->field_0xF0 = 0;  // Clear dirty flag
}
```

### THE CRITICAL ANSWER

**YES, the host/server performs a SIMPLE RELAY with message cloning.**

The relay loop at lines 5754-5770 of the decompiled code is:
```c
for each of 16 player slots:
    if (slot.active && slot.peerID != msg.senderPeerID && slot.peerID != wsn.localPeerID):
        clone = msg->Clone()
        TGNetwork::Send(wsn, slot.peerID, clone, 0)
```

The host clones the ORIGINAL raw network message (not the deserialized object) and sends it to every other active peer. This means:

1. **The server does NOT need to understand the payload content for relay purposes**
2. **The server DOES need to deserialize the object for its own bookkeeping** (tracking which objects exist, updating player slot objectIDs)
3. **For a dedicated server, the deserialization step can potentially be SKIPPED** for most message types -- the server just needs to:
   - Read byte[1] (sender slot index) to identify the sender
   - Clone the message
   - Send to all other active peers

### Simplification for Phase 1

For a dedicated server that doesn't run game simulation:
- **Clone the raw message and relay to all other peers** (the core relay)
- **Read byte[1] to find sender slot** (for routing, not for processing)
- **Track player slot <-> peerID mapping** (established during checksum completion)
- **DON'T deserialize game objects** (skip FUN_005a1f50 entirely)

The only messages the server must construct/understand:
- 0x00 (verification) -- server builds this
- 0x01 (status) -- server builds this
- 0x03 (reject) -- server builds this when full
- 0x20-0x27 (checksum) -- server handles checksum exchange

**Confidence: VERIFIED** -- Clear from decompiled relay loop in FUN_0069f620.

---

## Question 5: Chat Messages

### Chat is a PYTHON-LEVEL Message (NOT a native opcode 0x00-0x1F)

Chat messages use opcode `MAX_MESSAGE_TYPES + 1` (0x2D, assuming MAX_MESSAGE_TYPES=0x2C):

**Format:**
```
CHAT_MESSAGE:
[chr(CHAT_MESSAGE)]     -- 1 byte, opcode 0x2D
[long(senderPeerID)]    -- 4 bytes, who sent it
[short(stringLength)]   -- 2 bytes, message length
[string(message)]       -- N bytes, the chat text

TEAM_CHAT_MESSAGE:
[chr(TEAM_CHAT_MESSAGE)] -- 1 byte, opcode 0x2E
[long(senderPeerID)]     -- 4 bytes
[short(stringLength)]    -- 2 bytes
[string(message)]        -- N bytes
```

### Chat Relay Logic (Python)

From `MultiplayerMenus.py` line 2273:
```python
if (cType == CHAT_MESSAGE):
    if (App.g_kUtopiaModule.IsHost()):
        # Host forwards to everybody else
        pNewMessage = pMessage.Copy()
        pNetwork.SendTGMessageToGroup("NoMe", pNewMessage)
    # Then process locally (display the chat)
```

The "NoMe" group contains all peers except the local host. So chat relay is:
1. Client sends chat to HOST (not broadcast)
2. Host receives it, forwards a copy to "NoMe" group (all other peers)
3. Host also processes it locally (displays the chat)

For team chat, the host iterates through players and sends only to teammates.

### Server Implementation

For a dedicated server, chat handling must be in the Python mission scripts. The native engine relay (FUN_0069f620) handles chat messages the same as any other -- it clones and forwards. But the Python scripts also do their own forwarding for chat. This means:

**IMPORTANT**: Chat messages flow through BOTH paths:
1. The native C++ relay in FUN_0069f620 handles the raw network message
2. The Python ProcessMessageHandler also handles it

Wait -- re-reading more carefully: chat messages are sent via `pNetwork.SendTGMessage(pNetwork.GetHostID(), pMessage)` -- they are sent **TO THE HOST ONLY**, not broadcast. So the native relay in FUN_0069f620 won't forward them (since they're not received as broadcast). The Python script on the host explicitly copies and forwards.

**For the dedicated server**: The Python scripts must handle chat forwarding. The native relay won't do it because chat is sent point-to-point to the host.

**Confidence: VERIFIED** -- from Python script source and SendTGMessage call patterns.

---

## Question 6: Ship Selection Protocol

### There Is No "Ship Selection" Network Protocol

Ship selection is entirely local UI. Here is the complete flow:

1. **After checksum completion**, server sends opcode 0x00 (verification with settings) and 0x01 (status) to the client
2. Client transitions to the **ship selection screen** (MissionMenusShared.BuildShipSelectWindow)
3. Client selects species, star system, time limit, frag limit locally
4. Client clicks "Start" button in the mission pane

### What Happens When Client Clicks "Start" (Ship Select)

The "Start" button in the mission pane creates the ship locally via Python:
```python
pShip = Multiplayer.SpeciesToShip.CreateShip(iType)
```

This creates the ship game object, which triggers:
1. `ET_OBJECT_CREATED` event in the engine
2. ObjectCreatedHandler serializes it and sends CREATE_PLAYER_OBJECT_MESSAGE (0x05) to the host
3. Host relays it to all other clients via FUN_0069f620

### Team Assignment

Team assignment depends on the mission type:
- Mission 1 (FFA Deathmatch): All players are enemies, no teams
- Mission 2 (Team Deathmatch): Teams determined by ship placement in groups
- Mission 5/6 (Cooperative): Teams based on friendly/enemy groups

Team groups are managed in Python mission scripts using `pMission.GetFriendlyGroup()` and `pMission.GetEnemyGroup()`. The host's mission script sets up groups when ships are created.

### Ready State

There is no explicit "ready" network message. The flow is:
1. Checksum completes -> server sends 0x00/0x01 -> client shows ship select
2. Client creates ship -> fires ET_OBJECT_CREATED -> host relays to all
3. Host's mission script decides when to send MISSION_INIT_MESSAGE (which starts the actual game)

The host player's "Start" button in the mission pane controls when the game begins. Clients don't have a "ready" button -- they just select their ship and wait.

**For a dedicated server**: The host's mission script auto-starts when players have connected. Or it can use `CLIENT_READY_MESSAGE (0x2B)` for synchronization.

**Confidence: HIGH** -- from Python script analysis and handler registration.

---

## Server Implementation Implications

### Minimal Viable Relay Server

For Phase 1, the dedicated server needs:

1. **Network layer**: Full TGNetwork protocol (connection handshake, reliable delivery, packet framing)
2. **Checksum exchange**: Complete NetFile protocol (opcodes 0x20-0x27)
3. **Player slot management**: 16 slots, track active/peerID/objectID
4. **Simple message relay**: For opcodes 0x02+, clone message and send to all other active peers
5. **Python script hosting**: Run the mission scripts for:
   - Chat forwarding (CHAT_MESSAGE, TEAM_CHAT_MESSAGE)
   - Mission init (MISSION_INIT_MESSAGE)
   - Score tracking (SCORE_CHANGE_MESSAGE, SCORE_MESSAGE)
   - End game (END_GAME_MESSAGE)
   - Restart (RESTART_GAME_MESSAGE)
6. **Groups**: Maintain "NoMe" and "Forward" groups for message routing

### What the Server Does NOT Need (Phase 1)

- Ship physics simulation
- Weapon damage calculation
- AI behavior
- Rendering or audio
- NIF asset loading
- Collision detection
- Any game object deserialization (can treat message payloads as opaque blobs)

### Message Flow Diagram

```
Client A                    Server (Host)                    Client B
   |                            |                                |
   |--- CREATE_PLAYER_OBJ ---->|                                |
   |   (ship creation)         |--- Clone + Forward ----------->|
   |                            |                                |
   |--- SHIP_UPDATE ---------->|                                |
   |   (position/state)        |--- Clone + Forward ----------->|
   |                            |                                |
   |                            |<--- START_FIRING --- Client B --|
   |<--- Clone + Forward ------|                                |
   |                            |                                |
   |--- CHAT_MESSAGE --------->|   (to host only)               |
   |                            |--- Python forwards to NoMe -->|
   |                            |                                |
   |                            |--- MISSION_INIT (Python) ---->|
   |                            |--- MISSION_INIT (Python) ---->|
   |                            |                                |
   |                            |--- END_GAME (Python) -------->|
   |                            |--- END_GAME (Python) -------->|
```

### Critical Implementation Detail: The "Forward" Group

The "Forward" group is a TGNetwork group (WSN+0xF4 group list) that contains peer IDs of players who should receive forwarded game events. When a game event fires on the host (like ET_START_FIRING), the handler:

1. Looks up the "Forward" group by name
2. Checks if the source player is in the group's per-player tracking list
3. Sends to all members of the group using FUN_006b4ec0

This is different from the simple relay in FUN_0069f620 which uses the player slot array directly. The Forward group is used by the LOCAL event handlers (FUN_0069f930, FUN_0069fbb0, FUN_0069fda0) for events that originate from the host's own game engine, not from network messages.

For a dedicated server doing pure relay, the Forward group is less important -- the FUN_0069f620 relay loop handles the network message forwarding directly.

---

## Data Structure Addendum

### Message Payload Structure (Native Game Messages)

All native game messages (0x02-0x1F) follow this general format:
```
Offset  Size  Field
+0      1     opcode (message type byte)
+1      1     senderPlayerSlot (0-15)
+2      1     extraData (optional, only when hasPlayerSlot=true)
+2/+3   var   serialized game object data (type-specific)
```

The serialized game object data starts with:
```
+0      4     objectTypeID (read by FUN_005a1f50 via FUN_006cf670)
+4      4     objectClassID (read by FUN_005a1f50 via FUN_006cf670)
+8      var   object-specific serialized state
```

### Player Slot Structure (refined)

```
Offset  Size  Name           Notes
+0x00   1     active         0=empty, 1=in use
+0x04   4     peerID         TGNetwork peer identifier
+0x08   4     objectID       Game object ID for this player's ship
+0x0C   ?     ???            Possibly checksum state, team, etc.
```

The slot objectID is updated by FUN_0069f620 when processing messages with hasPlayerSlot=true:
```c
if (slot.peerID == msg.senderPeerID && hasPlayerSlot) {
    slot.objectID = gameObj->objectID;
}
```

---

## Open Questions

1. **What is 0x8009?** -- The type check `gameObj->GetType() == 0x8009` causes the host to skip local processing. This might be a "network-only" object type that shouldn't be instantiated on the host.

2. **What does param_2 (hasPlayerSlot) indicate?** -- The ReceiveMessageHandler at LAB_0069f2a0 calls FUN_0069f620 with param_2 set based on something. Some message types include a player slot byte, others don't. Need to trace the ReceiveMessageHandler to see what determines this.

3. **How does FUN_0069f930 differ from FUN_0069f620?** -- FUN_0069f930 appears to handle position/state update messages (SHIP_UPDATE_MESSAGE) and calls the "Forward" group rather than the raw relay. It also reads more complex payload (position, rotation, velocity).

4. **What is DAT_008e5528?** -- This is the name of a network group. Based on the Python API, `SendTGMessageToGroup("NoMe", msg)` is used for chat forwarding. The two groups created in the constructor are registered with FUN_006b70d0. One is DAT_008e5528 (likely "NoMe"), the other is "Forward" (s_Forward_008d94a0).

5. **Exact value of MAX_MESSAGE_TYPES** -- I inferred 0x2C (44) based on the enumeration count. This needs verification from the Appc.pyd SWIG module or runtime testing. If wrong, all the Python-level opcode numbers shift.
