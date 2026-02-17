#ifndef OPENBC_OPCODES_H
#define OPENBC_OPCODES_H

/*
 * BC Wire Protocol Opcodes
 *
 * Three layers of opcodes:
 *   1. Transport layer (UDP packet framing, reliability)
 *   2. Game layer (C++ opcode handlers, jump table at 0x0069F534)
 *   3. Python message layer (sent via SendTGMessage, received by Python scripts)
 *
 * Reference: docs/phase1-verified-protocol.md
 */

/* === Transport Layer Opcodes ===
 * These appear in the transport message type field.
 * Each UDP packet is: [direction:1][count:1][transport_msg...]
 */
#define BC_TRANSPORT_KEEPALIVE     0x00
#define BC_TRANSPORT_ACK           0x01
#define BC_TRANSPORT_CONNECT       0x03
#define BC_TRANSPORT_CONNECT_DATA  0x04
#define BC_TRANSPORT_CONNECT_ACK   0x05
#define BC_TRANSPORT_DISCONNECT    0x06
#define BC_TRANSPORT_RELIABLE      0x32  /* Reliable data envelope */

/* === Game Layer Opcodes ===
 * These are the payload opcode byte inside transport messages.
 * Dispatched by the jump table at 0x0069F534 (28 active entries).
 */
#define BC_OP_SETTINGS             0x00  /* Server settings (game time, collision, etc.) */
#define BC_OP_GAME_INIT            0x01  /* Game initialization data */
#define BC_OP_OBJ_CREATE           0x02  /* Object creation (single player) */
#define BC_OP_OBJ_CREATE_TEAM      0x03  /* Object creation with team assignment */
#define BC_OP_BOOT_PLAYER          0x04  /* Kick a player */
/* 0x05 does not exist (skipped in jump table) */
#define BC_OP_PYTHON_EVENT         0x06  /* Python event dispatch */
#define BC_OP_START_FIRING         0x07  /* Weapon start firing */
#define BC_OP_STOP_FIRING          0x08  /* Weapon stop firing */
#define BC_OP_STOP_FIRING_AT       0x09  /* Stop firing at specific target */
#define BC_OP_SUBSYS_STATUS        0x0A  /* Subsystem status update */
#define BC_OP_ADD_REPAIR_LIST      0x0B  /* Add to crew repair list (event 0x008000DF) */
#define BC_OP_CLIENT_EVENT         0x0C  /* Generic event forward (preserve=0) */
#define BC_OP_PYTHON_EVENT2        0x0D  /* Python event dispatch (variant) */
#define BC_OP_START_CLOAK          0x0E  /* Start cloaking */
#define BC_OP_STOP_CLOAK           0x0F  /* Stop cloaking */
#define BC_OP_START_WARP           0x10  /* Enter warp speed */
#define BC_OP_REPAIR_PRIORITY      0x11  /* Repair list priority ordering (event 0x008000E1) */
#define BC_OP_SET_PHASER_LEVEL     0x12  /* Phaser power/intensity setting (event 0x008000E0) */
#define BC_OP_HOST_MSG             0x13  /* Host-specific message */
#define BC_OP_DESTROY_OBJ          0x14  /* Destroy object */
#define BC_OP_COLLISION_EFFECT     0x15  /* Collision damage relay (C->S, host processes) */
#define BC_OP_UI_SETTINGS          0x16  /* UI settings sync */
#define BC_OP_DELETE_PLAYER_UI     0x17  /* Delete player UI elements */
#define BC_OP_DELETE_PLAYER_ANIM   0x18  /* Delete player animations */
#define BC_OP_TORPEDO_FIRE         0x19  /* Torpedo fired */
#define BC_OP_BEAM_FIRE            0x1A  /* Beam weapon fired */
#define BC_OP_TORP_TYPE_CHANGE     0x1B  /* Torpedo type changed */
#define BC_OP_STATE_UPDATE         0x1C  /* Ship state update (position, orientation, etc.) */
#define BC_OP_OBJ_NOT_FOUND        0x1D  /* Object not found response */
#define BC_OP_REQUEST_OBJ          0x1E  /* Request object data */
#define BC_OP_ENTER_SET            0x1F  /* Enter game set (scene) */
/* 0x20-0x28 are NetFile opcodes, not game opcodes */
#define BC_OP_EXPLOSION            0x29  /* Explosion effect */
#define BC_OP_NEW_PLAYER_IN_GAME   0x2A  /* New player has joined the game */

/* === NetFile / Checksum Opcodes (0x20-0x28) ===
 * These are dispatched separately from the main game opcode handler.
 */
#define BC_OP_CHECKSUM_REQ         0x20  /* Server requests file checksums */
#define BC_OP_CHECKSUM_RESP        0x21  /* Client responds with checksums */
#define BC_OP_VERSION_MISMATCH     0x22  /* Version string mismatch */
#define BC_OP_SYS_CHECKSUM_FAIL    0x23  /* System checksum validation failed */
#define BC_OP_FILE_TRANSFER        0x25  /* File transfer data */
#define BC_OP_FILE_TRANSFER_ACK    0x27  /* File transfer acknowledgment */
#define BC_OP_UNKNOWN_28           0x28  /* Unknown NetFile opcode */

/* === Python Message Opcodes ===
 * Offset from MAX_MESSAGE_TYPES (0x2B).
 * Sent via SendTGMessage(), received by Python ReceiveMessage handlers.
 */
#define BC_MAX_MESSAGE_TYPES       0x2B

#define BC_MSG_CHAT                0x2C  /* Chat message (MAX+1) */
#define BC_MSG_TEAM_CHAT           0x2D  /* Team chat message (MAX+2) */
#define BC_MSG_MISSION_INIT        0x35  /* Mission initialization (MAX+10) */
#define BC_MSG_SCORE_CHANGE        0x36  /* Score delta on kill (MAX+11) */
#define BC_MSG_SCORE               0x37  /* Full score sync at join (MAX+12) */
#define BC_MSG_END_GAME            0x38  /* Game over broadcast (MAX+13) */
#define BC_MSG_RESTART             0x39  /* Restart game broadcast (MAX+14) */
#define BC_MSG_SCORE_INIT          0x3F  /* Team score init (MAX+20) */
#define BC_MSG_TEAM_SCORE          0x40  /* Team score update (MAX+21) */
#define BC_MSG_TEAM_MESSAGE        0x41  /* Team message (MAX+22) */

/* === StateUpdate Dirty Flags (opcode 0x1C) ===
 * Bitmask in the flags byte of StateUpdate packets.
 */
#define BC_DIRTY_POSITION_ABS      0x01  /* 3x f32 + optional hash */
#define BC_DIRTY_POSITION_DELTA    0x02  /* CompressedVector4 (5 bytes) */
#define BC_DIRTY_ORIENT_FWD        0x04  /* CompressedVector3 (3 bytes) */
#define BC_DIRTY_ORIENT_UP         0x08  /* CompressedVector3 (3 bytes) */
#define BC_DIRTY_SPEED             0x10  /* CompressedFloat16 (2 bytes) */
#define BC_DIRTY_SUBSYSTEM_STATES  0x20  /* Round-robin subsystem health */
#define BC_DIRTY_CLOAK_STATE       0x40  /* u8 cloak on/off */
#define BC_DIRTY_WEAPON_STATES     0x80  /* Round-robin weapon health */

/* === Subsystem Indices ===
 * Fixed indices for ship subsystems (used in subsystem status messages).
 */
#define BC_SUBSYS_REACTOR          0x00
#define BC_SUBSYS_REPAIR           0x01
#define BC_SUBSYS_CLOAK            0x02
#define BC_SUBSYS_POWERED          0x03
#define BC_SUBSYS_LIFE_SUPPORT     0x04
#define BC_SUBSYS_SHIELDS          0x05
#define BC_SUBSYS_TORPEDO_1        0x06
#define BC_SUBSYS_TORPEDO_2        0x07
#define BC_SUBSYS_TORPEDO_3        0x08
#define BC_SUBSYS_TORPEDO_4        0x09
#define BC_SUBSYS_TORPEDO_5        0x0A
#define BC_SUBSYS_TORPEDO_6        0x0B
#define BC_SUBSYS_PHASER_1         0x0C
#define BC_SUBSYS_PHASER_2         0x0D
#define BC_SUBSYS_PHASER_3         0x0E
#define BC_SUBSYS_PHASER_4         0x0F
#define BC_SUBSYS_PHASER_5         0x10
#define BC_SUBSYS_PHASER_6         0x11
#define BC_SUBSYS_PHASER_7         0x12
#define BC_SUBSYS_PHASER_8         0x13
#define BC_SUBSYS_IMPULSE_1        0x14
#define BC_SUBSYS_IMPULSE_2        0x15
#define BC_SUBSYS_IMPULSE_3        0x16
#define BC_SUBSYS_IMPULSE_4        0x17
#define BC_SUBSYS_WARP_DRIVE       0x18
#define BC_SUBSYS_PHASER_CTRL      0x19
#define BC_SUBSYS_PULSE_WEAPON     0x1A
#define BC_SUBSYS_SENSORS          0x1B
#define BC_SUBSYS_REACTOR_2        0x1C
#define BC_SUBSYS_TRACTOR_1        0x1D
#define BC_SUBSYS_TRACTOR_2        0x1E
#define BC_SUBSYS_TRACTOR_3        0x1F
#define BC_SUBSYS_TRACTOR_4        0x20
#define BC_SUBSYS_MAX              0x21

/* === Reliable Data Flags (transport 0x32) === */
#define BC_RELIABLE_FLAG_GUARANTEED  0x01  /* Must be acknowledged */
#define BC_RELIABLE_FLAG_FRAGMENT    0x20  /* Part of a multi-fragment message */

/* === Connection Constants === */
#define BC_DEFAULT_PORT            0x5655  /* 22101 decimal */
#define BC_GAMESPY_PORT            0x5656  /* 22102 decimal */
/* Peer array size: slot 0 = dedicated server, slots 1-6 = up to 6 human players */
#define BC_MAX_PLAYERS             7

/* Opcode name lookup (returns static string, NULL for unknown) */
const char *bc_opcode_name(int opcode);

#endif /* OPENBC_OPCODES_H */
