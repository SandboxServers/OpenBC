# 9. Implementation Notes for Server


### Relay Behavior

The server must handle script messages in two ways:

1. **Messages it originates** (scoring, game flow): Create the payload, wrap in type 0x32, send to peers
2. **Messages it relays** (chat): Receive from sender, parse enough to identify type, forward to appropriate peers

For chat relay, the server receives CHAT_MESSAGE from a client (sent to host), then must create a new message with the same payload and send it to the "NoMe" group (all peers except the original sender).

### No Payload Modification

The server should never modify the script payload content when relaying. The payload bytes between the message type byte and end-of-message are opaque to the relay — just copy and forward.

### Custom Mod Messages

Mods may define message types in the 43-255 range. A generic server should relay any unrecognized message type byte to all peers (or the appropriate group) without parsing the payload. This enables mod compatibility without server-side mod knowledge.

---

