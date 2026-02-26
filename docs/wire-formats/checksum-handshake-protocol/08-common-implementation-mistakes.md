# Common Implementation Mistakes


### 1. Missing Round 3

The server MUST send all 5 rounds (0, 1, 2, 3, 0xFF). Skipping round 3 (`scripts/mainmenu`) and jumping straight to 0xFF after round 2 will cause the client to enter an inconsistent state.

### 2. Sending Rounds Simultaneously

The stock server sends each round only after receiving the previous round's response. Sending multiple rounds at once (e.g., queuing all 5 into the reliable transport together) may cause the client to process 0xFF early and transition to game state before completing earlier rounds.

### 3. Wrong BitByte Encoding

Writing the recursive flag as a plain `0x00`/`0x01` byte instead of the bit-packed format (`0x20`/`0x21`) will desynchronize the client's stream reader. All subsequent field reads will be shifted by the missing count bits, causing parse failures.

### 4. Not Handling Fragmented Responses

Round 2 and round 0xFF produce responses that exceed the reliable transport's maximum message size. These arrive as multiple fragments (observed: 3 fragments for round 2, 2+ for round 0xFF). The server must reassemble fragmented reliable messages before attempting to parse the ChecksumResp payload.

### 5. Missing Opcode 0x28

The stock server sends opcode `0x28` before Settings and GameInit. While its exact purpose is unclear from black-box observation, omitting it may affect client state.

---

