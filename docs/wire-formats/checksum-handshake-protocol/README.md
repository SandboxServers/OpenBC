# Checksum Handshake Protocol

This document has been decomposed into focused chapters for faster lookup and lower context load.


Observed behavior of the Bridge Commander 1.1 checksum exchange, documented from black-box packet captures of a stock dedicated server communicating with a stock BC 1.1 client. All data below reflects observed wire behavior only.

**Date**: 2026-02-17
**Method**: Packet capture with decryption (AlbyRules stream cipher), stock dedicated server + stock client, local loopback

---


## Contents

- [Overview](01-overview.md)
- [The 5 Rounds](02-the-5-rounds.md)
- [Wire Format](03-wire-format.md)
- [Sequencing Rules](04-sequencing-rules.md)
- [Observed Packet Trace (Stock Dedicated Server)](05-observed-packet-trace-stock-dedicated-server.md)
- [Timing](06-timing.md)
- [Post-Checksum Sequence](07-post-checksum-sequence.md)
- [Common Implementation Mistakes](08-common-implementation-mistakes.md)
- [Checksum Validation](09-checksum-validation.md)
- [Summary](10-summary.md)
