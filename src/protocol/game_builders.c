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
    if (!bc_buf_write_cf16(&b, damage)) return -1;
    if (!bc_buf_write_cf16(&b, radius)) return -1;

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
                           u8 killer_slot, u8 victim_slot, i32 new_score)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_MSG_SCORE_CHANGE)) return -1;
    if (!bc_buf_write_u8(&b, killer_slot)) return -1;
    if (!bc_buf_write_u8(&b, victim_slot)) return -1;
    if (!bc_buf_write_i32(&b, new_score)) return -1;

    return (int)b.pos;
}

int bc_build_end_game(u8 *buf, int buf_size, u8 winner_slot)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_MSG_END_GAME)) return -1;
    if (!bc_buf_write_u8(&b, winner_slot)) return -1;

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
                    const i32 *scores, int player_count)
{
    /* Wire format (from Mission1.py ReceiveScoreMessage):
     *   [0x37][count:u8][{slot:u8, score:i32}...] */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_MSG_SCORE)) return -1;
    if (!bc_buf_write_u8(&b, (u8)player_count)) return -1;

    for (int i = 0; i < player_count; i++) {
        if (!bc_buf_write_u8(&b, (u8)i)) return -1;        /* game_slot */
        if (!bc_buf_write_i32(&b, scores[i])) return -1;   /* score */
    }

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
