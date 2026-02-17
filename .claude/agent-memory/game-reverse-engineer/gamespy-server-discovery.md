# GameSpy Server Discovery Analysis

## Summary

BC uses the GameSpy SDK (QR1 for server-side, ServerBrowser for client-side) with TWO separate objects stored in the GameSpy wrapper:
- **Server-side qr_t**: answers LAN/Internet queries, sends heartbeats
- **Client-side server_browser_t**: queries LAN servers or master server

## Server Side (QR1 - Answering Queries)

### qr_t Object
- Created during GameSpy initialization
- Uses a socket shared with the game socket (port 22101)
- Has a secondary socket for heartbeat to master server
- The recvfrom-enable flag is set to 0 (disabled), managed by the peek router
- Registers for a ProcessQuery event type

### Query Handler
- Called when a `\`-prefixed packet arrives on the game socket
- Parses query type from a lookup table:
  - case 0: `\basic\` -> server info: hostname, gamename, mapname, etc.
  - case 1: `\rules\` -> game rules callback
  - case 2: `\players\` -> player list callback
  - case 3: `\status\` -> status callback (combines basic + rules + players)
  - case 4: all of the above
  - case 5: all with `\queryid\` separators
  - case 6: specific key query
  - case 7: echo query
- Response sent via sendto back to the querier's address on the same socket

### Heartbeat
- Sends `\heartbeat\%d\gamename\%s` to master server
- If state changed, appends `\statechanged\%d`
- Uses secondary socket (separate from game socket), destination is resolved master address
- Called periodically (~every 30 seconds)

## Client Side (ServerBrowser - Finding Servers)

### Two Modes: Internet vs LAN

#### StartSearching
- param == 0: **Internet** mode
- param != 0: **LAN** mode

### Internet Mode

1. **TCP connect to master server**
   - Host: `stbridgecmnd01.activision.com` (override via `masterserver.txt`)
   - Port: **28900**
   - Protocol: TCP

2. **Handshake with master server**
   - Receives `\secure\<challenge>` from master
   - Sends: `\gamename\%s\gamever\%s\location\%s` (game authentication)
   - Then sends: `\list\%s\gamename\%s\final\` or `\list\%s\gamename\%s\where\%s\final\`
   - Transitions to state 1 (receiving server list)

3. **State 1: Receive server list from master**
   - Reads TCP stream with IP:port pairs (4 bytes IP + 2 bytes port, network order)
   - Parses until `\final\` marker
   - Each server entry is stored in the server list
   - Transitions to state 3 when list complete

4. **State 3: Query individual servers**
   - Creates per-server UDP sockets: one per server entry
   - Sends query packets directly to each server's IP:port
   - Receives responses on individual sockets
   - 3-second timeout per server
   - Calls state change callback (2=server found) with server data

### LAN Mode

1. **Create broadcast socket**
   - UDP socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)
   - **Sets SO_BROADCAST** on the socket
   - Socket stored in browser object

2. **Send broadcast queries**
   - Destination: **255.255.255.255** (broadcast)
   - Sends `\status\` (8 bytes) to each port in range
   - **Port range: 22101 to 22201, step 1**
   - Total: broadcasts to 101 ports (22101 through 22201)
   - Transitions to state 2 (waiting for responses)

3. **State 2: Receive LAN responses**
   - select() + recvfrom on the broadcast socket
   - Looks for `\final\` in response
   - Adds responding servers to the server list
   - 3-second timeout
   - Calls state change callback (5=server found) with server data

## Port Summary

| Port | Protocol | Direction | Purpose |
|------|----------|-----------|---------|
| 22101 | UDP | Game traffic | TGNetwork game packets + QR1 queries (shared socket) |
| 22101-22201 | UDP | Client->Server | LAN broadcast range for `\status\` queries |
| 28900 | TCP | Client->Master | ServerBrowser list request to GameSpy master |
| 27900 | UDP | Server->Master | Heartbeat destination (resolved via DNS) |

## Key Constants

- gamename: "bcommander"
- game secret: "Nm3aZ9" (used for secure validation with master)
- Default master: stbridgecmnd01.activision.com (overridable via masterserver.txt)
- LAN query message: `\status\` (8 bytes including NUL)
- Heartbeat format: `\heartbeat\%d\gamename\%s\statechanged\%d`

## Server-Side SO_BROADCAST Bug

The server-side heartbeat uses sendto() on a secondary socket. For Internet mode, this is the master server's resolved IP. For a server trying to broadcast heartbeats on LAN, it fails with rc=-1 because SO_BROADCAST is NOT set on the QR1 server socket (which shares the game socket). Only the client-side LAN broadcast socket sets SO_BROADCAST.

## State Machine

### Server Browser States
- 0: Idle/Complete
- 1: Receiving server list from master (TCP)
- 2: Waiting for LAN broadcast responses (UDP)
- 3: Querying individual servers (UDP)

### QR1 Server States
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
