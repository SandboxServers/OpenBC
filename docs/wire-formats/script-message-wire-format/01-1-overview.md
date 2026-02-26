# 1. Overview


Bridge Commander has two categories of game-layer network messages:

1. **Engine opcodes (0x00-0x2A)**: Handled by C++ dispatch tables. Structured formats defined by the engine (object creation, weapon fire, state updates, etc.)

2. **Script messages (0x2C+)**: Created by Python scripts using `TGMessage_Create()` + `TGBufferStream`. The payload is entirely script-defined. The first byte is conventionally a message type identifier.

Both categories travel inside **the same type 0x32 transport envelope** (see [transport-layer.md](../../protocol/transport-layer.md)). There is no separate transport type for script messages.

---

