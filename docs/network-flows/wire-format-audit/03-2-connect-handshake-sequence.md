# 2. Connect Handshake Sequence


### What OpenBC does
```
C->S: Connect(0x03) dir=INIT
S->C: Connect(0x03) response with slot byte
S->C: ChecksumReq round 0 (separate packet)
```

### What traces show
```
C->S: [FF 01 03 0F C0 00 00 0A 0A 0A EF 5F 0A 00 00 00 00]
       dir=INIT, msgs=1, Connect(0x03) len=15

S->C: [01 02 03 06 C0 00 00 02 32 1B 80 00 00 20 00 ...]
       dir=S, msgs=2:
         msg0: Connect(0x03) len=6, payload=[00 00 02] (seq=0, slot=0x02)
         msg1: Reliable seq=0, ChecksumReq round 0
```

### Verdict: MATCH (with batching difference)
The stock dedi **batches** the Connect response and first ChecksumReq in a single UDP packet (msgs=2). OpenBC sends them separately. This is functionally equivalent -- the client handles both cases -- but the batching reduces round-trip latency.

**Slot assignment**: Stock dedi assigns slot 0x02 (wire_slot = peer_index + 1). First joiner gets slot 2 (index 1), matching OpenBC's `(u8)(slot + 1)`.

---

