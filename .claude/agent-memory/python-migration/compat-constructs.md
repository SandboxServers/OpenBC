# Python 1.5.2 Compatibility Constructs Found

## Source Transform Required
| Construct | Files Found | Count |
|-----------|------------|-------|
| `raise X, msg` | App.py, string.py, AI/Setup.py, AI/PlainAI/BaseAI.py, Camera.py, Bridge/HelmMenuHandlers.py, Actions/CameraScriptActions.py | 30+ |
| `apply(func, args)` | App.py only | 1,004 |
| Backtick repr `` `x` `` | string.py:474 only | 1 |
| `print "text"` (uncommented) | None found in server scripts (all commented out) | 0 |
| `except X, v:` | None found in Multiplayer/ scripts | 0 |

## Runtime Shim Required
| Construct | Files Found |
|-----------|------------|
| `dict.has_key()` | Mission1.py, Mission2.py, Mission3.py, Mission5.py, *Menus.py |
| `new.instancemethod()` | App.py (2,521 calls) |
| `import new` | App.py |
| `import cPickle` | Autoexec.py |
| `import imp` | loadspacehelper.py |
| `reload()` | SpeciesToShip.py, loadspacehelper.py, Autoexec.py, ships/Hardpoints/*.py |
| `from strop import *` | string.py |
| `sys.setcheckinterval()` | Autoexec.py |
| `string.split()` etc | Not used in Multiplayer/ directly, but string.py ships with game |

## Integer Division
`fDamageDone / 10.0` - always uses float divisor in Multiplayer scripts.
`string.py:457` - `half = n/2` - true integer division concern.
