# Checksum Validation


The server compares the client's hash data against its own computation for the same file set. If hashes match, the round passes. If not, the server can:
- Send an error (opcode `0x22` or `0x23`) and boot the player
- Allow a configurable tolerance (the Settings packet includes a checksum correction flag)

The hash algorithm itself is not documented here. For a reimplementation that doesn't validate checksums, the server can accept any response and proceed to the next round.

---

