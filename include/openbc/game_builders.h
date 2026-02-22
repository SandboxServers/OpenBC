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

/* Score: [0x37][player_id:i32][kills:i32][deaths:i32][score:i32]
 * 17 bytes total. Sent once per player to a newly joining client.
 * player_id is the network player ID (GetNetID()/wire_slot), not object_id.
 * Stock BC sends one message per player (not batched). */
int bc_build_score(u8 *buf, int buf_size,
                    i32 player_id, i32 kills, i32 deaths, i32 score);

/* ScoreChange: [0x36][killer_id:i32][if killer!=0: kills:i32, killer_score:i32]
 *              [victim_id:i32][deaths:i32][update_count:u8]
 *              [{player_id:i32, score:i32}...]
 * Variable length. Sent when a kill occurs. killer_id=0 for environmental kills.
 * killer_id/victim_id/player_id use network player IDs (GetNetID()/wire_slot).
 * extra_scores is an array of {player_id, score} pairs for damage-share updates. */
typedef struct {
    i32 player_id;
    i32 score;
} bc_score_entry_t;

int bc_build_score_change(u8 *buf, int buf_size,
                           i32 killer_id, i32 killer_kills, i32 killer_score,
                           i32 victim_id, i32 victim_deaths,
                           const bc_score_entry_t *extra, int extra_count);

/* ScoreInit (team mode join sync):
 * [0x3F][player_id:i32][kills:i32][deaths:i32][score:i32][team_id:u8]
 * 18 bytes total.
 * team_id: 0/1, or 255 when no team assignment exists. */
int bc_build_score_init(u8 *buf, int buf_size,
                         i32 player_id, i32 kills, i32 deaths, i32 score,
                         u8 team_id);

/* TeamScore (team mode aggregate update):
 * [0x40][team_id:u8][team_kills:i32][team_score:i32]
 * 10 bytes total. */
int bc_build_team_score(u8 *buf, int buf_size,
                         u8 team_id, i32 team_kills, i32 team_score);

/* EndGame: [0x38][reason:i32]
 * 5 bytes total. Sent when frag/time limit reached.
 * Reason codes: 0=over, 1=time_up, 2=frag_limit, 3=score_limit,
 *               4=starbase_dead, 5=borg_dead, 6=enterprise_dead */
#define BC_END_REASON_OVER          0
#define BC_END_REASON_TIME_UP       1
#define BC_END_REASON_FRAG_LIMIT    2
#define BC_END_REASON_SCORE_LIMIT   3
int bc_build_end_game(u8 *buf, int buf_size, i32 reason);

/* RestartGame: [0x39]
 * 1 byte total. */
int bc_build_restart_game(u8 *buf, int buf_size);

/* --- PythonEvent (0x06) builders --- */

/* PythonEvent factory IDs */
#define BC_FACTORY_SUBSYSTEM_EVENT  0x00000101
#define BC_FACTORY_OBJ_PTR_EVENT    0x0000010C
#define BC_FACTORY_OBJECT_EXPLODING 0x00008129

/* PythonEvent type constants */
#define BC_EVENT_ADD_TO_REPAIR      0x008000DF
#define BC_EVENT_REPAIR_COMPLETED   0x00800074
#define BC_EVENT_REPAIR_CANNOT      0x00800075
#define BC_EVENT_OBJECT_EXPLODING   0x0080004E

/* ObjPtrEvent network event types (factory BC_FACTORY_OBJ_PTR_EVENT).
 * Account for ~45% of combat PythonEvent traffic (see pythonevent-wire-format.md).
 * obj_ptr meaning varies by event type (target ID, subsystem ID, etc.).
 * Phaser/tractor fire each generate two events: start-specific + WEAPON_FIRED. */
#define BC_EVENT_WEAPON_FIRED       0x0080007C  /* obj_ptr = target ID or 0 */
#define BC_EVENT_PHASER_STARTED     0x00800081  /* obj_ptr = target ID */
#define BC_EVENT_PHASER_STOPPED     0x00800083  /* obj_ptr = target ID */
#define BC_EVENT_TRACTOR_STARTED    0x0080007D  /* obj_ptr = target ID */
#define BC_EVENT_TRACTOR_STOPPED    0x0080007F  /* obj_ptr = target ID */
#define BC_EVENT_REPAIR_PRIORITY    0x00800076  /* obj_ptr = subsystem ID */
#define BC_EVENT_STOP_AT_TARGET     0x008000DC  /* obj_ptr = target ID or 0; host-only (opcode 0x09) */

/* SubsystemEvent: [0x06][factory=0x101:i32][event_type:i32][source:i32][dest:i32]
 * 17 bytes total. Used for ADD_TO_REPAIR_LIST, REPAIR_COMPLETED, etc. */
int bc_build_python_subsystem_event(u8 *buf, int buf_size,
                                     i32 event_type,
                                     i32 source_obj_id,
                                     i32 dest_obj_id);

/* ObjPtrEvent: [0x06][factory=0x010C:i32][event_type:i32][source:i32][dest:i32][obj_ptr:i32]
 * 21 bytes total. Carries weapon-fire and tractor/repair-priority events.
 * obj_ptr is the network object ID of a third referenced object (e.g. the weapon). */
int bc_build_python_obj_ptr_event(u8 *buf, int buf_size,
                                   i32 event_type,
                                   i32 source_obj_id,
                                   i32 dest_obj_id,
                                   i32 obj_ptr);

/* ObjectExplodingEvent: [0x06][factory=0x8129:i32][event_type=0x4E:i32]
 *   [source:i32][dest=-1:i32][killer_id:i32][lifetime:f32]
 * 25 bytes total. */
int bc_build_python_exploding_event(u8 *buf, int buf_size,
                                     i32 source_obj_id,
                                     i32 firing_player_id,
                                     f32 lifetime);

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
