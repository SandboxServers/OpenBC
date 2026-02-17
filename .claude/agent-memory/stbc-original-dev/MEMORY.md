# STBC Original Dev Agent - Memory Index

## Key Files
- [design-intent.md](design-intent.md) - Core design decisions and rationale
- [multiplayer-architecture.md](multiplayer-architecture.md) - MP network model, player limits, tick rate
- [checksum-system.md](checksum-system.md) - Checksum protocol design and SkipChecksum flag
- [message-protocol.md](message-protocol.md) - Two-layer message architecture (C++ opcodes + Python TGMessage)
- [modding-conventions.md](modding-conventions.md) - Custom/ directory, event system, moddability
- [gameplay-server-intent.md](gameplay-server-intent.md) - Host authority, message relay, ship objects, game modes

## Critical Facts
- Player limit: 2-8 (Python), 16 slots (C++ array). NOT "2 player only"
- Architecture: Lockstep simulation with input relay. NOT server-authoritative
- Checksums: Anti-desync, not anti-cheat. 4 directories, sequential verification
- SkipChecksum flag: Debug/LAN feature, legitimate to expose
- Custom/ directory: Intentionally checksum-exempt for modding
- Tick rate: 30fps (33ms) is appropriate for dedicated server
- Lobby state: Push-based, host sends settings after checksum pass
- Two message layers: C++ opcodes 0x00-0x28, Python MAX_MESSAGE_TYPES+N
- Event handlers registered by string name for moddability/late-binding
- MissionMenusShared.py has all lobby config constants

## Gameplay Server Design (Phase 1 Expansion)
- Host is SCORING AUTHORITY only, not simulation authority
- Python tracks: kills, deaths, scores, damage-attribution, team assignments
- Python does NOT track: positions, HP, shields, physics, subsystem state
- Host relays team messages explicitly (Mission2/3/5 TEAM_MESSAGE pattern)
- Dedicated server was INTENDED: IsHost() && !IsClient() pattern in 20+ locations
- HandleHostStartClicked sets SetIsClient(0) when dedicated button checked
- Mission1/2/3: pure relay + scoring (feasible for Phase 1)
- Mission5: requires server-side Starbase AI (defer to later phase)
- No host migration exists; dedicated server solves host-disconnect problem
- All score messages use SetGuaranteed(1) for reliable delivery
- ET_WEAPON_HIT / ET_OBJECT_EXPLODING come from C++ sim, not network
- For OpenBC: synthesize these events from parsed network opcodes (Option B)
- SendTGMessage(0, msg) = broadcast; SendTGMessageToGroup("NoMe", msg) = all-but-self

## Source Locations (STBC-Dedicated-Server repo)
- Reference scripts: `reference/scripts/Multiplayer/`
- MissionMenusShared.py: Player limit constants (MIN=2, MAX=8)
- MissionShared.py: Message type constants, ProcessMessageHandler
- MultiplayerMenus.py: Chat messages, lobby UI, GameSpy setup, dedicated server button
- Mission1.py: FFA scoring, InitNetwork, ProcessMessageHandler
- Mission2.py: Team DM scoring, TEAM_MESSAGE relay pattern
- Mission5.py: Coop mode, CreateStarbase() (host-only AI), StarbaseAI.py
- DedicatedServer.py: `src/scripts/Custom/DedicatedServer.py`
