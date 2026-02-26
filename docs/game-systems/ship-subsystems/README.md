# Ship Subsystem Specification

This document has been decomposed into focused chapters for faster lookup and lower context load.


This document defines how ship subsystems are organized, serialized in the wire protocol, and how health data maps to byte positions in StateUpdate flag 0x20 messages.

**Clean room statement**: This specification is derived from observable network behavior (packet captures), the game's shipped Python scripting API, and hardpoint script analysis. No binary addresses, memory offsets, or decompiled code are referenced.

---


## Contents

- [1. Subsystem Architecture: Hierarchical, Not Flat](01-1-subsystem-architecture-hierarchical-not-flat.md)
- [2. Wire Protocol Index Mapping](02-2-wire-protocol-index-mapping.md)
- [3. WriteState Serialization Formats](03-3-writestate-serialization-formats.md)
- [4. Round-Robin Serialization Algorithm](04-4-round-robin-serialization-algorithm.md)
- [5. Receiver Algorithm](05-5-receiver-algorithm.md)
- [6. Cross-Reference: All Stock Flyable Ships](06-6-cross-reference-all-stock-flyable-ships.md)
- [7. Per-Ship Serialization Lists](07-7-per-ship-serialization-lists.md)
- [8. Sovereign-Class Detailed Example](08-8-sovereign-class-detailed-example.md)
- [9. Subsystem HP Values (Sovereign Class)](09-9-subsystem-hp-values-sovereign-class.md)
- [10. Subsystem Type Constants](10-10-subsystem-type-constants.md)
- [11. Key Behavioral Guarantees](11-11-key-behavioral-guarantees.md)
- [Related Documents](12-related-documents.md)
