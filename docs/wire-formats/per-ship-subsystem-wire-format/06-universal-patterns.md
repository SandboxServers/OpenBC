# Universal Patterns


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

