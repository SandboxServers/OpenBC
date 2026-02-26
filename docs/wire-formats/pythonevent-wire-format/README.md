# PythonEvent Wire Format (Opcode 0x06)

This document has been decomposed into focused chapters for faster lookup and lower context load.


Wire format specification for Star Trek: Bridge Commander's primary event forwarding
message, documented from network packet captures and the game's shipped Python
scripting API.

**Clean room statement**: This document describes observable multiplayer behavior and
network traffic patterns. No binary addresses, memory offsets, or decompiled code are
referenced.

---


## Contents

- [Overview](01-overview.md)
- [Message Structure](02-message-structure.md)
- [Object Reference Encoding](03-object-reference-encoding.md)
- [Four Event Classes](04-four-event-classes.md)
- [Three Producers](05-three-producers.md)
- [Receiver Behavior](06-receiver-behavior.md)
- [Collision Damage → PythonEvent Chain](07-collision-damage-pythonevent-chain.md)
- [Decoded Packet Examples](08-decoded-packet-examples.md)
- [Traffic Statistics (15-minute 3-player session)](09-traffic-statistics-15-minute-3-player-session.md)
- [Required Event Registrations](10-required-event-registrations.md)
- [Server Implementation Notes](11-server-implementation-notes.md)
- [Related Documents](12-related-documents.md)
