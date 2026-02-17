# How the Original BC Server Works

## Architecture: Peer-to-Peer with Host Relay

The original Bridge Commander multiplayer is NOT server-authoritative. It operates as:

1. **Every peer runs the full game engine** (NetImmerse renderer, physics, Python scripts)
2. **The host acts as a relay** for events between clients
3. **No server-side physics validation** -- clients simulate locally and trust each other

Evidence:
- The dedicated server proxy requires the full game binary running
- The simulation pipeline tick calls TGNetwork::Update from the game's main loop
- The "dedicated server" is literally the full game exe with rendering patched out
- MultiplayerGame handlers (StartFiring, StopFiring, etc.) are registered during construction
- Chat messages demonstrate the relay pattern: client sends to host, host forwards to "NoMe" group

## Event Relay Pattern (from Python scripts)

The chat system in MultiplayerMenus.py clearly shows the relay pattern:
```
Client: pNetwork.SendTGMessage(pNetwork.GetHostID(), pMessage)
Host:   pNetwork.SendTGMessageToGroup("NoMe", pNewMessage)  # forward to everyone except me
```

This same pattern applies to game events (firing, damage, etc.) -- the C++ handlers
implement the same relay logic in native code.

## MultiplayerGame Event Handlers

Registered during MultiplayerGame construction (28 handlers total):

| Handler | Nature |
|---------|--------|
| ReceiveMessageHandler | Opcode dispatch (core relay) |
| EnterSetHandler | Scene transition |
| ExitedWarpHandler | State relay |
| DisconnectHandler | Connection management |
| NewPlayerHandler | Player slot management |
| SystemChecksumPassedHandler | Checksum result |
| SystemChecksumFailedHandler | Checksum result |
| DeletePlayerHandler | Player removal |
| ObjectCreatedHandler | Entity spawn |
| HostEventHandler | Generic host event |
| ObjectExplodingHandler | Death/explosion |
| NewPlayerInGameHandler | Late join handling |
| StartFiringHandler | Weapon relay |
| StartWarpHandler | Warp relay |
| TorpedoTypeChangedHandler | Weapon config |
| StopFiringHandler | Weapon relay |
| StopFiringAtTargetHandler | Weapon relay |
| StartCloakingHandler | Cloak relay |
| StopCloakingHandler | Cloak relay |
| SubsystemStateChangedHandler | Damage relay |
| AddToRepairListHandler | Repair relay |
| ClientEventHandler | Generic client event |
| RepairListPriorityHandler | Repair relay |
| SetPhaserLevelHandler | Weapon config |
| DeleteObjectHandler | Entity removal |
| ChangedTargetHandler | Targeting relay |
| ChecksumCompleteHandler | Setup complete |
| KillGameHandler | Game end |
| RetryConnectHandler | Reconnection |

## What This Means for OpenBC Phase 1

### Lobby-Only Server (Minimum Phase 1)
The lobby phase (connection, checksum exchange, game setup, ship selection) requires:
- ZERO physics
- ZERO simulation
- Only: connection management, checksum verification, settings exchange, player slots

### Server-Authoritative Game (Phase 1 Extended / Phase 2)
If we want to improve on the original by making the server authoritative:
- We MUST implement physics server-side (the original never needed to)
- This is a major new capability, not a reimplementation
- The flight model, damage model, and collision detection must run on the server
- Clients would send inputs, server would send authoritative state

## Game Tick Rate
- Dedicated server runs a 33ms timer (~30fps) for the game loop
- The main application tick processes timers, events, and subsystems
- TGNetwork::Update is called separately (not inside the main tick)

## Python Mission Scripts Handle Game Logic
- Scoring is entirely in Python (Mission1.py: ObjectKilledHandler, DamageEventHandler)
- Ship creation is in Python (via MultiplayerMenus/MissionMenusShared)
- Game end conditions are in Python (time limit, frag limit, etc.)
- The C++ engine provides the simulation; Python drives game rules
