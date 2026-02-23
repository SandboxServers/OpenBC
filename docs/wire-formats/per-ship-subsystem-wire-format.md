# Per-Ship Subsystem Wire Format Catalog

Wire format specification for the subsystem health data (StateUpdate flag 0x20) across all
16 stock multiplayer ships. Derived from the game's shipped hardpoint scripts and verified
against a stock dedicated server capture.

## Overview

The subsystem health round-robin serializes the ship's **top-level subsystem list** in an
order determined by each ship class's hardpoint script. Each ship has a different number and
arrangement of subsystems. Both server and client build identical lists from the same
hardpoint file (verified by the checksum exchange), so they always agree on subsystem order.

For the round-robin algorithm and WriteState format details, see
[stateupdate-wire-format.md](stateupdate-wire-format.md).

## Species ID Mapping

The multiplayer ship selection screen maps species IDs to ship classes:

| Species | Ship | Faction |
|---------|------|---------|
| 1 | Akira | Federation |
| 2 | Ambassador | Federation |
| 3 | Galaxy | Federation |
| 4 | Nebula | Federation |
| 5 | Sovereign | Federation |
| 6 | Bird of Prey | Klingon |
| 7 | Vor'cha | Klingon |
| 8 | Warbird | Romulan |
| 9 | Marauder | Ferengi |
| 10 | Galor | Cardassian |
| 11 | Keldon | Cardassian |
| 12 | CardHybrid | Cardassian |
| 13 | KessokHeavy | Kessok |
| 14 | KessokLight | Kessok |
| 15 | Shuttle | Federation |
| 37 | Enterprise | Federation |

Maximum flyable ships: 16. The Enterprise (slot 37) uses the same ship class as the
Sovereign — identical subsystem layout, only capacity values differ.

## WriteState Types

Three serialization formats exist for subsystem health data:

| Format | Used By | Bytes (remote ship) | Layout |
|--------|---------|---------------------|--------|
| **Base** | Hull, Shield Generator | 1 + children | `[condition:u8][child_conditions...]` |
| **Powered** | Sensors, Impulse, Warp, Phasers, Torpedoes, Tractors, Pulse Weapons, Cloak, Repair | 1 + children + 2 | `[condition:u8][child_conditions...][hasData:bit=1][powerPct:u8]` |
| **Reactor** | Power Subsystem (reactor/warp core) | 3 | `[condition:u8][mainBattery:u8][backupBattery:u8]` |

- **condition**: health percentage scaled to 0–255 (0xFF = 100%, 0x00 = destroyed)
- **powerPct**: power allocation percentage (0–100)
- **mainBattery/backupBattery**: battery charge level scaled to 0–255
- The reactor always writes battery bytes regardless of ship ownership
- Powered subsystems only write power data for remote ships (not the local player's own ship)
- Child subsystems (individual weapons, engines) always use the 1-byte Base format

## Summary Table

| Sp | Ship | Top-Level | Children | Total | Cycle Bytes | Cloak | Pulse | Tractors | Bridge |
|----|------|-----------|----------|-------|-------------|-------|-------|----------|--------|
| 1 | Akira | 11 | 20 | 31 | 47 | — | — | 2 | Yes |
| 2 | Ambassador | 11 | 18 | 29 | 45 | — | — | 2 | Yes |
| 3 | Galaxy | 11 | 23 | 34 | 50 | — | — | 4 | Yes |
| 4 | Nebula | 11 | 20 | 31 | 47 | — | — | 2 | Yes |
| 5 | Sovereign | 11 | 22 | 33 | 49 | — | — | 4 | Yes |
| 6 | Bird of Prey | 10 | 6 | 16 | 32 | Yes | 2 | — | — |
| 7 | Vor'cha | 12 | 12 | 24 | 44 | Yes | 2 | 2 | — |
| 8 | Warbird | 13 | 13 | 26 | 46 | Yes | 4 | 2 | Yes |
| 9 | Marauder | 10 | 9 | 19 | 35 | — | 2 | 2 | — |
| 10 | Galor | 9 | 8 | 17 | 31 | — | — | — | — |
| 11 | Keldon | 10 | 13 | 23 | 39 | — | — | 2 | — |
| 12 | CardHybrid | 11 | 18 | 29 | 47 | — | 1 | 2 | — |
| 13 | KessokHeavy | 10 | 14 | 24 | 40 | Yes | — | — | — |
| 14 | KessokLight | 10 | 13 | 23 | 39 | Yes | — | — | — |
| 15 | Shuttle | 9 | 6 | 15 | 29 | — | — | 1 | — |
| 37 | Enterprise | 11 | 22 | 33 | 49 | — | — | 4 | Yes |

- **Top-Level**: Subsystems in the serialization list (parent systems only)
- **Children**: Subsystems nested under parent systems (serialized recursively)
- **Total**: All subsystem objects created for the ship
- **Cycle Bytes**: Total bytes to serialize all top-level subsystems once

## Per-Ship Subsystem Lists

Each ship's top-level subsystem list, in serialization order.

---

### Species 1: Akira

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Reactor | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 5 | Phasers | Powered | 8 (Ventral 1–4, Dorsal 1–4) | 11 |
| 6 | Warp Engines | Powered | 2 (Port, Starboard) | 5 |
| 7 | Torpedoes | Powered | 6 (Forward 1–2, Aft 1, Forward 3–4, Aft 2) | 9 |
| 8 | Engineering | Powered | 0 | 3 |
| 9 | Tractors | Powered | 2 (Forward, Aft) | 5 |
| 10 | Bridge | Base | 0 | 1 |

**Full cycle: 47 bytes**

---

### Species 2: Ambassador

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Reactor | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 5 | Phasers | Powered | 8 (Ventral 1–3, Dorsal 1–3, Aft 1–2) | 11 |
| 6 | Warp Engines | Powered | 2 (Port, Starboard) | 5 |
| 7 | Torpedoes | Powered | 4 (Forward 1–2, Aft 1–2) | 7 |
| 8 | Engineering | Powered | 0 | 3 |
| 9 | Bridge | Base | 0 | 1 |
| 10 | Tractors | Powered | 2 (Forward, Aft) | 5 |

**Full cycle: 45 bytes**

Note: Bridge at index 9 and Tractors at index 10 (reversed vs. most Federation ships).

---

### Species 3: Galaxy

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Reactor | 0 | 3 |
| 2 | Shield Generator | Base | 0 | 1 |
| 3 | Sensor Array | Powered | 0 | 3 |
| 4 | Torpedoes | Powered | 6 (Forward 1–4, Aft 1–2) | 9 |
| 5 | Phasers | Powered | 8 (Ventral 1–4, Dorsal 1–4) | 11 |
| 6 | Impulse Engines | Powered | 3 (Port, Starboard, Center) | 6 |
| 7 | Warp Engines | Powered | 2 (Port, Starboard) | 5 |
| 8 | Tractors | Powered | 4 (Aft 1–2, Forward 1–2) | 7 |
| 9 | Bridge | Base | 0 | 1 |
| 10 | Engineering | Powered | 0 | 3 |

**Full cycle: 50 bytes**

Notable: Warp Core at index 1 (before Shield Generator). **3 impulse engines** (unique
among Federation ships). Engineering at index 10 (last).

---

### Species 4: Nebula

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Reactor | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 5 | Phasers | Powered | 8 (Ventral 1–4, Dorsal 1–4) | 11 |
| 6 | Warp Engines | Powered | 2 (Port, Starboard) | 5 |
| 7 | Torpedoes | Powered | 6 (Forward 1–4, Aft 1–2) | 9 |
| 8 | Repair | Powered | 0 | 3 |
| 9 | Tractors | Powered | 2 (Aft, Forward) | 5 |
| 10 | Bridge | Base | 0 | 1 |

**Full cycle: 47 bytes**

---

### Species 5: Sovereign

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Reactor | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 5 | Torpedoes | Powered | 6 (Forward 1–4, Aft 1–2) | 9 |
| 6 | Repair | Powered | 0 | 3 |
| 7 | Phasers | Powered | 8 (Ventral 1–4, Dorsal 1–4) | 11 |
| 8 | Tractors | Powered | 4 (Aft 1–2, Forward 1–2) | 7 |
| 9 | Warp Engines | Powered | 2 (Port, Starboard) | 5 |
| 10 | Bridge | Base | 0 | 1 |

**Full cycle: 49 bytes**

Enterprise (species 37) has identical layout — only capacity values differ.

---

### Species 6: Bird of Prey

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Reactor | 0 | 3 |
| 3 | Disruptor Cannons | Powered | 2 (Port, Starboard) | 5 |
| 4 | Torpedoes | Powered | 1 (Forward) | 4 |
| 5 | Impulse Engines | Powered | 1 (single engine) | 4 |
| 6 | Warp Engines | Powered | 2 (Port, Starboard) | 5 |
| 7 | Cloaking Device | Powered | 0 | 3 |
| 8 | Sensor Array | Powered | 0 | 3 |
| 9 | Engineering | Powered | 0 | 3 |

**Full cycle: 32 bytes**

Notable: No phasers — uses pulse weapons (disruptor cannons) only. Single impulse engine,
single torpedo tube. Has cloaking device. No Bridge, no tractors.

---

### Species 7: Vor'cha

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Reactor | 0 | 3 |
| 3 | Disruptor Beams | Powered | 1 (single disruptor) | 4 |
| 4 | Disruptor Cannons | Powered | 2 (Port, Starboard) | 5 |
| 5 | Torpedoes | Powered | 3 (Forward 1–2, Aft) | 6 |
| 6 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 7 | Warp Engines | Powered | 2 (Port, Starboard) | 5 |
| 8 | Cloaking Device | Powered | 0 | 3 |
| 9 | Sensor Array | Powered | 0 | 3 |
| 10 | Repair System | Powered | 0 | 3 |
| 11 | Tractors | Powered | 2 (Aft, Forward) | 5 |

**Full cycle: 44 bytes**

Notable: Has BOTH beam weapons (1 disruptor beam) AND pulse weapons (2 cannons).
12 top-level subsystems — most of any non-Romulan ship. Has cloaking device.

---

### Species 8: Warbird

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Power Plant | Reactor | 0 | 3 |
| 3 | Disruptor Beam | Powered | 1 (single disruptor) | 4 |
| 4 | Disruptor Cannons | Powered | 4 (Port 1–2, Starboard 1–2) | 7 |
| 5 | Torpedoes | Powered | 2 (Forward, Aft) | 5 |
| 6 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 7 | Warp Engines | Powered | 2 (Port, Starboard) | 5 |
| 8 | Cloaking Device | Powered | 0 | 3 |
| 9 | Sensor Array | Powered | 0 | 3 |
| 10 | Engineering | Powered | 0 | 3 |
| 11 | Bridge | Base | 0 | 1 |
| 12 | Tractors | Powered | 2 (Aft, Forward) | 5 |

**Full cycle: 46 bytes**

Notable: **13 top-level** — most of any stock ship. Reactor named "Power Plant".
4 pulse weapons (most of any ship). Only non-Federation ship with Bridge hull.

---

### Species 9: Marauder

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Reactor | 0 | 3 |
| 3 | Phasers | Powered | 1 (Ventral Phaser) | 4 |
| 4 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 5 | Warp Engines | Powered | 2 (Starboard, Port) | 5 |
| 6 | Tractors | Powered | 2 (Forward, Aft) | 5 |
| 7 | Sensor Array | Powered | 0 | 3 |
| 8 | Repair Subsystem | Powered | 0 | 3 |
| 9 | Plasma Emitters | Powered | 2 (Port, Starboard) | 5 |

**Full cycle: 35 bytes**

Notable: No torpedoes at all — the only stock ship without them. Only 1 phaser bank.
Has Plasma Emitters (pulse weapons). No Bridge.

---

### Species 10: Galor

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Reactor | 0 | 3 |
| 3 | Compressors | Powered | 4 (Forward, Port, Starboard, Aft Beam) | 7 |
| 4 | Torpedoes | Powered | 1 (Forward) | 4 |
| 5 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 6 | Warp Engine | Powered | 1 (single engine) | 4 |
| 7 | Repair Subsystem | Powered | 0 | 3 |
| 8 | Sensor Array | Powered | 0 | 3 |

**Full cycle: 31 bytes**

Notable: Only **9 top-level** — smallest non-shuttle ship. Beam weapons named
"Compressors". Single warp engine, single torpedo tube. No tractors, no Bridge, no cloak.

---

### Species 11: Keldon

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Reactor | 0 | 3 |
| 3 | Compressors | Powered | 4 (Forward, Port, Starboard, Aft Beam) | 7 |
| 4 | Torpedoes | Powered | 2 (Forward, Aft) | 5 |
| 5 | Impulse Engines | Powered | 4 (Engine 1–4) | 7 |
| 6 | Warp Engine | Powered | 1 (single engine) | 4 |
| 7 | Sensor Array | Powered | 0 | 3 |
| 8 | Repair Subsystem | Powered | 0 | 3 |
| 9 | Tractors | Powered | 2 (Ventral, Dorsal) | 5 |

**Full cycle: 39 bytes**

Notable: **4 impulse engines** — unique among all stock ships. Like Galor, uses
"Compressors" for beam weapons and has single warp engine.

---

### Species 12: CardHybrid

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Reactor | 0 | 3 |
| 2 | Torpedoes | Powered | 3 (Torpedo 1–2, Aft Torpedo) | 6 |
| 3 | Repair System | Powered | 0 | 3 |
| 4 | Shield Generator | Base | 0 | 1 |
| 5 | Sensor Array | Powered | 0 | 3 |
| 6 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 7 | Warp Engines | Powered | 3 (Port, Starboard, Center) | 6 |
| 8 | Beams | Powered | 7 (Fwd Compressor, Forward 1–2, Ventral 1–2, Dorsal 1–2) | 10 |
| 9 | Disruptor Cannons | Powered | 1 (single cannon) | 4 |
| 10 | Tractors | Powered | 2 (Forward, Aft) | 5 |

**Full cycle: 47 bytes**

Notable: Unusual subsystem ordering — Warp Core at index 1, Repair at index 3,
Shield Generator at index 4. Has both beam weapons (7 banks — most of any ship) and
pulse weapons (1 cannon). **3 warp engines** (Port, Starboard, Center) — unique.

---

### Species 13: KessokHeavy

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Reactor | 0 | 3 |
| 2 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 3 | Warp Engines | Powered | 2 (Port, Starboard) | 5 |
| 4 | Positron Beams | Powered | 8 (Forward 1–4, Ventral 1–2, Dorsal 1–2) | 11 |
| 5 | Torpedoes | Powered | 2 (Tube 1–2) | 5 |
| 6 | Repair System | Powered | 0 | 3 |
| 7 | Shield Generator | Base | 0 | 1 |
| 8 | Sensor Array | Powered | 0 | 3 |
| 9 | Cloaking Device | Powered | 0 | 3 |

**Full cycle: 40 bytes**

Notable: Has Cloaking Device. Beam weapons named "Positron Beams" (8 banks).
Shield Generator at index 7 (unusual late position). No tractors, no Bridge.

---

### Species 14: KessokLight

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Reactor | 0 | 3 |
| 2 | Torpedoes | Powered | 1 (single torpedo) | 4 |
| 3 | Repair System | Powered | 0 | 3 |
| 4 | Shield Generator | Base | 0 | 1 |
| 5 | Sensor Array | Powered | 0 | 3 |
| 6 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 7 | Warp Engines | Powered | 2 (Port, Starboard) | 5 |
| 8 | Beams | Powered | 8 (Forward 1–2, Port 1–2, Starboard 1–2, Aft 1–2) | 11 |
| 9 | Cloaking Device | Powered | 0 | 3 |

**Full cycle: 39 bytes**

Notable: Has Cloaking Device. 8 beam banks. Only 1 torpedo tube.
No tractors, no Bridge.

---

### Species 15: Shuttle

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Impulse Engines | Powered | 2 (Port, Starboard) | 5 |
| 2 | Warp Core | Reactor | 0 | 3 |
| 3 | Sensor Array | Powered | 0 | 3 |
| 4 | Shield Generator | Base | 0 | 1 |
| 5 | Phasers | Powered | 1 (single phaser) | 4 |
| 6 | Repair | Powered | 0 | 3 |
| 7 | Warp Engines | Powered | 2 (Port, Starboard) | 5 |
| 8 | Tractors | Powered | 1 (Forward) | 4 |

**Full cycle: 29 bytes**

Notable: Smallest combat ship. No torpedoes. Only 1 phaser bank, 1 tractor beam.
Impulse Engines at index 1 (before Warp Core). No Bridge, no cloak.

---

## Universal Patterns

All 16 stock ships share these 7 subsystem types (always present):
1. **Hull** (Base) — at least 1; Federation capital ships + Warbird have 2 (Hull + Bridge)
2. **Shield Generator** (Base) — always 1 (shield facing HP is in flag 0x40, not 0x20)
3. **Reactor** — always 1 (named "Warp Core" or "Power Plant")
4. **Sensor Array** (Powered) — always 1
5. **Impulse Engines** (Powered) — always 1 system with 1–4 child engines
6. **Warp Engines** (Powered) — always 1 system with 1–3 child engines
7. **Repair** (Powered) — always 1

Optional subsystem types:
- **Beam Weapons** (Powered) — present on all ships except Bird of Prey (1–8 child banks)
- **Torpedoes** (Powered) — present on all ships except Marauder (1–6 child tubes)
- **Tractors** (Powered) — absent on: Bird of Prey, Galor, KessokHeavy, KessokLight
- **Pulse Weapons** (Powered) — present on: Bird of Prey, Vor'cha, Warbird, Marauder, CardHybrid
- **Cloaking Device** (Powered) — present on: Bird of Prey, Vor'cha, Warbird, KessokHeavy, KessokLight
- **Bridge Hull** (Base) — present on: 5 Federation capital ships + Warbird

## Round-Robin Timing

With the 10-byte per-tick budget at ~10 updates/second:

| Ship | Cycle Bytes | Ticks per Cycle | Full Cycle Time |
|------|-------------|----------------|-----------------|
| Shuttle | 29 | ~3 | ~0.3s |
| Galor | 31 | ~4 | ~0.4s |
| Bird of Prey | 32 | ~4 | ~0.4s |
| Marauder | 35 | ~4 | ~0.4s |
| Keldon, KessokLight | 39 | ~4 | ~0.4s |
| KessokHeavy | 40 | ~4 | ~0.4s |
| Vor'cha | 44 | ~5 | ~0.5s |
| Ambassador | 45 | ~5 | ~0.5s |
| Warbird | 46 | ~5 | ~0.5s |
| Akira, Nebula, CardHybrid | 47 | ~5 | ~0.5s |
| Sovereign, Enterprise | 49 | ~5 | ~0.5s |
| Galaxy | 50 | ~5 | ~0.5s |

All ships complete a full health sync cycle in under 1 second.

## Implementation Notes

1. **Subsystem list order is ship-specific.** An implementation must build the same
   ordered list for each ship class. Mismatches cause health values to be applied to
   the wrong subsystem on the receiver.

2. **Server and client must agree.** Both sides build the list from the same hardpoint
   data (verified by the checksum exchange). This guarantees identical subsystem order.

3. **Only top-level subsystems participate in the round-robin.** Children are serialized
   recursively inside their parent's WriteState call.

4. **Shield facing HP uses a different flag.** The Shield Generator in the subsystem list
   only writes 1 condition byte. The 6 individual shield facing values use a separate
   serialization path (flag 0x40).

5. **Mod ships will have different layouts.** This catalog covers only stock ships.
   Any mod-added ship will have its own subsystem order and composition, determined
   by its hardpoint script.

6. **The subsystem name ordering varies by faction.** Even within a faction, ships
   place subsystems in different order. The order is NOT standardized — each hardpoint
   script defines its own sequence.
