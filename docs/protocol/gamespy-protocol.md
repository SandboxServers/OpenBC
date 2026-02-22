# GameSpy Protocol Specification

Bridge Commander uses the GameSpy Query & Reporting (QR1) protocol for server discovery, both on LAN and over the internet via master servers. GameSpy traffic shares the same UDP socket as game traffic but is transmitted in plaintext (not encrypted).

**Clean room statement**: This document describes the GameSpy protocol as implemented in `src/network/gamespy.c` and `src/network/master.c`, and as observed in packet captures between stock BC clients and servers. No binary addresses, memory offsets, or decompiled code are referenced.

---

## 1. Traffic Discrimination

Game traffic and GameSpy traffic share a single UDP socket. They are distinguished by the first byte:

- **GameSpy**: First byte is `\` (0x5C) -- plaintext, NOT encrypted
- **Game**: First byte is `0x01`, `0x02`, or `0xFF` -- encrypted with AlbyRules cipher

The server peeks at the first byte of each incoming packet to route it to the correct handler.

---

## 2. LAN Discovery

### Client Query

The BC client discovers LAN servers by broadcasting a `\status\` query:

- **Target**: `255.255.255.255` (broadcast)
- **Port range**: UDP 22101 through 22201 (101 consecutive ports)
- **Payload**: `\status\` (8 bytes, ASCII)

Any server listening on a port in this range responds with its server information.

### Server Response

The server responds with a single unfragmented UDP packet containing backslash-delimited key-value pairs. The response includes fields from four callback categories assembled in this order:

**Verified field order** (from packet capture, 267 bytes):
```
\gamename\bcommander
\gamever\60
\location\0
\hostname\<server name>
\missionscript\<mission script path>
\mapname\<display name>
\numplayers\<N>
\maxplayers\<N>
\gamemode\<mode>
\timelimit\<N>
\fraglimit\<N>
\system\<star system>
\password\0
\player_0\<player name>
[...more \player_N\ entries for each connected player...]
\final\
\queryid\N.M
```

### Field Details

| Field | Values | Notes |
|-------|--------|-------|
| gamename | `bcommander` | Hardcoded, never changes |
| gamever | `60` | Numeric version (NOT "1.1") |
| location | `0` or `1` | Region code |
| hostname | string | Prefixed with `*` if password-protected |
| missionscript | string | e.g. `Multiplayer.Episode.Mission1.Mission1` |
| mapname | string | Human-readable display name |
| numplayers | integer | Currently connected players |
| maxplayers | integer | Server's player limit |
| gamemode | string | `openplaying` (in-game) or `settings` (lobby) |
| timelimit | integer | Minutes, -1 = no limit |
| fraglimit | integer | Kills, -2 = no limit |
| system | string | Star system script name |
| password | `0` or `1` | 0 = no password |
| player_N | string | Player name (one entry per connected player) |

### Response Termination

- `\final\` marks the end of response data
- `\queryid\N.M` follows immediately after `\final\` (no trailing backslash)
- The queryid echoes back whatever the client sent in its query, or defaults to `1.1`

### Query Types

The QR1 protocol supports 8 query types. BC clients typically send `\status\` which requests all fields, but the server should handle:

| Query | Fields Returned |
|-------|-----------------|
| `\basic\` | hostname, missionscript, mapname, numplayers, maxplayers, gamemode |
| `\info\` | gamename, gamever, location |
| `\rules\` | timelimit, fraglimit, system, password |
| `\players\` | player_0, player_1, ... |
| `\status\` | All of the above |
| `\packets\` | Not used by BC |
| `\echo\` | Echo query back |
| `\secure\` | Challenge-response (see Section 4) |

### Default Port

The default game port is **22101** (0x5655). The GameSpy query port is typically the game port (same socket).

---

## 3. Master Server Registration

### Overview

BC uses GameSpy master servers for internet server discovery. The original Activision master (`stbridgecmnd01.activision.com`) has been offline since ~2012. Community replacements (333networks, OpenSpy) speak the same protocol.

### Master Server Addresses

- Default master port: **UDP 27900** (heartbeat)
- Client browsing port: **TCP 28900** (server list retrieval)
- Override: `masterserver.txt` file in the game directory (one `host:port` per line)

### Heartbeat Protocol

The server registers with master servers by sending periodic heartbeats:

**Initial heartbeat** (at startup):
```
\heartbeat\<game_port>\gamename\bcommander\
```

**Subsequent heartbeats** (periodic, every 60 seconds):
```
\heartbeat\<game_port>\gamename\bcommander\
```

**State change heartbeat** (when game state changes):
```
\heartbeat\<game_port>\gamename\bcommander\statechanged\1
```

**Shutdown heartbeat** (when server is closing):
```
\heartbeat\<game_port>\gamename\bcommander\final\
```

### Master Server Handshake

```
1. Server  →  Master:27900   \heartbeat\<port>\gamename\bcommander\
2. Master  →  Server:game    \secure\<6-char challenge>
3. Server  →  Master         \gamename\bcommander\gamever\60\location\0\validate\<response>\final\\queryid\1.1
4. Master registers the server (may also send \status\ queries)
```

### Heartbeat Timing

| Parameter | Value |
|-----------|-------|
| Heartbeat interval | 60 seconds |
| Startup probe timeout | 3 seconds |
| Probe behavior | Send heartbeat to all masters, wait for responses |

A master is considered "registered" when it responds to a heartbeat (either with `\secure\` or `\status\`).

---

## 4. Challenge-Response Crypto (gsmsalg)

When a master server receives a heartbeat, it sends back a `\secure\` challenge. The server must respond with a valid `\validate\` hash to complete registration.

### Secret Key

```
Key: "Nm3aZ9" (6 ASCII bytes)
```

This key is specific to Bridge Commander and is publicly known from open-source GameSpy reimplementations.

### Algorithm

The algorithm is a modified RC4 variant with base64 encoding:

**Step 1: Key Scheduling (standard RC4 KSA)**
```
Initialize S[0..255] = identity permutation
j = 0
For i = 0..255:
    j = (j + S[i] + key[i % key_length]) mod 256
    swap(S[i], S[j])
```

**Step 2: Stream Cipher (modified PRGA)**

The stream generation differs from standard RC4. The index advancement uses `challenge[n] + 1` instead of a simple counter:

```
a = 0, b = 0
For each challenge byte challenge[n]:
    a = (a + challenge[n] + 1) mod 256     <-- NOTE: +1, not standard RC4
    x = S[a]
    b = (b + x) mod 256
    y = S[b]
    swap(S[a], S[b])  →  S[b] = x, S[a] = y
    output[n] = challenge[n] XOR S[(x + y) mod 256]
```

**Step 3: Base64 Encoding (standard alphabet)**
```
Alphabet: A-Z a-z 0-9 + /
Pad output to multiple of 3 bytes with 0x00
Encode each 3-byte group into 4 base64 characters
```

### Input/Output

- **Challenge**: 6 random printable ASCII characters (from `\secure\` packet)
- **Response**: 8 base64 characters (from gsmsalg output)

### Wire Format

```
Master → Server:  \secure\ABCDEF
Server → Master:  \gamename\bcommander\gamever\60\location\0\validate\XyZ12345\final\\queryid\1.1
```

The validate response is embedded in a full GameSpy response packet including gamename, gamever, and location fields.

---

## 5. Client Server Browsing (TCP)

For internet play, BC clients connect to the master server via TCP to retrieve the server list.

### Protocol Flow

```
1. Client  →  Master:28900   (TCP connect)
2. Master  →  Client         \basic\\secure\<challenge>
3. Client  →  Master         \gamename\bcommander\validate\<response>\final\\queryid\1.1\list\cmp\gamename\bcommander\final\
4. Master  →  Client         (binary server list)
5. Client closes connection
```

### Server List Binary Format

The server list response is binary (not backslash-delimited). Each entry is 6 bytes:

```
[ip:4 bytes, big-endian][port:2 bytes, big-endian]
```

The client iterates through the list and sends `\status\` queries directly to each server.

---

## 6. Implementation Notes

- GameSpy packets are never encrypted with the AlbyRules cipher
- The server must handle GameSpy queries arriving on the game port at any time (interleaved with game traffic)
- Multiple master servers can be registered simultaneously
- If DNS resolution fails for a master server, skip it (don't block startup)
- The original game sent `\heartbeat\0\gamename\bcommander\statechanged\1` with port=0, which may be a quirk of the original implementation

---

## Related Documents

- **[phase1-verified-protocol.md](phase1-verified-protocol.md)** -- Section 9 contains observed GameSpy wire data
- **[join-flow.md](../network-flows/join-flow.md)** -- Connection lifecycle including GameSpy discrimination
