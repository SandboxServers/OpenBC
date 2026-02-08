# Message Protocol - Two-Layer Architecture

## Layer 1: C++ TGNetwork Opcodes (NetFile/ChecksumManager)
System-level messages handled by C++ code:

| Opcode | Direction | Purpose |
|--------|-----------|---------|
| 0x00 | S->C | Post-checksum: gameTime, settings, playerSlot, mapName, passFail |
| 0x01 | S->C | Status byte (connection confirmed) |
| 0x20 | S->C | Checksum request: index, dir, filter, recursive |
| 0x21 | C->S | Checksum response: index, hashes |
| 0x22 | S->C | Checksum fail (file mismatch) |
| 0x23 | S->C | Checksum fail (reference mismatch) |
| 0x25 | Both | File transfer data |
| 0x27 | ? | Unknown |
| 0x28 | S->C | All files transferred |

## Layer 2: Python TGMessage Payloads
Application messages wrapped in TGMessage objects, first byte = message type.
Dispatched by Python ProcessMessageHandler via kStream.ReadChar().

### Base Messages (MultiplayerMenus.py)
| Type | Name | Direction |
|------|------|-----------|
| MAX_MESSAGE_TYPES + 1 | CHAT_MESSAGE | Broadcast |
| MAX_MESSAGE_TYPES + 2 | TEAM_CHAT_MESSAGE | Team broadcast |

### Shared Messages (MissionShared.py)
| Type | Name | Direction |
|------|------|-----------|
| MAX_MESSAGE_TYPES + 10 | MISSION_INIT_MESSAGE | Host->Client |
| MAX_MESSAGE_TYPES + 11 | SCORE_CHANGE_MESSAGE | Broadcast |
| MAX_MESSAGE_TYPES + 12 | SCORE_MESSAGE | Host->Client |
| MAX_MESSAGE_TYPES + 13 | END_GAME_MESSAGE | Host->All |
| MAX_MESSAGE_TYPES + 14 | RESTART_GAME_MESSAGE | Host->All |

### Team Mode Messages (Mission2/3/5)
| Type | Name | Direction |
|------|------|-----------|
| MAX_MESSAGE_TYPES + 20 | SCORE_INIT_MESSAGE | Host->Client |
| MAX_MESSAGE_TYPES + 21 | TEAM_SCORE_MESSAGE | Broadcast |
| MAX_MESSAGE_TYPES + 22 | TEAM_MESSAGE | Team |

## MISSION_INIT_MESSAGE Payload (from InitNetwork)
```
[MISSION_INIT_MESSAGE:u8]
[playerLimit:u8]
[system:u8]
[timeLimit:u8]         # 255 = no limit
[timeLeft:i32]         # only if timed (timeLimit != 255)
[fragLimit:u8]         # 255 = no limit
```

## C++ Event Handlers -> Network Messages
These fire as engine events, get relayed as TGMessages:
- StartFiring/StopFiring: weapon commands (shipID, weaponGroup, targetID)
- StartWarp: warp engagement (shipID, destination)
- TorpedoTypeChanged: torpedo selection (shipID, torpType)
- Start/StopCloaking: cloak toggle (shipID)
- SubsystemStateChanged: subsystem power (shipID, subsystemID, state)
- AddToRepairList/RepairListPriority: repair queue (shipID, subsystemID)
- SetPhaserLevel: phaser power (shipID, level)
- ChangedTarget: target selection (shipID, targetID)
- ObjectCreated: ship spawn / torpedo launch
- ObjectExploding/DeleteObject: destruction

## Key Insight for Phase 1
Server only generates opcodes 0x00 and 0x01 itself.
Everything else is relay: receive TGMessage from client A, forward to client B.
Server doesn't need to parse TGMessage payloads for relay.
