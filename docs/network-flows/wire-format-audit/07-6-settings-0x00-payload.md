# 6. Settings (0x00) Payload


### What OpenBC sends
```
[0x00][gameTime:f32][collision:bit][friendlyFire:bit][playerSlot:u8]
[mapLen:u16][mapName:bytes][checksumFlag:bit]
```
Total: 1 + 4 + 1(bits) + 1 + 2 + mapLen + 0(bits shared) = varies
Bit byte: `0x41` (count=2 with OpenBC's encoding, 3 bits)

### What traces show
```
Packet #27 (stock dedi) Settings payload (46 bytes):
  00 00 32 98 42 61 00 25 00 4D...31
  ^  |---f32----| ^  ^  |u16-| |--mapName--|
  op             bit sl
```

Byte-by-byte:
- `00`: opcode 0x00
- `00 32 98 42`: f32 gameTime = 76.10 (LE: 0x42983200)
- `61`: bit-packed byte (count=3, 3 bits: collision=1, friendlyFire=0, checksumCorr=0)
- `00`: playerSlot = 0
- `25 00`: mapLen = 37
- 37 bytes: "Multiplayer.Episode.Mission1.Mission1"

### Verdict: **MISMATCH due to bit-packing bug (#5)**
The field order and types are correct. The only difference is the bit-packed byte encoding:
- Real: `0x61` (count=3)
- OpenBC: `0x41` (count=2)

**Player slot note**: Both traces show slot=0. This means the server assigns slot 0 to the joining player in Settings, even though the wire_slot (direction byte) is 2. The slot in Settings appears to be the player's game-level slot, not the network slot. OpenBC sends `(u8)peer_slot` which starts at 1 for the first joiner. This may need investigation -- the stock dedi sends 0.

Actually, re-reading: the stock dedi uses slot 0 for the dedi server itself. But the decoded Settings shows `slot=0`. Let me re-examine. OpenBC sends `player_slot` as the peer_slot value. If the first joiner is at array index 1, OpenBC sends slot=1. But the trace shows slot=0.

Wait -- the `00` after the bit byte could be the player slot. Both traces show `00` (slot 0 for both). But the first joiner gets wire_slot=2 (peer index 1). So the Settings `slot` value is different from the wire slot. The Settings handler writes the byte via the stream writer, where the value is derived from a player slot lookup function. The result is the PLAYER SLOT INDEX (0-15), not the network peer index.

**Possible issue**: OpenBC might be sending the wrong slot value. Need to verify whether the stock dedi player slot 0 represents something different from our peer index. However, since slot=0 is the dedi server and first joiner should be slot 1, the trace showing slot=0 is suspicious. It might mean the "player slot" in Settings is actually a different numbering scheme.

---

