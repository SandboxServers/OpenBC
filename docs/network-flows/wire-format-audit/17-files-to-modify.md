# Files to Modify


| File | Changes |
|------|---------|
| `src/protocol/buffer.c` | Fix `bc_buf_write_bit` and `bc_buf_read_bit` count encoding |
| `src/protocol/handshake.c` | Fix final round 0xFF to send full request; fix checksum round directories |
| `src/server/main.c` | Add 0x28 send; remove UICollisionSetting from handshake; fix 0x2A direction; fix keepalive |
| `include/openbc/opcodes.h` | No changes needed |
| `src/network/transport.c` | Fix default ACK flags |
| `tests/test_protocol.c` | Update tests for new bit-packing encoding |
