# Open Questions


- **Reserved bytes**: The 3 bytes after speed are always observed as zero. They may encode initial state flags (cloak, warp, shield status) but this has not been confirmed.
- **Subsystem state format**: The trailing blob varies by ship type. It likely encodes per-subsystem health as floating-point values, but the exact layout per ship class has not been fully documented.
- **Orientation encoding**: Four consecutive floats after position are consistent with a quaternion (W, X, Y, Z), but Euler angles (with one unused float) have not been ruled out.
