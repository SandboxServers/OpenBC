# Power & Reactor System — Clean Room Specification

This document has been decomposed into focused chapters for faster lookup and lower context load.


Behavioral specification of the Bridge Commander power system, described purely in terms of observable behavior. No binary addresses, decompiled code, or implementation details. Suitable for clean-room reimplementation.

Derived from behavioral observation and publicly available game scripts. The reverse engineering
analysis (with binary addresses) is maintained separately in the STBC-Dedicated-Server repository.

---


## Contents

- [Overview](01-overview.md)
- [Conceptual Architecture](02-conceptual-architecture.md)
- [Power Generation](03-power-generation.md)
- [Conduit Limits](04-conduit-limits.md)
- [Consumer Power Draw](05-consumer-power-draw.md)
- [Power Initialization on Ship Spawn](06-power-initialization-on-ship-spawn.md)
- [Player Power Adjustment](07-player-power-adjustment.md)
- [Python API Surface](08-python-api-surface.md)
- [AdjustPower Algorithm (Client-Side Auto-Balance)](09-adjustpower-algorithm-client-side-auto-balance.md)
- [Ship Power Parameters](10-ship-power-parameters.md)
- [Subsystem Power Consumption](11-subsystem-power-consumption.md)
- [Power Budget Analysis](12-power-budget-analysis.md)
- [Multiplayer Network Propagation](13-multiplayer-network-propagation.md)
- [Power State Wire Format](14-power-state-wire-format.md)
- [Design Observations for Reimplementation](15-design-observations-for-reimplementation.md)
