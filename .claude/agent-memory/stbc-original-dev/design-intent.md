# Core Design Intent

## Multiplayer Was Always Secondary
- Single-player campaign was the product; MP was value-add
- Bridge simulation = emotional core, not tactical MP
- MP extends tactical combat for replay value
- Why host-as-player, not dedicated server: development resources

## Ship Combat Philosophy
- Weighty and tactical, not arcade
- Subsystem targeting (weapons, shields, engines) = strategic depth
- Ship destruction is consequential (Trek license requirement)
- Damage model: shields -> hull -> subsystems (directional)
- Power management: player allocates power between systems

## Lobby Design
- Host configures all settings, then opens for connections
- Push-based: settings sent to each client after checksum pass
- No mid-lobby settings update mechanism
- Two-phase join: C++ opcode 0x00 (connection metadata) then Python MISSION_INIT_MESSAGE (game rules)
- Ship selection happens after lobby, before game start

## Game Modes (Missions 1-5+)
- Mission1: Free-for-all deathmatch
- Mission2: Team deathmatch
- Mission3: Team with objectives
- Mission5: Cooperative vs AI
- Each mission defines its own ProcessMessageHandler and scoring
- Common logic in MissionShared.py

## Cut Content / Aspirational Features
- HOST_DEDICATED_BUTTON in MultiplayerMenus.py: dedicated server UI existed
- 16-slot array: designed for more players than shipped
- Co-op was partially implemented (Mission5 = coop vs AI)
- Larger battles were performance-limited, not design-limited

## Load-Bearing Design Decisions
- Sequential checksum verification (one at a time): intentional for memory
- Shared UDP socket for GameSpy + game traffic: intentional, peek-based demux
- Python 1.5.2 chosen for embeddability + Totally Games experience
- SWIG 1.x for C++/Python binding: pragmatic choice for the era
- NetImmerse 3.1: licensed engine, single-threaded, CPU-limited
