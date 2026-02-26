# Flag 0x02 — Position Delta (Compressed)


Sent on most updates. Encodes the change in position since the last absolute position was sent.

```
+0      5     cv4     position_delta     CompressedVector4 (see Compressed Types)
```

The delta is: `(current_x - saved_x, current_y - saved_y, current_z - saved_z)`.

Wire format: 3 direction bytes + 2-byte CompressedFloat16 magnitude = **5 bytes total**.

Only sent when the delta direction or magnitude has changed from the previously sent values.

