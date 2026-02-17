# ConnectData (Transport Type 0x04) Complete Analysis

## Summary
ConnectData (0x04) is a CONNECTION MANAGEMENT message, NOT a peer mesh info message.
It carries a 1-byte reason code and 1-byte peer address as payload. It is used for:
- Rejection notifications (codes 2, 3, 5)
- Disconnect/timeout notifications (code 1)

The "peer mesh" information is actually carried by **Keepalive (0x00)** messages.

## Wire Format (2-byte flags_len framing)
```
[type:1][flags_len:2 LE][seq:2][code:1][peerAddr:1]
  04      07 C0          NN 00   CC      PP
```
- Total wire size: 7 bytes
- flags_len = 0xC007: bit15=reliable, bit14=priority, totalLen=7
- code = reason code (stored at msg object[0x10])
- peerAddr = 1-byte peer slot/address

## Reason Codes (msg[0x10])
| Code | Meaning | Source | Context |
|------|---------|--------|---------|
| 0 | Default (unused?) | Constructor | - |
| 1 | Peer timeout/disconnect | 11_tgnetwork.c:2281 | Sent to all peers when a peer times out |
| 2 | Transport rejection: full/password | 11_tgnetwork.c:3971 | Sent to connecting peer |
| 3 | Game rejection: server full | 10_netfile_checksums.c:444 | Sent AFTER checksum pass when no player slots available |
| 5 | Transport rejection: duplicate | 11_tgnetwork.c:3952 | Sent when client IP already connected |

## Factory/Class Info
- Factory function: FUN_006badb0 (registered at factory table 0x009962d4 + 4*4 = 0x009962e4)
- Constructor: FUN_006bac70 (vtable PTR_LAB_0089596c, size 0x44 bytes)
- Extra field at object+0x40 (accessed as `int*[0x10]`) = reason code

## Key Decompiled Code Paths

### Rejection at checksum completion (code 3)
File: `10_netfile_checksums.c` lines 435-446
```c
// After checksum validation passes but no player slot available:
param_1 = CONCAT31(param_1._1_3_, (char)*(uint*)(iVar2 + 0x28));  // peer addr byte
piVar7 = FUN_006bac70(puVar4);  // Create ConnectData
piVar7[0x10] = 3;               // Code 3: game-level full
FUN_006b84d0(piVar7, &param_1, 1);  // 1-byte payload = peer addr
FUN_006b4c10(pvVar5, *(int*)(iVar2 + 0x28), piVar7, 0);  // Send to peer
```

### Rejection at transport (code 2, 5)
File: `11_tgnetwork.c` FUN_006b6640 lines 3950-3973

### Disconnect notification (code 1)
File: `11_tgnetwork.c` lines 2279-2285

## Important: ConnectData Does NOT Introduce Peers
Previous analysis was WRONG that ConnectData carries [peerID:1][peerIP:4][peerData:variable].
The actual peer introduction happens via Keepalive (0x00) messages that carry:
[slot:1][IP:4][name:UTF-16LE+null] -- total 26 bytes for "Unknown" name.

## The Server Sends NO ConnectData When Accepting a New Client
When the first client connects to a stock dedi:
1. Connect(0x03) response with slot assignment
2. ChecksumReq round 0 (batched with Connect)
3. Keepalive exchange (name)
4. 4 more checksum rounds (rounds 1, 2, 3, 255)
5. Unknown_28 + Settings + GameInit

NO ConnectData (0x04) is sent during successful connection acceptance.
