# 7. Receive-Side Dispatch


When a type 0x32 message arrives from the network:

1. **Transport layer** deserializes flags_len, seq_num, and payload from the UDP packet
2. **Reliable delivery** handles ACK generation and fragment reassembly if needed
3. **Event system** wraps the completed message as an `ET_NETWORK_MESSAGE_EVENT` and posts it to the event manager
4. **Multiple handlers** receive the event simultaneously:
   - **C++ game handler**: Reads first payload byte, dispatches opcodes 0x02-0x2A via switch table. Opcodes outside this range (including all script messages 0x2C+) are **ignored** by this handler.
   - **C++ UI handler**: Dispatches opcodes 0x00, 0x01, 0x16
   - **C++ checksum handler**: Dispatches opcodes 0x20-0x27
   - **Python handlers**: Registered via event system. Read first byte to identify message type, dispatch to appropriate handler function.

The C++ handlers and Python handlers are **independent** — they all receive the same event and each processes only the opcodes it recognizes. Script messages (0x2C+) pass through C++ handlers without effect and are handled exclusively by Python.

### Python Receive Pattern

```python
def ProcessMessageHandler(pObject, pEvent):
    pMessage = pEvent.Message()
    kStream = pMessage.GetBufferStream()

    cType = ord(kStream.ReadChar())  # Read message type byte

    if cType == CHAT_MESSAGE:
        HandleChat(kStream)
    elif cType == END_GAME_MESSAGE:
        HandleEndGame(kStream)
    # ... etc
```

---

