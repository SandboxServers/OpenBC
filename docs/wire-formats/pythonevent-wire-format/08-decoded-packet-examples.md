# Decoded Packet Examples

> **2026-05-29 cascade correction**: Earlier wording labelled factory 0x0101 as
> "SubsystemEvent" — that name was a fabrication. **Factory 0x0101 IS plain TGEvent.**
> The corrected hierarchy is TGEvent (0x0101) → CharEvent (0x0105) / ObjPtrEvent (0x010C).
> Examples below have been updated. See `../tgobjptrevent-wire-format.md` for the
> canonical reference and an open question on TGObjPtrEvent's extension size.

### Example 1: ADD_TO_REPAIR_LIST (17 bytes on-wire / 16-byte payload + 1 opcode)

```
06                    opcode = 0x06 (PythonEvent)
01 01 00 00           factory_id = 0x00000101 (TGEvent — base event class)
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

### Example 4: REPAIR_COMPLETED (21 bytes — pending verification of obj_ptr extension size)

```
06                    opcode = 0x06 (PythonEvent)
0C 01 00 00           factory_id = 0x0000010C (TGObjPtrEvent)
74 00 80 00           event_type = 0x00800074 (REPAIR_COMPLETED)
2A 00 00 00           source_obj = 0x0000002A (repaired subsystem's object ID)
1E 00 00 00           dest_obj = 0x0000001E (repair subsystem's object ID)
?? ?? ?? ??           obj_ptr   = (third object reference, +4 byte int32 per canonical wire-format doc)
```

> **2026-05-29 correction**: Earlier wording showed REPAIR_COMPLETED as factory 0x0101 / 17 bytes. Per the cascade correction, REPAIR_COMPLETED and REPAIR_CANNOT_BE_COMPLETED use factory **0x010C (TGObjPtrEvent)**, not 0x0101. The TGObjPtrEvent extension size (+4 bytes documented here) needs follow-up verification — see `../tgobjptrevent-wire-format.md`. OpenBC does not currently emit these events.

---

