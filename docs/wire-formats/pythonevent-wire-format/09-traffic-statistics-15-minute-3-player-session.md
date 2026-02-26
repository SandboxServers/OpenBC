# Traffic Statistics (15-minute 3-player session)


| Direction | Count | Notes |
|-----------|-------|-------|
| PythonEvent S→C | ~251 | Repair list + explosions + script events |
| PythonEvent C→S | 0 | Clients never send 0x06 in the collision path |
| CollisionEffect C→S | ~84 | Client collision reports (opcode 0x15) |

All collision-path PythonEvents are **host-generated, server-to-client only**.

---

