# Priority Fix List


### P0 -- Will break client parsing (CRITICAL)
1. **Fix bit-packing encoding**: Change count field from `bit_count-1` to `bit_count` in `bc_buf_write_bit`. Update `bc_buf_read_bit` to match. This affects ALL bit-packed messages.

### P1 -- Will cause handshake failure (HIGH)
2. **Fix checksum round 0xFF**: Send full checksum request `[0x20][0xFF][dirLen][Scripts/Multiplayer][filterLen][*.pyc][recursive=1]`, not just `[0x20][0xFF]`.
3. **Add opcode 0x28**: Send `[0x28]` (1 byte reliable) before Settings.
4. **Fix NewPlayerInGame direction**: Don't send 0x2A. Wait for client to send it, then respond with MissionInit.
5. **Remove UICollisionSetting from handshake**: Don't send 0x16 during initial connection.

### P2 -- May cause subtle issues (MEDIUM)
6. **Fix server keepalive**: Echo client identity data instead of sending minimal `[0x00][0x02]`.
7. **Verify Settings player_slot**: Stock dedi sends slot=0 but OpenBC sends peer_slot (1+). Need to verify correct semantics.
8. **Verify checksum directory trailing slashes**: Rounds 2-3 may need trailing slash removed (`scripts/ships` vs `scripts/ships/`).

### P3 -- Cosmetic / defensive (LOW)
9. **Fix ACK flags**: Use 0x00 instead of 0x80.
10. **Batch Connect + ChecksumReq**: Optional optimization to match stock behavior.

---

