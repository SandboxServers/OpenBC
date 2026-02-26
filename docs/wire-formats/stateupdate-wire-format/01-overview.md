# Overview


Opcode 0x1C (StateUpdate) is the most frequently sent message in the multiplayer protocol, accounting for approximately 97% of all game traffic. It carries per-ship position, orientation, speed, subsystem health, and weapon status using a dirty-flag system that transmits only changed fields.

StateUpdate is the only game message sent with **unreliable** delivery (fire-and-forget, no ACK required). All other game messages use reliable delivery. This is a deliberate bandwidth optimization — at ~10 updates per ship per second, retransmitting lost updates would be wasteful since the next update supersedes the lost one.

