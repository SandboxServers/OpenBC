# 9. Missing Opcode 0x28


### What OpenBC does
Does not send opcode 0x28 at any point.

### What traces show
The server sends opcode 0x28 as the FIRST message after checksum completion, BEFORE Settings:

```
Packet #27 (S->C), msg0:
  32 06 80 05 00 28
  Reliable seq=1280, payload=[28] (1 byte, opcode 0x28)
```

Sequence:
1. Checksum final round response received
2. Server sends: 0x28 (Unknown_28) -- 1 byte, no payload
3. Server sends: 0x00 (Settings) -- 46 bytes
4. Server sends: 0x01 (GameInit) -- 1 byte

All three are batched in one packet.

### Verdict: **MISMATCH -- HIGH severity**
The stock dedi always sends opcode 0x28 before Settings. This opcode is listed in `opcodes.h` as `BC_OP_UNKNOWN_28` (0x28). Its purpose is unclear from the name, but it may signal "checksum exchange complete" or trigger client-side state transition. Omitting it may cause the client to mishandle the subsequent Settings message.

**Fix needed**: Send `[0x28]` (1 byte reliable) before Settings.

---

