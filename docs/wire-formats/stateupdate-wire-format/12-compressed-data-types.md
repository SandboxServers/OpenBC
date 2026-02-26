# Compressed Data Types


StateUpdate uses three compressed types for bandwidth efficiency.

### Bit-Packed Booleans

Multiple boolean values are packed into a single byte:

```
Byte layout:  [count:3][bits:count]
              MSB              LSB

count (bits 7-5): Number of booleans packed (1-5), stored as count
bits  (bits 4-0): The boolean values, one per bit position
```

Examples:
- Single false: `0x20` = `0b001_00000` (count=1, bit0=0)
- Single true: `0x21` = `0b001_00001` (count=1, bit0=1)
- Three booleans (true, false, false): `0x61` = `0b011_00001`

Up to 5 booleans can share a single byte. After 5 bits, the next boolean starts a new byte.

**Critical**: Writing a plain `0x00`/`0x01` instead of bit-packed `0x20`/`0x21` will misalign all subsequent field reads in the stream.

### CompressedFloat16 (cf16)

16-bit logarithmic float encoding used for speed, damage, and distance values.

```
Format: [sign:1][scale:3][mantissa:12]
        Bit 15     = sign (1 = negative)
        Bits 14-12 = scale exponent (0-7)
        Bits 11-0  = mantissa (0-4095)
```

**Encoding**:
1. If value < 0: set sign bit, negate value
2. Find scale (0-7) such that value falls within the range for that scale octave. Each successive scale covers a range BASE * MULT^scale
3. Compute mantissa: `truncate(value / range * 4096)`
4. If scale overflows (>= 8): clamp to scale=7, mantissa=4096
5. Result: `(sign_flag * 8 + scale) * 0x1000 + mantissa`

**Decoding**:
1. mantissa = encoded & 0xFFF
2. raw_scale = encoded >> 12
3. sign = (raw_scale >> 3) & 1
4. scale = raw_scale & 0x7
5. Compute range boundaries: lo=0, hi=BASE; for each scale step: lo=hi, hi=hi*MULT
6. result = (hi - lo) * mantissa / 4095.0 + lo
7. If sign: result = -result

**Precision note**: The encoder divides by 4096.0 while the decoder divides by 4095.0. This intentional asymmetry means round-trip encode/decode has less than 0.025% error.

Observed decoded values from packet traces:
- Speed: 5.130, 7.618, 7.598
- Damage: 50.0, 10.1
- Radius: 5997.8

### CompressedVector3 (cv3)

3-byte direction vector. Used for forward/up orientation and torpedo directions.

```
Wire format: [dirX:u8][dirY:u8][dirZ:u8] = 3 bytes

Each byte: truncate_to_int(component * 127.0)
Range: -1.0 to +1.0 per component (signed byte interpretation)
```

Example from TorpedoFire: bytes `DF 87 11` = direction (-0.26, -0.95, 0.13).

### CompressedVector4 (cv4)

5-byte position delta. A cv3 direction plus a CompressedFloat16 magnitude.

```
Wire format: [dirX:u8][dirY:u8][dirZ:u8][magnitude:u16] = 5 bytes

Direction: same as cv3 (signed bytes / 127.0)
Magnitude: CompressedFloat16 encoding (2 bytes, little-endian)
```

To reconstruct the delta vector: `(dirX * magnitude, dirY * magnitude, dirZ * magnitude)`.

Used for position deltas in StateUpdate and impact positions in Explosion messages.

