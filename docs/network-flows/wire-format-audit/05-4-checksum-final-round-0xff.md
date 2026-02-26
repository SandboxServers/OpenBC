# 4. Checksum Final Round (0xFF)


### What OpenBC does
Sends minimal: `[0x20][0xFF]` (2 bytes)

### What traces show
Sends a FULL checksum request with round=0xFF:
```
Packet #24 (stock dedi) payload:
  20 FF 13 00 53 63 72 69 70 74 73 2F 4D 75 6C 74
  69 70 6C 61 79 65 72 05 00 2A 2E 70 79 63 21
```

Decoded:
```
opcode  = 0x20 (ChecksumReq)
round   = 0xFF
dirLen  = 0x0013 (19)
dir     = "Scripts/Multiplayer"    <-- Note capital S
filterLen = 0x0005 (5)
filter  = "*.pyc"
recursive = true  (bit byte = 0x21)
```

### Verdict: **MISMATCH -- HIGH severity**
OpenBC sends only `[0x20][0xFF]`. The real server sends a complete checksum request for `Scripts/Multiplayer/*.pyc` (recursive). The client expects to scan this directory and return checksums. Sending only 2 bytes will likely cause a parse error on the client side, or the client will respond with incomplete/wrong data.

**Fix needed**: Add a 5th checksum round definition:
```
{ "Scripts/Multiplayer", "*.pyc", true }
```

Note: capital "S" in "Scripts" (differs from rounds 0-3 which use lowercase "scripts/").

---

