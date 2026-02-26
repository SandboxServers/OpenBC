# Decision Logic: When to Send 0x02 vs 0x03


When replicating existing objects to a joining player (or responding to a RequestObject 0x1E),
the server decides which opcode to use:

```
For each existing game object:
    ship = GetShipWrapper(object)
    if ship exists AND ship has team info:
        send as 0x03 (ObjCreateTeam)
    else:
        send as 0x02 (ObjCreate)
```

### Key Behavioral Differences

| Property | 0x02 (ObjCreate) | 0x03 (ObjCreateTeam) |
|----------|-------------------|----------------------|
| Team byte | Not present | Present (1 byte after owner slot) |
| Player slot binding | Does NOT update player slot's base object ID | Updates player slot's base object ID |
| Scene entry | Does NOT trigger EnterScene on the host | Triggers EnterScene on the host |
| Typical objects | Environmental objects, host dummy ship, AI ships | Player-controlled ships |

### What Are Non-Team Objects?

Objects sent via 0x02 include:
- **Environmental objects**: asteroids, stations, comm arrays
- **Host dummy ship**: the server's own placeholder ship (player slot 0)
- **AI ships**: ships in cooperative mode without team assignment
- **Torpedoes in flight**: class_id 0x8009 objects (though these are transient)

In a 3-player trace, 3 instances of 0x02 were observed (all server-owned, low object IDs)
sent to the 3rd joining player during late-join replication.

### Implementation Note

A server that only sends 0x03 for player ships will leave late-joining clients without
environmental objects. Both opcodes must be implemented for correct late-join replication.

---

