# Flag 0x08 — Up Orientation


```
+0      3     cv3     up_vector          CompressedVector3
```

The ship's up direction as a unit vector. Same encoding as forward orientation.

**Size**: 3 bytes.

Together, forward + up fully define the ship's 3D orientation (the right vector can be derived via cross product).

