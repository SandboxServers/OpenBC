# Overview


Opcodes 0x02 (ObjCreate) and 0x03 (ObjCreateTeam) are sent by the host to create game objects — ships, torpedoes, stations, and asteroids — on all connected clients. The only difference is that 0x03 includes a team assignment byte.

These messages are relayed: when the host creates an object, it sends the message to every other connected peer.

