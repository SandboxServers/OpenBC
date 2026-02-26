# Receiver Behavior


When a peer receives an ObjCreate or ObjCreateTeam message, it:

1. Reads the envelope (opcode, owner slot, and team if opcode 0x03)
2. Temporarily sets the active player context to the owner's slot (so object IDs are allocated from the correct range)
3. Reads the object header (class_id, object_id)
4. Checks for duplicate object_id — if the object already exists, processing stops
5. Creates a new object instance based on class_id
6. Deserializes the object body (species, position, orientation, etc.)
7. For ships (class_id 0x8008): the species_type is used to load the correct ship model, hardpoints, and subsystem configuration via the `SpeciesToShip` scripting API
8. For torpedoes (class_id 0x8009): the species_type loads the torpedo definition via `SpeciesToTorp`
9. If team (opcode 0x03): assigns the team_id to the object
10. If the host is processing: relays the message to all other connected peers (excluding the sender)
11. For ships only: attaches a network position/velocity tracker for state synchronization

