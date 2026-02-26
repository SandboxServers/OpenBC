# Sequencing Rules


The complete observed flow:

```
Server                              Client
  |                                    |
  |--- ConnectAck + ChecksumReq[0] -->|  (round 0 bundled with connect response)
  |                                    |
  |<------- ChecksumResp[0] ----------|
  |                                    |
  |--- ACK + ChecksumReq[1] --------->|  (round 1 sent only after round 0 response)
  |                                    |
  |<------- ChecksumResp[1] ----------|
  |                                    |
  |------- ChecksumReq[2] ----------->|  (round 2 sent only after round 1 response)
  |                                    |
  |<------- ChecksumResp[2] ----------|  (LARGE: fragmented, ~400 bytes)
  |                                    |
  |--- ACK + ChecksumReq[3] --------->|  (round 3 sent only after round 2 response)
  |                                    |
  |<------- ChecksumResp[3] ----------|
  |                                    |
  |--- ACK + ChecksumReq[0xFF] ------>|  (final round after round 3 response)
  |                                    |
  |<------- ChecksumResp[0xFF] -------|  (LARGE: fragmented, ~275 bytes)
  |                                    |
  |--- 0x28 + Settings + GameInit --->|  (game phase begins)
  |                                    |
```

**Critical**: The stock server never sends the next ChecksumReq until the previous ChecksumResp has been received and validated. Sending multiple rounds simultaneously or sending round 0xFF before round 3 completes will confuse the client's state machine.

---

