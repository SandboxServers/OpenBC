# Per-Ship Subsystem Wire Format Catalog

This document has been decomposed into focused chapters for faster lookup and lower context load.


Wire format specification for the subsystem health data (StateUpdate flag 0x20) across all
16 stock multiplayer ships. Derived from the game's shipped hardpoint scripts and verified
against a stock dedicated server capture.


## Contents

- [Overview](01-overview.md)
- [Species ID Mapping](02-species-id-mapping.md)
- [WriteState Types](03-writestate-types.md)
- [Summary Table](04-summary-table.md)
- [Per-Ship Subsystem Lists](05-per-ship-subsystem-lists.md)
- [Universal Patterns](06-universal-patterns.md)
- [Round-Robin Timing](07-round-robin-timing.md)
- [Implementation Notes](08-implementation-notes.md)
