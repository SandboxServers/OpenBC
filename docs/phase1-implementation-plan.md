# RFC: OpenBC Standalone Server Architecture

**Status**: Draft
**Author**: Cadacious / Claude
**Date**: 2026-02-15

## 1. Goals

OpenBC is a fully open-source, mod-extensible multiplayer server for Star Trek: Bridge Commander. It replaces all original STBC content — binary, scripts, and data files — with clean-room implementations while maintaining wire-protocol compatibility with stock BC 1.1 clients.

**Design principles:**
- **Protocol-first**: The client is a protocol endpoint. Compatibility means speaking the wire protocol correctly.
- **Mod-native**: Every layer exposes extension points. Mods are first-class, not bolted on.
- **Data-driven**: Ship stats, weapon configs, maps, and game rules live in data files, not code.
- **Zero original content**: The server ships with no copyrighted STBC material. Game data is provided via hash manifests and data registries that reference the client's local files.

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                    OpenBC Server                         │
│                                                          │
│  ┌─────────┐  ┌──────────┐  ┌─────────┐  ┌───────────┐  │
│  │ Network  │  │ Checksum │  │  Game   │  │   Mod     │  │
│  │  Layer   │──│ Validator│──│  State  │──│  Loader   │  │
│  │ (UDP)    │  │(Manifest)│  │ Engine  │  │           │  │
│  └────┬─────┘  └──────────┘  └────┬────┘  └─────┬─────┘  │
│       │                          │              │         │
│  ┌────┴─────┐  ┌──────────┐  ┌───┴────┐  ┌─────┴─────┐  │
│  │ Protocol │  │  Opcode  │  │Physics │  │   Data    │  │
│  │  Codec   │  │ Handlers │  │  Sim   │  │ Registry  │  │
│  │(wire fmt)│  │ (41 ops) │  │        │  │(ships,etc)│  │
│  └──────────┘  └──────────┘  └────────┘  └───────────┘  │
└──────────────────────────────────────────────────────────┘
        ▲                                        ▲
        │ UDP packets                            │ JSON/TOML
        ▼                                        │
  ┌───────────┐                          ┌───────────────┐
  │ Stock BC  │                          │  Mod Packs    │
  │  Client   │                          │ (data + hash  │
  │  (1.1)    │                          │  manifests)   │
  └───────────┘                          └───────────────┘
```

## 3. Network Layer

### 3.1 Transport

**Responsibility**: UDP socket management, peer tracking, packet framing, reliability.

**Wire format** (documented in [`wire-format-spec.md`](wire-format-spec.md)):
- AlbyRules! XOR cipher (key constant, applied to all payloads)
- Direction byte (0x01=server, 0x02=client, 0xFF=init)
- Message count byte
- Per-message framing: ACK (0x01), Reliable (0x32), keepalive, fragmentation
- Reliable delivery: sequence numbers, ACK tracking, retransmit

**Reimplementation scope**: ~1,500 LOC
- Socket bind/listen on port 22101
- Peer array (max 16 peers, connection state machine)
- Send/receive queues per peer
- Reliable message tracking (seq numbers, ACKs, retransmit timer)
- Fragment reassembly (messages > ~400 bytes)

**Mod extension points:**
- Custom transport hooks (packet logging, rate limiting, ban lists)
- Configurable port, max peers
- Future: TCP fallback, NAT traversal

### 3.2 Protocol Codec

**Responsibility**: Serialize/deserialize game data to/from wire format.

**Stream primitives** (from [`wire-format-spec.md`](wire-format-spec.md)):
- WriteByte/ReadByte, WriteShort/ReadShort, WriteInt32/ReadInt32, WriteFloat/ReadFloat
- WriteBit/ReadBit (bit-packing into shared bytes)
- Compressed types: LogFloat16, DeltaVector3, DeltaQuaternion

**Reimplementation scope**: ~500 LOC
- TGBufferStream equivalent (position-tracked byte buffer)
- Bit-packing state machine (bookmark position, pack/unpack booleans)
- Compressed float/vector/quaternion codecs (formulas documented)

**Mod extension points:**
- Custom serialization for new message types
- Compression tuning (precision vs bandwidth)

### 3.3 GameSpy LAN Discovery

**Responsibility**: Respond to LAN browser queries so clients can find the server.

**Protocol**: Standard GameSpy QR (query/response) over UDP.
- Heartbeat broadcast (server announces presence)
- Query handler (client asks for server info: name, map, players, etc.)
- Response builder (key-value pairs in GameSpy format)

**Reimplementation scope**: ~300 LOC (well-documented protocol)

**Mod extension points:**
- Custom server info fields (mod name, version, custom rules)

### 3.4 Master Server (Internet Play)

**Responsibility**: Register with GameSpy-compatible master servers for internet game discovery.

**Protocol**: Standard GameSpy heartbeat/query protocol, compatible with modern community-maintained replacements (333networks, OpenSpy, etc.).
- Heartbeat: periodic UDP to master server (configurable URL/IP)
- Query response: same format as LAN discovery
- NAT-friendly: master server facilitates client discovery

**Reimplementation scope**: ~400 LOC (heartbeat sender + master server query handler)

**Server config**:
```toml
[network]
port = 22101
lan_discovery = true

[network.master_server]
enabled = true
address = "master.333networks.com"
port = 28900
heartbeat_interval = 60  # seconds
```

**Mod extension points:**
- Multiple master server registrations
- Custom server metadata for master server listings

## 4. Checksum Validation Layer

### 4.1 Hash Algorithms

Two custom algorithms, both fully verified from behavioral analysis:

**StringHash**:
- 4-lane Pearson hash using 4x256-byte substitution tables (1,024 bytes, extracted via hash manifest tool)
- Input: directory name or filename string
- Output: u32 identifier
- Purpose: Match directories/files between client and server by name

```c
uint32_t StringHash(const char *str) {
    uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0;
    while (*str) {
        uint8_t c = (uint8_t)*str++;
        h0 = TABLE_0[c ^ h0];
        h1 = TABLE_1[c ^ h1];
        h2 = TABLE_2[c ^ h2];
        h3 = TABLE_3[c ^ h3];
    }
    return (h0 << 24) | (h1 << 16) | (h2 << 8) | h3;
}
```

**FileHash**:
- Rotate-XOR over file contents: `hash = (hash ^ dword[i]) ROL 1`
- Deliberately skips DWORD index 1 (bytes 4-7 = .pyc modification timestamp)
- Remaining bytes (file_size % 4) use MOVSX sign-extension before XOR
- Input: file contents
- Output: u32 content hash
- Purpose: Verify file integrity (same bytecode = same hash regardless of compile time)

**Implementation**: Both algorithms are deterministic and trivial (~50 LOC total). The 1,024-byte lookup table is extracted once via the hash manifest tool and stored as a constant array.

### 4.2 Hash Manifest System

**Core concept**: The server doesn't need game files. It needs precomputed hash values.

**Manifest format** (JSON):
```json
{
  "name": "Star Trek: Bridge Commander 1.1",
  "version_string": "60",
  "version_string_hash": "0x7E0CE243",
  "directories": [
    {
      "index": 0,
      "path": "scripts/",
      "filter": "App.pyc",
      "recursive": false,
      "dir_name_hash": "0x...",
      "files": [
        {
          "filename": "App.pyc",
          "name_hash": "0x...",
          "content_hash": "0x..."
        }
      ],
      "subdirs": []
    },
    {
      "index": 2,
      "path": "scripts/ships/",
      "filter": "*.pyc",
      "recursive": true,
      "dir_name_hash": "0x...",
      "files": [ "..." ],
      "subdirs": [
        {
          "name": "Hardpoints",
          "name_hash": "0x...",
          "files": [ "..." ]
        }
      ]
    }
  ]
}
```

**Manifest generation tool**: A Python 3 CLI that:
1. Walks a game install's checksummed directories
2. Computes StringHash for every directory/filename
3. Computes FileHash for every .pyc file
4. Computes the version string hash
5. Outputs a manifest JSON

**Server config** selects one or more valid manifests:
```toml
[checksum]
# Accept clients matching ANY of these manifests
manifests = [
  "manifests/vanilla-1.1.json",
  "manifests/kobayashi-maru-1.0.json",
  "manifests/custom-ships-v3.json"
]

# Additional mod directories to request checksums for
# (beyond the stock 4 directories)
extra_directories = [
  { path = "scripts/Custom/MyMod/", filter = "*.pyc", recursive = true }
]
```

### 4.3 Validation Flow

1. Client connects → server sends 4x opcode 0x20 (checksum requests)
2. Client hashes its local files → sends 4x opcode 0x21 (hash trees)
3. Server walks each response tree, comparing against active manifest(s):
   - Version string hash checked first (index 0 only) — reject on mismatch
   - Directory name hashes compared
   - File name hashes compared (sorted, deterministic order)
   - File content hashes compared
4. All match → fire ChecksumComplete, send Settings (0x00) + GameInit (0x01)
5. Any mismatch → send 0x22 (file mismatch) or 0x23 (version mismatch) with failing filename

**Mod extension points:**
- Multiple valid manifests (accept vanilla OR specific mod)
- Additional checksum directories beyond the stock 4
- Whitelist/blacklist specific files
- Custom validation hooks (e.g., "require mod X version >= 2.0")

## 5. Game State Engine

### 5.1 Object Model

**Responsibility**: Track all game objects (ships, projectiles, explosions) and their state.

**Ship object state** (from StateUpdate analysis in [`wire-format-spec.md`](wire-format-spec.md)):
```
Ship:
  object_id: u32          # Player N base = 0x3FFFFFFF + N * 0x40000
  position: Vector3       # World-space position
  orientation: Quaternion  # Rotation
  velocity: Vector3       # Current speed vector
  forward_speed: float    # Scalar speed (sent as compressed float16)
  hull_health: float      # 0.0-1.0
  subsystems[33]:         # Per-subsystem health (round-robin serialized)
    type: SubsystemType   # 14 types (Hull, Shields, Sensors, etc.)
    health: float         # 0.0-1.0
    enabled: bool         # Online/offline
  weapons[]:
    type: WeaponType      # Phaser, torpedo, pulse
    target_id: u32        # Current target object
    firing: bool
```

**Object ID allocation**: Player N gets IDs starting at `0x3FFFFFFF + N * 0x40000` (262,143 IDs per player). Extract player slot: `(objID - 0x3FFFFFFF) >> 18`.

**Reimplementation scope**: ~2,000 LOC
- Object registry (create, destroy, lookup by ID)
- Per-object state struct with dirty-flag tracking
- Player slot management (max 16)

**Mod extension points:**
- Custom object types (space stations, asteroids, deployable mines)
- Custom subsystem types beyond the stock 14
- Custom properties on objects (shield facings, armor plating, crew count)

### 5.2 StateUpdate Generation

**Responsibility**: Serialize changed object state and send to clients.

**Wire format** (from [`wire-format-spec.md`](wire-format-spec.md), Flag 0x1C):
- Dirty-flag byte: which fields changed since last update
  - Bit 0 (0x01): Object header (create/destroy)
  - Bit 1 (0x02): Delta position
  - Bit 2 (0x04): Forward direction
  - Bit 3 (0x08): Up direction
  - Bit 4 (0x10): Speed
  - Bit 5 (0x20): Subsystem states (round-robin, S→C only)
  - Bit 7 (0x80): Weapon status (C→S only)
- Compressed types reduce bandwidth:
  - Positions: delta-encoded Vector3 (3 bytes when small changes)
  - Directions: compressed Quaternion (6 bytes)
  - Speed: LogFloat16 (2 bytes)
  - Subsystem health: 1 byte per subsystem (0-255 mapped to 0.0-1.0)

**Reimplementation scope**: ~1,500 LOC
- Dirty-flag tracker per object per client
- Round-robin subsystem serializer (cycle through 33 subsystems, N per tick)
- Compressed type encoders (LogFloat16, DeltaVector3, DeltaQuaternion)

**Mod extension points:**
- Custom dirty flags for new state fields
- Configurable update rate per object type
- Priority-based serialization (nearby objects update more often)

### 5.3 Opcode Handlers

**Responsibility**: Process incoming game messages and generate responses.

All 28 game opcodes (0x00-0x2A) plus 8 Python messages (0x2C-0x39):

**Server-generated (S→C):**

| Opcode | Name | What OpenBC Must Generate |
|--------|------|---------------------------|
| 0x00 | Settings | Game config (time, map, slot assignment) |
| 0x01 | GameInit | Single byte, triggers client game start |
| 0x03 | ObjCreateTeam | Ship spawn with team assignment |
| 0x14 | DestroyObject | Object death notification |
| 0x15 | CollisionEffect | Collision damage visual effect |
| 0x29 | Explosion | Explosion damage notification |
| 0x2A | NewPlayerInGame | Player join handshake |
| 0x36 | SCORE_CHANGE | Score delta (Python message) |
| 0x37 | SCORE_MESSAGE | Full score sync (Python message) |
| 0x38 | END_GAME | Game over (Python message) |

**Client-generated (C→S, server must handle):**

| Opcode | Name | What OpenBC Must Process |
|--------|------|--------------------------|
| 0x06 | PythonEvent | Primary event forwarding (relay to all) |
| 0x07 | StartFiring | Weapon fire begin |
| 0x08 | StopFiring | Weapon fire end |
| 0x0A | SubsysStatus | Subsystem toggle (shields up/down) |
| 0x0E | StartCloak | Cloak engage |
| 0x12 | SetPhaserLevel | Phaser power level |
| 0x19 | TorpedoFire | Torpedo launch |
| 0x1A | BeamFire | Beam weapon hit |
| 0x1C | StateUpdate | Position/orientation/speed from client |
| 0x2C | CHAT_MESSAGE | Chat relay (Python message) |

**Reimplementation scope**: ~3,000 LOC (41 handlers, most are relay-to-all-peers)

**Mod extension points:**
- Custom opcode handlers (new game mechanics)
- Event filters (block/modify specific events before relay)
- Custom game modes via handler composition
- Server-side anti-cheat hooks (validate client claims)

## 6. Physics Simulation

### 6.1 Movement

**Responsibility**: Update ship positions based on velocity, handle warp/impulse.

**Simplified model** (sufficient for gameplay):
- Ships have position, orientation, velocity, max speed
- Impulse: linear acceleration to target speed
- Warp: instant speed multiplier (client handles visual effect)
- Turn rate: configurable per ship class

**Reimplementation scope**: ~1,000 LOC (Euler integration, no need for full rigid body sim)

**Mod extension points:**
- Custom movement models per ship class
- Inertia/mass simulation (optional realism mode)
- Custom warp mechanics (charge time, cooldown)

### 6.2 Collision Detection

**Responsibility**: Detect when ships overlap and trigger damage.

**Two-phase approach** (broad + narrow):
- **Broad phase**: Bounding sphere test per ship class (precomputed radius from NIF geometry, stored in data registry). `distance(A, B) < radius_A + radius_B` filters obvious non-collisions cheaply.
- **Narrow phase**: Mesh-accurate collision using convex hull data extracted from NIF model geometry. A build-time tool parses NIF files and generates collision mesh data (convex hulls) stored in the data registry alongside ship definitions.

**Stock BC uses**: NetImmerse proximity manager with mesh-accurate collision. OpenBC matches this fidelity.

**Reimplementation scope**: ~2,500 LOC (broad-phase sphere + narrow-phase convex hull, NIF collision mesh extractor tool)

**Mod extension points:**
- Custom collision shapes per ship (auto-generated from NIF or hand-authored)
- Collision layers (allies don't collide, etc.)
- Custom collision response (bounce, grapple, tractor lock)

### 6.3 Damage System

**Responsibility**: Apply damage from collisions, weapons, and explosions.

**Formulas** (documented in [`damage-system.md`](damage-system.md) from behavioral analysis):
- Collision damage: proportional to relative velocity and ship mass
- Weapon damage: defined per weapon type in data registry
- Subsystem distribution: damage applied to subsystems based on proximity to impact point
- Shield absorption: shields reduce incoming damage by their health percentage

**Reimplementation scope**: ~800 LOC (formulas are known and documented)

**Mod extension points:**
- Custom damage formulas per weapon type
- Shield facing mechanics (forward/aft/port/starboard shields)
- Armor types and resistances
- Subsystem damage cascading (engine damage reduces speed, etc.)
- Custom death/destruction effects

## 7. Data Registry

### 7.1 Ship Registry

**Replaces**: 51 `ships/*.py` + 51 `ships/Hardpoints/*.py` (102 files → 1 data file)

```toml
# ships.toml — Ship class definitions

[ships.sovereign]
name = "USS Sovereign"
species_id = 3
bounding_radius = 245.0
mass = 3500000.0
max_impulse_speed = 12.0
max_warp_speed = 180.0
turn_rate = 0.8
hull_strength = 1.0

[ships.sovereign.subsystems]
shields = { count = 6, strength = 1.0 }
hull = { count = 1, strength = 1.0 }
sensors = { count = 1, strength = 1.0 }
impulse_engines = { count = 4, strength = 1.0 }
warp_core = { count = 1, strength = 1.0 }

[ships.sovereign.weapons.phaser_1]
type = "phaser"
position = [0.0, 5.0, 120.0]
damage = 0.15
range = 500.0
arc = 180.0

[ships.sovereign.weapons.torpedo_1]
type = "torpedo"
position = [0.0, -2.0, 130.0]
damage = 0.35
speed = 80.0
```

**Mod extension**: Mod packs add new `[ships.custom_warbird]` entries. No file replacement needed.

### 7.2 Map Registry

**Replaces**: 265 `Systems/*.py` files → structured data

```toml
# maps.toml — Star system/map definitions

[maps.multi1]
name = "Vesuvi System"
display_name = "Vesuvi"
spawn_points = [
  { position = [0, 0, 500], rotation = [0, 0, 0, 1] },
  { position = [1000, 0, -500], rotation = [0, 1, 0, 0] },
]
```

### 7.3 Game Rules

**Replaces**: Hardcoded constants in mission scripts → configurable

```toml
# rules.toml — Game mode configuration

[rules.deathmatch]
frag_limit = 10
time_limit = 600        # seconds
respawn_delay = 5.0
friendly_fire = true
collision_damage = true

[rules.team_deathmatch]
inherits = "deathmatch"
teams = 2
team_frag_limit = 25
friendly_fire = false
```

**Mod extension**: Custom game modes are just new `[rules.my_mode]` sections.

## 8. Mod System

### 8.1 Mod Pack Structure

```
my-mod/
  manifest.json          # Hash manifest (generated by tool)
  ships.toml             # Additional/modified ship definitions
  maps.toml              # Additional/modified maps
  rules.toml             # Custom game rules
  README.md              # Mod description
```

### 8.2 Mod Loading

Server config specifies active mods:

```toml
# server.toml

[server]
name = "OpenBC Deathmatch"
port = 22101
max_players = 8

[mods]
active = ["vanilla-1.1", "custom-ships-v3"]

# Load order: later mods override earlier ones
# Ship/map/rules from custom-ships-v3 override vanilla-1.1
```

### 8.3 Client Validation

When a mod changes checksummed files (ships/*.pyc), the mod's manifest must include the new hashes. The server validates that connecting clients have the correct mod installed:

1. Server loads mod manifest (contains expected hashes for modified files)
2. Client connects → server sends checksum requests
3. Client responds with hashes of its local files
4. Server compares against mod manifest
5. Match → client has the mod installed, allow connection
6. Mismatch → reject with clear error ("Requires Custom Ships v3")

Mods that only affect `scripts/Custom/` (the checksum-exempt directory) need no manifest — any client can connect.

## 9. Migration Path

### From Current Proxy to Standalone OpenBC

The transition is incremental. Each phase produces a working server:

**Phase A: Hash Manifest Tool** (immediate)
- Extract lookup tables via hash manifest tool (one-time, 1,024 bytes)
- Python 3 reimplementation of StringHash + FileHash
- CLI tool: `openbc-hash generate --game-dir /path/to/bc/ --output vanilla-1.1.json`
- CLI tool: `openbc-hash verify --manifest vanilla-1.1.json --game-dir /path/to/bc/`
- Deliverable: standalone tool, no server changes needed

**Phase B: Protocol Library** (foundation)
- Clean-room AlbyRules! cipher
- TGBufferStream equivalent (serialize/deserialize)
- Compressed type codecs (LogFloat16, DeltaVector3, DeltaQuaternion)
- Deliverable: library that can decode/encode any BC packet

**Phase C: Lobby Server** (first playable)
- UDP socket + peer management
- Checksum validation via manifests
- Settings (0x00) + GameInit (0x01) generation
- Chat relay (0x2C, 0x2D)
- Scoring (0x36, 0x37)
- GameSpy LAN response
- Deliverable: clients connect, see each other, chat works, no gameplay

**Phase D: Relay Server** (basic gameplay)
- StateUpdate parsing (C→S) and relay (S→C)
- Position/orientation tracking
- Weapon fire relay (0x07, 0x08, 0x19, 0x1A)
- Object creation (0x03) and destruction (0x14)
- Ship data registry (ships.toml)
- Deliverable: clients fly around and shoot, server relays everything

**Phase E: Simulation Server** (full gameplay)
- Server-authoritative physics (position, velocity, collision)
- Damage system (collision + weapon + explosion)
- Subsystem state tracking and round-robin serialization
- Game rules engine (frag limit, time limit, respawn)
- Deliverable: fully functional multiplayer, standalone server

### Coexistence

During migration, both servers can operate:
- **Proxy server** (current): runs game executable, full fidelity, proven stable
- **OpenBC server**: standalone, mod-extensible, growing feature set

Clients can connect to either — they speak the same protocol.

## 10. Technology Choices

| Component | Recommendation | Rationale |
|-----------|---------------|-----------|
| Language | **C** (core) + **Python 3** (tooling, config) | Matches existing proxy codebase; C for performance-critical paths |
| Build | **Make** (existing) | Already works, cross-compiles from WSL2 |
| Config format | **TOML** | Human-readable, supports nested tables, widely supported |
| Manifest format | **JSON** | Machine-generated, easy to validate, tool-friendly |
| Physics | **Custom** (Euler integration + mesh-accurate collision) | Matches original BC fidelity; convex hulls from NIF geometry |
| Networking | **Winsock UDP** (Win32) | Must match client platform; could abstract for Linux later |
| Collision meshes | **NIF parser** (build-time tool) | Extract convex hulls from ship NIF models |

## 11. Resolved Design Decisions

1. **Version string**: Identified as `"60"`. Hash: `StringHash("60") = 0x7E0CE243`. Used as version gate in checksum exchange. **RESOLVED** -- implemented in manifest system.

2. **StateUpdate direction split**: Server sends subsystem health (flag 0x20), client sends weapon status (flag 0x80). Verified as mutually exclusive by direction in multiplayer (10,459 C→S packets all use 0x80, 19,997 S→C packets all use 0x20). This is enforced by the IsMultiplayer flag differing between client and server code paths. **RESOLVED** -- documented fact, no design choice needed.

3. **Mesh-accurate collision**: **YES.** OpenBC will implement mesh-accurate collisions by extracting convex hull data from NIF model geometry. Bounding spheres are used as a broad-phase pass, with NIF-derived collision meshes for narrow-phase. The data registry includes collision mesh references per ship class, not just radius. This requires a NIF geometry parser as part of the asset pipeline tooling.

4. **Internet play**: **YES.** OpenBC will implement the GameSpy master server protocol for internet game discovery, compatible with modern drop-in replacement services like 333networks and other community-maintained GameSpy reimplementations. The server will support both LAN broadcast discovery and master server registration/heartbeat. Master server URL is configurable.

## 12. Files Referenced

| File | Contains |
|------|----------|
| [`wire-format-spec.md`](wire-format-spec.md) | Complete wire format (transport, opcodes, StateUpdate, compressed types) |
| [`multiplayer-flow.md`](multiplayer-flow.md) | Full join flow (connect → checksum → settings → ship select → play) |
| [`network-protocol.md`](network-protocol.md) | Protocol architecture, dispatcher routing |
| [`damage-system.md`](damage-system.md) | Damage pipeline (collision, weapon, explosion paths + formulas) |
| `src/proxy/ddraw_main/packet_trace_and_decode.inc.c` | AlbyRules! cipher implementation |
| `src/proxy/ddraw_main/game_loop_and_bootstrap.inc.c` | Current bootstrap phases + game loop |
| `src/scripts/Custom/DedicatedServer.py` | SWIG API usage (48 functions = minimal engine contract) |
| Extracted via hash manifest tool | StringHash 4x256-byte lookup tables (1,024 bytes) |
| Verified via manifest tool | FileHash algorithm (rotate-XOR, skip bytes 4-7) |
| Verified via manifest tool | StringHash algorithm (4-lane Pearson hash) |
| Verified via protocol capture | Version gate string = `"60"` |
