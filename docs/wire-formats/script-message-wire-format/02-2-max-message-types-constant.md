# 2. MAX_MESSAGE_TYPES Constant


`MAX_MESSAGE_TYPES = 43` (0x2B)

This constant defines the boundary between engine opcodes and script message types. Python scripts define custom types as `MAX_MESSAGE_TYPES + N` to avoid colliding with engine opcodes.

### Stock Message Types

| Constant | Value | Hex | Defined In | Purpose |
|----------|-------|-----|------------|---------|
| MAX_MESSAGE_TYPES | 43 | 0x2B | Engine (SWIG) | Boundary marker |
| CHAT_MESSAGE | 44 | 0x2C | MultiplayerMenus | Player chat |
| TEAM_CHAT_MESSAGE | 45 | 0x2D | MultiplayerMenus | Team-only chat |
| MISSION_INIT_MESSAGE | 53 | 0x35 | MissionShared | Game config at join |
| SCORE_CHANGE_MESSAGE | 54 | 0x36 | MissionShared | Score delta on kill |
| SCORE_MESSAGE | 55 | 0x37 | MissionShared | Full score sync |
| END_GAME_MESSAGE | 56 | 0x38 | MissionShared | Game over |
| RESTART_GAME_MESSAGE | 57 | 0x39 | MissionShared | Restart broadcast |
| SCORE_INIT_MESSAGE | 63 | 0x3F | Mission5 (team modes) | Team score init |
| TEAM_SCORE_MESSAGE | 64 | 0x40 | Mission5 (team modes) | Team score update |
| TEAM_MESSAGE | 65 | 0x41 | Mission5 (team modes) | Team assignment |

Mods can define custom types using any value from 43 to 255 (the type is a single byte written via `WriteChar`).

---

