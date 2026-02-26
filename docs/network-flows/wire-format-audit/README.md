# OpenBC Wire Format Audit Report

This document has been decomposed into focused chapters for faster lookup and lower context load.


**Date**: 2026-02-16
**Source**: Stock BC dedicated server packet traces (90MB+)
**Traces analyzed**: Stock BC dedicated server packet captures (90MB+, multiple sessions)

Cross-referenced against observed protocol behavior from stock BC sessions.

---


## Contents

- [Summary of Findings](01-summary-of-findings.md)
- [1. Transport Framing](02-1-transport-framing.md)
- [2. Connect Handshake Sequence](03-2-connect-handshake-sequence.md)
- [3. Checksum Exchange (Rounds 0-3)](04-3-checksum-exchange-rounds-0-3.md)
- [4. Checksum Final Round (0xFF)](05-4-checksum-final-round-0xff.md)
- [5. TGBufferStream Bit-Packing (CRITICAL)](06-5-tgbufferstream-bit-packing-critical.md)
- [6. Settings (0x00) Payload](07-6-settings-0x00-payload.md)
- [7. GameInit (0x01)](08-7-gameinit-0x01.md)
- [8. NewPlayerInGame (0x2A) Direction](09-8-newplayeringame-0x2a-direction.md)
- [9. Missing Opcode 0x28](10-9-missing-opcode-0x28.md)
- [10. Server Keepalive Format](11-10-server-keepalive-format.md)
- [11. ACK Flags](12-11-ack-flags.md)
- [12. UICollisionSetting (0x16) Ordering](13-12-uicollisionsetting-0x16-ordering.md)
- [Complete Handshake: Expected vs. OpenBC](14-complete-handshake-expected-vs-openbc.md)
- [Collision Test (2026-02-22)](15-collision-test-2026-02-22.md)
- [Priority Fix List](16-priority-fix-list.md)
- [Files to Modify](17-files-to-modify.md)
