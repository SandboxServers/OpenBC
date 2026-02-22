# Checksum Handshake Protocol

Observed behavior of the Bridge Commander 1.1 checksum exchange, documented from black-box packet captures of a stock dedicated server communicating with a stock BC 1.1 client. All data below reflects observed wire behavior only.

**Date**: 2026-02-17
**Method**: Packet capture with decryption (AlbyRules stream cipher), stock dedicated server + stock client, local loopback

---

## Overview

After the TCP-level connection is established and the Connect/ConnectAck handshake completes, the server initiates a **5-round checksum exchange** before sending game configuration. Each round asks the client to hash a set of files. The server verifies each response before proceeding to the next round.

**Critical protocol rule**: Rounds are sent **one at a time, sequentially**. The server sends round N, waits for the client's response, validates it, then sends round N+1. The server MUST NOT send the next round before receiving and processing the current round's response.

---

## The 5 Rounds

| Round Index | Directory | Filter | Recursive | Purpose |
|-------------|-----------|--------|-----------|---------|
| `0x00` | `scripts/` | `App.pyc` | No | Core application module |
| `0x01` | `scripts/` | `Autoexec.pyc` | No | Startup script |
| `0x02` | `scripts/ships` | `*.pyc` | **Yes** | All ship definition modules |
| `0x03` | `scripts/mainmenu` | `*.pyc` | No | Menu system modules |
| `0xFF` | `Scripts/Multiplayer` | `*.pyc` | **Yes** | Multiplayer mission scripts |

Notes:
- Round 0xFF is a special "final" round with index 255, not sequential with 0-3
- Directory strings do NOT have trailing path separators (no trailing `/` or `\`)
- Round 0xFF uses capital-S `Scripts/Multiplayer` (observed on wire)
- The recursive flag means "scan subdirectories" for wildcard filters

---

## Wire Format

### ChecksumReq (Server -> Client, opcode `0x20`)

Sent inside a reliable transport wrapper.

```
Offset  Type    Field
------  ------  -----
0       u8      opcode = 0x20
1       u8      round_index (0x00, 0x01, 0x02, 0x03, or 0xFF)
2       u16le   directory_length
4       bytes   directory_name (NOT null-terminated)
+0      u16le   filter_length
+2      bytes   filter_name (NOT null-terminated)
+0      bitbyte recursive_flag (see Bit-Packed Boolean below)
```

### ChecksumResp (Client -> Server, opcode `0x21`)

The client responds with opcode `0x21` containing the round index and hash data.

```
Offset  Type    Field
------  ------  -----
0       u8      opcode = 0x21
1       u8      round_index (echoes the request's round_index)
2       u32le   ref_hash (ONLY round 0 -- StringHash of gamever "60")
6       u32le   dir_hash (hash of the directory name)
6/10+   var     file_tree (see Checksum Response Tree Format below)

Round 0:  [0x21][round][ref_hash:u32][dir_hash:u32][file_tree...]  (10-byte header)
Other:    [0x21][round][dir_hash:u32][file_tree...]                 (6-byte header)
```

**Important**: Only round 0 includes `ref_hash`. Rounds 1, 2, 3, and 0xFF start
directly with `dir_hash` at offset 2. Verified by live client packet analysis.

### Checksum Response Tree Format

The hash data after the directory hash follows a recursive tree structure. For **non-recursive rounds** (0, 1, 3), it contains a flat list of files. For **recursive rounds** (2, 0xFF), it additionally contains subdirectories, each with their own file list.

**File tree** (self-describing, same format at all nesting levels):
```
[file_count:u16le]
  repeated file_count times:
    [name_hash:u32le]       -- hash of the filename
    [content_hash:u32le]    -- hash of the file contents
[subdir_count:u8]             -- ALWAYS present (u8, NOT u16)
  ALL subdir name hashes listed first:
    [subdir_name_hash_0:u32le]
    [subdir_name_hash_1:u32le]
    ...
  THEN each subdir's tree (same recursive format):
    [tree_0]
    [tree_1]
    ...
```

The tree format is self-describing: if `subdir_count` is 0, the node is a leaf.
Non-recursive rounds (0, 1, 3) always have `subdir_count=0` (one trailing byte).
Recursive rounds (2, 0xFF) have the full recursive structure.

**Important**: Name hashes are listed contiguously BEFORE the trees, not interleaved
with them. Each subtree recursively uses this same format.

**Example decode** -- Round 0 response (packet #7):
```
Hash data (after opcode + index):
43 E2 0C 7E  -- ref_hash = 0x7E0CE243 (StringHash("60"))
2F CB AF 4D  -- dir_hash = 0x4DAFCB2F (hash of "scripts/")
01 00        -- file_count = 1
77 B6 3E 37  -- files[0].name_hash = 0x373EB677 (StringHash("App.pyc"))
A7 A0 F8 00  -- files[0].content_hash (FileHash of App.pyc)
00           -- subdir_count = 0
```

Round 0 has 1 file (App.pyc). Round 1 has 1 file (Autoexec.pyc). Round 2 is large (~400 bytes) because it contains all ship `.pyc` files plus subdirectories. Round 0xFF is similarly large for `Scripts/Multiplayer` with subdirectories.

**Hash algorithm**: Both `name_hash` and `content_hash` use a Pearson-based hash with four 256-byte substitution tables forming Mutually Orthogonal Latin Squares (MOLS). `StringHash` operates on the lowercased filename string; `FileHash` operates on the raw file bytes, skipping bytes 4-7 in `.pyc` files (the compilation timestamp). Verified test vector: `StringHash("60") == 0x7E0CE243`.

The `ref_hash` is `StringHash("60")` = `0x7E0CE243`, corresponding to the game version string. It appears only in round 0 and acts as a protocol version marker. All other rounds omit it.

### Bit-Packed Boolean Encoding

The recursive flag is encoded as a single bit using a compact bit-packing scheme:

```
Byte layout: [count_minus_1:3][padding:4][bit0:1]

For a single boolean:
  false = 0x20  (count=1 in upper 3 bits, bit0=0)
  true  = 0x21  (count=1 in upper 3 bits, bit0=1)
```

This is NOT a plain byte — it is a bit-packed value. The upper 3 bits encode how many boolean values are packed into this byte (value 1 = one bit stored). The lowest bit holds the actual boolean. Implementations that write a plain `0x00`/`0x01` byte here will break the client's stream parser, causing all subsequent reads to be misaligned.

---

## Sequencing Rules

The complete observed flow:

```
Server                              Client
  |                                    |
  |--- ConnectAck + ChecksumReq[0] -->|  (round 0 bundled with connect response)
  |                                    |
  |<------- ChecksumResp[0] ----------|
  |                                    |
  |--- ACK + ChecksumReq[1] --------->|  (round 1 sent only after round 0 response)
  |                                    |
  |<------- ChecksumResp[1] ----------|
  |                                    |
  |------- ChecksumReq[2] ----------->|  (round 2 sent only after round 1 response)
  |                                    |
  |<------- ChecksumResp[2] ----------|  (LARGE: fragmented, ~400 bytes)
  |                                    |
  |--- ACK + ChecksumReq[3] --------->|  (round 3 sent only after round 2 response)
  |                                    |
  |<------- ChecksumResp[3] ----------|
  |                                    |
  |--- ACK + ChecksumReq[0xFF] ------>|  (final round after round 3 response)
  |                                    |
  |<------- ChecksumResp[0xFF] -------|  (LARGE: fragmented, ~275 bytes)
  |                                    |
  |--- 0x28 + Settings + GameInit --->|  (game phase begins)
  |                                    |
```

**Critical**: The stock server never sends the next ChecksumReq until the previous ChecksumResp has been received and validated. Sending multiple rounds simultaneously or sending round 0xFF before round 3 completes will confuse the client's state machine.

---

## Observed Packet Trace (Stock Dedicated Server)

The following hex dumps are from a captured session between a stock BC 1.1 dedicated server and a stock BC 1.1 client on loopback. All data shown is **after** AlbyRules stream cipher decryption. Reliable transport wrappers are included for context.

### Packet #2: ConnectAck + ChecksumReq Round 0

```
Server -> Client, 35 bytes:
0000: 01 02 03 06 C0 00 00 02 32 1B 80 00 00 20 00 08  |........2.... ..|
0010: 00 73 63 72 69 70 74 73 2F 07 00 41 70 70 2E 70  |.scripts/..App.p|
0020: 79 63 20                                         |yc |

Messages:
  [0] ConnectAck (0x03) len=6
  [1] Reliable seq=0 len=27 flags=0x80
      ChecksumReq: round=0 dir="scripts/" filter="App.pyc" recursive=0
                                                        ^^
                                              bitByte 0x20 = false
```

### Packet #7: ChecksumResp Round 0

```
Client -> Server, 28 bytes:
0000: 02 01 32 1A 80 00 00 21 00 43 E2 0C 7E 2F CB AF  |..2....!.C..~/..|
0010: 4D 01 00 77 B6 3E 37 68 A7 A0 F8 00              |M..w.>7h....|

Messages:
  [0] Reliable seq=0 len=26 flags=0x80
      ChecksumResp: round=0, 24 bytes hash data
```

### Packet #8: ACK + ChecksumReq Round 1

```
Server -> Client, 38 bytes:
0000: 01 02 01 00 00 00 32 20 80 01 00 20 01 08 00 73  |......2 ... ...s|
0010: 63 72 69 70 74 73 2F 0C 00 41 75 74 6F 65 78 65  |cripts/..Autoexe|
0020: 63 2E 70 79 63 20                                |c.pyc |

Messages:
  [0] ACK seq=0
  [1] Reliable seq=256 len=32 flags=0x80
      ChecksumReq: round=1 dir="scripts/" filter="Autoexec.pyc" recursive=0
```

### Packet #10: ChecksumResp Round 1

```
Client -> Server, 24 bytes:
0000: 02 01 32 16 80 01 00 21 01 2F CB AF 4D 01 00 A1  |..2....!./..M...|
0010: E6 01 85 49 00 93 17 00                          |...I....|

Messages:
  [0] Reliable seq=256 len=22 flags=0x80
      ChecksumResp: round=1, 20 bytes hash data
```

### Packet #11: ChecksumReq Round 2

```
Server -> Client, 32 bytes:
0000: 01 01 32 1E 80 02 00 20 02 0D 00 73 63 72 69 70  |..2.... ...scrip|
0010: 74 73 2F 73 68 69 70 73 05 00 2A 2E 70 79 63 21  |ts/ships..*.pyc!|

Messages:
  [0] Reliable seq=512 len=30 flags=0x80
      ChecksumReq: round=2 dir="scripts/ships" filter="*.pyc" recursive=1
                                                               ^^
                                                     bitByte 0x21 = true
```

### Packet #18: ChecksumResp Round 2 (Fragment 1 of 3)

```
Client -> Server, 418 bytes:
0000: 02 02 01 02 00 00 32 9C A1 02 00 00 03 21 02 C4  |......2......!..|
0010: D1 17 C0 33 00 BA 3E B2 3E 70 71 D3 CC 97 75 FC  |...3..>.>pq...u.|
...
01A0: 70 63                                            |pc|

Messages:
  [0] ACK seq=2
  [1] Reliable seq=512 len=156 flags=0xA1 frag=0/3 more=1
      ChecksumResp: round=2, first fragment (156 bytes of ~400 total)

NOTE: This response is fragmented across 3 reliable transport frames.
The server MUST reassemble all fragments before processing the response.
Total reassembled response is approximately 400 bytes of hash data.
```

### Packet #21: ACK + ChecksumReq Round 3

```
Server -> Client, 45 bytes:
0000: 01 03 01 02 00 01 01 01 02 00 01 02 32 21 80 03  |............2!..|
0010: 00 20 03 10 00 73 63 72 69 70 74 73 2F 6D 61 69  |. ...scripts/mai|
0020: 6E 6D 65 6E 75 05 00 2A 2E 70 79 63 20           |nmenu..*.pyc |

Messages:
  [0] ACK seq=2
  [1] ACK seq=1
  [2] ACK seq=2 (transport-level acks for fragmented response)
  [3] Reliable seq=768 len=33 flags=0x80
      ChecksumReq: round=3 dir="scripts/mainmenu" filter="*.pyc" recursive=0
```

### Packet #22: ChecksumResp Round 3

```
Client -> Server, 52 bytes:
0000: 02 02 01 03 00 00 32 2E 80 03 00 21 03 5D 47 09  |......2....!.]G.|
0010: 34 04 00 8B 66 46 58 7C 74 09 89 9D D2 70 43 C1  |4...fFX|t....pC.|
0020: BB 58 B8 A2 9F DD E9 AD D0 8C 2C 23 1D 4D B5 A7  |.X........,#.M..|
0030: 30 B1 2D 00                                      |0.-.|

Messages:
  [0] ACK seq=3
  [1] Reliable seq=768 len=46 flags=0x80
      ChecksumResp: round=3, 44 bytes hash data
```

### Packet #23: ACK + ChecksumReq Round 0xFF (Final)

```
Server -> Client, 42 bytes:
0000: 01 02 01 03 00 00 32 24 80 04 00 20 FF 13 00 53  |......2$... ...S|
0010: 63 72 69 70 74 73 2F 4D 75 6C 74 69 70 6C 61 79  |cripts/Multiplay|
0020: 65 72 05 00 2A 2E 70 79 63 21                    |er..*.pyc!|

Messages:
  [0] ACK seq=3
  [1] Reliable seq=1024 len=36 flags=0x80
      ChecksumReq: round=0xFF dir="Scripts/Multiplayer" filter="*.pyc" recursive=1
```

### Packet #24: ChecksumResp Round 0xFF (Fragmented)

```
Client -> Server, 275 bytes:
0000: 02 01 32 11 81 04 00 21 FF 3F D1 94 87 09 00 2D  |..2....!.?.....-|
0010: 71 11 2C 8F 73 CE 86 85 C0 A4 67 82 A7 61 59 29  |q.,.s.....g..aY)|
...
0110: 5B 4D 00                                         |[M.|

Messages:
  [0] Reliable seq=1024 len=17 flags=0x81
      ChecksumResp: round=0xFF, fragmented (~270 bytes hash data)
```

### Packet #25: Checksum Complete -> Settings + GameInit

```
Server -> Client, 65 bytes:
0000: 01 03 32 06 80 05 00 28 32 33 80 06 00 00 00 00  |..2....(23......|
0010: 05 42 61 00 25 00 4D 75 6C 74 69 70 6C 61 79 65  |.Ba.%.Multiplaye|
0020: 72 2E 45 70 69 73 6F 64 65 2E 4D 69 73 73 69 6F  |r.Episode.Missio|
0030: 6E 31 2E 4D 69 73 73 69 6F 6E 31 32 06 80 07 00  |n1.Mission12....|
0040: 01                                               |.|

Messages:
  [0] Reliable seq=1280 len=6 flags=0x80
      Opcode 0x28 (checksum-complete signal, no game payload)
  [1] Reliable seq=1536 len=51 flags=0x80
      Settings (0x00): gameTime=33.25 slot=0 map="Multiplayer.Episode.Mission1.Mission1"
  [2] Reliable seq=1792 len=6 flags=0x80
      GameInit (0x01): trigger, no payload
```

---

## Timing

From the stock dedi packet trace (loopback, negligible network latency):

| Event | Timestamp | Delta |
|-------|-----------|-------|
| Connect | 09:18:40.179 | - |
| ConnectAck + Round 0 | 09:18:40.181 | +2ms |
| Round 0 Response | 09:18:40.188 | +7ms |
| Round 1 Sent | 09:18:40.189 | +1ms |
| Round 1 Response | 09:18:40.190 | +1ms |
| Round 2 Sent | 09:18:40.191 | +1ms |
| Round 2 Response (frag 1) | 09:18:40.896 | +705ms |
| Round 3 Sent | 09:18:41.357 | +461ms |
| Round 3 Response | 09:18:41.360 | +3ms |
| Round 0xFF Sent | 09:18:41.361 | +1ms |
| Round 0xFF Response | 09:18:41.424 | +63ms |
| Settings + GameInit | 09:18:41.472 | +48ms |

**Total checksum phase**: ~1.3 seconds on loopback.

Note: Round 2 takes **705ms** for the client to respond (scanning all ship `.pyc` files recursively). This is by far the longest round. Over a real network, expect 1-2 seconds for this round.

---

## Post-Checksum Sequence

After all 5 rounds pass validation, the server sends three messages in a single packet:

1. **Opcode `0x28`** (6 bytes) -- Checksum-complete signal. No game-level payload. Appears to be a NetFile-layer acknowledgement.
2. **Opcode `0x00` (Settings)** -- Game configuration: game time (float), two config bytes, player slot (byte), map name (length-prefixed string), checksum correction flag.
3. **Opcode `0x01` (GameInit)** -- Single-byte trigger that tells the client to initialize the game with the settings received in opcode 0x00.

The client then transitions from the checksum/lobby state to the game loading state.

---

## Common Implementation Mistakes

### 1. Missing Round 3

The server MUST send all 5 rounds (0, 1, 2, 3, 0xFF). Skipping round 3 (`scripts/mainmenu`) and jumping straight to 0xFF after round 2 will cause the client to enter an inconsistent state.

### 2. Sending Rounds Simultaneously

The stock server sends each round only after receiving the previous round's response. Sending multiple rounds at once (e.g., queuing all 5 into the reliable transport together) may cause the client to process 0xFF early and transition to game state before completing earlier rounds.

### 3. Wrong BitByte Encoding

Writing the recursive flag as a plain `0x00`/`0x01` byte instead of the bit-packed format (`0x20`/`0x21`) will desynchronize the client's stream reader. All subsequent field reads will be shifted by the missing count bits, causing parse failures.

### 4. Not Handling Fragmented Responses

Round 2 and round 0xFF produce responses that exceed the reliable transport's maximum message size. These arrive as multiple fragments (observed: 3 fragments for round 2, 2+ for round 0xFF). The server must reassemble fragmented reliable messages before attempting to parse the ChecksumResp payload.

### 5. Missing Opcode 0x28

The stock server sends opcode `0x28` before Settings and GameInit. While its exact purpose is unclear from black-box observation, omitting it may affect client state.

---

## Checksum Validation

The server compares the client's hash data against its own computation for the same file set. If hashes match, the round passes. If not, the server can:
- Send an error (opcode `0x22` or `0x23`) and boot the player
- Allow a configurable tolerance (the Settings packet includes a checksum correction flag)

The hash algorithm itself is not documented here. For a reimplementation that doesn't validate checksums, the server can accept any response and proceed to the next round.

---

## Summary

| Property | Value |
|----------|-------|
| Total rounds | 5 (indices 0, 1, 2, 3, 0xFF) |
| Request opcode | `0x20` |
| Response opcode | `0x21` |
| Delivery | Reliable transport (sequenced, ACK'd) |
| Sequencing | Strict serial: send request, wait for response, repeat |
| Largest response | Round 2 (~400 bytes, fragmented) |
| Post-checksum | Opcode `0x28` + `0x00` Settings + `0x01` GameInit |
| Total duration (loopback) | ~1.3 seconds |
