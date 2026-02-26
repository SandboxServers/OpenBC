# 8. NewPlayerInGame (0x2A) Direction


### What OpenBC does
Server sends `[0x2A][0x20]` to ALL clients after completing handshake:
```c
u8 npig[2] = { BC_OP_NEW_PLAYER_IN_GAME, 0x20 };
send_to_all(npig, 2);
```

### What traces show
**0x2A is sent by the CLIENT to the SERVER**, not the other way around:
```
Packet #30 (C->S): [02 01 32 07 80 05 00 2A 20]
  dir=CLIENT, msgs=1, Reliable: [0x2A 0x20]
```

After the server sends Settings + GameInit, the CLIENT processes them and sends 0x2A back to the server. The server then responds with MissionInit (0x35).

Both trace sessions confirm: 0x2A is always client-to-server. No server-to-client 0x2A was observed.

### Verdict: **MISMATCH -- HIGH severity**
OpenBC proactively sends 0x2A, but it should WAIT for the client to send it. The correct flow:
1. Server sends Settings (0x00) + GameInit (0x01)
2. Client processes, sends NewPlayerInGame (0x2A) back
3. Server receives 0x2A, then sends MissionInit (0x35)

**Fix needed**: Remove the `send_to_all(npig, 2)` call. Add a handler for incoming 0x2A that triggers MissionInit send.

---

