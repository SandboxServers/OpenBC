# Subsystem Integrity Hash — Behavioral Specification

## Overview

The subsystem integrity hash is a tamper-detection system that computes a 32-bit checksum from all ship subsystem property values (health, weapon stats, shield facings, etc.), then XOR-folds it to 16 bits for wire transmission. The receiver recomputes the hash locally and compares; on mismatch, the offending player is kicked.

### Dead Code in Multiplayer

In the stock game, this system is **dead code in multiplayer**:

- The **sender** only includes the hash in **single-player** mode (`isMultiplayer == false`)
- The **receiver** only validates the hash in **multiplayer** mode (`isMultiplayer == true`)

These conditions are mutually exclusive. The hash is never both sent and checked in any stock gameplay session.

### Reimplementation Guidance

- **Multiplayer-only server**: Can safely omit the entire hash system. Stock behavior never activates it in MP.
- **Single-player fidelity**: Include the sender (hash computation + wire encoding) for completeness, but the receiver validation path is dead and can be omitted without behavioral difference.
- **Enhanced anti-cheat**: If desired for a reimplementation, both sender and receiver conditions could be changed to activate in multiplayer. This would require careful testing to ensure subsystem state stays synchronized.

---

## hash_fold Algorithm

The core accumulator function, called once per hashed value:

1. Convert `float value` to `int32` via truncation
2. Take absolute value of the truncated integer
3. XOR each of the 4 bytes of the integer into the corresponding byte of the accumulator
4. Rotate the accumulator left by 1 bit

**Note**: Because of the absolute-value step, positive and negative values of the same magnitude produce identical contributions (e.g., 3.7 and -3.7 both hash as integer 3).

```
function hash_fold(value: float, accumulator: ref uint32):
    ival = abs(truncate_to_int(value))
    for i in 0..3:
        accumulator.byte[i] ^= ival.byte[i]
    accumulator = rotate_left(accumulator, 1)
```

---

## Base Subsystem Hash

Called for every subsystem. Produces a minimum of 11 hash_fold calls, plus additional for children and powered subsystems.

### Hashed fields (in order):

1. **7 base property floats**: maxCondition, currentPower, and 5 other property fields
2. **Recursive children**: Each child subsystem is recursively hashed via base_subsystem_hash, and the returned float is folded into the accumulator
3. **4 boolean sentinels**: Boolean flags hashed as arbitrary float constants (see table below)
4. **PoweredSubsystem extra**: If the subsystem is a PoweredSubsystem, one additional energy property float is hashed

**Critical ordering**: Children are hashed BEFORE the boolean sentinels. The accumulator state from children affects how boolean values contribute to the final hash.

---

## Hash Order — ComputeSubsystemHash

The hash computation takes the subsystem container as input (12 pointer slots). Subsystems are hashed in this exact fixed order:

| Order | Subsystem | Hash Method | Extra Fields Beyond Base |
|-------|-----------|-------------|--------------------------|
| 1 | Power Reactor | base only | none |
| 2 | Shield Generator | base + type-specific | 12 floats: 6 maxShield facings + 6 chargePerSecond facings |
| 3 | Powered Master | base + type-specific | 5 property floats |
| 4 | Cloak Device | base + type-specific | 1 property float |
| 5 | Impulse Engine | base + type-specific | 4 property floats |
| 6 | Sensor Array | base only | none |
| 7 | Warp Drive | base + type-specific | 1 property float |
| 8 | Crew / Unknown-A | base only | side-effect getter call |
| 9 | Torpedo System | weapon_system_hash | children + torpedo type data |
| 10 | Phaser System | weapon_system_hash | children (no torpedo data) |
| 11 | Pulse Weapon System | weapon_system_hash | children (no torpedo data) |
| 12 | Tractor Beam System | weapon_system_hash | children (no torpedo data) |

Each slot is NULL-checked before hashing. If a slot is empty, it is skipped.

Slots 1–8 use `base_subsystem_hash` plus optional type-specific extras. Slots 9–12 use `weapon_system_hash` which wraps base_subsystem_hash with weapon-specific extensions.

---

## Weapon System Hash

Called for the 4 weapon-system slots (Torpedo, Phaser, Pulse, Tractor). Extends base_subsystem_hash:

1. **base_subsystem_hash** of the weapon system subsystem itself
2. **2 boolean sentinels**: weapon system enabled flag and weapon system online flag
3. **Per-child weapon dispatch**: Each child weapon is hashed via individual_weapon_hash (see below)
4. **Torpedo data** (only for actual torpedo systems): Per torpedo type:
   - maxTorpedoes count (as float)
   - Script name mirror convolution
   - Type object name mirror convolution
   - Product of two integer fields (as float)

### Torpedo Mirror Convolution

For a string of length N, each character at position `j` is multiplied by the character at position `N-1-j`, and each product is hash_folded individually:

```
for j in 0..len-1:
    hash_fold(char[j] * char[len-1-j], accumulator)
```

For "ABCD": contributions are `A*D`, `B*C`, `C*B`, `D*A`. This makes the hash palindrome-sensitive.

Only subsystems that are actual torpedo systems contribute torpedo data. Phaser, Pulse, and Tractor systems skip the torpedo block entirely.

---

## Individual Weapon Dispatch

Each weapon child is first hashed with base_subsystem_hash, then checked against 5 weapon types. A weapon can match multiple types due to class inheritance (e.g., a phaser matches both EnergyWeapon and PhaserBank).

### EnergyWeapon
7 property floats: maxDamagePerShot, maxCharge, maxDamage, maxDamageDistance, rechargeRate, dischargeRate, minDamageRange

### PhaserBank
2 firing arc direction vectors (6 floats) + 6 additional property floats

### PulseWeapon
3 vectors — position + 2 directions (9 floats) + 5 property floats + weapon name string mirror convolution (folded as single float sum)

### TractorBeamProjector
3 vectors (9 floats) + 4 property floats

### TorpedoTube
2 firing direction vectors (6 floats) + 2 property floats + 1 integer-to-float cast field

---

## Wire Format

The hash is carried in StateUpdate packets, within the flag 0x01 (POSITION_ABSOLUTE) section, after position data:

```
[position data] [has_hash: uint8] [if has_hash != 0: hash16: uint16]
```

### 32-to-16-bit Fold

The 32-bit accumulator is XOR-folded to 16 bits before transmission:

```
wire_hash = (hash32 >> 16) XOR (hash32 & 0xFFFF)
```

---

## Sender/Receiver Conditions

| Mode | Sender writes hash? | Receiver checks hash? | Result |
|------|---------------------|-----------------------|--------|
| Single-player | YES (has_hash=1, hash16 follows) | NO (skips validation) | Hash sent but ignored |
| Multiplayer | NO (has_hash=0) | YES (would check if has_hash were 1) | Check never reached |

The sender and receiver conditions are **never active simultaneously** in stock gameplay.

On hash mismatch (if it could ever trigger), the receiver posts an ET_BOOT_PLAYER event which results in the player being kicked with reason code 4.

---

## Boolean Sentinel Values

Boolean flags are hashed as arbitrary float constants rather than 0/1, ensuring boolean state changes always affect the hash.

### Base Subsystem (4 pairs, applied to every subsystem)

| # | Field | True Value | False Value | Meaning |
|---|-------|------------|-------------|---------|
| 1 | Disableable flag | 64.0002 | 76.6 | Whether subsystem can be disabled |
| 2 | Operational state | 98.6 | 100.0 | Whether subsystem is currently operational |
| 3 | Repairable flag | 14.3 | 456.1 | Whether subsystem can be repaired |
| 4 | Primary flag | 27.3 | 16.1 | Whether this is a primary subsystem |

### Weapon System (2 pairs, applied to weapon system slots only)

| # | Field | True Value | False Value | Meaning |
|---|-------|------------|-------------|---------|
| 5 | WS enabled | 0.4 | 99.1 | Whether weapon system is enabled |
| 6 | WS online | 32.6 | 487.1 | Whether weapon system is online |

---

## Hash Contribution Summary

For a typical ship with all 12 subsystem slots populated:

- **Per non-weapon subsystem** (slots 1–8): 7 base floats + N children (recursive) + 4 boolean sentinels + 0–1 powered extra + 0–12 type-specific extras
- **Per weapon system** (slots 9–12): base_subsystem_hash (as above) + 2 WS boolean sentinels + per-child individual_weapon_hash + optional torpedo data
- **Per individual weapon**: base_subsystem_hash + type-specific fields (7–15 floats depending on weapon type) + optional name hash

The total number of hash_fold calls for a fully-loaded ship is in the hundreds.
