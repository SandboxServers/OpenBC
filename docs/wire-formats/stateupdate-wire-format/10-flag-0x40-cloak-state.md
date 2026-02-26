# Flag 0x40 — Cloak State


```
+0      1     bitpacked   cloak_active     Boolean: 0x20=decloaked, 0x21=cloaked
```

The cloak field is a **bit-packed boolean**, NOT a raw 0/1 byte. Writing a plain `0x00`/`0x01` instead of `0x20`/`0x21` will desynchronize the stream parser.

Only sent when the cloak state changes.

**Size**: 1 byte.

