# Round-Robin Timing


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

