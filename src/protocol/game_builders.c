#include "openbc/game_builders.h"
#include "openbc/buffer.h"
#include "openbc/opcodes.h"
#include <string.h>

/* Object ID formula:
 * base=0x3FFFFFFF, each slot owns 2^18 (0x40000) consecutive IDs.
 * sub_index selects within the slot's range (0 = primary ship). */
i32 bc_make_object_id(int player_slot, int sub_index)
{
    return (i32)(0x3FFFFFFF + (u32)player_slot * 0x40000 + (u32)sub_index);
}

i32 bc_make_ship_id(int player_slot)
{
    return bc_make_object_id(player_slot, 0);
}

int bc_build_object_create_team(u8 *buf, int buf_size,
                                 u8 owner_slot, u8 team_id,
                                 const u8 *ship_data, int ship_data_len)
{
    /* Wire: [0x03][owner:u8][team:u8][ship_blob...] */
    int total = 3 + ship_data_len;
    if (total > buf_size) return -1;

    buf[0] = BC_OP_OBJ_CREATE_TEAM;
    buf[1] = owner_slot;
    buf[2] = team_id;
    memcpy(buf + 3, ship_data, (size_t)ship_data_len);

    return total;
}

int bc_build_torpedo_fire(u8 *buf, int buf_size,
                           i32 shooter_id, u8 subsys_index,
                           f32 vx, f32 vy, f32 vz,
                           bool has_target, i32 target_id,
                           f32 ix, f32 iy, f32 iz)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_TORPEDO_FIRE)) return -1;
    if (!bc_buf_write_i32(&b, shooter_id)) return -1;
    if (!bc_buf_write_u8(&b, subsys_index)) return -1;

    u8 flags = has_target ? 0x02 : 0x00;
    if (!bc_buf_write_u8(&b, flags)) return -1;
    if (!bc_buf_write_cv3(&b, vx, vy, vz)) return -1;

    if (has_target) {
        if (!bc_buf_write_i32(&b, target_id)) return -1;
        if (!bc_buf_write_cv4(&b, ix, iy, iz)) return -1;
    }

    return (int)b.pos;
}

int bc_build_beam_fire(u8 *buf, int buf_size,
                        i32 shooter_id, u8 flags,
                        f32 dx, f32 dy, f32 dz,
                        bool has_target, i32 target_id)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_BEAM_FIRE)) return -1;
    if (!bc_buf_write_i32(&b, shooter_id)) return -1;
    if (!bc_buf_write_u8(&b, flags)) return -1;
    if (!bc_buf_write_cv3(&b, dx, dy, dz)) return -1;

    u8 more_flags = has_target ? 0x01 : 0x00;
    if (!bc_buf_write_u8(&b, more_flags)) return -1;

    if (has_target) {
        if (!bc_buf_write_i32(&b, target_id)) return -1;
    }

    return (int)b.pos;
}

int bc_build_explosion(u8 *buf, int buf_size,
                        i32 object_id,
                        f32 ix, f32 iy, f32 iz,
                        f32 damage, f32 radius)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_EXPLOSION)) return -1;
    if (!bc_buf_write_i32(&b, object_id)) return -1;
    if (!bc_buf_write_cv4(&b, ix, iy, iz)) return -1;
    if (!bc_buf_write_cf16(&b, radius)) return -1;
    if (!bc_buf_write_cf16(&b, damage)) return -1;

    return (int)b.pos;
}

int bc_build_destroy_obj(u8 *buf, int buf_size, i32 object_id)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_DESTROY_OBJ)) return -1;
    if (!bc_buf_write_i32(&b, object_id)) return -1;

    return (int)b.pos;
}

int bc_build_chat(u8 *buf, int buf_size,
                   u8 sender_slot, bool team, const char *message)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    u8 opcode = team ? BC_MSG_TEAM_CHAT : BC_MSG_CHAT;
    if (!bc_buf_write_u8(&b, opcode)) return -1;
    if (!bc_buf_write_u8(&b, sender_slot)) return -1;
    /* 3 padding bytes */
    if (!bc_buf_write_u8(&b, 0x00)) return -1;
    if (!bc_buf_write_u8(&b, 0x00)) return -1;
    if (!bc_buf_write_u8(&b, 0x00)) return -1;
    if (!bc_buf_write_u16(&b, (u16)strlen(message))) return -1;
    if (!bc_buf_write_bytes(&b, (const u8 *)message, strlen(message)))
        return -1;

    return (int)b.pos;
}

int bc_build_score_change(u8 *buf, int buf_size,
                           i32 killer_id, i32 killer_kills, i32 killer_score,
                           i32 victim_id, i32 victim_deaths,
                           const bc_score_entry_t *extra, int extra_count)
{
    /* Wire format (from stock MissionShared.py SendScoreChangeMessage):
     *   [0x36][killer_id:i32]
     *   [if killer_id != 0: kills:i32, killer_score:i32]
     *   [victim_id:i32][deaths:i32]
     *   [update_count:u8][{player_id:i32, score:i32}...]
     * IDs are network player IDs (GetNetID()/wire_slot), not object IDs.
     */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_MSG_SCORE_CHANGE)) return -1;
    if (!bc_buf_write_i32(&b, killer_id)) return -1;

    if (killer_id != 0) {
        if (!bc_buf_write_i32(&b, killer_kills)) return -1;
        if (!bc_buf_write_i32(&b, killer_score)) return -1;
    }

    if (!bc_buf_write_i32(&b, victim_id)) return -1;
    if (!bc_buf_write_i32(&b, victim_deaths)) return -1;
    if (!bc_buf_write_u8(&b, (u8)extra_count)) return -1;

    for (int i = 0; i < extra_count; i++) {
        if (!bc_buf_write_i32(&b, extra[i].player_id)) return -1;
        if (!bc_buf_write_i32(&b, extra[i].score)) return -1;
    }

    return (int)b.pos;
}

int bc_build_end_game(u8 *buf, int buf_size, i32 reason)
{
    /* Wire format (from stock MissionShared.py ReceiveEndGameMessage):
     *   [0x38][reason:i32]
     * reason: 0=over, 1=time_up, 2=frag_limit, 3=score_limit */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_MSG_END_GAME)) return -1;
    if (!bc_buf_write_i32(&b, reason)) return -1;

    return (int)b.pos;
}

int bc_build_restart_game(u8 *buf, int buf_size)
{
    if (buf_size < 1) return -1;
    buf[0] = BC_MSG_RESTART;
    return 1;
}

int bc_build_python_subsystem_event(u8 *buf, int buf_size,
                                     i32 event_type,
                                     i32 source_obj_id,
                                     i32 dest_obj_id)
{
    /* Wire: [0x06][factory=0x101:i32][event_type:i32][source:i32][dest:i32]
     * 17 bytes total. */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_PYTHON_EVENT)) return -1;
    if (!bc_buf_write_i32(&b, BC_FACTORY_SUBSYSTEM_EVENT)) return -1;
    if (!bc_buf_write_i32(&b, event_type)) return -1;
    if (!bc_buf_write_i32(&b, source_obj_id)) return -1;
    if (!bc_buf_write_i32(&b, dest_obj_id)) return -1;

    return (int)b.pos;
}

int bc_build_python_obj_ptr_event(u8 *buf, int buf_size,
                                   i32 event_type,
                                   i32 source_obj_id,
                                   i32 dest_obj_id,
                                   i32 obj_ptr)
{
    /* Wire: [0x06][factory=0x010C:i32][event_type:i32][source:i32][dest:i32][obj_ptr:i32]
     * 21 bytes total. obj_ptr is a third network object reference (e.g. the weapon). */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_PYTHON_EVENT)) return -1;
    if (!bc_buf_write_i32(&b, BC_FACTORY_OBJ_PTR_EVENT)) return -1;
    if (!bc_buf_write_i32(&b, event_type)) return -1;
    if (!bc_buf_write_i32(&b, source_obj_id)) return -1;
    if (!bc_buf_write_i32(&b, dest_obj_id)) return -1;
    if (!bc_buf_write_i32(&b, obj_ptr)) return -1;

    return (int)b.pos;
}

int bc_build_python_exploding_event(u8 *buf, int buf_size,
                                     i32 source_obj_id,
                                     i32 firing_player_id,
                                     f32 lifetime)
{
    /* Wire: [0x06][factory=0x8129:i32][event_type=0x4E:i32]
     *   [source:i32][dest=-1:i32][killer_id:i32][lifetime:f32]
     * 25 bytes total. */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_PYTHON_EVENT)) return -1;
    if (!bc_buf_write_i32(&b, BC_FACTORY_OBJECT_EXPLODING)) return -1;
    if (!bc_buf_write_i32(&b, BC_EVENT_OBJECT_EXPLODING)) return -1;
    if (!bc_buf_write_i32(&b, source_obj_id)) return -1;
    if (!bc_buf_write_i32(&b, (i32)-1)) return -1;  /* dest = sentinel */
    if (!bc_buf_write_i32(&b, firing_player_id)) return -1;
    if (!bc_buf_write_f32(&b, lifetime)) return -1;

    return (int)b.pos;
}

int bc_build_state_update(u8 *buf, int buf_size,
                           i32 object_id, f32 game_time, u8 dirty_flags,
                           const u8 *field_data, int field_data_len)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_STATE_UPDATE)) return -1;
    if (!bc_buf_write_i32(&b, object_id)) return -1;
    if (!bc_buf_write_f32(&b, game_time)) return -1;
    if (!bc_buf_write_u8(&b, dirty_flags)) return -1;
    if (field_data_len > 0) {
        if (!bc_buf_write_bytes(&b, field_data, (size_t)field_data_len))
            return -1;
    }

    return (int)b.pos;
}

int bc_build_score(u8 *buf, int buf_size,
                    i32 player_id, i32 kills, i32 deaths, i32 score)
{
    /* Wire format (from stock Mission1.py SendScoreMessage):
     *   [0x37][player_id:i32][kills:i32][deaths:i32][score:i32]
     * 17 bytes total. One message per player.
     * player_id is the network player ID (GetNetID()/wire_slot). */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_MSG_SCORE)) return -1;
    if (!bc_buf_write_i32(&b, player_id)) return -1;
    if (!bc_buf_write_i32(&b, kills)) return -1;
    if (!bc_buf_write_i32(&b, deaths)) return -1;
    if (!bc_buf_write_i32(&b, score)) return -1;

    return (int)b.pos;
}

int bc_build_score_init(u8 *buf, int buf_size,
                         i32 player_id, i32 kills, i32 deaths, i32 score,
                         u8 team_id)
{
    /* Wire format:
     *   [0x3F][player_id:i32][kills:i32][deaths:i32][score:i32][team_id:u8] */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_MSG_SCORE_INIT)) return -1;
    if (!bc_buf_write_i32(&b, player_id)) return -1;
    if (!bc_buf_write_i32(&b, kills)) return -1;
    if (!bc_buf_write_i32(&b, deaths)) return -1;
    if (!bc_buf_write_i32(&b, score)) return -1;
    if (!bc_buf_write_u8(&b, team_id)) return -1;

    return (int)b.pos;
}

int bc_build_team_score(u8 *buf, int buf_size,
                         u8 team_id, i32 team_kills, i32 team_score)
{
    /* Wire format:
     *   [0x40][team_id:u8][team_kills:i32][team_score:i32] */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_MSG_TEAM_SCORE)) return -1;
    if (!bc_buf_write_u8(&b, team_id)) return -1;
    if (!bc_buf_write_i32(&b, team_kills)) return -1;
    if (!bc_buf_write_i32(&b, team_score)) return -1;

    return (int)b.pos;
}

int bc_build_event_forward(u8 *buf, int buf_size,
                            u8 opcode, const u8 *data, int data_len)
{
    int total = 1 + data_len;
    if (total > buf_size) return -1;

    buf[0] = opcode;
    if (data_len > 0) {
        memcpy(buf + 1, data, (size_t)data_len);
    }

    return total;
}
