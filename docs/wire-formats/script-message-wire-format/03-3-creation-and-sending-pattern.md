# 3. Creation and Sending Pattern


The canonical Python pattern for creating and sending a script message:

```python
# Create message object
pMessage = App.TGMessage_Create()
pMessage.SetGuaranteed(1)           # Reliable delivery (recommended)

# Create write stream and fill payload
kStream = App.TGBufferStream()
kStream.OpenBuffer(256)             # Allocate write buffer

kStream.WriteChar(chr(messageType)) # First byte: message type (e.g., 0x38)
kStream.WriteInt(someValue)         # Additional payload data
kStream.WriteShort(textLength)
kStream.Write(textString, textLength)

# Attach payload to message and send
pMessage.SetDataFromStream(kStream) # Copies stream bytes into message
pNetwork.SendTGMessage(0, pMessage) # 0 = broadcast to all peers
kStream.CloseBuffer()               # Clean up stream buffer
```

### Key Points

- `SetDataFromStream` copies the stream's written bytes directly into the message's data buffer. **No additional framing or header is added.** The stream content IS the message payload.
- `SetGuaranteed(1)` enables reliable delivery (ACK + retransmit). The default after `TGMessage_Create()` is unreliable. All stock scripts explicitly set guaranteed=1.
- The message type byte is the **first byte of the payload**, written by the script. The engine does not prepend anything.

### Stream Write Primitives

All writes are little-endian (native x86):

| Python Method | Size | Format |
|---------------|------|--------|
| `WriteChar(chr(N))` | 1 byte | uint8 |
| `WriteBool(N)` | 1 byte | uint8 (0 or 1) |
| `WriteShort(N)` | 2 bytes | uint16 LE |
| `WriteInt(N)` | 4 bytes | int32 LE |
| `WriteLong(N)` | 4 bytes | int32 LE (same as WriteInt on 32-bit) |
| `WriteFloat(N)` | 4 bytes | float32 LE (IEEE 754) |
| `Write(buf, len)` | N bytes | Raw memcpy |
| `WriteCString(s)` | 2+N bytes | [uint16 LE strlen][raw chars, no null] |

---

