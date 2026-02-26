# Complete Handshake: Expected vs. OpenBC


### Stock Dedi (from trace)
```
C->S: Connect(0x03) dir=INIT
S->C: Connect(0x03) + ChecksumReq round 0          (batched, 1 packet)
C->S: ACK + ACK + Keepalive(name)                   (batched)
S->C: ACK + Keepalive(echo)                          (server echoes identity)
C->S: ChecksumResp round 0
S->C: ACK + ChecksumReq round 1
C->S: ACK + ChecksumResp round 1
S->C: ACK + ChecksumReq round 2
C->S: ACK + ChecksumResp round 2 (fragmented)
S->C: ACK + ACK + ChecksumReq round 3
C->S: ACK + ChecksumResp round 3
S->C: ACK + ChecksumReq round 0xFF (Scripts/Multiplayer)
C->S: ACK + ChecksumResp round 0xFF (fragmented)
S->C: ACK + 0x28 + Settings(0x00) + GameInit(0x01)  (batched, 1 packet)
C->S: ACK + ACK + ACK
C->S: NewPlayerInGame(0x2A)                          (CLIENT sends this)
S->C: ACK + MissionInit(0x35) + 0x17                 (server responds)
```

### OpenBC (current)
```
C->S: Connect(0x03) dir=INIT
S->C: Connect(0x03) response                         (separate packet)
S->C: ChecksumReq round 0                            (separate packet)
C->S: ACK + Keepalive(name)
S->C: Keepalive(minimal 2 bytes)                      <-- WRONG format
C->S: ChecksumResp round 0
S->C: ChecksumReq round 1
...rounds 1-3 same...
S->C: [0x20][0xFF]                                    <-- WRONG: minimal, not full request
C->S: ChecksumResp round 0xFF
S->C: Settings(0x00)                                  <-- WRONG: bit packing (0x41 not 0x61)
S->C: UICollisionSetting(0x16)                        <-- WRONG: not in real handshake
S->C: GameInit(0x01)
S->C: NewPlayerInGame(0x2A)                           <-- WRONG: server shouldn't send this
S->C: MissionInit(0x35)                               <-- WRONG: should wait for client 0x2A
```

---

