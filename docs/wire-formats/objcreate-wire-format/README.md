# ObjCreate / ObjCreateTeam Wire Format (Opcodes 0x02 / 0x03)

This document has been decomposed into focused chapters for faster lookup and lower context load.


Wire format specification for Star Trek: Bridge Commander's object creation messages, documented from network packet captures and the game's shipped Python scripting API.


## Contents

- [Overview](01-overview.md)
- [Message Envelope](02-message-envelope.md)
- [Serialized Object Stream](03-serialized-object-stream.md)
- [Ship Object Body (class_id = 0x8008)](04-ship-object-body-class-id-0x8008.md)
- [Torpedo Object Body (class_id = 0x8009)](05-torpedo-object-body-class-id-0x8009.md)
- [Species Mapping Tables](06-species-mapping-tables.md)
- [Decision Logic: When to Send 0x02 vs 0x03](07-decision-logic-when-to-send-0x02-vs-0x03.md)
- [Receiver Behavior](08-receiver-behavior.md)
- [Host Relay Behavior](09-host-relay-behavior.md)
- [Decoded Packet Examples](10-decoded-packet-examples.md)
- [Open Questions](11-open-questions.md)
