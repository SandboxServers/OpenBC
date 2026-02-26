# 10. Server Keepalive Format


### What OpenBC does
Sends minimal keepalive: `[0x00][0x02]` (2 bytes, type=keepalive, totalLen=2, no body)

### What traces show
Server sends full keepalive with identity data (22 bytes):
```
Packet #11 (S->C):
  00 16 C0 01 00 02 0A 0A 0A EF 43 00 61 00 64 00 79 00 32 00 00 00
```

Format: `[0x00][totalLen=0x16][flags=0xC0][?=0x01][?=0x00][slot=0x02][IP:4][name_utf16le]`

The server echoes back the client's keepalive data (same format the client sent). This includes:
- flags (0xC0 = reliable+priority markers in the keepalive context)
- some unknown bytes
- the client's wire slot
- the client's IP address
- the client's UTF-16LE name

### Verdict: **MISMATCH -- MEDIUM severity**
The minimal keepalive may work for keeping the connection alive, but the stock server mirrors the client's identity data back. The client might expect this echo for connection state validation.

**Possible fix**: Cache the client's keepalive data and echo it back instead of sending minimal keepalives. Or construct a server-side keepalive with the dedi's own identity.

---

