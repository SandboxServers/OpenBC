# 7. Per-Ship Serialization Lists


For each ship, the table shows the serialization list after the linking pass removes children. The **List Index** column is the `start_index` value used in the wire protocol. The **Bytes** column shows bytes written per WriteState call for a **remote ship** (Powered subsystems include the power data bit + byte).

### Federation

#### Sovereign (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Power | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 5 | Torpedoes | Powered | 6 (Fwd 1-4, Aft 1-2) | 9 |
| 6 | Repair | Powered | 0 | 3 |
| 7 | Phasers | Powered | 8 (Ventral 1-4, Dorsal 1-4) | 11 |
| 8 | Tractors | Powered | 4 (Aft 1-2, Fwd 1-2) | 7 |
| 9 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 10 | Bridge | Base | 0 | 1 |

Full cycle (remote): ~49 bytes over ~7 ticks (~700ms)

#### Galaxy (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Power | 0 | 3 |
| 2 | Shield Generator | Base | 0 | 1 |
| 3 | Sensor Array | Powered | 0 | 3 |
| 4 | Torpedoes | Powered | 6 (Fwd 1-4, Aft 1-2) | 9 |
| 5 | Phasers | Powered | 8 (Ventral 1-4, Dorsal 1-4) | 11 |
| 6 | Impulse Engines | Powered | 3 (Port, Star, Center) | 6 |
| 7 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 8 | Tractors | Powered | 4 (Aft 1-2, Fwd 1-2) | 7 |
| 9 | Bridge | Base | 0 | 1 |
| 10 | Engineering | Powered | 0 | 3 |

#### Nebula (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Power | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 5 | Phasers | Powered | 8 (Ventral 1-4, Dorsal 1-4) | 11 |
| 6 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Torpedoes | Powered | 6 (Fwd 1-4, Aft 1-2) | 9 |
| 8 | Repair | Powered | 0 | 3 |
| 9 | Tractors | Powered | 2 (Aft, Fwd) | 5 |
| 10 | Bridge | Base | 0 | 1 |

#### Akira (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Power | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 5 | Phasers | Powered | 8 (Ventral 1-4, Dorsal 1-4) | 11 |
| 6 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Torpedoes | Powered | 6 (Fwd 1-4, Aft 1-2) | 9 |
| 8 | Engineering | Powered | 0 | 3 |
| 9 | Tractors | Powered | 2 (Fwd, Aft) | 5 |
| 10 | Bridge | Base | 0 | 1 |

#### Ambassador (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Sensor Array | Powered | 0 | 3 |
| 3 | Warp Core | Power | 0 | 3 |
| 4 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 5 | Phasers | Powered | 8 (Ventral 1-3, Dorsal 1-3, Aft 1-2) | 11 |
| 6 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Torpedoes | Powered | 4 (Fwd 1-2, Aft 1-2) | 7 |
| 8 | Engineering | Powered | 0 | 3 |
| 9 | Bridge | Base | 0 | 1 |
| 10 | Tractors | Powered | 2 (Fwd, Aft) | 5 |

#### Shuttle (9 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Sensor Array | Powered | 0 | 3 |
| 4 | Shield Generator | Base | 0 | 1 |
| 5 | Phasers | Powered | 1 (Phaser) | 4 |
| 6 | Repair | Powered | 0 | 3 |
| 7 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 8 | Tractors | Powered | 1 (Fwd Tractor) | 4 |

Full cycle: ~29 bytes over ~3-4 ticks

### Klingon

#### Vor'cha (12 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Disruptor Beams | Powered | 1 (Disruptor) | 4 |
| 4 | Disruptor Cannons | Powered | 2 (Port, Star Cannon) | 5 |
| 5 | Torpedoes | Powered | 3 (Fwd 1-2, Aft) | 6 |
| 6 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 8 | Cloaking Device | Powered | 0 | 3 |
| 9 | Sensor Array | Powered | 0 | 3 |
| 10 | Repair System | Powered | 0 | 3 |
| 11 | Tractors | Powered | 2 (Aft, Fwd) | 5 |

Note: Two weapon systems — Disruptor Beams (WST_PHASER, idx 3) and Disruptor Cannons (WST_PULSE, idx 4).

#### Bird of Prey (10 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Disruptor Cannons | Powered | 2 (Port, Star Cannon) | 5 |
| 4 | Torpedoes | Powered | 1 (Fwd Torpedo) | 4 |
| 5 | Impulse Engines | Powered | 1 (Impulse Engine) | 4 |
| 6 | Warp Engines | Powered | 2 (Port, Star Warp) | 5 |
| 7 | Cloaking Device | Powered | 0 | 3 |
| 8 | Sensor Array | Powered | 0 | 3 |
| 9 | Engineering | Powered | 0 | 3 |

Note: No beam weapon system (only pulse cannons). No tractors, no bridge.

### Romulan

#### Warbird (13 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Power Plant | Power | 0 | 3 |
| 3 | Disruptor Beam | Powered | 1 (Disruptor) | 4 |
| 4 | Disruptor Cannons | Powered | 4 (Port 1-2, Star 1-2) | 7 |
| 5 | Torpedoes | Powered | 2 (Fwd, Aft) | 5 |
| 6 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 8 | Cloaking Device | Powered | 0 | 3 |
| 9 | Sensor Array | Powered | 0 | 3 |
| 10 | Engineering | Powered | 0 | 3 |
| 11 | Bridge | Base | 0 | 1 |
| 12 | Tractors | Powered | 2 (Aft, Fwd) | 5 |

Most complex ship (13 top-level entries = longest full cycle). Two weapon systems — Disruptor Beam (WST_PHASER) and Disruptor Cannons (WST_PULSE). "Power Plant" is the Romulan name for the reactor.

### Ferengi

#### Marauder (10 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Phasers | Powered | 1 (Ventral Phaser) | 4 |
| 4 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 5 | Warp Engines | Powered | 2 (Star, Port) | 5 |
| 6 | Tractors | Powered | 2 (Fwd, Aft) | 5 |
| 7 | Sensor Array | Powered | 0 | 3 |
| 8 | Repair Subsystem | Powered | 0 | 3 |
| 9 | Plasma Emitters | Powered | 2 (Port, Star Emitter) | 5 |

Two weapon systems — Phasers (WST_PHASER, idx 3) and Plasma Emitters (WST_PULSE, idx 9). No torpedoes.

### Cardassian

#### Galor (9 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Compressors | Powered | 4 (Fwd, Port, Star, Aft Beam) | 7 |
| 4 | Torpedoes | Powered | 1 (Fwd Torpedo) | 4 |
| 5 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 6 | Warp Engine | Powered | 1 (Warp Engine 1) | 4 |
| 7 | Repair Subsystem | Powered | 0 | 3 |
| 8 | Sensor Array | Powered | 0 | 3 |

Note: Single warp engine system with 1 child. No tractors. "Compressors" is the Cardassian phaser system name.

#### Keldon (10 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Shield Generator | Base | 0 | 1 |
| 2 | Warp Core | Power | 0 | 3 |
| 3 | Compressors | Powered | 4 (Fwd, Port, Star, Aft Beam) | 7 |
| 4 | Torpedoes | Powered | 2 (Fwd, Aft) | 5 |
| 5 | Impulse Engines | Powered | 4 (Engine 1-4) | 7 |
| 6 | Warp Engine | Powered | 1 (Warp Engine 1) | 4 |
| 7 | Sensor Array | Powered | 0 | 3 |
| 8 | Repair Subsystem | Powered | 0 | 3 |
| 9 | Tractors | Powered | 2 (Ventral, Dorsal) | 5 |

Note: 4 impulse engines (unique among stock ships). Single warp engine system with 1 child.

#### Cardassian Hybrid (11 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Power | 0 | 3 |
| 2 | Torpedoes | Powered | 3 (Torpedo 1-2, Aft) | 6 |
| 3 | Repair System | Powered | 0 | 3 |
| 4 | Shield Generator | Base | 0 | 1 |
| 5 | Sensor Array | Powered | 0 | 3 |
| 6 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Warp Engines | Powered | 3 (Port, Star, Center) | 6 |
| 8 | Beams | Powered | 7 (Fwd Compressor, Fwd 1-2, Ventral 1-2, Dorsal 1-2) | 10 |
| 9 | Disruptor Cannons | Powered | 1 (Cannon) | 4 |
| 10 | Tractors | Powered | 2 (Fwd, Aft) | 5 |

Two weapon systems — Beams (WST_PHASER, idx 8) and Disruptor Cannons (WST_PULSE, idx 9). 3 warp engine children (unique: has Center Warp).

### Kessok

#### Kessok Heavy (10 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Power | 0 | 3 |
| 2 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 3 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 4 | Positron Beams | Powered | 8 (Fwd 1-4, Ventral 1-2, Dorsal 1-2) | 11 |
| 5 | Torpedoes | Powered | 2 (Tube 1-2) | 5 |
| 6 | Repair System | Powered | 0 | 3 |
| 7 | Shield Generator | Base | 0 | 1 |
| 8 | Sensor Array | Powered | 0 | 3 |
| 9 | Cloaking Device | Powered | 0 | 3 |

"Positron Beams" is the Kessok phaser system name (WST_PHASER type, 8 children).

#### Kessok Light (10 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Warp Core | Power | 0 | 3 |
| 2 | Torpedoes | Powered | 1 (Torpedo) | 4 |
| 3 | Repair System | Powered | 0 | 3 |
| 4 | Shield Generator | Base | 0 | 1 |
| 5 | Sensor Array | Powered | 0 | 3 |
| 6 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 7 | Warp Engines | Powered | 2 (Port, Star) | 5 |
| 8 | Beams | Powered | 8 (Fwd 1-2, Port 1-2, Star 1-2, Aft 1-2) | 11 |
| 9 | Cloaking Device | Powered | 0 | 3 |

### Civilian

#### Cardassian Freighter (8 top-level)

| Idx | Subsystem | Format | Children | Bytes |
|-----|-----------|--------|----------|-------|
| 0 | Hull | Base | 0 | 1 |
| 1 | Impulse Engines | Powered | 2 (Port, Star) | 5 |
| 2 | Warp Engines | Powered | 1 (Warp) | 4 |
| 3 | Engineering | Powered | 0 | 3 |
| 4 | Tractors | Powered | 1 (Tractor Beam) | 4 |
| 5 | Sensor Array | Powered | 0 | 3 |
| 6 | Shield Generator | Base | 0 | 1 |
| 7 | Warp Core | Power | 0 | 3 |

Simplest ship: no weapons, no bridge. Full cycle: ~24 bytes over ~3 ticks.

---

