# Authority Model Summary


```
           Ship Owner (Client)                    Server
           =====================                  ======
Sends:     Position (0x01/0x02)                   Subsystem health (0x20)
           Orientation (0x04, 0x08)
           Speed (0x10)
           Weapon status (0x80)
           Cloak (0x40)

Delivery:  Unreliable (0x32 flags=0x00)           Unreliable (0x32 flags=0x00)

Rate:      ~10 Hz per ship                        ~10 Hz per ship

Anti-cheat: Sends subsys hash with Position       Verifies hash, kicks on mismatch
```

