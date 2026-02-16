# OpenBC Architect Memory

## ARCHITECTURE CHANGE (2026-02-15): Standalone C Server

Phase 1 architecture was COMPLETELY REWRITTEN. The Python/SWIG/flecs design is DEAD.

### New Architecture: Standalone C Server
- **NO Python embedding**, NO SWIG API, NO original BC scripts, NO flecs ECS
- **C + Make** build system (not CMake)
- **Data-driven**: ships.toml, maps.toml, rules.toml replace script execution
- **Hash manifests**: precomputed JSON replaces live file hashing (server needs no game files)
- **Mod-native**: TOML data packs with layered overrides, first-class extension points
- **Mesh-accurate collision**: NIF convex hull extraction tool (build-time)
- **Internet play**: GameSpy master server protocol (333networks compatible)
- **5 phases**: A (hash tool) -> B (protocol lib) -> C (lobby) -> D (relay) -> E (sim server)
- **Requirements doc**: [docs/phase1-requirements.md](../../docs/phase1-requirements.md)
- **RFC/impl plan**: [docs/phase1-implementation-plan.md](../../docs/phase1-implementation-plan.md)

### Critical Protocol Details (UNCHANGED)
- Port 22101 (0x5655), raw UDP, AlbyRules XOR cipher ("AlbyRules!" 10-byte key)
- GameSpy and game traffic share ONE UDP socket, demuxed by peek at first byte
- connState: 2=HOST (counterintuitive), 3=CLIENT, 4=DISCONNECTED
- Checksum: 4 sequential requests (0x20/0x21), version gate "60" hash 0x7E0CE243
- Settings packet (0x00): gameTime + settings + playerSlot + mapName + passFail
- 28 active game opcodes (jump table 0x00-0x2A has 41 entries, many DEFAULT) + 8 Python messages (0x2C-0x39)
- MAX_MESSAGE_TYPES = 0x2B (43)
- Object ID: Player N base = 0x3FFFFFFF + N * 0x40000
- Two hash algorithms: StringHash (4-lane Pearson, 1024-byte table) + FileHash (rotate-XOR, skip DWORD[1])
- 30 Hz tick rate (33ms)

### Key Design Decisions
- Server is HOST (IsHost=1, IsClient=0, IsMultiplayer=1)
- Game lifecycle FSM: LOBBY -> SHIP_SELECT -> PLAYING -> GAME_OVER -> RESTARTING
- Phase D = relay server (clients simulate locally), Phase E = server-authoritative
- "NoMe" group = all peers except server, used for broadcast relay
- Scoring via C opcode handlers, not Python scripts
- Ship data from ships.toml, not hardpoint script execution

### Data Registry (see docs/phase1-api-surface.md)
- `data/ships.toml` -- ship classes with subsystems/weapons (replaces ship + hardpoint scripts)
- `data/species.toml` -- species ID->numeric mapping + modifier classes
- `data/modifiers.toml` -- NxN score modifier table (vanilla: 3x3)
- `data/maps.toml` -- multiplayer maps with spawn points (replaces Systems/Multi*/)
- `data/rules.toml` -- game mode config (replaces hardcoded Mission1-5 constants)
- `manifests/*.json` -- precomputed StringHash/FileHash for client validation
- `server.toml` -- server config (port, mods, rules)
- Mod packs: mods/<name>/ with TOML overlays, additive ships/maps, replacement modifiers
- 15 subsystem types, up to 33 per ship, matching original engine slots
- Data extraction tool (Python 3, build-time) reads original scripts -> TOML

### Build Targets
- `openbc-server`: dedicated server binary (C, Make)
- `openbc-hash`: manifest generation/verification CLI (Python 3 or C)
- `openbc-nif-hull`: NIF collision mesh extraction tool (Phase E)
- `tools/extract_data.py`: one-time data extraction from BC install -> TOML

### Reference Code Locations (STBC-Dedicated-Server repo)
- docs/network-protocol.md -- checksum exchange, packet opcodes, event types
- docs/multiplayer-flow.md -- complete client->server->ship selection flow
- docs/dedicated-server.md -- bootstrap sequence, game loop, binary patches
- docs/wire-format-spec.md -- complete wire format specification
- docs/damage-system.md -- DoDamage formulas

### Resolved RE Items (all critical items solved)
- Wire format: 30K+ packet traces, fully verified
- Hash algorithms: both fully reverse-engineered with lookup tables
- ACK/reliable delivery: three-tier queue system documented
- Ship creation: ObjectCreateTeam (0x03) format verified
- StateUpdate: dirty-flag format, compressed types, S->C vs C->S direction split
- DoDamage: collision/weapon/explosion formulas documented

### Open Risks
- Manifest hash vs. live hash cross-validation needed
- Ship data accuracy (ships.toml vs. hardpoint scripts)
- NIF convex hull extraction (format documented but tool not built)
- StateUpdate compression bit-exact fidelity
- Late join state sync completeness
