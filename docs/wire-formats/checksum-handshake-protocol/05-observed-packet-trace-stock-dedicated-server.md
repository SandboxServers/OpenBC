# Observed Packet Trace (Stock Dedicated Server)


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

