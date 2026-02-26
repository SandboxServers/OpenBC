# 5. Byte-By-Byte Wire Examples


### Example 1: CHAT_MESSAGE (0x2C)

Python code:
```python
kStream.WriteChar(chr(CHAT_MESSAGE))     # 0x2C
kStream.WriteLong(pNetwork.GetLocalID()) # sender network ID (e.g., 2)
kStream.WriteShort(5)                    # text length
kStream.Write("hello", 5)               # raw text bytes
```

TGMessage payload (12 bytes):
```
2C 02 00 00 00 05 00 68 65 6C 6C 6F
^^                                      message type (CHAT_MESSAGE = 44)
   ^^ ^^ ^^ ^^                         sender ID (int32 LE = 2)
               ^^ ^^                    text length (uint16 LE = 5)
                     ^^ ^^ ^^ ^^ ^^    "hello" (raw bytes, no null)
```

Type 0x32 transport message (17 bytes):
```
32 11 80 01 00 2C 02 00 00 00 05 00 68 65 6C 6C 6F
^^                                                     type 0x32
   ^^ ^^                                               flags_len LE = 0x8011
                                                         bit 15 = reliable
                                                         bits 12-0 = 17 (total size)
         ^^ ^^                                          seq_num LE = 0x0001
               ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^  payload (12 bytes)
```

Full UDP packet (19 bytes, shown decrypted):
```
01 01 32 11 80 01 00 2C 02 00 00 00 05 00 68 65 6C 6C 6F
^^                                                           peer_id (server)
   ^^                                                        msg_count (1)
      ^^...                                                  transport message
```

### Example 2: Custom Mod Message (type 205)

Python code:
```python
MY_MSG = App.MAX_MESSAGE_TYPES + 162  # = 43 + 162 = 205 = 0xCD
kStream.WriteChar(chr(MY_MSG))        # 0xCD
kStream.WriteInt(42)                  # custom data
```

TGMessage payload (5 bytes):
```
CD 2A 00 00 00
^^              custom message type (205)
   ^^ ^^ ^^ ^^ int value 42 (int32 LE)
```

Type 0x32 transport message (10 bytes):
```
32 0A 80 01 00 CD 2A 00 00 00
^^                              type 0x32
   ^^ ^^                        flags_len LE = 0x800A (reliable, size=10)
         ^^ ^^                  seq_num LE = 0x0001
               ^^ ^^ ^^ ^^ ^^  payload (5 bytes)
```

### Example 3: END_GAME_MESSAGE (0x38)

Python code (from MissionShared.py):
```python
kStream.WriteChar(chr(END_GAME_MESSAGE))  # 0x38
kStream.WriteInt(iReason)                  # reason code (int32)
```

TGMessage payload (5 bytes):
```
38 03 00 00 00
^^              END_GAME_MESSAGE (56)
   ^^ ^^ ^^ ^^ reason code 3 (int32 LE)
```

---

