# 11. Key Behavioral Guarantees


1. **Order is script-determined**: The serialization list order comes from the hardpoint script's `AddToSet()` call order, not from any fixed table.

2. **Both sides agree**: Server and client always have identical serialization lists because they run the same checksum-verified hardpoint script and the same setup/linking logic.

3. **Missing subsystems are absent, not padded**: A ship without a cloaking device simply doesn't have a cloak entry in its list. There is no 0xFF padding or skipped index.

4. **Children are recursive**: When a system writes its state, it recursively writes all children. The receiver reads them in the same order.

5. **Round-robin is position-based**: The `start_index` byte is a list position (0 through list_length-1), not a subsystem type ID.

6. **Budget is approximate**: The 10-byte budget (including `start_index`) is checked **before** each subsystem's WriteState call. A subsystem that starts within budget will finish its complete WriteState output even if it exceeds the limit.

7. **Full cycle time depends on ship class**: Ships with more subsystems (especially many weapons) take more ticks to complete a full health cycle.

---

