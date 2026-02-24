# Checksum System - Developer Intent

## Purpose: Anti-Desync (Primary), Anti-Cheat (Secondary)
- In lockstep relay model, "cheating" creates split-brain, not advantage
- Modified scripts = different simulation outcomes = desync
- Checksums ensure both clients run identical bytecode

## Four Checksum Requests (Sequential)
| # | Path | Filter | Recursive | Why |
|---|------|--------|-----------|-----|
| 0 | scripts/ | App.pyc | No | Core engine-script interface |
| 1 | scripts/ | Autoexec.pyc | No | Startup configuration |
| 2 | scripts/ships/ | *.pyc | Yes | Ship definitions (balance-critical) |
| 3 | scripts/mainmenu/ | *.pyc | No | Menu scripts (init flow) |

## Why These Specific Paths
- App.pyc: If this differs, everything desyncs
- Autoexec.pyc: Global behavior modification
- ships/: Weapon stats, hull, shields = balance
- mainmenu/: Initialization flow affects game setup
- NOT checksummed: Bridge scripts (cosmetic), Custom/ (modding)

## Why .pyc Not .py
- Python 1.5.2 executes .pyc bytecode, not .py source
- Different .py (comments, whitespace) can produce identical .pyc
- Behavioral equivalence = bytecode equivalence

## Index 0 Reference Hash
- Hardcoded hash of retail App.pyc stored in game binary
- Version stamp to ensure retail version, not just any matching version
- Probably added after QA found beta App.pyc causing subtle desyncs

## Sequential Protocol
- Server sends request 0, waits for response, verifies
- On match: sends next request. On mismatch: opcode 0x22/0x23 + event
- Serialized to keep memory usage low (one comparison in flight)
- Queue in NetFile hash table B, dequeue on verify

## SkipChecksum Flag
- Debug/development feature left accessible in the game binary
- Likely controllable via command-line, config, or memory write
- LAN hosting mode probably sets this (HOST_LAN_BUTTON in MultiplayerMenus.py)
- Legitimate for LAN play where file matching is assumed
- For OpenBC: expose as server configuration option
