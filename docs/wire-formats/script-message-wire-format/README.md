# Script Message Wire Format

This document has been decomposed into focused chapters for faster lookup and lower context load.


How Python-created network messages (TGMessage) are framed on the UDP wire. Covers the creation API, wire encoding, send/receive dispatch, and byte-level examples.

Derived from protocol analysis of stock BC packet captures and reference Python scripts (MissionShared.py, MultiplayerMenus.py, MultiplayerGame.py).

---


## Contents

- [1. Overview](01-1-overview.md)
- [2. MAX_MESSAGE_TYPES Constant](02-2-max-message-types-constant.md)
- [3. Creation and Sending Pattern](03-3-creation-and-sending-pattern.md)
- [4. Wire Format](04-4-wire-format.md)
- [5. Byte-By-Byte Wire Examples](05-5-byte-by-byte-wire-examples.md)
- [6. Send Functions](06-6-send-functions.md)
- [7. Receive-Side Dispatch](07-7-receive-side-dispatch.md)
- [8. Stock Message Formats](08-8-stock-message-formats.md)
- [9. Implementation Notes for Server](09-9-implementation-notes-for-server.md)
- [Related Documents](10-related-documents.md)
