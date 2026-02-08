# Mod Compatibility Tester - Agent Memory

## Key Findings

### Phase 1 Server-Side API Surface
- Vanilla MP scripts need ~298 unique API functions (functions + singleton methods)
- Plus ~124 constants and ~29 global singletons
- Hardpoints (ship definitions) use 19 Property_Create functions and ~132 Set* methods
- The ~297 estimate from CLAUDE.md is accurate for VANILLA; mods will push it higher
- **With gameplay scope: ~360 functions + 130 constants + 29 singletons = ~519 symbols**

### Critical Architecture Patterns
- `IsHost() and not IsClient()` = dedicated server mode (no GUI)
- MissionShared.py mixes server logic (scoring, timers, network messages) with UI code
- Ship definitions: ships/*.py (stats) + ships/Hardpoints/*.py (property configuration)
- Checksum system covers: App.pyc, Autoexec.pyc, ships/**/*.pyc, mainmenu/*.pyc
- Custom/ directory is CHECKSUM EXEMPT - mods go here for server compatibility

### Gameplay-Scope Findings (Full Multiplayer Matches)
- **Server IS the damage authority** - pEvent.GetDamage() comes from C engine on host
- Ship hardpoints needed for physics, not just display (shield HP, hull HP, weapon damage)
- Custom messages self-describe (type byte first) - unrecognized types safely ignored
- Mission4 does not exist in vanilla; MissionShared references Mission6/7/9 as mod slots
- DedicatedServer.py wraps (not stubs) event handlers; stubs GUI functions
- RestartGame() in all missions calls TopWindow/MultiplayerWindow - crashes headless
- Modifier table (Modifier.py) directly affects scoring; mods extend to larger matrices
- Mission5 adds AI system requirement (ConditionalAI, ConditionScript, StarbaseAI)

### Detailed Reports
- [phase1-mod-analysis.md](phase1-mod-analysis.md) - Phase 1 lobby mod compatibility
- [gameplay-mod-analysis.md](gameplay-mod-analysis.md) - Full gameplay mod analysis
- [api-gaps.md](api-gaps.md) - API functions needed by mods beyond vanilla (TBD)
- [foundation-compat.md](foundation-compat.md) - Foundation Technologies analysis (TBD)

### Server Script Dependencies
Multiplayer game flow: Autoexec.py -> MultiplayerGame.py -> Episode.py -> Mission1-5.py
Ship creation: SpeciesToShip.py -> ships/*.py -> Hardpoints/*.py -> GlobalPropertyTemplates.py
System (map) creation: SpeciesToSystem.py -> Systems/Multi1-7/*.py
Scoring: Mission1.py (server-side) handles ET_OBJECT_EXPLODING, ET_WEAPON_HIT events

### Gameplay Mod Compatibility Ratings
| Category | Rating | Key Dependency |
|----------|--------|---------------|
| Vanilla ships | WORKS | Property API (19 Create + 132 Set) |
| Custom ships (standalone) | PARTIAL | kSpeciesTuple entries + SPECIES_* constants |
| Custom ships (Foundation) | PARTIAL | Foundation __import__ hook on Python 3.x |
| Custom weapons | WORKS | Same Property API as vanilla hardpoints |
| Custom game modes | PARTIAL | Same mission pattern; GUI stubs needed |
| KM rebalance | PARTIAL | Depends on Foundation working first |

### The STBC-Dedicated-Server Approach (Lessons)
The existing dedicated server project (DDraw proxy) reveals key headless mode challenges:
1. GUI functions must be stubbed/no-oped (LoadBridge, MultiplayerMenus, Mission*Menus)
2. Python 1.5 `import` hook needed to fix package attribute resolution
3. Event handlers referencing None GUI widgets crash - need try/except wrappers
4. Shadow class vs. raw SWIG pointer issue (dedicated server uses functional Appc API)
5. InitNetwork must work without shadow class methods
6. RestartGame() crashes headless (TopWindow/MultiplayerWindow/ChatWindow)
7. SortedRegionMenu_GetWarpButton() returns None - crashes SetupEventHandlers
8. OpenBC should provide dummy objects instead of None for headless mode stubs
