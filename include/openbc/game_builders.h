#ifndef OPENBC_GAME_BUILDERS_H
#define OPENBC_GAME_BUILDERS_H

#include "openbc/types.h"

/*
 * Game message builders -- construct wire-format payloads for game opcodes.
 *
 * Two tiers:
 *   Tier 1 (proper): Full builders for parsed opcodes (torpedo, beam, etc.)
 *   Tier 2 (generic): Opcode + opaque data for server-relayed opcodes
 *
 * All functions write into a caller-provided buffer and return the number
 * of bytes written, or -1 on error.
 */

/* --- Object ID arithmetic (RE-verified) ---
 * Base = 0x3FFFFFFF, each slot owns 2^18 (0x40000) IDs.
 *   bc_make_object_id(0, 0) = 0x3FFFFFFF (slot 0, ship)
 *   bc_make_object_id(1, 0) = 0x4003FFFF (slot 1, ship)
 */
i32 bc_make_object_id(int player_slot, int sub_index);
i32 bc_make_ship_id(int player_slot);

/* --- Tier 1: Proper builders --- */

/* ObjectCreateTeam: [0x03][owner:u8][team:u8][ship_blob...]
 * ship_data is the serialized ship blob (108-121 bytes from traces). */
int bc_build_object_create_team(u8 *buf, int buf_size,
                                 u8 owner_slot, u8 team_id,
                                 const u8 *ship_data, int ship_data_len);

/* TorpedoFire: [0x19][shooter:i32][subsys:u8][flags:u8][vel:cv3]
 *   + optional [target:i32][impact:cv4]
 * flags bit 1 = has_target. */
int bc_build_torpedo_fire(u8 *buf, int buf_size,
                           i32 shooter_id, u8 subsys_index,
                           f32 vx, f32 vy, f32 vz,
                           bool has_target, i32 target_id,
                           f32 ix, f32 iy, f32 iz);

/* BeamFire: [0x1A][shooter:i32][flags:u8][dir:cv3][more_flags:u8]
 *   + optional [target:i32]
 * more_flags bit 0 = has_target. */
int bc_build_beam_fire(u8 *buf, int buf_size,
                        i32 shooter_id, u8 flags,
                        f32 dx, f32 dy, f32 dz,
                        bool has_target, i32 target_id);

/* Explosion: [0x29][obj:i32][impact:cv4][damage:cf16][radius:cf16]
 * 14 bytes total. */
int bc_build_explosion(u8 *buf, int buf_size,
                        i32 object_id,
                        f32 ix, f32 iy, f32 iz,
                        f32 damage, f32 radius);

/* DestroyObject: [0x14][obj:i32]
 * 5 bytes total. */
int bc_build_destroy_obj(u8 *buf, int buf_size, i32 object_id);

/* Chat: [0x2C|0x2D][slot:u8][pad:3][len:u16][ascii...]
 * team=false -> 0x2C, team=true -> 0x2D. */
int bc_build_chat(u8 *buf, int buf_size,
                   u8 sender_slot, bool team, const char *message);

/* --- Tier 2: Generic builders --- */

/* StateUpdate: [0x1C][obj:i32][time:f32][dirty:u8][field_data...] */
int bc_build_state_update(u8 *buf, int buf_size,
                           i32 object_id, f32 game_time, u8 dirty_flags,
                           const u8 *field_data, int field_data_len);

/* Generic event: [opcode][extra_data...]
 * Covers 0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0E,0x0F,0x10,0x11,0x12,0x15,0x1B */
int bc_build_event_forward(u8 *buf, int buf_size,
                            u8 opcode, const u8 *data, int data_len);

#endif /* OPENBC_GAME_BUILDERS_H */
