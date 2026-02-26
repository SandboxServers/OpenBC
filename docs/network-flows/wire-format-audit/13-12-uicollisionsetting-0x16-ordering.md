# 12. UICollisionSetting (0x16) Ordering


### What OpenBC does
Sends UICollisionSetting (0x16) BETWEEN Settings and GameInit:
```
Settings (0x00) -> UICollisionSetting (0x16) -> GameInit (0x01)
```

### What traces show
NO UICollisionSetting (0x16) is sent during the handshake. The server sends:
```
0x28 (Unknown_28) -> 0x00 (Settings) -> 0x01 (GameInit)
```

UICollisionSetting may be sent later or not at all during the initial handshake sequence.

### Verdict: **MISMATCH -- HIGH severity**
Sending 0x16 during handshake is not observed in traces. The collision setting is already included in the Settings (0x00) message via the bit-packed flags. The 0x16 opcode may be used during gameplay for runtime setting changes, not during initial setup. Sending it during handshake might confuse the client's state machine.

**Fix needed**: Remove UICollisionSetting from the handshake sequence.

---

