# Power State Wire Format


This section describes the exact wire encoding and update algorithm for power state replication, providing the detail needed for a server to match client expectations.

### Two Serialization Interfaces

Power state uses two distinct serialization interfaces depending on context:

#### Interface A: Round-Robin (StateUpdate flag 0x20)

Used during **ongoing gameplay** in the subsystem health round-robin. Each powered subsystem conditionally includes a power byte:

**Sender:**
```
[condition: byte]            // subsystem health: (currentHP / maxHP) * 255, truncated
[children: recursive]        // each child subsystem writes its own condition byte
[hasData: 1 bit]             // 1 = remote ship (include power), 0 = own ship (skip)
[if hasData=1:]
  [powerPctWanted: byte]     // (int)(powerPercentageWanted * 100.0), range 0-125
```

**Receiver:**
```
read condition byte + children (recursive)
hasData = read 1 bit
if hasData:
    pctByte = read byte
    if previousTimestamp < packetTimestamp:
        SetPowerPercentageWanted(pctByte * 0.01)
```

The `hasData` bit allows the sender to skip power data when the recipient owns the ship, preventing stale server data from overwriting the player's local slider settings.

#### Interface B: ObjCreate / Full Snapshot

Used during **initial object creation** (ObjCreate opcode) and weapon round-robin. This path uses sign-bit encoding to pack the on/off state into the power byte:

**Sender:**
```
[condition: byte]            // health
[children: recursive]
[powerByte: signed byte]     // positive = ON, negative = OFF
                             // absolute value = (int)(powerPercentageWanted * 100.0)
```

**Receiver:**
```
read condition byte + children
powerByte = read signed byte
if powerByte < 1:            // 0 or negative
    isOn = false
    powerPercentageWanted = (-powerByte) * 0.01
else:
    isOn = true
    powerPercentageWanted = powerByte * 0.01
```

**Sign-bit encoding rationale**: During ObjCreate, the receiver needs both the power percentage AND the on/off state. Packing them into one byte avoids an extra bit/byte for the toggle. A subsystem at 50% and OFF encodes as `-50`; the receiver restores both the slider position (50%) and the disabled state. This allows a player's pre-disable slider position to survive across the network.

### Round-Robin Algorithm

The subsystem health round-robin uses a **persistent cursor** per peer per ship, allowing it to resume where it left off each tick:

1. **Per-peer state**: The server maintains a cursor (linked list node pointer) and an index counter for each ship being sent to each peer. These persist across ticks.
2. **Start index**: Each flag 0x20 block begins with a `startIndex` byte telling the receiver which subsystem index the data starts from.
3. **10-byte budget**: The serializer writes subsystem data until either 10 bytes of stream space are consumed, or the cursor wraps back to its starting position (full cycle complete).
4. **Wrap detection**: When the cursor reaches the end of the subsystem linked list, it wraps to the beginning and resets the index to 0. If it reaches its starting position, the full cycle is complete and it stops.

```
[startIndex: byte]           // which subsystem index the round-robin starts from
[subsystem_0: WriteState]    // subsystem at startIndex position
[subsystem_1: WriteState]    // next subsystem (if budget allows)
...                          // continues until 10-byte budget exhausted or full wrap
```

### Power Byte Encoding

| Direction | Formula | Range | Precision |
|-----------|---------|-------|-----------|
| Send | `(int)(powerPercentageWanted * 100.0)` | 0–125 | Truncation toward zero |
| Receive | `(float)byte * 0.01` | 0.00–1.25 | 1% steps |

Maximum precision loss: 0.009 (e.g., 0.256 → byte 25 → 0.25).

### Own-Ship Skip

When sending state about ship X to the player who owns ship X:

- **hasData = 0** (Interface A): Power byte is omitted. The client's local slider state is authoritative for its own ship.
- **Interface B** (ObjCreate): Always includes power data, including for own ship.

**Own-ship determination**: The sender compares the ship's object ID against the target peer's assigned ship object ID. If they match (and it's a multiplayer game), the ship is "own-ship" for that peer.

**Server MUST implement this**: If the server sends power data (hasData=1) to the ship's owner, the server will continuously overwrite the player's local engineering slider positions with stale data, making the F5 panel unusable.

### Timestamp Ordering

The receiver only applies power data from Interface A if the packet timestamp is newer than the last known update timestamp. This prevents stale data from overwriting newer state in cases of packet reordering. The receiver saves the previous timestamp BEFORE processing the base class data (which updates the stored timestamp), then compares the incoming packet timestamp against the saved value.

### Update Timing

- **StateUpdate rate**: ~10Hz per ship (one StateUpdate packet every ~100ms)
- **Per-tick budget**: 10 bytes for flag 0x20 (subsystem health)
- **Sovereign example**: 11 top-level subsystems, variable bytes per subsystem (1–11 bytes depending on children and power data). Full cycle takes ~3-5 ticks.
- **Full cycle time**: ~0.3–0.5 seconds at 10Hz for all subsystem power percentages to transmit
- **Server must send StateUpdates at this rate** for clients to see smooth power bar updates on remote ships

### Server Implementation Requirements

For an OpenBC server to correctly replicate power state:

1. **Track per-peer write cursor**: Maintain a subsystem list node pointer and index counter for each ship being sent to each peer. Persist across ticks.
2. **Respect 10-byte budget**: Stop writing subsystem data when 10 bytes of stream space are consumed in the flag 0x20 block.
3. **Send startIndex byte**: Write the current index counter at the start of each flag 0x20 block so the receiver knows which subsystem the data starts from.
4. **Skip power data for own-ship**: When sending to the peer who owns the ship, set `hasData = 0` (omit power byte). This is critical — failing to do this makes the F5 panel unresponsive.
5. **Include power data for remote ships**: For all other peers, set `hasData = 1` and encode power as `(int)(powerPercentageWanted * 100.0)`.
6. **Use sign-bit encoding on ObjCreate**: When sending initial object state, pack on/off into the power byte (positive = ON, negative = OFF). This gives the client both the slider position and the subsystem's enable state.
7. **Apply received power only if timestamp is newer**: When receiving power state from clients, compare the packet timestamp against the saved previous timestamp. Only apply if newer.
8. **Do NOT validate power values**: The stock server does not clamp or reject power percentages. A client sending 125% (or even higher via a mod) should be accepted as-is.

---

