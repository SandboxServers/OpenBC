# 2. Wire Protocol Index Mapping


### There Is No Fixed Index Table

**Critical correction**: The wire protocol does NOT use a fixed index table (0=Reactor, 1=Repair, etc.). The `start_index` byte in flag 0x20 is a **position in the ship's serialization list**, and that list's contents and order are determined entirely by the hardpoint script.

Two different ship classes may have completely different subsystems at the same index position. A Sovereign has Hull at index 0; a Bird of Prey might have a different subsystem at index 0 depending on its hardpoint script.

### How Sender and Receiver Stay In Sync

Both the server and client:
1. Execute the **same** hardpoint Python script for each ship class (scripts are checksum-verified)
2. Run the **same** subsystem setup and linking code
3. End up with **identical** serialization lists in the same order

The `start_index` byte tells the receiver which list position the round-robin starts from. The receiver skips to that position in its own (identical) list and reads the data sequentially.

### Determining a Ship's Subsystem Order

To know the wire order for any ship class, examine its hardpoint script's `LoadPropertySet()` function. The order of `AddToSet("Scene Root", prop)` calls determines the initial list order. After the linking pass removes children, the remaining top-level systems are in their original relative order.

---

