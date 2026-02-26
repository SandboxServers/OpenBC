# Overview


After the TCP-level connection is established and the Connect/ConnectAck handshake completes, the server initiates a **5-round checksum exchange** before sending game configuration. Each round asks the client to hash a set of files. The server verifies each response before proceeding to the next round.

**Critical protocol rule**: Rounds are sent **one at a time, sequentially**. The server sends round N, waits for the client's response, validates it, then sends round N+1. The server MUST NOT send the next round before receiving and processing the current round's response.

---

