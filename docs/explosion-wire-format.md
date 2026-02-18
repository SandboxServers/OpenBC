# Explosion Event Wire Format

This document describes the wire encoding of Explosion events (opcode `0x29`), including the CompressedFloat16 (CF16) format used for damage and radius fields.

**Clean room statement**: The CF16 format is described from observable wire data and the publicly-known algorithm structure. Scale constants were determined by encoding known damage values and observing the resulting wire bytes. No binary addresses, memory offsets, or decompiled code are referenced.

---

## 1. Explosion Packet (Opcode 0x29)

Sent from server to all clients when an explosion occurs (ship death, torpedo impact, etc.).

```
Offset  Size  Type    Field
------  ----  ----    -----
0       1     u8      opcode = 0x29
1       4     i32     object_id         (source object)
+0      var   cv4     impact_position   (CompressedVector4)
+0      2     u16     radius            (CompressedFloat16)
+0      2     u16     damage            (CompressedFloat16)
```

- **object_id**: The object that caused the explosion (e.g., the dying ship)
- **impact_position**: 3D position of the explosion center, compressed as CV4
- **radius**: Explosion damage radius, CF16 encoded
- **damage**: Explosion damage value, CF16 encoded

### Direction

Server → All Clients (never sent client → server).

Explosion events are resent to newly joining players if still active.

---

## 2. CompressedFloat16 (CF16) Format

CF16 is a custom 16-bit floating point format used for damage, radius, speed, and distance values throughout the wire protocol.

### Bit Layout

```
[sign:1][scale:3][mantissa:12]  = 16 bits total
```

- **sign** (bit 15): 0 = positive, 1 = negative
- **scale** (bits 14-12): 3-bit index selecting the value range (0-7)
- **mantissa** (bits 11-0): 12-bit linear interpolation within the selected range (0-4095)

### Scale Table

The format uses logarithmic scaling with a base of 0.001 and a multiplier of 10.0 per scale step. Each scale covers exactly one decimal order of magnitude:

| Scale | Range Low | Range High | Step Size | Useful For |
|-------|-----------|------------|-----------|------------|
| 0 | 0.0 | 0.001 | ~2.4×10⁻⁷ | Near-zero values |
| 1 | 0.001 | 0.01 | ~2.2×10⁻⁶ | Thousandths |
| 2 | 0.01 | 0.1 | ~2.2×10⁻⁵ | Hundredths |
| 3 | 0.1 | 1.0 | ~2.2×10⁻⁴ | Fractional values |
| 4 | 1.0 | 10.0 | ~0.0022 | Single digits |
| 5 | 10.0 | 100.0 | ~0.022 | Weapon damage (small) |
| 6 | 100.0 | 1,000.0 | ~0.22 | Weapon damage (medium) |
| 7 | 1,000.0 | 10,000.0 | ~2.2 | Large damage/radius |

**Maximum representable value**: 10,000.0 (scale 7, mantissa 4095).

**Overflow**: Values ≥ 10,000 are clamped to scale 7, mantissa 4095.

### Encoding Algorithm

```
function CF16_Encode(value):
    negative = (value < 0)
    if negative: value = -value

    scale = 0
    boundary = 0.001        // BASE
    prev_boundary = 0.0

    while scale < 8:
        if value < boundary:
            mantissa = truncate((value - prev_boundary) / (boundary - prev_boundary) * 4095.0)
            break
        prev_boundary = boundary
        boundary = boundary * 10.0    // MULT
        scale += 1

    if scale == 8:           // overflow
        mantissa = 0xFFF
        scale = 7

    if negative: scale = scale | 0x8

    return (scale << 12) | mantissa
```

**Key detail**: The encoder uses **truncation** (floor toward zero), NOT rounding. The encoded value is always ≤ the original value within the bin.

### Decoding Algorithm

```
function CF16_Decode(encoded):
    mantissa = encoded & 0xFFF
    scale_nibble = (encoded >> 12) & 0xF

    negative = (scale_nibble & 0x8) != 0
    if negative: scale_nibble = scale_nibble & 0x7

    range_lo = 0.0
    range_hi = 0.001         // BASE

    for i in 0 .. scale_nibble:
        range_lo = range_hi
        range_hi = range_lo * 10.0   // MULT

    result = (range_hi - range_lo) * mantissa * (1.0 / 4095.0) + range_lo

    if negative: result = -result
    return result
```

**Key detail**: The decoder uses `1.0 / 4095.0` (NOT `1.0 / 4096.0`). Mantissa 0 decodes exactly to `range_lo`; mantissa 4095 decodes exactly to `range_hi`.

---

## 3. Precision Characteristics

### General Properties

- **Relative precision**: ~0.024% per scale (1/4095 of the range width)
- **Absolute precision varies by scale**: sub-microsecond at scale 0, ~2.2 at scale 7
- **Always undershoots**: Due to truncation, decoded values are always ≤ original

### Integer Preservation by Scale

| Scale | Range | Integers Preserved by round()? |
|-------|-------|-------------------------------|
| 0-3 | 0-1 | N/A (sub-integer range) |
| 4 | 1-10 | YES — all 9 integers unique |
| 5 | 10-100 | YES — all 90 integers unique |
| 6 | 100-1000 | YES — all 900 integers unique |
| 7 | 1000-10000 | **NO** — step ~2.2 loses integer precision |

Below 1000, every integer value gets a unique CF16 encoding and survives `round(decoded)` faithfully. Above 1000, adjacent integers may share the same mantissa.

### Example Round-Trips

| Original | Encoded | Decoded | round() | Exact Match? |
|----------|---------|---------|---------|-------------|
| 0.5 | 0x371B | 0.4998 | 0 | YES (≤1.0) |
| 5.0 | 0x471B | 4.9978 | 5 | YES |
| 15.0 | 0x50E3 | 14.989 | 15 | YES |
| 25.0 | 0x52AA | 24.989 | 25 | YES |
| 100.0 | 0x5FFE | 99.978 | 100 | YES |
| 273.0 | 0x6313 | 272.967 | 273 | YES |
| 1000.0 | 0x6FFE | 999.780 | 1000 | YES |
| 1500.0 | 0x70E3 | 1498.90 | 1499 | **NO** |
| 2063.0 | 0x71E3 | 2061.54 | 2062 | **NO** |
| 5000.0 | 0x771B | 4997.80 | 4998 | **NO** |

---

## 4. Mod Weapon Type Identification

### The Problem

Mods (particularly BC Remastered) encode weapon type identifiers as specific damage float values in explosion events. After CF16 round-trip, the receiver must identify which weapon type caused the explosion.

### BC Remastered Values

| Weapon Type | Damage Value | CF16 Encoded | Decoded | Error |
|-------------|-------------|-------------|---------|-------|
| Type A | 15.0 | 0x50E3 | 14.989 | 0.011 |
| Type B | 25.0 | 0x52AA | 24.989 | 0.011 |
| Type C | 273.0 | 0x6313 | 272.967 | 0.033 |
| Type D | 2063.0 | 0x71E3 | 2061.539 | 1.461 |

All four values produce **unique CF16 encodings** — they are fully distinguishable.

### Matching Strategies

**Tolerance matching (recommended)**:
```
function identify_weapon(decoded_damage):
    for each (target, type_name) in weapon_types:
        if abs(decoded_damage - target) < 1.5:
            return type_name
    return "unknown"
```

A tolerance of 1.5 works for all four values. The minimum separation between any two BC Remastered values is 10.0 (between 15.0 and 25.0), so there is zero cross-match risk.

**Range matching**:
```
function identify_weapon(decoded_damage):
    if 14.0 < decoded_damage < 16.0: return "type_A"
    if 24.0 < decoded_damage < 26.0: return "type_B"
    if 272.0 < decoded_damage < 274.0: return "type_C"
    if 2060.0 < decoded_damage < 2064.0: return "type_D"
    return "unknown"
```

**Exact CF16 comparison** (if raw uint16 is available):
```
EXPECTED = {0x50E3: "type_A", 0x52AA: "type_B", 0x6313: "type_C", 0x71E3: "type_D"}
function identify_weapon(raw_cf16): return EXPECTED[raw_cf16]
```

### Design Guidelines for New Weapon Type IDs

| Damage Range | Minimum Spacing | Notes |
|-------------|----------------|-------|
| 1 - 10 | 1 | All integers unique |
| 10 - 100 | 1 | All integers unique |
| 100 - 1,000 | 1 | All integers unique |
| 1,000 - 10,000 | **3** | Step ~2.2; adjacent integers may collide |

**Recommended**: Choose weapon type IDs below 1,000 where every integer is uniquely representable. If values above 1,000 are needed, space them at least 3 apart and use tolerance-based matching.

---

## 5. Other CF16 Fields in the Protocol

CF16 is used for several wire protocol fields beyond explosions:

| Context | Field | Typical Range |
|---------|-------|--------------|
| Explosion (0x29) | damage | 0-5000 |
| Explosion (0x29) | radius | 0-10000 |
| StateUpdate | speed values | 0-100 |
| CompressedVector | magnitude | 0-1000 |

All CF16 fields share the same scale table and precision characteristics documented above.

---

## Related Documents

- **[phase1-verified-protocol.md](phase1-verified-protocol.md)** -- Full wire protocol reference (Section 17: Explosion)
- **[combat-system.md](combat-system.md)** -- How explosion damage feeds into the damage pipeline
- **[stateupdate-wire-format.md](stateupdate-wire-format.md)** -- StateUpdate fields that also use CF16
