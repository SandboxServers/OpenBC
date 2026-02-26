# Torpedo Object Body (class_id = 0x8009)


Torpedoes use the same `species_type` byte at the start of their body, but the value indexes into the `SpeciesToTorp` table instead. Torpedo serialization does not include spatial tracking data — torpedo position and movement are handled by dedicated fire messages (opcodes 0x19 TorpedoFire, 0x1A BeamFire).

