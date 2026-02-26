# Summary of Findings


| Area | Status | Severity |
|------|--------|----------|
| 1. Transport framing | MATCH (with caveats) | -- |
| 2. Connect handshake | MATCH | -- |
| 3. Checksum rounds 0-3 | MATCH | -- |
| 4. Checksum final round 0xFF | **MISMATCH** | HIGH |
| 5. TGBufferStream bit-packing | **MISMATCH** | **CRITICAL** |
| 6. Settings (0x00) payload | **MISMATCH** (due to #5) | **CRITICAL** |
| 7. GameInit (0x01) | MATCH | -- |
| 8. NewPlayerInGame (0x2A) direction | **MISMATCH** | HIGH |
| 9. Opcode 0x28 missing | **MISMATCH** | HIGH |
| 10. Server keepalive format | **MISMATCH** | MEDIUM |
| 11. ACK flags | MISMATCH | LOW |
| 12. UICollisionSetting (0x16) | MISMATCH (ordering) | HIGH |

---

