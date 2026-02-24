# Multiplayer Architecture - Developer Intent

## Player Limits
- C++ layer: 16-slot player array in MultiplayerGame (24 bytes per slot)
- Python layer: MIN_PLAYER_LIMIT=2, MAX_PLAYER_LIMIT=8 (MissionMenusShared.py:153-155)
- Host cycles limit via ChangePlayerLimit(), calls pGame.SetMaxPlayers()
- NewPlayerHandler checks active player count < maxPlayers
- 16 slots = headroom. 8 max = bandwidth/CPU concession for 56k era
- "2 player" misconception comes from typical usage, not a hard limit

## Network Model: Lockstep Input Relay
- Each client runs full deterministic simulation
- Server relays input events (StartFiring, StartWarp, etc.), not state updates
- Checksums ensure identical script bytecode = identical simulation behavior
- Standard for tactical games in 2001 (AoE, StarCraft used same approach)
- Server-authoritative rejected because simulation coupled to NetImmerse scene graph

## Why Relay, Not Server-Auth
1. Bandwidth: Inputs = bytes, state = kilobytes per tick
2. Development time: Single-player sim already exists, just add relay
3. Era precedent: FPS = server-auth, strategy/tactical = lockstep
4. Acceptable tradeoffs: Desyncs rare if checksums pass, cheating less critical

## Tick Rate
- Original game: simulation tied to render frame rate (30-60fps)
- Dedicated server proxy: 33ms timer (~30fps) - appropriate
- TGNetwork handles retransmission timing independently of app tick
- ProcessEvents needs to run frequently enough for prompt event dispatch
- 30fps sweet spot: responsive without wasting CPU

## OpenBC Phase Strategy
- Phase 1: Faithful relay model for retail client compatibility
- Phase 2+: Consider server-authoritative with flecs ECS headless simulation
