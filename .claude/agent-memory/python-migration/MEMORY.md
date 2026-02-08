# Python Migration Agent Memory

## Key Findings

### App.py SWIG Wrapper Structure
- 14,078 lines, 630 classes (315 base + 315 Ptr wrappers)
- 1,004 uses of `apply()`, 2,521 uses of `new.instancemethod()`
- 10 uses of `raise AttributeError,name` (comma form)
- Pattern: `Appc.globals.g_kXxx` exposed as `App.g_kXxx`
- See [app-py-structure.md](app-py-structure.md) for details

### Python 1.5.2 Constructs Found in Server Scripts
- `dict.has_key()` - heavy use in Multiplayer/Episode/Mission*.py
- `reload()` - used in SpeciesToShip.py, loadspacehelper.py
- `import cPickle` - Autoexec.py
- `import new` + `new.instancemethod()` - App.py (2,521 uses)
- `import imp` - loadspacehelper.py
- `apply()` - App.py (1,004 uses, all in SWIG wrapper)
- `raise X, msg` - App.py (10), string.py (15+), others
- Backtick repr `` `x` `` - only in string.py:474 (`zfill`)
- `from strop import *` - string.py:583
- `sys.setcheckinterval()` - Autoexec.py
- See [compat-constructs.md](compat-constructs.md)

### Server-Critical Script Chain
Multiplayer/ -> MissionShared.py, MultiplayerGame.py, MultiplayerMenus.py,
Episode/Episode.py, Episode/Mission1-5/, SpeciesToShip/System/Torp.py, Modifier.py
Support: MissionLib.py, loadspacehelper.py, string.py, copy_reg.py

### App Globals Used by Server Scripts
g_kUtopiaModule, g_kEventManager, g_kTimerManager, g_kRealtimeTimerManager,
g_kVarManager, g_kSetManager, g_kLocalizationManager, g_kModelPropertyManager,
g_kSystemWrapper

### Design Decision: App.py Rewrite vs Transform
App.py is the SWIG-generated wrapper and uses `import new`, `apply()`, `raise X,name`
pervasively. Two options:
1. Source-transform it (complex, fragile for 14K lines)
2. Rewrite App.py as a modern Python 3.x module (RECOMMENDED for Phase 1)
   - Our Appc is reimplemented anyway, so App.py wrapping it can also be reimplemented
   - Eliminates 1004 apply() + 2521 new.instancemethod() + 10 raise transforms

### Gameplay Script Analysis (Phase 1 Dedicated Server)
- 193 `dict.has_key()`, 11 `list.sort(cmp_func)`, 103 `chr()`/`ord()`, 17 `__import__()`
- `list.sort(cmp_func)` is a CRITICAL breaking change -- Python 3 removed cmp param
  - Shim must monkey-patch `list.sort` to detect cmp-style callable and wrap with `functools.cmp_to_key`
- All `print` statements in gameplay scripts are commented out (only DedicatedServer.py has active ones)
- No `except E, e:`, no `apply()`, no `string` module usage in gameplay scripts
- `__import__()` 1.5 semantics: returns top-level but scripts expect submodule attrs -- import hook must fix
- DedicatedServer.py should be REWRITTEN for Python 3.x (it's our code, not a game script)
- Module globals (MissionMenusShared) are server state -- must be pre-populated before mission init
- Server must stub: MultiplayerMenus (all), LoadBridge (all), MissionNMenus (all), DynamicMusic
- See [gameplay-script-analysis.md](gameplay-script-analysis.md) for full details

### Links
- [app-py-structure.md](app-py-structure.md) - SWIG wrapper analysis
- [compat-constructs.md](compat-constructs.md) - All 1.5.2 constructs found
- [phase1-script-chain.md](phase1-script-chain.md) - Server script dependency graph
- [gameplay-script-analysis.md](gameplay-script-analysis.md) - Full multiplayer gameplay script analysis
