# 4. Wire Format


### No Game-Layer Header

Script messages have **zero additional framing** between the type 0x32 transport envelope and the script payload. The game-layer payload starts directly with the script's first `WriteChar` byte.

```
UDP packet (decrypted):
  [peer_id:1][msg_count:1]
    [0x32][flags_len:2 LE][seq:2 if reliable]  ← transport envelope
    [script_payload...]                          ← starts with message type byte
```

### Payload = Exactly What the Script Wrote

When a script writes:
```python
kStream.WriteChar(chr(0x38))  # END_GAME_MESSAGE
kStream.WriteInt(2)            # reason code
```

The TGMessage payload is exactly 5 bytes: `38 02 00 00 00`

The transport layer wraps this with its own header (type byte + flags_len + optional seq_num), but the game-layer payload is the raw stream content with no additions.

---

