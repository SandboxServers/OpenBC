# Stream Primitives — Clean Room Specification

Specification of the serialization primitives used in Bridge Commander's multiplayer protocol: the buffer stream, bit packing, CompressedFloat16 (CF16), and CompressedVector3/4 types.

**Clean room statement**: This specification describes wire format behavior as observable from network packet captures and the game's shipped scripting API. No binary addresses, memory offsets, or decompiled code are referenced.

**Implementation criticality**: These primitives underpin ALL wire formats in the protocol. Incorrect implementation of CF16 or bit packing will cause deserialization failures across every opcode that uses them.

---

## TGBufferStream

All multiplayer serialization uses a buffer stream object with sequential read/write semantics. The stream tracks:

- **Buffer pointer**: The underlying byte array
- **Buffer capacity**: Maximum size of the buffer
- **Current position**: Read/write cursor (advances after each operation)
- **Bit-packing bookmark**: Position where the current bit-packed byte started
- **Bit-packing state**: Whether a bit-packed group is active (0 = no active group)

### Primitive Read/Write Operations

| Operation | Wire Size | Description |
|-----------|-----------|-------------|
| WriteByte / ReadByte | 1 byte | Unsigned 8-bit integer |
| WriteShort / ReadShort | 2 bytes | Unsigned 16-bit integer, little-endian |
| WriteInt32 / ReadInt32 | 4 bytes | Signed/unsigned 32-bit integer, little-endian |
| WriteFloat / ReadFloat | 4 bytes | IEEE 754 float32, little-endian |
| WriteBytes / ReadBytes | N bytes | Raw byte array (memcpy) |
| WriteBit / ReadBit | 0-1 bytes | Packed boolean bit (see Bit Packing) |

All multi-byte integers and floats are **little-endian** (x86 native byte order).

---

## Bit Packing

`WriteBit` and `ReadBit` pack multiple boolean values into a single byte, using a compact scheme that avoids wasting a full byte per boolean.

### Byte Layout

```
Bit layout: [count:3][bits:5]
            MSB          LSB

count (bits 7-5): Number of bits stored in this byte (1-5)
bits  (bits 4-0): The actual boolean values, one per bit position
```

A single byte can hold up to **5 boolean values**.

### Write State Machine

1. **First WriteBit**: Allocates a new byte at the current stream position. Sets bit 0 to the value, count = 1.
2. **Subsequent WriteBit calls**: OR the value into the next bit position (bit 1, bit 2, etc.). Increment count.
3. **After 5 bits**: The byte is full. The next WriteBit starts a fresh byte.
4. **Non-bit write**: If a WriteByte, WriteShort, etc. is called while mid-pack, the current bit-packed byte is finalized and the non-bit write proceeds at the next position.

### Read State Machine

1. **ReadBit** extracts each bit in order from the packed byte, tracking how many have been consumed.
2. When all packed bits in the current byte are consumed (count exhausted), the next ReadBit reads a fresh byte from the stream.

### Example

Writing three booleans (true, false, true) followed by an int32:

```
Byte at position N: [count=3] [bit2=1, bit1=0, bit0=1] = 0b011_00101 = 0x65
Bytes at position N+1..N+4: int32 value (4 bytes, little-endian)
```

**Alignment**: Bit packing does NOT enforce byte alignment before non-bit operations. The bit-packed byte occupies exactly 1 byte regardless of how many bits were packed. Non-bit operations always start at the next byte boundary after the bit-packed byte.

---

## CompressedFloat16 (CF16)

A 16-bit lossy floating-point compression format used throughout the protocol for values in the range [0, 10000]. Provides approximately 0.022% relative precision across 8 logarithmic decades.

### Bit Layout

```
[sign:1][scale:3][mantissa:12] = 16 bits total

Bit 15     = sign (1 = negative, 0 = positive)
Bits 14-12 = scale exponent (0-7)
Bits 11-0  = mantissa (0-4095)
```

### Scale Table

| Scale | Range Low | Range High | Step Size | Relative Precision |
|-------|-----------|------------|-----------|-------------------|
| 0 | 0 | 0.001 | 2.442e-7 | ~0.024% |
| 1 | 0.001 | 0.01 | 2.198e-6 | ~0.022% |
| 2 | 0.01 | 0.1 | 2.198e-5 | ~0.022% |
| 3 | 0.1 | 1.0 | 2.198e-4 | ~0.022% |
| 4 | 1.0 | 10.0 | 2.198e-3 | ~0.022% |
| 5 | 10.0 | 100.0 | 2.198e-2 | ~0.022% |
| 6 | 100.0 | 1000.0 | 2.198e-1 | ~0.022% |
| 7 | 1000.0 | 10000.0 | 2.198 | ~0.022% |

Scale boundaries are powers of 10 multiplied by the BASE constant:
- Scale 0: [0, BASE)
- Scale N: [BASE × 10^(N-1), BASE × 10^N)

Where **BASE = 0.001** and the multiplier is **10.0**.

### Encoder Algorithm

```
function CF16_Encode(value: float) -> uint16:
    sign = (value < 0)
    if sign: value = -value

    // Find the scale bucket
    scale = 0
    boundary = BASE    // 0.001
    while scale < 8:
        if value < boundary: break
        boundary *= 10.0
        scale++

    if scale >= 8:
        // Overflow: clamp to maximum representable value
        return sign ? 0xFFFF : 0x7FFF

    // Compute range for this scale
    if scale == 0:
        lo = 0.0
        hi = BASE
    else:
        lo = BASE * pow(10.0, scale - 1)
        hi = BASE * pow(10.0, scale)

    // Fractional position in range, quantized to 4096 levels
    frac = (value - lo) / (hi - lo)
    mantissa = truncate_to_int(frac * 4095.0)
    mantissa = min(mantissa, 4095)

    return ((sign << 3) | scale) << 12 | mantissa
```

**Critical**: The encoder uses **truncation** (floor toward zero), NOT rounding. This means encoded values always round DOWN.

### Decoder Algorithm

```
function CF16_Decode(raw: uint16) -> float:
    mantissa = raw & 0xFFF
    scale = (raw >> 12) & 0x7
    sign = (raw >> 15) & 1

    // Rebuild range iteratively
    lo = 0.0
    hi = BASE    // 0.001
    for i in 0..scale:
        lo = hi
        hi = lo * 10.0

    // Decode: linear interpolation within the range
    result = (hi - lo) * mantissa * DECODER_INVERSE + lo

    if sign: result = -result
    return result
```

Where **DECODER_INVERSE = float32(1/4095)** ≈ 0.000244200258.

### Critical Implementation Detail: 1/4095, NOT 1/4096

The decoder constant is `1/4095`, making the system **symmetric**:
- Mantissa 0 decodes to the bottom of the range (lo)
- Mantissa 4095 decodes to exactly the top of the range (hi)
- 4096 discrete levels: 0/4095, 1/4095, 2/4095, ..., 4095/4095

Using `1/4096` instead of `1/4095` would introduce a systematic bias where mantissa 4095 never quite reaches the top of the range. This WILL cause deserialization mismatches.

### Precision Characteristics

- Encoding is **lossy** — values always round down due to truncation
- Maximum error per scale is one step size (the truncation residual)
- The float32 representation of 1/4095 introduces negligible additional bias (~6e-6% relative error)
- **No integer value exactly survives a CF16 round-trip** — even values at range boundaries (1.0, 10.0, 100.0, 1000.0) have ~0.0002 to ~0.002 error
- Maximum representable value: ~10,000.0

### Mod Compatibility Note

Some mods use specific damage float values as weapon type identifiers, checking `GetDamage()` for exact values. These values pass through CF16 compression and do NOT survive the round-trip. For example:
- 15.0 decodes to ~14.989
- 273.0 decodes to ~272.967
- 2063.0 decodes to ~2061.539

Mod-compatible implementations should use tolerance comparison (`abs(damage - expected) < step_size`) or `round(damage)` rather than exact equality.

### Where CF16 Is Used

- **StateUpdate (0x1C)**: Speed field (flag 0x10)
- **Explosion (0x29)**: Radius and damage fields
- **CompressedVector3/4**: Magnitude component
- All positions, velocities, and distances encoded as compressed vectors

---

## CompressedVector3

A 5-byte encoding of a 3D vector as a normalized direction (3 bytes) plus a CF16 magnitude (2 bytes).

### Wire Format

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      dirX        Normalized X direction (signed byte)
1       1     u8      dirY        Normalized Y direction (signed byte)
2       1     u8      dirZ        Normalized Z direction (signed byte)
3       2     u16     magnitude   CF16-encoded magnitude
```

**Total: 5 bytes**

### Encoding

```
function WriteCompressedVector3(dx, dy, dz):
    magnitude = sqrt(dx*dx + dy*dy + dz*dz)

    if magnitude <= epsilon:
        // Zero vector: write zero direction and zero magnitude
        write_byte(0)
        write_byte(0)
        write_byte(0)
        write_uint16(CF16_Encode(0.0))
        return

    dirX = truncate_to_byte(dx / magnitude * DIRECTION_SCALE)
    dirY = truncate_to_byte(dy / magnitude * DIRECTION_SCALE)
    dirZ = truncate_to_byte(dz / magnitude * DIRECTION_SCALE)
    magnitude_compressed = CF16_Encode(magnitude)

    write_byte(dirX)
    write_byte(dirY)
    write_byte(dirZ)
    write_uint16(magnitude_compressed)
```

### Decoding

```
function ReadCompressedVector3() -> (float, float, float):
    dirX = read_byte()
    dirY = read_byte()
    dirZ = read_byte()
    magnitude = CF16_Decode(read_uint16())

    // Reconstruct vector from direction bytes + magnitude
    return decompress(dirX, dirY, dirZ, magnitude)
```

The direction bytes encode a unit vector using signed byte quantization. The exact decompression reconstructs the original vector by scaling the unit direction by the CF16 magnitude.

---

## CompressedVector4

A variant of CompressedVector3 with a configurable 4th component that can be either a CF16 (2 bytes) or a raw float32 (4 bytes).

### Wire Format (CF16 magnitude, 5 bytes)

When the 4th parameter flag is set (most common):

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      dirX        Normalized X direction
1       1     u8      dirY        Normalized Y direction
2       1     u8      dirZ        Normalized Z direction
3       2     u16     magnitude   CF16-encoded magnitude
```

### Wire Format (float32 magnitude, 7 bytes)

When the 4th parameter flag is NOT set:

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      dirX        Normalized X direction
1       1     u8      dirY        Normalized Y direction
2       1     u8      dirZ        Normalized Z direction
3       4     f32     magnitude   Raw float32 magnitude
```

The choice between CF16 and float32 magnitude is made by the caller and must match between encoder and decoder. The decoder reads the same flag to determine which format to expect.

---

## Implementation Requirements

An OpenBC implementation SHALL:

1. **Implement TGBufferStream** with sequential read/write, byte-order-aware integer operations, and bit packing
2. **Implement bit packing** using the [count:3][bits:5] byte layout with up to 5 bits per byte
3. **Implement CF16 encoding** using BASE=0.001, MULT=10.0, ENCODE_SCALE=4095.0, truncation (not rounding)
4. **Implement CF16 decoding** using DECODER_INVERSE=float32(1/4095), iterative range computation
5. **Implement CompressedVector3** as 3 direction bytes + CF16 magnitude (5 bytes total)
6. **Implement CompressedVector4** with conditional CF16 (5 bytes) or float32 (7 bytes) magnitude
7. **Match the encoder/decoder exactly** — even small differences (1/4096 vs 1/4095, rounding vs truncation) will cause protocol incompatibility

---

## Constants

| Name | Value | Description |
|------|-------|-------------|
| CF16_BASE | 0.001 | Scale range base (float32) |
| CF16_MULT | 10.0 | Scale multiplier (float32) |
| CF16_ENCODE_SCALE | 4095.0 | Encoder mantissa multiplier |
| CF16_DECODE_INVERSE | float32(1/4095) ≈ 0.000244200258 | Decoder mantissa inverse |
| CF16_MAX_SCALE | 7 | Maximum scale exponent |
| CF16_MAX_MANTISSA | 4095 | Maximum mantissa value (0xFFF) |
| CF16_OVERFLOW_POS | 0x7FFF | Positive overflow encoding |
| CF16_OVERFLOW_NEG | 0xFFFF | Negative overflow encoding |
| BITS_PER_PACK_BYTE | 5 | Maximum boolean bits per packed byte |
