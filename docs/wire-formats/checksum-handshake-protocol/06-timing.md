# Timing


From the stock dedi packet trace (loopback, negligible network latency):

| Event | Timestamp | Delta |
|-------|-----------|-------|
| Connect | 09:18:40.179 | - |
| ConnectAck + Round 0 | 09:18:40.181 | +2ms |
| Round 0 Response | 09:18:40.188 | +7ms |
| Round 1 Sent | 09:18:40.189 | +1ms |
| Round 1 Response | 09:18:40.190 | +1ms |
| Round 2 Sent | 09:18:40.191 | +1ms |
| Round 2 Response (frag 1) | 09:18:40.896 | +705ms |
| Round 3 Sent | 09:18:41.357 | +461ms |
| Round 3 Response | 09:18:41.360 | +3ms |
| Round 0xFF Sent | 09:18:41.361 | +1ms |
| Round 0xFF Response | 09:18:41.424 | +63ms |
| Settings + GameInit | 09:18:41.472 | +48ms |

**Total checksum phase**: ~1.3 seconds on loopback.

Note: Round 2 takes **705ms** for the client to respond (scanning all ship `.pyc` files recursively). This is by far the longest round. Over a real network, expect 1-2 seconds for this round.

---

