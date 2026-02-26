# Decoded Packet Examples


### Example 1: ADD_TO_REPAIR_LIST (17 bytes)

```
06                    opcode = 0x06 (PythonEvent)
01 01 00 00           factory_id = 0x00000101 (SubsystemEvent)
DF 00 80 00           event_type = 0x008000DF (ADD_TO_REPAIR_LIST)
2A 00 00 00           source_obj = 0x0000002A (damaged subsystem's object ID)
1E 00 00 00           dest_obj = 0x0000001E (repair subsystem's object ID)
```

Note: subsystem object IDs are small sequential integers from the global counter, not
player-base IDs like ship objects.

### Example 2: WEAPON_FIRED (21 bytes)

```
06                    opcode = 0x06 (PythonEvent)
0C 01 00 00           factory_id = 0x0000010C (ObjPtrEvent)
7C 00 80 00           event_type = 0x0080007C (WEAPON_FIRED)
FF FF FF 3F           source_obj = 0x3FFFFFFF (Player 0's ship)
00 00 00 00           dest_obj = NULL
3E 00 C0 3F           obj_ptr = 0x3FC0003E (weapon subsystem object ID)
```

### Example 3: OBJECT_EXPLODING (25 bytes)

```
06                    opcode = 0x06 (PythonEvent)
29 81 00 00           factory_id = 0x00008129 (ObjectExplodingEvent)
4E 00 80 00           event_type = 0x0080004E (OBJECT_EXPLODING)
FF FF FF 3F           source_obj = 0x3FFFFFFF (Player 0's ship, exploding)
FF FF FF FF           dest_obj = sentinel (-1)
02 00 00 00           firing_player_id = 2 (killed by player 2)
00 00 80 3F           lifetime = 1.0f (1 second explosion)
```

### Example 4: REPAIR_COMPLETED (17 bytes)

```
06                    opcode = 0x06 (PythonEvent)
01 01 00 00           factory_id = 0x00000101 (SubsystemEvent)
74 00 80 00           event_type = 0x00800074 (REPAIR_COMPLETED)
2A 00 00 00           source_obj = 0x0000002A (repaired subsystem's object ID)
1E 00 00 00           dest_obj = 0x0000001E (repair subsystem's object ID)
```

---

