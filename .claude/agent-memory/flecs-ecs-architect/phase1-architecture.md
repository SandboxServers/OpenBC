# Phase 1 ECS Architecture - Detailed Reference

## Modules
1. **ObcNetworkModule** - Network components (ObcNetworkPeer, ObcNetworkState) + systems (Receive, Send)
2. **ObcGameStateModule** - Game state (ObcGameSession, ObcPlayerSlot, ObcChecksumExchange) + systems (Checksum, Lobby) + observers + custom events
3. **ObcCompatModule** - SWIG handle components (ObcSWIGHandle) + handle table + global object entities

## Singletons
| Singleton | Maps To | Purpose |
|---|---|---|
| ObcServerConfig | Config + CLI | port, max_players, timeouts, skip_checksums |
| ObcGameSession | MultiplayerGame fields | state machine (lobby/loading/ingame/ending) |
| ObcNetworkState | TGWinsockNetwork internal | socket_fd, peer_id counter, tick_count |
| ObcConfigMapping | App.g_kConfigMapping | Opaque ptr to hash map |
| ObcVarManager | App.g_kVarManager | Opaque ptr to var store |

## Entity Archetypes
| Type | Components | Lifecycle |
|---|---|---|
| Peer | ObcNetworkPeer + ObcPlayerSlot + ObcChecksumExchange + ObcSWIGHandle | Connect to disconnect |
| Event Ticket | ObcPendingScriptEvent | Created and deleted same tick |
| SWIG Global | ObcSWIGHandle (typed) | Startup to shutdown |

## Tags
- ObcChecksumPassed - Peer passed all 4 checksum rounds
- ObcChecksumFailed - Peer failed a checksum round
- ObcPeerDisconnecting - Peer in teardown
- ObcPeerTimedOut - Peer exceeded timeout

## Custom Events (ecs_emit)
| Event Entity | Original ET_* | Trigger |
|---|---|---|
| OBC_ET_NETWORK_NEW_PLAYER | ET_NETWORK_NEW_PLAYER | Observer on ObcPlayerSlot OnSet |
| OBC_ET_CHECKSUM_COMPLETE | ET_CHECKSUM_COMPLETE (0x8000e8) | ChecksumSystem, all rounds passed |
| OBC_ET_SYSTEM_CHECKSUM_FAILED | ET_SYSTEM_CHECKSUM_FAILED (0x8000e7) | ChecksumSystem, hash mismatch |
| OBC_ET_NETWORK_DISCONNECT | ET_NETWORK_DISCONNECT | CleanupSystem timeout |
| OBC_ET_KILL_GAME | ET_KILL_GAME (0x8000e9) | Admin command or script |
| OBC_ET_NETWORK_MESSAGE | ET_NETWORK_MESSAGE (0x60001) | Direct dispatch from NetworkReceive |

## Handle System
- 32-bit handle: type (8 bits) | index (24 bits)
- SWIG format: "_{hex_handle}_p_{TypeName}"
- Pool per type with free list for slot reuse
- Type validation on every SWIG function call

## SWIG API Implementation Pattern
```
Python call -> Parse SWIG ptr string -> Validate type -> Resolve to entity
  -> Query flecs world -> Return result as Python object
```

## Phase 2 Extension Points
- New components: ObcShip, ObcTransform, ObcPhysics, ObcHull, etc.
- New phases: AI, Weapons, Physics, Damage inserted via DependsOn chains
- Ship-to-player: ecs_add_pair(world, ship, EcsChildOf, player_entity)
- New handle types: OBC_HANDLE_SHIP_CLASS = 100, etc.
