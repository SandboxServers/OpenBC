# Multiplayer Network Propagation


### Power Distribution Has No Dedicated Network Message

When a player adjusts the 4 engineering sliders (weapons, shields, engines, sensors), the
change to `powerPercentageWanted` is **not sent as a dedicated network message**. There is
no event-forwarding opcode, no Python-level TGMessage, and no immediate network send call.

Instead, power distribution percentages propagate **exclusively through the StateUpdate
subsystem health round-robin** (flag 0x20 block). Each client sets power locally, the
periodic state replication includes the current power percentages, and remote peers apply
them on receipt.

### Why No Dedicated Message?

The engine's event forwarding system selectively registers certain subsystem events for
network broadcast (weapon firing, cloak toggle, phaser intensity, etc.). The
"subsystem power changed" event is deliberately **not registered** for network forwarding.
It fires locally (for UI update) but never reaches the network layer.

The complete list of events that ARE network-forwarded:

| Event | Network Opcode | Description |
|-------|---------------|-------------|
| StartFiring | 0x07 | Weapon begins firing |
| StopFiring | 0x08 | Weapon stops firing |
| StopFiringAtTarget | 0x09 | Beam stops tracking |
| SubsystemStatus | 0x0A | Subsystem on/off toggle |
| RepairListPriority | 0x11 | Repair queue ordering |
| SetPhaserLevel | 0x12 | Phaser intensity (LOW/MED/HIGH) |
| StartCloaking | 0x0E | Cloak activated |
| StopCloaking | 0x0F | Cloak deactivated |
| StartWarp | 0x10 | Warp engaged |
| TorpedoTypeChange | 0x1B | Torpedo type selection |

**Power slider changes are absent from this list.**

### How Power Percentages Travel Over the Network

Power percentages are serialized inside the StateUpdate subsystem health block. Each
powered subsystem writes its current `powerPercentageWanted` as part of its periodic
state replication:

**Encoding (sender):**
```
[condition: byte]            // subsystem health 0-255
[children: recursive]        // child subsystem conditions
[hasData: 1 bit]             // 1 = remote ship, 0 = own ship
[if hasData=1:]
  [powerPctWanted: byte]     // (int)(powerPercentageWanted * 100.0)
```

**Decoding (receiver):**
```
if hasData bit is set:
    pctByte = read byte
    if timestamp is newer than last update:
        powerPercentageWanted = pctByte * 0.01
```

**Precision**: 1% steps (0-100 for normal range, up to 125 for overclocked subsystems).

### Own-Ship Skip

When the host sends a StateUpdate about ship X back to the player who owns ship X, the
power data is **omitted** (hasData = 0). This prevents the host from overwriting the
owner's local slider settings with stale network data.

When the host sends ship X's state to any other player, the power data IS included
(hasData = 1).

### Sign Bit for On/Off State

An alternate deserialization path packs the subsystem on/off state into the power byte:
- **Positive byte** → subsystem is ON, percentage = byte * 0.01
- **Negative byte** (bit 7 set) → subsystem is OFF, percentage = (-byte) * 0.01

This allows the on/off toggle to propagate via StateUpdate as well as through the
dedicated SubsystemStatus opcode (0x0A). The dedicated opcode provides immediate
notification; the StateUpdate provides eventual consistency.

### Data Flow

```
Client A adjusts engineering slider
  → SetPowerPercentageWanted(newValue) applied locally
  → No network message sent

Client A's next StateUpdate cycle (every ~100ms per subsystem)
  → PoweredSubsystem::WriteState includes powerPctWanted byte
  → Sent to host via opcode 0x1C

Host receives StateUpdate
  → Applies power percentage to its copy of Client A's ship
  → Rebroadcasts in next StateUpdate to Client B (hasData=1)
  → Sends back to Client A with hasData=0 (no overwrite)

Client B receives StateUpdate
  → Applies Client A's power percentages to its local copy
```

### Timing and Latency

- Power changes propagate at the StateUpdate round-robin rate (~10Hz per ship)
- With ~11 top-level subsystems on a Sovereign-class ship, a full cycle takes 1-2 seconds
- This means a slider change may take up to 1-2 seconds to reach all peers
- This is acceptable because power distribution is a gradual management activity, not a
  frame-critical action like weapon firing

### Server Authority

The host does **not** validate or enforce power percentages. It applies whatever the client
sends. A client could set `powerPercentageWanted` to 200% and the host would accept it.

### Auto-Balance is Client-Side Only

The `AdjustPower` algorithm (see above) runs locally on the adjusting client's UI layer.
Other peers see only the final balanced percentages via StateUpdate, not the intermediate
auto-balancing steps.

### Contrast: What IS Immediately Forwarded

| Action | Network Mechanism | Latency |
|--------|-------------------|---------|
| Subsystem on/off toggle | Dedicated opcode (0x0A) | Immediate (reliable) |
| Phaser intensity (LOW/MED/HIGH) | Dedicated opcode (0x12) | Immediate (reliable) |
| Power slider adjustment | StateUpdate (0x1C) piggyback | 1-2 seconds (round-robin) |
| Torpedo type change | Dedicated opcode (0x1B) | Immediate (reliable) |

The design rationale: discrete toggles (on/off, LOW/MED/HIGH, torpedo type) use immediate
reliable messages. Continuous values (power percentages) use the existing state replication,
which is more bandwidth-efficient for frequently-changing values.

---

