# 1. Transport Framing


### What OpenBC does
- Header: `[direction:1][msg_count:1]` (2 bytes)
- ACK: `[0x01][counter:1][0x00][flags:1]` (4 bytes fixed)
- Reliable: `[0x32][totalLen:1][flags:1][seqHi:1][seqLo:1][payload...]`
- Other: `[type:1][totalLen:1][data...]`

### What traces show
Identical framing. Examples from trace packet #8 (S->C):
```
01 02 03 06 C0 00 00 02 32 1B 80 00 00 20 00 08 00 73 63 72 69 70 74 73 2F ...
^^ ^^ -- msgs=2
      |-- msg0: Connect(0x03) len=6 body=4
                              |-- msg1: Reliable seq=0 len=27
```

### Verdict: MATCH
Transport framing is correct. Connect-type messages (0x03-0x06) use the same `[type][totalLen][data]` format as the "Other" case, where `totalLen` includes the type byte. The 2-byte flags_len encoding (bit15=reliable, bit14=priority, bits13-0=totalLen) happens to have the total length in the first byte for messages under 256 bytes.

**Note**: OpenBC's generic parser correctly handles Connect messages because it reads `totalLen = data[pos+1]`, which works for small messages. For messages >255 bytes, a 2-byte length parser would be needed, but Connect messages are always small.

---

