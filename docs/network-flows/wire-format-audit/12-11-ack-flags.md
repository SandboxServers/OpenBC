# 11. ACK Flags


### What OpenBC does
Always sends ACK with flags=0x80:
```c
bc_outbox_add_ack(&peer->outbox, seq, 0x80);
```

### What traces show
Server ACKs use varying flags:
```
01 00 00 02  -> flags=0x02 (after Connect/keepalive)
01 01 00 02  -> flags=0x02
01 00 00 00  -> flags=0x00 (after reliable game messages)
01 02 00 01  -> flags=0x01
```

The pattern suggests:
- flags=0x02: ACK for keepalive/connection messages
- flags=0x00: ACK for standard reliable messages
- flags=0x01: ACK for fragment-capable messages

### Verdict: **MISMATCH -- LOW severity**
The client likely doesn't use the ACK flags byte for anything critical (it just needs the seq number to clear its retransmit queue). However, using 0x80 is an unusual value not seen in any trace. Safer to use 0x00 as the default.

**Suggested fix**: Change default ACK flags to 0x00 instead of 0x80.

---

