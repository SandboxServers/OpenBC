# Post-Checksum Sequence


After all 5 rounds pass validation, the server sends three messages in a single packet:

1. **Opcode `0x28`** (6 bytes) -- Checksum-complete signal. No game-level payload. Appears to be a NetFile-layer acknowledgement.
2. **Opcode `0x00` (Settings)** -- Game configuration: game time (float), two config bytes, player slot (byte), map name (length-prefixed string), checksum correction flag.
3. **Opcode `0x01` (GameInit)** -- Single-byte trigger that tells the client to initialize the game with the settings received in opcode 0x00.

The client then transitions from the checksum/lobby state to the game loading state.

---

