# Per-Ship Subsystem Lists


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

