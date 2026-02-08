# How the Original BC Server Works

## Architecture: Peer-to-Peer with Host Relay

The original Bridge Commander multiplayer is NOT server-authoritative. It operates as:

1. **Every peer runs the full game engine** (NetImmerse renderer, physics, Python scripts)
2. **The host acts as a relay** for events between clients
3. **No server-side physics validation** -- clients simulate locally and trust each other

Evidence:
- The dedicated server at `STBC-Dedicated-Server` requires the full stbc.exe binary running
- SimulationPipelineTick (0x00451ac0) calls TGNetwork::Update from the game's main loop
- The "dedicated server" is literally the full game exe with rendering patched out
- MultiplayerGame handlers like StartFiringHandler are at handler addresses inside the
  MultiplayerGame object (0x006a1790 etc.), registered via FUN_0069efe0
- Chat messages demonstrate the relay pattern: client sends to host, host forwards to "NoMe" group

## Event Relay Pattern (from Python scripts)

The chat system in MultiplayerMenus.py clearly shows the relay pattern:
```
Client: pNetwork.SendTGMessage(pNetwork.GetHostID(), pMessage)
Host:   pNetwork.SendTGMessageToGroup("NoMe", pNewMessage)  # forward to everyone except me
```

This same pattern applies to game events (firing, damage, etc.) -- the C++ handlers
at the addresses listed in function-map.md implement the same relay logic in native code.

## MultiplayerGame Event Handlers

Registered by FUN_0069efe0 (RegisterMPGameHandlers):

| Handler                     | Address    | Nature           |
|-----------------------------|------------|------------------|
| ReceiveMessageHandler       | 0x0069f2a0 | Opcode dispatch  |
| EnterSetHandler             | 0x006a07d0 | Scene transition |
| ExitedWarpHandler           | 0x006a0a10 | State relay      |
| DisconnectHandler           | 0x006a0a20 | Connection mgmt  |
| NewPlayerHandler            | 0x006a0a30 | Player slot mgmt |
| SystemChecksumPassedHandler | 0x006a0c60 | Checksum result  |
| SystemChecksumFailedHandler | 0x006a0c90 | Checksum result  |
| DeletePlayerHandler         | 0x006a0ca0 | Player removal   |
| ObjectCreatedHandler        | 0x006a0f90 | Entity spawn     |
| HostEventHandler            | 0x006a1150 | Generic host evt |
| ObjectExplodingHandler      | 0x006a1240 | Death/explosion  |
| NewPlayerInGameHandler      | 0x006a1590 | Late join        |
| StartFiringHandler          | 0x006a1790 | Weapon relay     |
| StartWarpHandler            | 0x006a17a0 | Warp relay       |
| TorpedoTypeChangedHandler   | 0x006a17b0 | Weapon config    |
| StopFiringHandler           | 0x006a18d0 | Weapon relay     |
| StopFiringAtTargetHandler   | 0x006a18e0 | Weapon relay     |
| StartCloakingHandler        | 0x006a18f0 | Cloak relay      |
| StopCloakingHandler         | 0x006a1900 | Cloak relay      |
| SubsystemStateChangedHandler| 0x006a1910 | Damage relay     |
| AddToRepairListHandler      | 0x006a1920 | Repair relay     |
| ClientEventHandler          | 0x006a1930 | Generic client   |
| RepairListPriorityHandler   | 0x006a1940 | Repair relay     |
| SetPhaserLevelHandler       | 0x006a1970 | Weapon config    |
| DeleteObjectHandler         | 0x006a1a60 | Entity removal   |
| ChangedTargetHandler        | 0x006a1a70 | Targeting relay  |
| ChecksumCompleteHandler     | 0x006a1b10 | Setup complete   |
| KillGameHandler             | 0x006a2640 | Game end         |
| RetryConnectHandler         | 0x006a2a40 | Reconnection     |

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
- UtopiaApp_MainTick processes timers, events, subsystems
- TGNetwork::Update is called separately (not inside MainTick)

## Python Mission Scripts Handle Game Logic
- Scoring is entirely in Python (Mission1.py: ObjectKilledHandler, DamageEventHandler)
- Ship creation is in Python (via MultiplayerMenus/MissionMenusShared)
- Game end conditions are in Python (time limit, frag limit, etc.)
- The C++ engine provides the simulation; Python drives game rules
