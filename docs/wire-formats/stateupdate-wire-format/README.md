# StateUpdate Wire Format (Opcode 0x1C)

This document has been decomposed into focused chapters for faster lookup and lower context load.


Wire format specification for Star Trek: Bridge Commander's state synchronization message, documented from network packet captures and the game's shipped scripting API.


## Contents

- [Overview](01-overview.md)
- [Message Header](02-message-header.md)
- [Dirty Flags](03-dirty-flags.md)
- [Flag 0x01 — Absolute Position](04-flag-0x01-absolute-position.md)
- [Flag 0x02 — Position Delta (Compressed)](05-flag-0x02-position-delta-compressed.md)
- [Flag 0x04 — Forward Orientation](06-flag-0x04-forward-orientation.md)
- [Flag 0x08 — Up Orientation](07-flag-0x08-up-orientation.md)
- [Flag 0x10 — Speed](08-flag-0x10-speed.md)
- [Flag 0x20 — Subsystem Health (Round-Robin)](09-flag-0x20-subsystem-health-round-robin.md)
- [Flag 0x40 — Cloak State](10-flag-0x40-cloak-state.md)
- [Flag 0x80 — Weapon Health (Round-Robin)](11-flag-0x80-weapon-health-round-robin.md)
- [Compressed Data Types](12-compressed-data-types.md)
- [Force-Update Timing](13-force-update-timing.md)
- [Decoded Packet Examples](14-decoded-packet-examples.md)
- [Authority Model Summary](15-authority-model-summary.md)
- [Bandwidth Profile](16-bandwidth-profile.md)
- [Related Documents](17-related-documents.md)
