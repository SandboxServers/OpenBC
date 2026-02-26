# 5. TGBufferStream Bit-Packing (CRITICAL)


### What OpenBC does
The `bc_buf_write_bit` function stores the count as `bit_count - 1`:
```c
byte = (byte & 0x1F) | (((buf->bit_count - 1) & 0x7) << 5);
```

For 1 bit:  count field = 0 (byte = 0x00 | value)
For 2 bits: count field = 1 (byte = 0x20 | values)
For 3 bits: count field = 2 (byte = 0x40 | values)

### What the real TGBufferStream does
From the stock client's bit-packing writer:
```c
bVar3 = (bVar3 >> 5) + 1;   // increment count
...
data[bookmark] = bVar2 | bVar3 * 0x20;   // store count * 32
```

For 1 bit:  count field = 1 (byte = 0x20 | value)
For 2 bits: count field = 2 (byte = 0x40 | values)
For 3 bits: count field = 3 (byte = 0x60 | values)

### Wire evidence
Settings payload bit byte (both traces): `0x61 = (3<<5) | 0x01`

With 3 booleans (collision=1, friendlyFire=0, checksumCorrection=0):
- Real encoding: `(3<<5) | 0x01 = 0x61` -- MATCHES trace
- OpenBC encoding: `((3-1)<<5) | 0x01 = 0x41` -- DOES NOT match

Checksum request recursive=false bit byte: `0x20`
- Real encoding: `(1<<5) | 0x00 = 0x20` -- MATCHES trace
- OpenBC encoding: `((1-1)<<5) | 0x00 = 0x00` -- DOES NOT match

### Impact
The BC client's bit-packing reader uses `1 << count_field` as the threshold to determine how many bits to read from the packed byte. If OpenBC sends count=2 but the client expects count=3 (because the client's reader is calibrated for the real encoding), the reader will terminate after 2 bits and start a new bit group for the 3rd ReadBit, consuming an extra byte from the stream. **This corrupts the entire stream parse from that point forward.**

### Verdict: **CRITICAL -- will break all messages containing bit-packed fields**
Affected messages include:
- Settings (0x00): 3 booleans
- ChecksumReq (0x20): 1 boolean (recursive flag)
- UICollisionSetting (0x16): 1 boolean
- StateUpdate (0x1C): CompressedVector uses bits internally
- Any message using WriteBit

### Fix required
Change `bc_buf_write_bit` to store count (not count-1):
```c
// WRONG (current):
byte = (byte & 0x1F) | (((buf->bit_count - 1) & 0x7) << 5);

// CORRECT:
byte = (byte & 0x1F) | ((buf->bit_count & 0x7) << 5);
```

Change `bc_buf_read_bit` to NOT add 1 when reading count:
```c
// WRONG (current):
u8 count = ((byte >> 5) & 0x7) + 1;

// CORRECT:
u8 count = (byte >> 5) & 0x7;
```

---

