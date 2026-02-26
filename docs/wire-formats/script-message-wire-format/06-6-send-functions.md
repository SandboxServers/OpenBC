# 6. Send Functions


### SendTGMessage(targetID, message)

Sends a message to a specific peer or broadcasts to all.

| targetID | Behavior |
|----------|----------|
| `0` | **Broadcast**: Send to ALL connected peers (message cloned for each) |
| `> 0` | **Unicast**: Send to the specific peer with that network ID |
| `pNetwork.GetHostID()` | Send to host only (common for client→server messages) |

### SendTGMessageToGroup(groupName, message)

Sends a message to all members of a named network group.

**Built-in groups** (created by the multiplayer game constructor):

| Group Name | Members | Used For |
|------------|---------|----------|
| `"NoMe"` | All connected peers EXCEPT the sender | Chat relay (host receives chat, forwards to "NoMe") |
| `"Forward"` | Same membership as "NoMe" | Engine event forwarding (weapon fire, cloak, etc.) |

Both groups contain the same peers. The naming convention distinguishes script-level relay ("NoMe") from engine-level event forwarding ("Forward").

### Guaranteed vs Unreliable

- `SetGuaranteed(1)`: Reliable delivery — transport sets bit 15 in flags_len, includes seq_num, ACKs and retransmits. **All stock scripts use this.**
- `SetGuaranteed(0)` (default): Fire-and-forget — no seq_num, no ACK, no retransmit. Supported but never used by stock scripts.

---

