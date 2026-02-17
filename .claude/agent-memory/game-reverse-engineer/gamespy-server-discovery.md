# GameSpy Server Discovery Analysis

## Summary

BC uses the GameSpy SDK (QR1 for server-side, ServerBrowser for client-side) with TWO separate objects stored in the GameSpy wrapper:
- **GameSpy+0xDC** = qr_t (server-side, answers LAN/Internet queries, sends heartbeats)
- **GameSpy+0xE0** = server_browser_t (client-side, queries LAN servers or master server)

## Server Side (QR1 - Answering Queries)

### Object: qr_t at GameSpy+0xDC
- Created by FUN_0069c240 (GameSpy_StartHeartbeat, not in decompiled output - merged by Ghidra into FUN_0069c140)
- qr_t[0] = SOCKET (shared with game socket WSN+0x194 = port 22101)
- qr_t[1] = secondary socket for heartbeat to master server
- qr_t[0x39] (byte offset 0xE4) = recvfrom enable flag (0 = disabled, managed by peek router)
- Registers for event type 0x60006 (ProcessQueryHandler)

### Query Handler: FUN_006ac1e0
- Called when a `\`-prefixed packet arrives on the game socket
- Parses query type from PTR_s_basic_0095a71c table:
  - case 0: `\basic\` -> FUN_006ac5f0 (server info: hostname, gamename, mapname, etc.)
  - case 1: `\rules\` -> FUN_006ac7a0 (game rules callback)
  - case 2: `\players\` -> FUN_006ac810 (player list callback)
  - case 3: `\status\` -> FUN_006ac880 (status callback)
  - case 4: all of the above
  - case 5: all with `\queryid\` separators
  - case 6: specific key query (FUN_006ac8f0)
  - case 7: echo query
- Response sent via sendto back to the querier's address on the same socket

### Heartbeat: FUN_006aca60
- Sends `\heartbeat\%d\gamename\%s` to master server (to_00995880 global)
- If param_2 != 0, appends `\statechanged\%d`
- Uses qr_t+0x04 as socket (separate from game socket), destination is to_00995880
- Called periodically by FUN_006abd80 (every ~30 seconds)

## Client Side (ServerBrowser - Finding Servers)

### Two Modes: Internet vs LAN

#### FUN_0069ccd0 - StartSearching (GameSpy wrapper method)
- param_1 == 0: **Internet** mode -> FUN_006aa2f0
- param_1 != 0: **LAN** mode -> FUN_006aa6b0

### Internet Mode (FUN_006aa310, mode=5)

1. **FUN_006aa410**: TCP connect to master server
   - Host: `stbridgecmnd01.activision.com` (override via `masterserver.txt`)
   - Port: **28900** (0x70e4)
   - Protocol: TCP (socket type SOCK_STREAM=1, proto TCP=6)
   - Socket stored at sb_t+0x88

2. **FUN_006aa4c0**: Handshake with master server
   - Receives `\secure\<challenge>` from master
   - Sends: `\gamename\%s\gamever\%s\location\%s` (game authentication)
   - Then sends: `\list\%s\gamename\%s\final\` or `\list\%s\gamename\%s\where\%s\final\`
   - Transitions to state 1 (receiving server list)

3. **State 1 (FUN_006aacc0)**: Receive server list from master
   - Reads TCP stream with IP:port pairs (4 bytes IP + 2 bytes port, network order)
   - Parses until `\final\` marker
   - Each server entry is stored in the server list
   - Transitions to state 3 when list complete

4. **State 3 (FUN_006ab150)**: Query individual servers
   - Creates per-server UDP sockets (FUN_006aa3a0): one per server entry
   - Sends query packets directly to each server's IP:port
   - Query types come from PTR_s__basic__0095a53c table (same as QR1)
   - Receives responses on individual sockets
   - 3-second timeout per server
   - Calls state change callback (2=server found) with server data

### LAN Mode (FUN_006aa6b0)

1. **FUN_006aa720**: Create broadcast socket
   - UDP socket (AF_INET=2, SOCK_DGRAM=2, IPPROTO_UDP=0x11)
   - **Sets SO_BROADCAST** (setsockopt level=0xFFFF=SOL_SOCKET, opt=0x20=SO_BROADCAST)
   - Socket stored at sb_t+0x88

2. **FUN_006aa770**: Send broadcast queries
   - Destination: **255.255.255.255** (sa_data[2..5] = 0xFF)
   - Sends `\status\` (s__status__0095a554, 8 bytes) to each port in range
   - **Port range: 22101 (0x5655) to 22201 (0x56b9), step 1**
   - Uses sb_t+0x88 (= sb_t[0x22]) socket
   - Total: broadcasts to 101 ports (22101 through 22201)
   - Transitions to state 2 (waiting for responses)

3. **State 2 (FUN_006aab90)**: Receive LAN responses
   - select() + recvfrom on the broadcast socket (sb_t[0x22])
   - Looks for `\final\` in response
   - Adds responding servers to the server list
   - 3-second timeout
   - Calls state change callback (5=server found) with server data

## Port Summary

| Port | Protocol | Direction | Purpose |
|------|----------|-----------|---------|
| 22101 (0x5655) | UDP | Game traffic | TGNetwork game packets + QR1 queries (shared socket) |
| 22101-22201 | UDP | Client->Server | LAN broadcast range for `\status\` queries |
| 28900 (0x70e4) | TCP | Client->Master | ServerBrowser list request to GameSpy master |
| 27900 | UDP | Server->Master | Heartbeat destination (to_00995880, NOT a fixed global - set by DNS resolution) |

## Key Constants

- gamename: "bcommander"
- game secret: "Nm3aZ9" (used for secure validation with master)
- Default master: stbridgecmnd01.activision.com (overridable via masterserver.txt)
- LAN query message: `\status\` (8 bytes including NUL)
- Heartbeat format: `\heartbeat\%d\gamename\%s\statechanged\%d`

## Server-Side SO_BROADCAST Bug

The server-side heartbeat uses `sendto()` on `qr_t+0x04` socket to `to_00995880`. For Internet mode, this is the master server's resolved IP. For the original stock server trying to broadcast heartbeats on LAN, it fails with rc=-1 because SO_BROADCAST is NOT set on the QR1 server socket (qr_t[0] = game socket). Only the client-side LAN broadcast socket (sb_t+0x88) sets SO_BROADCAST.

## State Machine

### Server Browser States (sb_t[0])
- 0: Idle/Complete
- 1: Receiving server list from master (TCP)
- 2: Waiting for LAN broadcast responses (UDP)
- 3: Querying individual servers (UDP)

### QR1 Server States (qr_t, managed by qr_t+0xE4 flag)
- 0: Not active
- 1: Active (receives and responds to queries)

## Data Flow Diagram

```
INTERNET MODE:
Client ─TCP:28900─> GameSpy Master (stbridgecmnd01.activision.com)
  1. Client: \gamename\bcommander\gamever\...\location\...
  2. Master: \secure\<challenge>
  3. Client: \list\bcommander\gamename\bcommander\final\
  4. Master: [IP1:port1][IP2:port2]...\final\
  5. Client ─UDP:port─> each server: \status\ (or \basic\, \rules\, etc.)
  6. Server ─UDP:port─> Client: \hostname\...\mapname\...\final\

LAN MODE:
Client ─UDP broadcast 255.255.255.255:22101-22201─> \status\
Server (listening on port 22101) ─UDP─> Client: \hostname\...\final\
```

## Source References
- FUN_006aa100 (qr_init): `10_netfile_checksums.c:5974`
- FUN_006aa310 (sb_init Internet): `10_netfile_checksums.c:6130`
- FUN_006aa410 (TCP connect to master): `10_netfile_checksums.c:6189`
- FUN_006aa4c0 (master handshake): `10_netfile_checksums.c:6221`
- FUN_006aa6b0 (sb_init LAN): `10_netfile_checksums.c:6306`
- FUN_006aa720 (create broadcast socket): `10_netfile_checksums.c:6336`
- FUN_006aa770 (send LAN broadcasts): `10_netfile_checksums.c:6357`
- FUN_006aab40 (sb_think state machine): `10_netfile_checksums.c:6436`
- FUN_006aab90 (state 2 - LAN recv): `10_netfile_checksums.c:6460`
- FUN_006aacc0 (state 1 - master recv): `10_netfile_checksums.c:6509`
- FUN_006ab150 (state 3 - query servers): `10_netfile_checksums.c:6702`
- FUN_006abca0 (qr_process for server): `10_netfile_checksums.c:6950`
- FUN_006abce0 (qr_process_incoming): `10_netfile_checksums.c:6973`
- FUN_006ac1e0 (qr_handle_query): `10_netfile_checksums.c:7200`
- FUN_006aca60 (heartbeat send): `10_netfile_checksums.c:7832`
- FUN_0069c3a0 (create qr_t for server browser): `09_multiplayer_game.c:4234`
- FUN_0069ccd0 (start searching: LAN vs Internet): `09_multiplayer_game.c:4617`
- FUN_0069bfa0 (GameSpy constructor): `09_multiplayer_game.c:4094`
