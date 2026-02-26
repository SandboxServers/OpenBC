# Summary


| Property | Value |
|----------|-------|
| Total rounds | 5 (indices 0, 1, 2, 3, 0xFF) |
| Request opcode | `0x20` |
| Response opcode | `0x21` |
| Delivery | Reliable transport (sequenced, ACK'd) |
| Sequencing | Strict serial: send request, wait for response, repeat |
| Largest response | Round 2 (~400 bytes, fragmented) |
| Post-checksum | Opcode `0x28` + `0x00` Settings + `0x01` GameInit |
| Total duration (loopback) | ~1.3 seconds |
