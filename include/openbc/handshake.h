#ifndef OPENBC_HANDSHAKE_H
#define OPENBC_HANDSHAKE_H

#include "openbc/types.h"
#include "openbc/buffer.h"

/*
 * Connection handshake -- checksum exchange and Settings/GameInit delivery.
 *
 * After a client connects, the server runs 4 checksum rounds:
 *   Round 0: scripts/         App.pyc          non-recursive
 *   Round 1: scripts/         Autoexec.pyc     non-recursive
 *   Round 2: scripts/ships/   *.pyc            recursive
 *   Round 3: scripts/mainmenu/ *.pyc           non-recursive
 *
 * After all 4 pass, the server sends Settings (0x00) then GameInit (0x01).
 *
 * Reference: docs/phase1-verified-protocol.md Section 4 + 7
 */

#define BC_CHECKSUM_ROUNDS  4

/* Build a checksum request payload (opcode 0x20) for the given round.
 * Writes into buf. Returns bytes written, or -1 on error. */
int bc_checksum_request_build(u8 *buf, int buf_size, int round);

/* Build the Settings payload (opcode 0x00).
 * Parameters:
 *   game_time       - current game clock value
 *   collision_dmg   - collision damage enabled
 *   friendly_fire   - friendly fire enabled
 *   player_slot     - assigned player slot (0-5)
 *   map_name        - mission TGL path (e.g. "DeepSpace9")
 * Returns bytes written, or -1 on error. */
int bc_settings_build(u8 *buf, int buf_size,
                      f32 game_time,
                      bool collision_dmg,
                      bool friendly_fire,
                      u8 player_slot,
                      const char *map_name);

/* Build the GameInit payload (opcode 0x01).
 * Returns bytes written (always 1), or -1 on error. */
int bc_gameinit_build(u8 *buf, int buf_size);

#endif /* OPENBC_HANDSHAKE_H */
