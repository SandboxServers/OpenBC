# Host Relay Behavior


When the host processes an ObjCreate/ObjCreateTeam from a client, it relays the original message (unmodified) to every other connected peer. This ensures all clients receive object creations regardless of which player originated them.

The host iterates over all 16 possible player slots and sends to each connected peer that is neither the original sender nor the host itself.

