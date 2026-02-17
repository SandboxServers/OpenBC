# Connect Handshake Analysis

## Summary
The TGNetwork Connect handshake uses type 0x03 (Connect) for BOTH the client request and server response.
ConnectAck (0x05) is NOT used for connection acceptance -- it's only used for shutdown notification.

## Evidence Sources
- Packet trace: `STBC-Dedicated-Server/game/stock-dedi/packet_trace.log` (lines 158-175)
- Packet trace: `STBC-Dedicated-Server/logs/stock/battle-of-valentines-day/packet_trace.log` (lines 20-38)
- Decompiled code: `11_tgnetwork.c` FUN_006b6640 (server Connect handler, lines 3881-4069)
- Factory table: `message_factory_hooks.inc.c` (type 0x03 -> FUN_006be860, type 0x05 -> FUN_006bf410)

## Connect Message Wire Format

Connect/ConnectData/ConnectAck all use a DIFFERENT framing than the simple [type:1][len:1] format:

```
[type:1][flags_len_lo:1][flags_len_hi:1][seq:2 if flags_len!=0][data...]
```

The 2nd and 3rd bytes form a uint16 LE with embedded flags:
- Bit 15 (0x8000): reliable delivery required
- Bit 14 (0x4000): priority queue
- Bits 13-0: total message length including type byte

This is parsed by FUN_006be860 (Connect factory) at `*(ushort*)(param_1+1)`.

## Client Connect (type 0x03, client -> server)

Full packet (17 bytes):
```
FF 01 03 0F C0 00 00 0A 0A 0A EF 5F 0A 00 00 00 00
```
- FF: direction = init handshake
- 01: msg_count = 1
- 03: type = Connect
- 0F C0: flags_len = 0xC00F (reliable, priority, totalLen=15)
- 00 00: sequence = 0
- 0A 0A 0A EF: client IP address (10.10.10.239)
- Remaining: additional connect data (port, padding)

Payload contains client's IP address and other identification data.

## Server Connect Response (type 0x03, server -> client)

Transport message (6 bytes):
```
03 06 C0 00 00 02
```
- 03: type = Connect
- 06 C0: flags_len = 0xC006 (reliable, priority, totalLen=6)
- 00 00: sequence = 0
- 02: payload = assigned peer slot number

Always batched with first ChecksumReq in same UDP packet.

## Decompiled Code: FUN_006b6640 (Server Connect Handler)

Key path (when this+0x10e != 0, i.e. IS_HOST):

1. Read client data: `FUN_006b8530(param_1, &local_124)` -> gets payload+size
2. Read client IP: `local_130 = *piVar3` (first 4 bytes of payload)
3. Check duplicate: `(*vtable[0x5c])(local_130)` returns 1 if already connected
   - If duplicate: send rejection with code 5 (piVar3[0x10] = 5)
4. Check server capacity: `this+0xe8 > 0`
   - Check password if data too short: reject with code 2
5. Allocate slot: `iVar6 = FUN_006b7540(this)` -> returns next free peer ID
6. Register peer: `FUN_006bba10(this+0x28, clientIP, peerID)`
7. Send Connect response:
   ```c
   piVar3 = FUN_006be730(puVar5);      // Create Connect message
   cStack_132 = (char)iVar6;            // Slot as byte
   FUN_006b84d0(piVar3, &cStack_132, 1); // 1-byte payload
   FUN_006b4c10(this, peerAddr, piVar3, 0); // Send
   ```
8. Send ConnectData (0x04) about existing peers to new client
9. Post ET_NEW_PLAYER (0x60004) event

## Rejection Codes (piVar3[0x10])

| Code | Meaning | When |
|------|---------|------|
| 1 | Keepalive/ping | Internal |
| 2 | Rejected (full/password) | Server capacity reached or wrong password |
| 5 | Duplicate connection | Client IP already registered |

## ConnectAck (0x05) -- Shutdown Only

ConnectAck is used when the server shuts down, sending a notification to all peers.
Format from trace: `[0x05][0x0A][0xC0][0x00][0x00][slot][ip:4]`

## ConnectData (0x04) -- Peer Mesh Info

After accepting a new client, the server sends ConnectData messages to introduce
existing peers. This builds the peer-to-peer mesh. Constructed via FUN_006bc5b0.
Each contains: [peerID:1][peerIP:4][peerData:variable]

## OpenBC Impact

Current OpenBC sends ConnectAck(0x05) which is WRONG but happens to work because
the stock client is tolerant. Should be changed to send Connect(0x03) with:
- 2-byte flags/len (0x06 0xC0 for reliable+priority, totalLen=6)
- 2-byte sequence (0x00 0x00)
- 1-byte slot number payload

The Connect framing uses a different wire format than Keepalive/Disconnect.
