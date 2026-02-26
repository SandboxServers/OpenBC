# 6. Cross-Reference: All Stock Flyable Ships


Summary of all 16 stock multiplayer ships showing top-level subsystem count and weapon/engine children. Ships are grouped by faction.

| Ship | Faction | Top-Level | Beams | Torps | Impulse | Warp | Tractors | Cloak | Repair | Bridge |
|------|---------|-----------|-------|-------|---------|------|----------|-------|--------|--------|
| Sovereign | Federation | 11 | 8 | 6 | 2 | 2 | 4 | -- | Yes | Yes |
| Galaxy | Federation | 11 | 8 | 6 | 3 | 2 | 4 | -- | Yes | Yes |
| Nebula | Federation | 11 | 8 | 6 | 2 | 2 | 2 | -- | Yes | Yes |
| Akira | Federation | 11 | 8 | 6 | 2 | 2 | 2 | -- | Yes | Yes |
| Ambassador | Federation | 11 | 8 | 4 | 2 | 2 | 2 | -- | Yes | Yes |
| Shuttle | Federation | 9 | 1 | 0 | 2 | 2 | 1 | -- | Yes | -- |
| Vor'cha | Klingon | 12 | 1 | 3 | 2 | 2 | 2 | Yes | Yes | -- |
| Bird of Prey | Klingon | 10 | 0 | 1 | 1 | 2 | 0 | Yes | Yes | -- |
| Warbird | Romulan | 13 | 1 | 2 | 2 | 2 | 2 | Yes | Yes | Yes |
| Marauder | Ferengi | 10 | 1 | 0 | 2 | 2 | 2 | -- | Yes | -- |
| Galor | Cardassian | 9 | 4 | 1 | 2 | 1 | 0 | -- | Yes | -- |
| Keldon | Cardassian | 10 | 4 | 2 | 4 | 1 | 2 | -- | Yes | -- |
| Card. Hybrid | Cardassian | 11 | 7 | 3 | 2 | 3 | 2 | -- | Yes | -- |
| Kessok Heavy | Kessok | 10 | 8 | 2 | 2 | 2 | 0 | Yes | Yes | -- |
| Kessok Light | Kessok | 10 | 8 | 1 | 2 | 2 | 0 | Yes | Yes | -- |
| Card. Freighter | Civilian | 8 | 0 | 0 | 2 | 1 | 1 | -- | Yes | -- |

**Column notes**:
- **Top-Level**: Number of entries in the serialization list (determines round-robin cycle length)
- **Beams/Torps/Impulse/Warp/Tractors**: Number of *children* recursively serialized within their parent system
- **Cloak**: Whether a Cloaking Device entry exists in the serialization list
- **Repair**: All ships have a repair system (named "Repair", "Repair System", or "Engineering")
- **Bridge**: Whether a Bridge hull subsystem exists (separate from the main Hull entry)

**Weapon system note**: The Vor'cha, Warbird, Bird of Prey, Marauder, and Card. Hybrid have **two weapon system entries** — one for beam/phaser weapons (`WST_PHASER`) and one for pulse/disruptor cannons (`WST_PULSE`). These appear as separate top-level entries in the serialization list. All other ships have a single phaser/beam system.

---

