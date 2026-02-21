# OpenBC Tools

## Tool Inventory

| Tool | Type | Purpose |
|------|------|---------|
| `manifest.c` → `openbc-hash.exe` | C (built by `make all`) | Hash manifest CLI: generate, verify, hash-string, hash-file |
| `scrape_bc.py` | Python 3 | Extract ship/projectile data from BC reference scripts → `data/vanilla-1.1/` |
| `compare_traces.py` | Python 3 | Compare OBCTRACE binary logs vs reference payloads |
| `gs_query.py` | Python 3 | GameSpy query diagnostic tool |
| `ahk/bc-test.ahk` | AutoHotkey v2 | Automate real BC client for manual integration testing ([see README](ahk/README.md)) |

## openbc-hash.exe

Hash manifest CLI tool. Built from `tools/manifest.c` by `make all`.

```
# Generate a manifest from a BC install
./build/openbc-hash.exe generate /path/to/bc/install -o manifests/vanilla-1.1.json

# Verify a game directory against a manifest
./build/openbc-hash.exe verify manifests/vanilla-1.1.json /path/to/bc/install

# Hash a single string (StringHash -- 4-lane Pearson)
./build/openbc-hash.exe hash-string "scripts/ships/Hardpoints/galaxy.py"

# Hash a single file (FileHash -- rotate-XOR)
./build/openbc-hash.exe hash-file /path/to/file.pyc
```

## scrape_bc.py

Extracts ship stats and projectile data from Bridge Commander reference scripts. Parses the auto-generated Python hardpoint files using regex (doesn't import them, since they depend on the BC runtime).

```
# Extract from reference scripts directory
python3 tools/scrape_bc.py <bc-scripts-dir> -o data/vanilla-1.1/
```

Output: JSON with 16 flyable ships (hull, shields, subsystems, weapon hardpoints) and 15 projectile types (damage, speed, lifetime, guidance).

## compare_traces.py

Compares OpenBC's OBCTRACE binary output against reference payloads from the Valentine's Day battle trace. Used to verify wire-format compatibility byte-by-byte.

```
python3 tools/compare_traces.py battle_trace.bin
```

Reference payloads are embedded in the script, extracted from the Valentine's Day battle trace.

## gs_query.py

GameSpy query diagnostic tool. Sends QR1 queries to an OpenBC server and validates the response format.

```
# Query localhost (default)
python3 tools/gs_query.py

# Query a specific server
python3 tools/gs_query.py 192.168.1.100 22101
```

Tests direct queries, broadcast queries, and response field parsing.

## ahk/bc-test.ahk

AutoHotkey v2 script that automates a real BC 1.1 client for manual integration testing. See [ahk/README.md](ahk/README.md) for setup, hotkeys, and calibration.
