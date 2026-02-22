# Transport Cipher Specification

The BC multiplayer protocol encrypts all game traffic with a stream cipher keyed on the string `"AlbyRules!"`. GameSpy traffic (first byte `\`) is transmitted in plaintext and is NOT encrypted.

**Clean room statement**: This document describes the cipher algorithm as implemented in `src/protocol/cipher.c`. No binary addresses, memory offsets, or decompiled code are referenced.

---

## Overview

The cipher is **not** a simple repeating XOR. It is a PRNG-based stream cipher with plaintext feedback, meaning each byte's output depends on all preceding plaintext bytes. The cipher state resets at the start of every packet (no session state across packets).

Key properties:
- **Key**: `"AlbyRules!"` (10 ASCII bytes: `0x41 0x6C 0x62 0x79 0x52 0x75 0x6C 0x65 0x73 0x21`)
- **Byte 0 is unencrypted**: The first byte of each UDP packet (the direction flag: `0x01`, `0x02`, or `0xFF`) is transmitted in cleartext. Encryption begins at byte 1.
- **Per-packet reset**: The cipher state is initialized fresh for each packet
- **Encrypt/decrypt are NOT symmetric**: They differ in when plaintext feedback is applied

---

## Algorithm

### State

The cipher maintains the following state, initialized to zero at the start of each packet:

```
key_string[10]     -- working copy of the key (starts as "AlbyRules!")
key_word[5]         -- 5 key words derived from key_string pairs
running_sum         -- PRNG running accumulator
state_a             -- PRNG cross-multiplication state
round_counter       -- current PRNG round (0-4)
prng_output         -- output of last PRNG step
accumulator         -- XOR of all 5 PRNG outputs
byte_state          -- sign-extended input byte
```

### Key Schedule (per byte)

For each payload byte, a full 5-round key schedule runs:

1. **Derive 5 key words** from consecutive byte pairs in `key_string`:
   ```
   key_word[0] = key_string[0] * 256 + key_string[1]
   key_word[1] = (key_string[2] * 256 + key_string[3]) XOR key_word[0]
   key_word[2] = (key_string[4] * 256 + key_string[5]) XOR key_word[1]
   key_word[3] = (key_string[6] * 256 + key_string[7]) XOR key_word[2]
   key_word[4] = (key_string[8] * 256 + key_string[9]) XOR key_word[3]
   ```

2. **Run 5 PRNG rounds** (one per key word), accumulating outputs:
   ```
   For round r = 0..4:
     mix         = running_sum + r
     cross1      = mix * 0x4E35
     cross2      = key_word[r] * 0x015A
     running_sum = state_a + cross1 + cross2
     state_a     = cross2
     key_word[r] = key_word[r] * 0x4E35 + 1
     prng_output = running_sum XOR key_word[r]
   ```

3. **Compute accumulator** as XOR of all 5 `prng_output` values:
   ```
   accumulator = prng_output[0] XOR prng_output[1] XOR ... XOR prng_output[4]
   ```

### Per-Byte Encryption

```
For each plaintext byte P[i] (starting at byte 1 of the packet):
  1. Sign-extend P[i] to a 32-bit integer (MOVSX behavior: 0x80-0xFF become negative)
  2. Run key schedule (produces accumulator)
  3. Compute ciphertext: C[i] = P[i] XOR (accumulator & 0xFF) XOR (accumulator >> 8)
  4. Feed PLAINTEXT P[i] back into key: key_string[j] ^= P[i] for all j in 0..9
```

### Per-Byte Decryption

```
For each ciphertext byte C[i]:
  1. Sign-extend C[i] to a 32-bit integer
  2. Run key schedule (produces accumulator)
  3. Compute plaintext: P[i] = C[i] XOR (accumulator & 0xFF) XOR (accumulator >> 8)
  4. Feed PLAINTEXT P[i] back into key: key_string[j] ^= P[i] for all j in 0..9
```

### Encrypt vs Decrypt Asymmetry

The critical difference:
- **Encrypt**: plaintext is available *before* the XOR, so feedback uses the original plaintext byte
- **Decrypt**: plaintext is available *after* the XOR, so feedback uses the computed plaintext byte

Both sides feed the same plaintext value into the key, ensuring the key schedules stay synchronized.

---

## Sign Extension Behavior

The input byte is **sign-extended** to a 32-bit integer before processing (equivalent to x86 `MOVSX`). This means:
- Bytes 0x00-0x7F are zero-extended (0x41 → 0x00000041)
- Bytes 0x80-0xFF are sign-extended (0x80 → 0xFFFFFF80, 0xFF → 0xFFFFFFFF)

The sign-extended value is stored in `byte_state` and used as the initial value for the XOR computation. The final output is truncated back to a single byte.

---

## PRNG Constants

| Constant | Value | Role |
|----------|-------|------|
| Multiplier | `0x4E35` (20021) | LCG multiplication factor |
| Addend | `0x015A` (346) | Cross-multiplication factor |
| Key word update | `key_word * 0x4E35 + 1` | Per-round key word advancement |
| Accumulator mask | `& 0xFF` and `>> 8` | Low byte and second byte extraction |

---

## Implementation Notes

- All arithmetic is 32-bit signed integer (C `int` type)
- The PRNG produces different outputs for encrypt vs decrypt only if the feedback diverges (which it doesn't, since both feed back plaintext)
- The `round_counter` resets to 0 after each key schedule (5 rounds per byte)
- Running `running_sum`, `state_a`, and `key_word[]` persist across bytes within a packet
- `key_string[]` mutates via plaintext feedback, making the cipher position-dependent

---

## Packet-Level Usage

```
Encrypt a packet:
  1. Leave byte 0 (direction) as-is
  2. Call encrypt_payload(data + 1, len - 1)

Decrypt a packet:
  1. Read byte 0 (direction) in cleartext
  2. Call decrypt_payload(data + 1, len - 1)
```

GameSpy packets (identified by first byte = `\` = 0x5C after decryption attempt) are never encrypted. The transport layer distinguishes game traffic from GameSpy traffic before applying the cipher.

---

## Test Vectors

To verify a cipher implementation, encrypt the following plaintext (after the direction byte) and compare:

**Input** (direction byte 0x01 + 5 payload bytes):
```
Hex: 01 00 00 00 00 00
```

After encrypting bytes 1-5 with `"AlbyRules!"`:
- Byte 1 (plaintext 0x00): key schedule runs with initial key, accumulator computed, XOR with 0x00 produces the raw keystream byte
- Each subsequent byte feeds back 0x00 into the key (no mutation), so the keystream is deterministic for this input

Implementors should compare their output against the reference implementation in `src/protocol/cipher.c`.
