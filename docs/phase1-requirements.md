# OpenBC Phase 1: Standalone Dedicated Server - Requirements Document

## Document Status
- **Created**: 2026-02-07
- **Revised**: 2026-02-15 (complete rewrite: standalone C server architecture, no Python/SWIG/flecs)
- **Architecture**: RFC at [phase1-implementation-plan.md](phase1-implementation-plan.md)
- **Wire protocol**: [phase1-verified-protocol.md](phase1-verified-protocol.md)
- **Engine internals**: [phase1-engine-architecture.md](phase1-engine-architecture.md)

---

## 1. Executive Summary

Phase 1 delivers a **standalone, open-source dedicated server** for Star Trek: Bridge Commander multiplayer. It is a clean-room C implementation that speaks the BC 1.1 wire protocol -- no original binaries, no Python scripts, no game data files required on the server.

The server replaces both the stbc.exe game engine and its Python scripting layer with:
- **Data-driven configuration** via TOML files (ships, maps, game rules)
- **Precomputed hash manifests** (JSON) for checksum validation without game files
- **A mod system** where every data layer has extension points
- **Mesh-accurate collision** data extracted from NIF geometry at build time

Development proceeds in five phases (A through E), each producing a working deliverable. Phase A is a standalone hash manifest tool. Phase E is a fully simulation-authoritative multiplayer server with no stbc.exe dependency.

### Success Criteria

A vanilla, unmodified Star Trek: Bridge Commander 1.1 client can:
1. Discover the OpenBC server via LAN browser or internet master server list
2. Connect to the server and receive a player slot assignment
3. Complete the 4-round checksum exchange (validated against precomputed manifests)
4. Receive game settings and reach the ship selection screen
5. Select a ship and have it broadcast to all other connected clients
6. **Play a complete multiplayer match** (movement, weapons, combat functional)
7. **Chat with other players** during lobby and gameplay
8. Experience **game end** (time limit, frag limit) and **restart**
9. Up to 16 clients connected simultaneously
10. Remain connected without spurious timeouts
11. Disconnect cleanly with slot recovery
12. **Reconnect mid-game** (late join with game state sync)
13. **Connect via internet** through GameSpy-compatible master server listings

Server operators can:
14. Configure game rules, ship roster, and maps via TOML files without scripting
15. Load mod packs that add ships, maps, and game modes as TOML data
16. Generate hash manifests for any mod combination using a CLI tool
17. Run the server on Linux or Windows with no GPU or display

---

## 2. Scope

### 2.1 In Scope

| Subsystem | Description |
|-----------|-------------|
| **Network transport** | Raw UDP sockets, AlbyRules cipher, reliable delivery, fragmentation |
| **Protocol codec** | TGBufferStream equivalent, compressed types (LogFloat16, DeltaVector3, DeltaQuaternion) |
| **GameSpy LAN discovery** | Query/response handler for server browser visibility |
| **Master server registration** | GameSpy heartbeat protocol for internet play (333networks, OpenSpy, etc.) |
| **Checksum validation** | Manifest-based 4-round file hash verification (opcodes 0x20-0x28) |
| **Hash manifest tool** | CLI to generate/verify JSON hash manifests from a BC install |
| **Player slot management** | 16-slot array with connect/disconnect lifecycle |
| **Opcode handlers** | 28 active game opcodes (from jump table entries 0x00-0x2A, many DEFAULT/unhandled) plus 8 Python messages (0x2C-0x39) |
| **Message relay** | Clone-and-forward of game messages between connected peers (Phases C-D) |
| **Ship object model** | Data-driven ship state tracked per player (from ships.toml) |
| **Data registry** | TOML-based ship, map, and game rule definitions |
| **Game lifecycle** | State machine: Lobby -> Ship Select -> Playing -> Game Over -> Restart |
| **Chat relay** | Text message forwarding (regular + team chat) |
| **Peer groups** | Named peer groups ("NoMe" = all except self) for targeted sends |
| **Physics simulation** | Server-authoritative movement, collision detection, damage (Phase E) |
| **Collision meshes** | NIF convex hull extraction tool for mesh-accurate collision data |
| **Mod system** | TOML data packs with manifest-based client validation |
| **Build system** | C + Make, cross-compile from WSL2 |

### 2.2 Out of Scope

| Subsystem | Phase | Rationale |
|-----------|-------|-----------|
| Rendering (bgfx, NIF visual loading) | Future | Server is headless |
| Audio (miniaudio) | Future | Server is headless |
| UI (RmlUi) | Future | Server is headless |
| Input (SDL3 input events) | Future | Server is headless |
| Scene graph (NiNode hierarchy) | Future | No rendering |
| Python embedding | Never (Phase 1) | Replaced by data-driven design |
| SWIG API compatibility layer | Never (Phase 1) | Replaced by native C opcode handlers |
| Original BC script execution | Never (Phase 1) | Replaced by TOML configuration |
| AI systems / Coop mode | Future | Requires behavioral AI |
| Single-player / bridge crew | Future | Client-only |
| NAT traversal / TCP fallback | Future | UDP-only for now |

### 2.3 Architecture vs. Previous Design

The previous requirements called for embedding Python 3.x, reimplementing ~595 SWIG API functions, and running original BC multiplayer scripts. That architecture has been replaced:

| Concern | Old (Python/SWIG) | New (Standalone C) |
|---------|-------------------|-------------------|
| Ship definitions | Execute ship + hardpoint Python scripts | Read ships.toml |
| Map definitions | Execute map system Python scripts | Read maps.toml |
| Game rules | Hardcoded in mission scripts | Read rules.toml |
| Checksum validation | Hash game files at runtime | Compare against precomputed JSON manifests |
| Scoring | Python ObjectKilledHandler | C opcode handler for kill events |
| Chat relay | Python message handler | C opcode handler |
| Game lifecycle | Python state machine | C state machine |
| Mod support | Swap script files | Layer TOML data packs |
| Build dependencies | Python 3.8+, CMake, flecs | C compiler, Make |

---

## 3. Functional Requirements

### REQ-NET: Network Transport

#### REQ-NET-01: Raw UDP Transport
The server MUST use raw UDP sockets (NOT ENet or any networking library) to communicate with vanilla BC clients. The wire format is a custom protocol incompatible with standard libraries.

#### REQ-NET-02: Shared UDP Socket
Single UDP socket (default port 22101 / 0x5655) shared between GameSpy and game protocol via peek-based first-byte demultiplexing: `\` prefix = GameSpy query (plaintext), binary = TGNetwork packet (AlbyRules encrypted).

#### REQ-NET-03: AlbyRules Cipher
All game packets (not GameSpy) MUST be encrypted/decrypted with the AlbyRules XOR stream cipher:
```
Key: "AlbyRules!" = { 0x41, 0x6C, 0x62, 0x79, 0x52, 0x75, 0x6C, 0x65, 0x73, 0x21 }
Encrypt/Decrypt (symmetric):
  for i in 0..packet_len:
      packet[i] ^= key[i % 10]
```

#### REQ-NET-04: Packet Wire Format
After AlbyRules decryption:
```
Offset  Size  Field
------  ----  -----
0       1     direction     (0x01=from server, 0x02=from client, 0xFF=init handshake)
1       1     msg_count     (number of transport messages in this packet)
2+      var   messages      (sequence of self-describing transport messages)
```
Max 254 messages per packet. Default max packet size 512 bytes. See [phase1-verified-protocol.md](phase1-verified-protocol.md) for complete wire format.

#### REQ-NET-05: Transport Message Types
| Type | Name | Format | Size | Confidence |
|------|------|--------|------|------------|
| 0x00 | Connection | `[0x00][totalLen:1][data...]` | variable | Verified (observed in captures) |
| 0x01 | ACK | `[0x01][seq:1][0x00][flags:1]` | 4 bytes fixed | Verified (observed in captures) |
| 0x02 | Internal | `[0x02][totalLen:1][data...]` | padding/discard | Inferred from code, not observed |
| 0x03 | Data | `[0x03][totalLen:1][payload...]` | variable, game opcodes | Inferred from code, not observed |
| 0x04 | Disconnect | `[0x04][totalLen:1][data...]` | variable | Inferred from code, not observed |
| 0x05 | Keepalive | `[0x05][totalLen:1]` | 2 bytes | Inferred from code, not observed |
| 0x32 | Reliable Data | `[0x32][totalLen:1][flags:1][seq_hi:1][seq_lo:1][payload...]` | variable | Verified (observed in captures) |

Note: Types 0x00, 0x01, and 0x32 are confirmed via packet captures. Types 0x02-0x05 are inferred from behavioral analysis but have not been individually observed in network traces.

#### REQ-NET-06: Three-Tier Send Queues
Per peer: Priority Reliable (ACKs, retried 8x), Reliable (guaranteed, 360s timeout), Unreliable (fire-and-forget). See [phase1-verified-protocol.md](phase1-verified-protocol.md) Section 3.

#### REQ-NET-07: Reliable Delivery
Sequence numbering (u16 wrapping), ACK generation, retry with timeout, disconnect after 8 priority retries, sequence window validation (0x4000 range).

#### REQ-NET-08: Fragment Reassembly
Messages exceeding ~400 bytes MUST be split into fragments using the 0x32 reliable data format with fragment flags:
```
Flags byte: bit 0 (0x01) = more fragments follow, bit 5 (0x20) = fragmented message
First fragment:  [frag_idx:u8][total_frags:u8][inner_opcode:u8][payload...]
Subsequent:      [frag_idx:u8][continuation_data...]
```

#### REQ-NET-09: Connection Handshake
Accept connection messages (type 0x00) from unknown peers, create peer record, assign peer ID, begin checksum exchange.

#### REQ-NET-10: Connection State Machine
Server operates in HOST state (value 2). States: 4=DISCONNECTED, 2=HOST, 3=CLIENT (server never enters CLIENT state). Note: 2=HOST is counterintuitive but matches the original.

#### REQ-NET-11: Peer Management
Sorted array, binary-searched by peer ID. Max 16 simultaneous peers. Creation on connect, removal on disconnect/timeout.

#### REQ-NET-12: Timeout Handling
Reliable message timeout: 360s. Disconnect timeout: 45s. Priority max retries: 8. Keepalive interval: configurable (default matches original).

### REQ-CODEC: Protocol Codec

#### REQ-CODEC-01: Stream Primitives
The server MUST implement a TGBufferStream equivalent providing:
- WriteByte/ReadByte, WriteShort/ReadShort, WriteInt32/ReadInt32, WriteFloat/ReadFloat
- WriteBit/ReadBit (bit-packing into shared bytes with bookmark position)

#### REQ-CODEC-02: Compressed Types
The server MUST implement compressed type codecs for bandwidth-efficient state updates:
- **LogFloat16**: 2-byte logarithmic float encoding for speed values
- **DeltaVector3**: 3-byte delta-encoded position (when changes are small)
- **DeltaQuaternion**: 6-byte compressed rotation
- **Subsystem health**: 1 byte (0-255 mapped to 0.0-1.0)

#### REQ-CODEC-03: Bit-Packing
Boolean fields MUST be packed into shared bytes using bookmark-based bit-packing, matching the original TGBufferStream behavior.

### REQ-GS: GameSpy Discovery

#### REQ-GS-01: LAN Query Response
Respond to `\basic\`, `\status\`, `\info\` queries with backslash-delimited key-value pairs including: hostname, numplayers, maxplayers, mapname, gametype, hostport.

#### REQ-GS-02: LAN Broadcast Discovery
Server MUST be discoverable by vanilla BC clients scanning for LAN games on the same subnet.

#### REQ-GS-03: Master Server Registration
The server MUST support registering with GameSpy-compatible master servers (333networks, OpenSpy, etc.) for internet game discovery via periodic UDP heartbeat.

#### REQ-GS-04: Master Server Configuration
```toml
[network.master_server]
enabled = true
address = "master.333networks.com"
port = 28900
heartbeat_interval = 60  # seconds
```
Multiple master server registrations MUST be supported.

#### REQ-GS-05: Custom Server Metadata
Server info fields MUST include mod name and version when mods are active, so clients can identify required mods from the server browser.

### REQ-CHK: Checksum Validation

#### REQ-CHK-01: Four-Round Verification
Sequential checksum requests (opcode 0x20) for four directory scopes:

| Round | Directory | Filter | Recursive |
|-------|-----------|--------|-----------|
| 0 | `scripts/` | `App.pyc` | No (single file) |
| 1 | `scripts/` | `Autoexec.pyc` | No (single file) |
| 2 | `scripts/ships/` | `*.pyc` | Yes |
| 3 | `scripts/mainmenu/` | `*.pyc` | No |

`scripts/Custom/` is EXEMPT from all checksum validation (mods install here).

Client responds with opcode 0x21 containing hash trees. Server compares against loaded manifest(s).

#### REQ-CHK-02: StringHash Algorithm
4-lane Pearson hash using four 256-byte substitution tables (1,024 bytes total, extracted via the hash manifest tool):
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
Used for: directory name hashing, filename hashing, version string hashing.

#### REQ-CHK-03: FileHash Algorithm
Rotate-XOR over file contents:
```c
uint32_t FileHash(const uint8_t *data, size_t len) {
    uint32_t hash = 0;
    const uint32_t *dwords = (const uint32_t *)data;
    size_t count = len / 4;
    for (size_t i = 0; i < count; i++) {
        if (i == 1) continue;  // Skip DWORD index 1 (bytes 4-7 = .pyc timestamp)
        hash ^= dwords[i];
        hash = (hash << 1) | (hash >> 31);  // ROL 1
    }
    // Remaining bytes (len % 4): MOVSX sign-extension before XOR
    size_t remainder = len % 4;
    if (remainder > 0) {
        // Sign-extend each remaining byte and XOR into hash
        const uint8_t *tail = data + (count * 4);
        for (size_t i = 0; i < remainder; i++) {
            int32_t extended = (int8_t)tail[i];  // MOVSX
            hash ^= (uint32_t)extended;
            hash = (hash << 1) | (hash >> 31);
        }
    }
    return hash;
}
```
Used for: .pyc file content verification. Deliberately skips bytes 4-7 (.pyc modification timestamp) so that the same bytecode produces the same hash regardless of compile time.

#### REQ-CHK-04: Version String Gate
The version string `"60"` has hash `StringHash("60") = 0x7E0CE243`. This is checked in the first checksum round (index 0). Version mismatch causes immediate rejection.

#### REQ-CHK-05: Hash Manifest Format
The server validates checksums against precomputed JSON manifests, NOT live file hashing:
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
    }
  ]
}
```

#### REQ-CHK-06: Manifest Validation Flow
1. Client connects -> server sends 4x opcode 0x20 (checksum requests)
2. Client hashes its local files -> sends 4x opcode 0x21 (hash trees)
3. Server walks each response tree, comparing against active manifest(s):
   - Version string hash checked first (index 0 only) -- reject on mismatch
   - Directory name hashes compared
   - File name hashes compared (sorted, deterministic order)
   - File content hashes compared
4. All match -> send Settings (0x00) + GameInit (0x01)
5. Any mismatch -> send 0x22 (file mismatch) or 0x23 (version mismatch) with failing filename

#### REQ-CHK-07: Multiple Manifests
The server MUST support loading multiple valid manifests and accepting a client if it matches ANY of them. This enables servers that accept both vanilla and specific mod configurations.

#### REQ-CHK-08: Skip Checksums Option
Config flag to bypass checksum verification entirely for development and testing.

### REQ-MANIFEST: Hash Manifest Tool

#### REQ-MANIFEST-01: Generation CLI
A standalone tool (Python 3 or C) that:
1. Walks a BC game install's checksummed directories
2. Computes StringHash for every directory name and filename
3. Computes FileHash for every .pyc file
4. Computes the version string hash
5. Outputs a JSON manifest file

```
openbc-hash generate --game-dir /path/to/bc/ --output vanilla-1.1.json
```

#### REQ-MANIFEST-02: Verification CLI
Verify a manifest against a game install:
```
openbc-hash verify --manifest vanilla-1.1.json --game-dir /path/to/bc/
```

#### REQ-MANIFEST-03: Lookup Table Source
The 1,024-byte StringHash lookup table is extracted once via the hash manifest tool and stored as a constant array in the tool's source code (4 tables x 256 bytes).

#### REQ-MANIFEST-04: Mod Manifest Generation
The tool MUST support generating manifests for modded installs, including additional directories beyond the stock 4 checksum scopes.

### REQ-OPC: Opcode Handlers

#### REQ-OPC-01: Server-Generated Opcodes
The server MUST generate the following opcodes:

| Opcode | Name | Purpose |
|--------|------|---------|
| 0x00 | Settings | Game config (time limit, frag limit, map, slot assignment) |
| 0x01 | GameInit | Single byte, triggers client game start |
| 0x03 | ObjCreateTeam | Ship spawn with team assignment |
| 0x04 | BootPlayer | Player rejection/kick |
| 0x14 | DestroyObject | Object death notification |
| 0x15 | CollisionEffect | Collision damage visual effect (Phase E) |
| 0x29 | Explosion | Explosion damage notification (Phase E) |
| 0x2A | NewPlayerInGame | Player join handshake completion |
| 0x36 | SCORE_CHANGE | Score delta (Python message format) |
| 0x37 | SCORE_MESSAGE | Full score sync (Python message format) |
| 0x38 | END_GAME | Game over notification (Python message format) |

#### REQ-OPC-02: Client-Handled Opcodes
The server MUST process the following incoming opcodes:

| Opcode | Name | Server Action |
|--------|------|---------------|
| 0x06 | PythonEvent | Relay to all peers |
| 0x07 | StartFiring | Relay (Phase D); process for damage (Phase E) |
| 0x08 | StopFiring | Relay (Phase D); process for damage (Phase E) |
| 0x0A | SubsysStatus | Relay + track subsystem state |
| 0x0E | StartCloak | Relay + track cloak state |
| 0x12 | SetPhaserLevel | Relay + track phaser power |
| 0x19 | TorpedoFire | Relay (Phase D); process for damage (Phase E) |
| 0x1A | BeamFire | Relay (Phase D); process for damage (Phase E) |
| 0x1C | StateUpdate | Parse position/orientation/speed, relay to peers |
| 0x2C | CHAT_MESSAGE | Relay to group "NoMe" |
| 0x2D | TEAM_CHAT_MESSAGE | Relay to teammates only |

#### REQ-OPC-03: Relay Opcodes
All remaining game opcodes MUST be relayed to all other connected peers as raw bytes without deserialization. The opcode space spans 0x00-0x2A with 28 active handlers -- the rest are unhandled. See [phase1-verified-protocol.md](phase1-verified-protocol.md) for the complete opcode table.

#### REQ-OPC-04: Python Message Opcodes
Python-level messages (opcode >= MAX_MESSAGE_TYPES = 0x2B) MUST be handled:

| Message | Opcode | Server Action |
|---------|--------|---------------|
| CHAT_MESSAGE | 0x2C | Relay to group "NoMe" |
| TEAM_CHAT_MESSAGE | 0x2D | Relay to teammates only |
| MISSION_INIT_MESSAGE | 0x35 | Server generates on player join |
| SCORE_CHANGE_MESSAGE | 0x36 | Server generates on kill (Phase E) |
| SCORE_MESSAGE | 0x37 | Server generates (full score sync) |
| END_GAME_MESSAGE | 0x38 | Server generates (game over broadcast) |
| RESTART_GAME_MESSAGE | 0x39 | Server generates (restart broadcast) |

### REQ-OBJ: Object Model

#### REQ-OBJ-01: Ship State Tracking
The server MUST track per-ship state:
```
Ship:
  object_id: u32          # Player N base = 0x3FFFFFFF + N * 0x40000
  ship_class: string      # Key into ships.toml
  team: u8                # Team assignment
  position: Vector3       # World-space position (from client StateUpdate)
  orientation: Quaternion  # Rotation (from client StateUpdate)
  velocity: Vector3       # Current speed vector
  forward_speed: float    # Scalar speed
  hull_health: float      # 0.0-1.0
  subsystems[]:           # Per-subsystem state
    type: SubsystemType
    health: float
    enabled: bool
  alive: bool             # Death state
```

#### REQ-OBJ-02: Object ID Allocation
Player N gets object IDs starting at `0x3FFFFFFF + N * 0x40000` (262,143 IDs per player). Extract player slot from object ID: `(objID - 0x3FFFFFFF) >> 18`.

#### REQ-OBJ-03: Ship Lifecycle
- **Create**: Server generates ObjCreateTeam (0x03) when client selects a ship
- **Death**: Mark dead on DestroyObject (0x14)
- **Respawn**: Create new object with new ID after respawn delay
- **Game restart**: Destroy all ship objects

#### REQ-OBJ-04: StateUpdate Processing
The server MUST parse incoming StateUpdate (0x1C) messages to track ship positions, and generate outgoing StateUpdates for all clients:
- Dirty-flag byte indicates which fields changed
- Compressed types used for bandwidth (DeltaVector3, DeltaQuaternion, LogFloat16)
- Subsystem states serialized round-robin (S->C only, flag 0x20)
- Weapon status (C->S only, flag 0x80)

### REQ-PLR: Player Management

#### REQ-PLR-01: 16-Slot Array
Player slots (max 16): each slot tracks active flag, peer ID, player object ID, team assignment, ship class selection, score.

#### REQ-PLR-02: Slot Assignment
First empty slot on connection. Send rejection message (opcode 0x04) if all slots full.

#### REQ-PLR-03: Disconnect Handling
On disconnect or timeout: clear slot, destroy player's ship object, notify remaining clients, free resources.

#### REQ-PLR-04: Peer Groups
The server MUST support named peer groups. At minimum the "NoMe" group (all peers except the server itself). `SendToGroup("NoMe", msg)` sends to all connected clients.

### REQ-LIFE: Game Lifecycle

#### REQ-LIFE-01: State Machine
The server MUST implement a game lifecycle state machine:

| State | Description | Relay Active | Accept Players |
|-------|-------------|--------------|---------------|
| LOBBY | Waiting for players, checksum exchange | No | Yes |
| SHIP_SELECT | Players choosing ships | Chat only | Yes |
| PLAYING | Active match | Full relay | Yes (late join) |
| GAME_OVER | Scores displayed | Chat only | No |
| RESTARTING | Cleanup, back to ship select | No | No |

#### REQ-LIFE-02: Game Start
Server sends Settings (0x00) with game parameters (time limit, frag limit, map name, player slot) followed by GameInit (0x01). Transitions to SHIP_SELECT on first client.

#### REQ-LIFE-03: Game End
Server sends END_GAME_MESSAGE (0x38) when time limit or frag limit is reached. Transitions to GAME_OVER.

#### REQ-LIFE-04: Game Restart
Server sends RESTART_GAME_MESSAGE (0x39) after GAME_OVER timeout. Destroys all objects. Transitions to SHIP_SELECT.

#### REQ-LIFE-05: Late Join
When a player joins mid-game: assign slot, send Settings + GameInit, send MISSION_INIT_MESSAGE (0x35) with current game parameters, transition player directly into PLAYING state.

### REQ-CHAT: Chat System

#### REQ-CHAT-01: Regular Chat
Client sends CHAT_MESSAGE (0x2C) to server. Server relays to all other clients (group "NoMe"). Wire format: `[opcode:1][senderSlot:1][padding:3][stringLen:2][string:N]`, reliable delivery.

#### REQ-CHAT-02: Team Chat
Client sends TEAM_CHAT_MESSAGE (0x2D) to server. Server relays only to players on the same team. Same wire format as regular chat.

### REQ-DATA: Data Registry

#### REQ-DATA-01: Ship Registry
Ship definitions loaded from TOML files. Values from verified hardpoint scripts:
```toml
[ships.sovereign]
name = "Sovereign"
species = "sovereign"              # Key into species.toml -> ID 102
mass = 100.0                       # Default (not in GlobalPropertyTemplates)
max_speed = 7.50                   # From hardpoint ImpulseEngineProperty
max_accel = 1.60
max_angular_velocity = 0.300
max_angular_accel = 0.150

[ships.birdofprey]
name = "Bird of Prey"
species = "birdofprey"             # Key into species.toml -> ID 401
mass = 75.0                        # From GlobalPropertyTemplates.py
max_speed = 6.20
max_accel = 2.50
max_angular_velocity = 0.500
max_angular_accel = 0.350
```

The server MUST load ship definitions at startup and use them for object creation, collision detection, and damage calculation. See [Data Registry Specification](phase1-api-surface.md) for the complete ship physics table and schema.

#### REQ-DATA-02: Map Registry
Map definitions loaded from TOML:
```toml
[maps.multi1]
name = "Vesuvi System"
display_name = "Vesuvi"
spawn_points = [
  { position = [0, 0, 500], rotation = [0, 0, 0, 1] },
  { position = [1000, 0, -500], rotation = [0, 1, 0, 0] },
]
```

#### REQ-DATA-03: Game Rules
Game mode configuration loaded from TOML:
```toml
[rules.deathmatch]
frag_limit = 10
time_limit = 600
respawn_delay = 5.0
friendly_fire = true
collision_damage = true

[rules.team_deathmatch]
inherits = "deathmatch"
teams = 2
team_frag_limit = 25
friendly_fire = false
```

Custom game modes are additional `[rules.*]` sections -- no code changes required.

#### REQ-DATA-04: Registry Layering
When multiple data sources are loaded (base + mods), later sources override earlier ones. A mod's `ships.toml` can add new ships or override existing ship definitions by key.

### REQ-PHYS: Physics Simulation (Phase E)

#### REQ-PHYS-01: Movement Model
Server-authoritative ship movement:
- Impulse: linear acceleration to target speed
- Warp: instant speed multiplier (client handles visual effect)
- Turn rate: configurable per ship class (from ships.toml)
- Integration: Euler integration at 30 Hz tick rate

#### REQ-PHYS-02: Broad-Phase Collision
Bounding sphere test per ship class using precomputed radius from ships.toml (derived from NIF geometry). `distance(A, B) < radius_A + radius_B` filters non-collisions cheaply.

#### REQ-PHYS-03: Narrow-Phase Collision
Mesh-accurate collision using convex hull data extracted from NIF model geometry by a build-time tool. Convex hull data stored in the data registry alongside ship definitions.

#### REQ-PHYS-04: NIF Collision Mesh Extractor
A build-time tool that parses NIF model files and generates convex hull collision data for each ship class. Output format is a compact binary or TOML representation of hull vertices suitable for GJK/SAT intersection tests.

#### REQ-PHYS-05: Damage System
Damage calculations (verified from behavioral observation):
- **Collision damage**: proportional to relative velocity and ship mass
- **Weapon damage**: defined per weapon type in ships.toml
- **Subsystem targeting**: damage applied to subsystems based on proximity to impact point
- **Shield absorption**: shields reduce incoming damage by their health percentage

#### REQ-PHYS-06: StateUpdate Generation
Server MUST generate outgoing StateUpdate (0x1C) messages at 30 Hz with:
- Dirty-flag byte tracking which fields changed
- Compressed position/orientation/speed using DeltaVector3, DeltaQuaternion, LogFloat16
- Round-robin subsystem health serialization (flag 0x20, S->C only)

### REQ-MOD: Mod System

#### REQ-MOD-01: Mod Pack Structure
```
my-mod/
  manifest.json          # Hash manifest for checksummed files
  ships.toml             # Additional/modified ship definitions
  maps.toml              # Additional/modified maps
  rules.toml             # Custom game rules
```

#### REQ-MOD-02: Mod Loading
Server config specifies active mods with load-order semantics:
```toml
[mods]
active = ["vanilla-1.1", "custom-ships-v3"]
# Later mods override earlier ones
```

#### REQ-MOD-03: Mod Client Validation
When a mod changes checksummed files (ships/*.pyc), the mod's manifest provides the expected hashes. The server validates that connecting clients have the correct mod installed. Mods affecting only non-checksummed directories need no manifest.

#### REQ-MOD-04: Extension Points
Every data layer MUST have mod extension points:
- Ships: add new `[ships.*]` entries or override existing ones
- Maps: add new `[maps.*]` entries
- Rules: add new `[rules.*]` game modes
- Network: custom transport hooks (packet logging, rate limiting, ban lists)
- Opcode handlers: custom handlers for new game mechanics
- Collision: custom collision shapes per ship

### REQ-CFG: Server Configuration

#### REQ-CFG-01: Server Config File
TOML-based server configuration:
```toml
[server]
name = "OpenBC Deathmatch"
port = 22101
max_players = 8
map = "multi1"
game_mode = "deathmatch"
tick_rate = 30

[network]
lan_discovery = true

[network.master_server]
enabled = true
address = "master.333networks.com"
port = 28900
heartbeat_interval = 60

[checksum]
skip = false
manifests = ["manifests/vanilla-1.1.json"]

[mods]
active = ["vanilla-1.1"]
```

#### REQ-CFG-02: Command-Line Overrides
Command-line arguments MUST override config file values:
```
--port=22101           --max-players=16       --name="OpenBC"
--map=multi1           --game-mode=deathmatch --skip-checksums
--time-limit=600       --frag-limit=20
```

### REQ-BLD: Build System

#### REQ-BLD-01: Language and Toolchain
C (C99 or C11), compiled with Make. No C++ dependency. No external library dependencies beyond standard C library and platform socket API (Winsock2 on Windows, POSIX sockets on Linux).

#### REQ-BLD-02: Platform Support
Primary: Windows (Win32, Winsock2). Secondary: Linux (POSIX sockets). Cross-compilation from WSL2 MUST work.

#### REQ-BLD-03: Build Targets
- `openbc-server`: the dedicated server binary
- `openbc-hash`: the hash manifest generation/verification tool
- `openbc-nif-collider`: the NIF collision mesh extraction tool (Phase E)

#### REQ-BLD-04: No GPU Dependencies
The server binary MUST have zero GPU, display, or graphics library dependencies.

#### REQ-BLD-05: Docker Support
Server MUST run in a Docker container for cloud deployment. Target image size < 50 MB (no Python runtime, no game data).

---

## 4. Non-Functional Requirements

### NFR-01: Tick Rate
30 Hz (33ms per tick), matching the original game's Windows timer.

### NFR-02: Performance
16 simultaneous connections with active gameplay. Target: < 5% CPU on a single modern core (the server has no Python interpreter or script execution overhead).

### NFR-03: Memory
Server footprint < 20 MB RAM (no Python runtime, no game files, just network state + ship data + manifests).

### NFR-04: Bandwidth
~5-10 KB/s per client (6 players), ~12-24 KB/s per client (16 players). Position updates unreliable, game logic reliable.

### NFR-05: Endianness
All protocol values little-endian regardless of host platform.

### NFR-06: Clean Build
`make` on supported platforms with zero manual setup beyond a C compiler.

### NFR-07: Startup Time
Server MUST be ready to accept connections within 1 second of launch (no script compilation, no file hashing, no game data loading).

### NFR-08: Zero Game Data
The server binary and its data files (TOML configs, JSON manifests) MUST contain zero copyrighted STBC material. All game-specific data is either precomputed hashes (non-reversible) or clean-room ship/map definitions derived from publicly available information.

---

## 5. Design Decisions

### DD-01: Raw UDP, Not ENet
ENet wire format is incompatible with vanilla BC clients. The custom TGNetwork protocol must be reimplemented from scratch.

### DD-02: Standalone C, Not Python/SWIG
The previous design embedded Python 3.x and reimplemented ~595 SWIG API functions to run original BC scripts. This was replaced with a standalone C server because:
- Eliminates ~40,000 LOC of compatibility layer code
- Removes Python 3.x runtime dependency (~30 MB)
- Removes Python 1.5.2 compatibility shim complexity
- Enables < 20 MB memory footprint vs. ~100 MB with Python
- Startup in < 1 second vs. ~5 seconds for Python init + script loading
- All game logic expressed as data (TOML) is simpler, more maintainable, and more moddable than reimplementing the script execution environment
- Server operators configure via TOML, not by editing Python scripts

### DD-03: Hash Manifests, Not Live Hashing
The server does not need game files. It needs precomputed hash values. Benefits:
- Zero copyrighted material on the server
- Faster checksum validation (compare precomputed values, not hash files on disk)
- Supports any mod combination via multiple manifest files
- Manifest generation is a one-time offline step, not a runtime dependency

### DD-04: Data-Driven Design
Ship stats, weapon configs, maps, and game rules live in TOML data files:
- Server operators can tune gameplay without programming
- Mods are TOML data packs, not code patches
- The data registry supports layered overrides (base + mod1 + mod2)
- Adding a new ship class requires zero code changes

### DD-05: Mesh-Accurate Collision
OpenBC implements mesh-accurate collisions matching the original NetImmerse proximity manager fidelity:
- **Broad phase**: bounding sphere (cheap filter)
- **Narrow phase**: convex hulls extracted from NIF geometry (accurate)
- Collision data generated by a build-time tool, stored in the data registry
- This matches original BC behavior where the NetImmerse engine performs mesh-level intersection tests

### DD-06: Phased Development (A through E)
Each phase produces a working, testable deliverable:
- **Phase A**: Hash manifest tool (standalone, no server)
- **Phase B**: Protocol codec library (encode/decode any BC packet)
- **Phase C**: Lobby server (connect, checksum, chat -- no gameplay)
- **Phase D**: Relay server (gameplay via message forwarding)
- **Phase E**: Simulation server (server-authoritative physics and damage)

### DD-07: Internet Play via GameSpy Protocol
The original BC uses GameSpy for server discovery. Community-maintained replacements (333networks, OpenSpy) provide drop-in compatible master server infrastructure. OpenBC implements the standard GameSpy heartbeat/query protocol to support internet play out of the box.

---

## 6. Phased Development Plan

### Phase A: Hash Manifest Tool (Immediate)
**Deliverable**: Standalone CLI tool for generating and verifying hash manifests.

| Component | Scope |
|-----------|-------|
| StringHash algorithm | 4-lane Pearson hash with extracted lookup tables |
| FileHash algorithm | Rotate-XOR with DWORD[1] skip |
| Manifest generator | Walk BC install, compute all hashes, output JSON |
| Manifest verifier | Compare manifest against a BC install |
| Lookup table constant | 1,024 bytes extracted via hash manifest tool, stored in source |

**Exit criteria**: Generate a manifest from a vanilla BC 1.1 install. Use that manifest to correctly validate a stock client's checksum responses.

### Phase B: Protocol Library (Foundation)
**Deliverable**: C library that can decode and encode any BC packet.

| Component | Scope |
|-----------|-------|
| AlbyRules cipher | XOR encrypt/decrypt |
| TGBufferStream equivalent | Position-tracked byte buffer with read/write primitives |
| Compressed type codecs | LogFloat16, DeltaVector3, DeltaQuaternion |
| Bit-packing | Bookmark-based boolean packing |
| Unit tests | Round-trip encode/decode for all types against captured packets |

**Exit criteria**: Decode 30,000+ captured packets with zero errors. Re-encode and get bit-identical output.

### Phase C: Lobby Server (First Playable)
**Deliverable**: Server that clients can connect to, pass checksums, and chat.

| Component | Scope |
|-----------|-------|
| UDP socket + peer management | Bind, accept connections, track peers |
| Checksum validation | Load manifest, validate 4-round exchange |
| Settings + GameInit generation | Opcodes 0x00 + 0x01 |
| Chat relay | Opcodes 0x2C + 0x2D |
| GameSpy LAN response | Server browser visibility |
| Master server registration | Internet play discovery |
| Server config | TOML configuration file loading |

**Exit criteria**: Vanilla BC client discovers server in LAN browser, connects, passes checksums, reaches ship selection, chat works between 2+ clients.

### Phase D: Relay Server (Basic Gameplay)
**Deliverable**: Full multiplayer gameplay via message relay.

| Component | Scope |
|-----------|-------|
| StateUpdate relay | Parse C->S, relay to other clients |
| Weapon fire relay | Opcodes 0x07, 0x08, 0x19, 0x1A |
| Object creation/destruction | Opcodes 0x03, 0x14 |
| Ship data registry | Load ships.toml, map ship selections to data |
| Map data registry | Load maps.toml, configure spawn points |
| Game lifecycle FSM | Lobby -> Ship Select -> Playing -> Game Over -> Restart |
| Scoring | Track kills/deaths, generate SCORE_CHANGE (0x36) |
| Late join | MISSION_INIT (0x35) for mid-game joiners |
| All remaining opcodes | Relay as raw bytes |

**Exit criteria**: 2+ vanilla BC clients play a complete FFA deathmatch match. Ships fly, weapons fire, kills register, score tracks, game ends on frag/time limit, restart works.

### Phase E: Simulation Server (Full Gameplay)
**Deliverable**: Server-authoritative physics, collision, and damage.

| Component | Scope |
|-----------|-------|
| Server-authoritative movement | Euler integration at 30 Hz |
| Broad-phase collision | Bounding sphere from ships.toml |
| Narrow-phase collision | Convex hulls from NIF extraction tool |
| NIF collision mesh extractor | Build-time tool to parse NIF geometry |
| Damage system | Collision + weapon + explosion formulas |
| Subsystem state management | Track all 14+ subsystem types per ship |
| StateUpdate generation | Server generates authoritative state updates |
| Game rules engine | Frag limit, time limit, respawn from rules.toml |
| Anti-cheat | Validate client claims against server state |

**Exit criteria**: Server detects and resolves collisions without relying on client simulation. Server-generated damage matches original BC behavior. Clients with modified game data cannot cheat because the server is authoritative.

---

## 7. Dependencies

### Build-Time Dependencies
- C compiler (GCC, Clang, or MSVC)
- Make
- TOML parser library (vendored or single-header)
- JSON parser library (vendored or single-header, for manifest loading)

### Data Dependencies (User-Supplied for Manifest Generation Only)
- Vanilla BC 1.1 install (GOG edition) -- needed ONLY to run the manifest generation tool
- Not needed on the server at runtime

### Protocol Data (All Verified)
- 4x256-byte StringHash lookup tables (extracted via hash manifest tool)
- Version string `"60"`, hash `0x7E0CE243`
- AlbyRules cipher key: `"AlbyRules!"` (10-byte XOR)
- Complete wire format: [phase1-verified-protocol.md](phase1-verified-protocol.md)
- 28 active game opcodes (jump table has 41 entries, many DEFAULT) with addresses and frequency counts
- 8 Python message opcodes (0x2C-0x39)
- MAX_MESSAGE_TYPES = 0x2B (43)
- Object ID allocation formula: `0x3FFFFFFF + N * 0x40000`
- StateUpdate dirty-flag format and compressed type formulas
- Damage system formulas (verified from behavioral observation)
- Connection state values: 2=HOST, 3=CLIENT, 4=DISCONNECTED
- Settings packet (0x00) format: gameTime + settings + playerSlot + mapName + passFail
- Checksum exchange flow: 4 sequential requests (0x20/0x21), version gate, file/dir hashing

---

## 8. Test Plan

### Phase A Tests

| Test | Method | Pass Criteria |
|------|--------|---------------|
| StringHash correctness | Hash known strings, compare to known-good values | Bit-identical results |
| FileHash correctness | Hash known .pyc files, compare to known-good values | Bit-identical results |
| Version string hash | `StringHash("60")` | Returns `0x7E0CE243` |
| Manifest generation | Run tool on vanilla BC install | Valid JSON, all hashes populated |
| Manifest verification | Verify generated manifest against same install | All entries match |
| Cross-validation | Compare manifest hashes against captured checksum exchange | All hashes match network traffic |

### Phase B Tests

| Test | Method | Pass Criteria |
|------|--------|---------------|
| AlbyRules round-trip | Encrypt then decrypt known data | Bit-identical to original |
| Packet decoding | Decode 30K+ captured packets | Zero parse errors |
| Packet re-encoding | Decode then re-encode captured packets | Bit-identical output |
| LogFloat16 round-trip | Encode/decode known float values | Values within tolerance |
| DeltaVector3 round-trip | Encode/decode known positions | Values within tolerance |
| DeltaQuaternion round-trip | Encode/decode known orientations | Values within tolerance |

### Phase C Tests

| Test | Method | Pass Criteria |
|------|--------|---------------|
| Server starts | Run binary | Binds port, no crash, < 1s startup |
| LAN discovery | BC client scan | Server visible with correct info |
| Master server registration | Configure master server | Server appears in internet list |
| Client connect | BC client join | Connection accepted, slot assigned |
| Checksum exchange | BC client join | All 4 rounds pass via manifest |
| Settings received | BC client join | Ship selection screen reached |
| Chat | Type message in game | Message appears on all clients |
| Max players | 17th client | Rejected with opcode 0x04 |
| Keepalive | Client idle 60s | Still connected |
| Config loading | TOML with various settings | Settings reflected in server behavior |

### Phase D Tests

| Test | Method | Pass Criteria |
|------|--------|---------------|
| Ship selection | Select ship in BC | Ship created, broadcast to other clients |
| Gameplay relay | 2+ clients in match | Movement, weapons, combat all functional |
| Scoring | Kill opponent | SCORE_CHANGE sent, score tracks |
| Game end (time) | Time limit expires | END_GAME_MESSAGE received by all |
| Game end (frag) | Frag limit reached | END_GAME_MESSAGE received by all |
| Restart | After game over | All clients return to ship select |
| Late join | Client joins mid-game | Receives MISSION_INIT, can play |
| Disconnect | Kill BC client | Slot freed, game continues |
| Multi-client | 4+ clients | Independent play, all state consistent |
| Mod loading | Custom ships.toml | New ships available in game |

### Phase E Tests

| Test | Method | Pass Criteria |
|------|--------|---------------|
| Ship movement | Fly ship | Position matches client expectation |
| Collision detection | Ram another ship | Collision detected, damage applied |
| Mesh collision accuracy | Graze ship hull | Near-miss works, no false positives at range |
| Weapon damage | Fire phasers | Target takes correct damage amount |
| Subsystem damage | Target subsystems | Correct subsystem health changes |
| Shield absorption | Fire at shielded target | Shield reduces damage correctly |
| Kill tracking | Destroy opponent | Kill registered to correct player |
| Respawn | Die and respawn | New ship created after delay |
| Anti-cheat | Modified client sends false position | Server rejects/corrects |
| Docker | Container run | Accessible from host network |
| Cross-platform | Linux + Windows builds | Both pass all tests |

---

## 9. Risk Register

| Risk | Probability | Impact | Mitigation | Status |
|------|-------------|--------|------------|--------|
| Wire format mismatch | **LOW** | HIGH | **SOLVED**: Complete wire format verified from 30K+ packet traces. See [phase1-verified-protocol.md](phase1-verified-protocol.md) | Resolved |
| Hash algorithm mismatch | **LOW** | HIGH | **SOLVED**: Both StringHash (4-lane Pearson) and FileHash (rotate-XOR) fully verified with lookup tables extracted | Resolved |
| ACK priority queue stall | **LOW** | HIGH | **SOLVED**: Three-tier queue system fully verified from protocol observation with timeouts and retry logic | Resolved |
| Game opcode relay ordering | **LOW** | MEDIUM | **SOLVED**: Complete opcode table with jump table addresses and frequency counts | Resolved |
| Ship creation protocol | **LOW** | HIGH | **SOLVED**: ObjectCreateTeam (0x03) format verified, working in STBC-Dedi proxy | Resolved |
| Manifest hash vs. live hash mismatch | MEDIUM | HIGH | Cross-validate manifest tool output against captured checksum exchanges from real sessions | Open |
| Ship data accuracy | MEDIUM | MEDIUM | Compare ships.toml values against hardpoint script execution results; community review | Open |
| NIF convex hull extraction | MEDIUM | MEDIUM | NIF format is documented; fallback to bounding spheres if hull extraction fails for a model | Open |
| StateUpdate compression fidelity | MEDIUM | HIGH | Bit-exact round-trip tests against captured packets; compressed type formulas are documented | Open |
| Master server protocol details | LOW | LOW | GameSpy protocol is well-documented and 333networks provides reference implementations | Open |
| Late join state sync incomplete | MEDIUM | MEDIUM | Test with vanilla client joining mid-game; compare against STBC-Dedi behavior | Open |
| Damage formula accuracy | MEDIUM | MEDIUM | Compare DoDamage output against original engine for known inputs; formulas are documented | Open |
| Mod TOML schema stability | LOW | LOW | Version the schema; provide migration guidance for breaking changes | Open |
| Cross-platform socket API differences | LOW | LOW | Abstract Winsock2/POSIX behind thin platform layer | Open |

---

## 10. Definition of Done

Phase 1 is complete when ALL of the following are true:

### Phase A Complete
- [ ] StringHash produces bit-identical results to known-good values for all test strings
- [ ] FileHash produces bit-identical results to known-good values for all test .pyc files
- [ ] Manifest tool generates valid JSON from a vanilla BC 1.1 install
- [ ] Generated manifest validates successfully against a stock client's checksum exchange

### Phase B Complete
- [ ] Protocol library decodes 30,000+ captured packets with zero errors
- [ ] Re-encoded packets are bit-identical to originals
- [ ] All compressed types (LogFloat16, DeltaVector3, DeltaQuaternion) round-trip correctly

### Phase C Complete
- [ ] Server starts and binds port in < 1 second
- [ ] Vanilla BC client discovers server in LAN browser
- [ ] Server appears in 333networks (or equivalent) internet server list
- [ ] Client connects and passes 4-round checksum exchange via manifest
- [ ] Client reaches ship selection screen
- [ ] Chat messages relay between 2+ clients
- [ ] Server rejects 17th client correctly

### Phase D Complete
- [ ] 2+ vanilla BC clients play a complete multiplayer match
- [ ] Ship movement, weapons, and combat work via relay
- [ ] Kill scoring works (SCORE_CHANGE messages sent)
- [ ] Game ends on time limit and frag limit
- [ ] Game restart returns all clients to ship select
- [ ] Late join works (client joins mid-game and can play)
- [ ] Server reads ship/map/rule data from TOML files
- [ ] Mod data packs can add new ships and game modes

### Phase E Complete
- [ ] Server-authoritative physics match original BC movement feel
- [ ] Collision detection works with mesh-accurate convex hulls
- [ ] Damage system produces correct results for collision, weapon, and explosion damage
- [ ] Server generates StateUpdate messages at 30 Hz
- [ ] Subsystem damage and shield mechanics work
- [ ] Server runs in Docker container, accessible from host network
- [ ] Server builds on both Linux and Windows
- [ ] No copyrighted material present in server binary or data files
