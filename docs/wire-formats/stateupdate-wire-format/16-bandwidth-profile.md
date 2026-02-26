# Bandwidth Profile


From a 34-minute 3-player combat session:

| Metric | Value |
|--------|-------|
| Total StateUpdate messages | 199,541 |
| Percentage of all game messages | ~97% |
| Average rate | ~98 messages/second (3 ships x ~10 Hz x 2 directions + overhead) |
| Typical client->server size | 15-25 bytes |
| Typical server->client size | 15-25 bytes |
| Delivery mode | Unreliable (no ACK, no retransmit) |

