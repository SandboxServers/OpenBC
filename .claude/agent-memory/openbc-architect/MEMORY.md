# OpenBC Architect Memory

## Key Architectural Decisions

### Phase 1: Playable Dedicated Server (REVISED 2026-02-08)
- **Design doc (lobby)**: [phase1-architecture.md](phase1-architecture.md)
- **Design doc (gameplay expansion)**: [gameplay-architecture-expansion.md](gameplay-architecture-expansion.md)
- Phase 1 expanded from lobby-only to playable relay server
- ~579 SWIG API functions (up from ~297), ~80 UI no-op stubs
- BC multiplayer is HOST-RELAYED, not true P2P -- host copies+forwards messages
- "NoMe" group = all peers except host, used for broadcast relay
- 4 new subsystems: message relay, ship object model, game lifecycle FSM, scoring support
- Game lifecycle: LOBBY -> SHIP_SELECT -> LOADING -> PLAYING -> GAME_OVER -> RESTARTING
- Ship objects are data-only (no physics/render) -- just ECS components for script queries
- Most game opcodes (0x02-0x1F) are pure relay: copy + rebroadcast to "NoMe"
- Dedicated server: IsHost=1, IsClient=0, IsMultiplayer=1 (scripts check this)
- Phase 2 boundary shifted: Phase 1=relay server, Phase 2=server-authoritative simulation
- Raw UDP sockets, NOT ENet -- vanilla BC clients speak TGNetwork protocol
- 30 Hz tick rate (33ms), matching original Windows timer
- Event system is the backbone: TGNetwork -> EventManager -> handlers -> ECS
- Handle map bridges SWIG pointer strings to flecs entities
- SWIG handle format: `_HEXADDR_p_TypeName` (we use entity ID as hex addr)
- Timeline: 13 chunks, ~12-14 weeks (up from 10 chunks, 8-10 weeks)

### Critical Protocol Details
- Port 22101 (0x5655)
- GameSpy and game traffic share ONE UDP socket, demuxed by peek at first byte
- `\` prefix = GameSpy query, binary = TGNetwork packet
- connState: 2=HOST (counterintuitive), 3=CLIENT, 4=DISCONNECTED
- Checksum exchange: 4 sequential requests (opcodes 0x20/0x21), server-initiated
- Settings packet (opcode 0x00): gameTime + settings + playerSlot + mapName + passFail
- Two separate message dispatchers: NetFile (0x20-0x27) and MultiplayerGame (0x00-0x1F)

### Event System
- Key events confirmed hex values: 0x60001 (MSG), 0x60002 (CONNECT), 0x8000e7 (CHECKSUM_FAIL), 0x8000e8 (CHECKSUM_COMPLETE), 0x8000e9 (KILL_GAME), 0x800053 (START)
- Python handlers registered as "Module.FuncName" strings
- Events have source, destination (for instance handlers), and type
- EventManager processes queue each tick (can run multiple times per tick)

### Entity-Handle Mapping
- Singleton entities: UtopiaModule(1), EventManager(2), ConfigMapping(3), VarManager(4), TopWindow(5), Network(6)
- Type checking: TGWinsockNetwork IS-A TGNetwork, MultiplayerGame IS-A Game
- Null handle: `_ffffffff_p_void`
- Generation counter for use-after-free detection

### Build Architecture
- `openbc_core` static lib: shared between server and client
- `openbc_server`: links core only (no GPU deps)
- `openbc_client` (Phase 3+): links core + bgfx + SDL3 + miniaudio + RmlUi
- CMake flag: `OPENBC_SERVER_ONLY=ON` for server-only build

### Reference Code Locations (STBC-Dedicated-Server repo)
- docs/network-protocol.md -- checksum exchange, packet opcodes, event types
- docs/multiplayer-flow.md -- complete client->server->ship selection flow
- docs/dedicated-server.md -- bootstrap sequence, game loop, binary patches
- docs/swig-api.md -- key SWIG API functions with signatures
- docs/function-map.md -- all 18,247 functions organized by category
- reference/scripts/App.py -- SWIG shadow classes, all ET_/CT_ constants
- reference/scripts/Multiplayer/ -- multiplayer game scripts

### Risks
- TGNetwork wire format must be captured from actual BC sessions
- Hash algorithm (FUN_007202e0) must match bit-for-bit
- ET_* integer values need extraction from Appc.pyd binary
- Python 1.5.2 syntax requires offline migration tool + runtime shim
- Opcode dispatch table (DAT_009962d4) needed for relay vs event-firing opcodes
- Ship creation protocol (opcode 0x02) format must be reverse-engineered
- UI stub coverage: missing stubs cause AttributeError in Python scripts
- Late-join state sync: InitNetwork(iToID) must work for mid-game joins
- MAX_MESSAGE_TYPES constant value needed (custom messages offset from it)

### Key Script Patterns (from reference analysis)
- Host relay pattern: `if IsHost(): pMessage.Copy(); SendTGMessageToGroup("NoMe", copy)`
- Team chat: host checks FriendlyGroup/EnemyGroup membership, forwards selectively
- Scoring: host-only (ObjectKilledHandler, DamageEventHandler registered only on host)
- Custom messages: MISSION_INIT = MAX_MESSAGE_TYPES+10, SCORE_CHANGE = +11, etc.
- Ship queries: ShipClass_Cast, GetNetPlayerID, IsPlayerShip, GetObjID, GetNetType
- Ship lookup: MultiplayerGame_GetShipFromPlayerID(mg, playerID)
- Dedicated check: `IsHost() and not IsClient()` = dedicated server mode
