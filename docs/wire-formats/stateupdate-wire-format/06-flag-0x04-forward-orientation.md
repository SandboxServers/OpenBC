# Flag 0x04 — Forward Orientation


```
+0      3     cv3     forward_vector     CompressedVector3 (see Compressed Types)
```

The ship's forward-facing direction as a unit vector. Each byte is a signed direction component: `truncate_to_int(component * 127.0)`, giving a range of -1.0 to +1.0 per axis.

**Size**: 3 bytes.

