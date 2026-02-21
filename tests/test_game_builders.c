#include "test_util.h"
#include "openbc/game_builders.h"
#include "openbc/game_events.h"
#include "openbc/buffer.h"
#include "openbc/opcodes.h"
#include <string.h>
#include <math.h>

static i32 read_i32_le(const u8 *p)
{
    return (i32)((u32)p[0] |
                 ((u32)p[1] << 8) |
                 ((u32)p[2] << 16) |
                 ((u32)p[3] << 24));
}

/* === Object ID arithmetic === */

TEST(object_id_slot0)
{
    i32 id = bc_make_object_id(0, 0);
    ASSERT_EQ((u32)id, 0x3FFFFFFF);
    ASSERT_EQ_INT(bc_object_id_to_slot(id), 0);
}

TEST(object_id_slot2)
{
    i32 id = bc_make_object_id(2, 0);
    ASSERT_EQ((u32)id, 0x4007FFFF);
    ASSERT_EQ_INT(bc_object_id_to_slot(id), 2);
}

TEST(ship_id_slot1)
{
    i32 id = bc_make_ship_id(1);
    ASSERT_EQ((u32)id, 0x4003FFFF);
    ASSERT_EQ_INT(bc_object_id_to_slot(id), 1);
}

/* === ObjectCreateTeam === */

TEST(object_create_team_header)
{
    u8 ship_data[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    u8 buf[32];
    int len = bc_build_object_create_team(buf, sizeof(buf), 2, 4,
                                           ship_data, sizeof(ship_data));

    ASSERT_EQ_INT(len, 7); /* 3 header + 4 data */
    ASSERT_EQ(buf[0], BC_OP_OBJ_CREATE_TEAM); /* 0x03 */
    ASSERT_EQ(buf[1], 2);  /* owner */
    ASSERT_EQ(buf[2], 4);  /* team */
    ASSERT_EQ(buf[3], 0xAA);
    ASSERT_EQ(buf[6], 0xDD);

    /* Verify header parses correctly */
    bc_object_create_header_t hdr;
    ASSERT(bc_parse_object_create_header(buf, len, &hdr));
    ASSERT_EQ(hdr.type_tag, 3);
    ASSERT_EQ(hdr.owner_slot, 2);
    ASSERT_EQ(hdr.team_id, 4);
    ASSERT(hdr.has_team);
}

/* === TorpedoFire === */

TEST(torpedo_fire_no_target)
{
    u8 buf[64];
    int len = bc_build_torpedo_fire(buf, sizeof(buf),
                                     bc_make_ship_id(0), 6,
                                     1.0f, 0.0f, 0.0f,
                                     false, 0, 0, 0, 0);

    ASSERT(len == 10); /* 1+4+1+1+3 */
    ASSERT_EQ(buf[0], BC_OP_TORPEDO_FIRE);
}

TEST(torpedo_fire_with_target)
{
    u8 buf[64];
    int len = bc_build_torpedo_fire(buf, sizeof(buf),
                                     bc_make_ship_id(0), 6,
                                     0.0f, 0.0f, 1.0f,
                                     true, bc_make_ship_id(1),
                                     50.0f, 30.0f, 0.0f);

    ASSERT(len == 19); /* 10 + 4 + 5 */
    ASSERT_EQ(buf[0], BC_OP_TORPEDO_FIRE);
    ASSERT_EQ(buf[6], 0x02); /* flags: has_target */
}

TEST(torpedo_roundtrip)
{
    u8 buf[64];
    int len = bc_build_torpedo_fire(buf, sizeof(buf),
                                     bc_make_ship_id(0), 6,
                                     0.0f, 0.0f, 1.0f,
                                     true, bc_make_ship_id(2),
                                     100.0f, 50.0f, 25.0f);
    ASSERT(len > 0);

    bc_torpedo_event_t ev;
    ASSERT(bc_parse_torpedo_fire(buf, len, &ev));
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.shooter_id), 0);
    ASSERT_EQ(ev.subsys_index, 6);
    ASSERT(ev.has_target);
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.target_id), 2);
    ASSERT(fabsf(ev.vel_z - 1.0f) < 0.02f);
    ASSERT(fabsf(ev.impact_x - 100.0f) < 3.0f);
}

/* === BeamFire === */

TEST(beam_fire_no_target)
{
    u8 buf[64];
    int len = bc_build_beam_fire(buf, sizeof(buf),
                                  bc_make_ship_id(0), 0x01,
                                  0.0f, 1.0f, 0.0f,
                                  false, 0);

    ASSERT(len == 10); /* 1+4+1+3+1 */
    ASSERT_EQ(buf[0], BC_OP_BEAM_FIRE);
}

TEST(beam_fire_with_target)
{
    u8 buf[64];
    int len = bc_build_beam_fire(buf, sizeof(buf),
                                  bc_make_ship_id(0), 0x01,
                                  1.0f, 0.0f, 0.0f,
                                  true, bc_make_ship_id(1));

    ASSERT(len == 14); /* 10 + 4 */
    ASSERT_EQ(buf[0], BC_OP_BEAM_FIRE);
}

TEST(beam_roundtrip)
{
    u8 buf[64];
    int len = bc_build_beam_fire(buf, sizeof(buf),
                                  bc_make_ship_id(1), 0x03,
                                  0.0f, 0.0f, 1.0f,
                                  true, bc_make_ship_id(0));
    ASSERT(len > 0);

    bc_beam_event_t ev;
    ASSERT(bc_parse_beam_fire(buf, len, &ev));
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.shooter_id), 1);
    ASSERT_EQ(ev.flags, 0x03);
    ASSERT(ev.has_target);
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.target_id), 0);
}

/* === Explosion === */

TEST(explosion_14_bytes)
{
    u8 buf[64];
    int len = bc_build_explosion(buf, sizeof(buf),
                                  bc_make_ship_id(1),
                                  10.0f, 20.0f, 30.0f,
                                  150.0f, 45.0f);

    ASSERT(len == 14); /* 1+4+5+2+2 */
    ASSERT_EQ(buf[0], BC_OP_EXPLOSION);
}

TEST(explosion_roundtrip)
{
    u8 buf[64];
    int len = bc_build_explosion(buf, sizeof(buf),
                                  bc_make_ship_id(2),
                                  50.0f, 30.0f, 10.0f,
                                  200.0f, 100.0f);
    ASSERT(len > 0);

    bc_explosion_event_t ev;
    ASSERT(bc_parse_explosion(buf, len, &ev));
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.object_id), 2);
    ASSERT(fabsf(ev.damage - 200.0f) < 3.0f);
    ASSERT(fabsf(ev.radius - 100.0f) < 2.0f);
}

/* === DestroyObject === */

TEST(destroy_obj_5_bytes)
{
    u8 buf[16];
    int len = bc_build_destroy_obj(buf, sizeof(buf), bc_make_ship_id(1));

    ASSERT(len == 5);
    ASSERT_EQ(buf[0], BC_OP_DESTROY_OBJ);

    bc_destroy_event_t ev;
    ASSERT(bc_parse_destroy_obj(buf, len, &ev));
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.object_id), 1);
}

/* === Chat === */

TEST(chat_all)
{
    u8 buf[128];
    int len = bc_build_chat(buf, sizeof(buf), 0, false, "gg");

    ASSERT(len == 9); /* 1+1+3+2+2 */
    ASSERT_EQ(buf[0], BC_MSG_CHAT);

    bc_chat_event_t ev;
    ASSERT(bc_parse_chat_message(buf, len, &ev));
    ASSERT_EQ(ev.sender_slot, 0);
    ASSERT(strcmp(ev.message, "gg") == 0);
}

TEST(chat_team)
{
    u8 buf[128];
    int len = bc_build_chat(buf, sizeof(buf), 3, true, "help me");

    ASSERT(len > 0);
    ASSERT_EQ(buf[0], BC_MSG_TEAM_CHAT);

    bc_chat_event_t ev;
    ASSERT(bc_parse_chat_message(buf, len, &ev));
    ASSERT_EQ(ev.sender_slot, 3);
    ASSERT(strcmp(ev.message, "help me") == 0);
}

/* === Score messages === */

TEST(score_message_single_player)
{
    u8 buf[32];
    int len = bc_build_score(buf, sizeof(buf), 2, 3, 1, 7);

    ASSERT_EQ_INT(len, 17);
    ASSERT_EQ(buf[0], BC_MSG_SCORE);
    ASSERT_EQ_INT(read_i32_le(buf + 1), 2);
    ASSERT_EQ_INT(read_i32_le(buf + 5), 3);
    ASSERT_EQ_INT(read_i32_le(buf + 9), 1);
    ASSERT_EQ_INT(read_i32_le(buf + 13), 7);
}

TEST(score_change_with_killer)
{
    u8 buf[64];
    int len = bc_build_score_change(buf, sizeof(buf),
                                     3, 4, 11,
                                     2, 5,
                                     NULL, 0);

    ASSERT_EQ_INT(len, 22);
    ASSERT_EQ(buf[0], BC_MSG_SCORE_CHANGE);
    ASSERT_EQ_INT(read_i32_le(buf + 1), 3);   /* killer_id */
    ASSERT_EQ_INT(read_i32_le(buf + 5), 4);   /* killer_kills */
    ASSERT_EQ_INT(read_i32_le(buf + 9), 11);  /* killer_score */
    ASSERT_EQ_INT(read_i32_le(buf + 13), 2);  /* victim_id */
    ASSERT_EQ_INT(read_i32_le(buf + 17), 5);  /* victim_deaths */
    ASSERT_EQ(buf[21], 0);                    /* update_count */
}

TEST(score_change_environmental_kill)
{
    u8 buf[64];
    int len = bc_build_score_change(buf, sizeof(buf),
                                     0, 0, 0,
                                     2, 6,
                                     NULL, 0);

    ASSERT_EQ_INT(len, 14);
    ASSERT_EQ(buf[0], BC_MSG_SCORE_CHANGE);
    ASSERT_EQ_INT(read_i32_le(buf + 1), 0);   /* killer_id=0 omits killer stats */
    ASSERT_EQ_INT(read_i32_le(buf + 5), 2);   /* victim_id */
    ASSERT_EQ_INT(read_i32_le(buf + 9), 6);   /* victim_deaths */
    ASSERT_EQ(buf[13], 0);                    /* update_count */
}

TEST(score_change_extra_entries)
{
    bc_score_entry_t extra[2];
    extra[0].player_id = 2;
    extra[0].score = 12;
    extra[1].player_id = 4;
    extra[1].score = 9;

    u8 buf[96];
    int len = bc_build_score_change(buf, sizeof(buf),
                                     3, 8, 20,
                                     2, 7,
                                     extra, 2);

    ASSERT_EQ_INT(len, 38);
    ASSERT_EQ(buf[0], BC_MSG_SCORE_CHANGE);
    ASSERT_EQ(buf[21], 2);                    /* update_count */
    ASSERT_EQ_INT(read_i32_le(buf + 22), 2);  /* extra[0].player_id */
    ASSERT_EQ_INT(read_i32_le(buf + 26), 12); /* extra[0].score */
    ASSERT_EQ_INT(read_i32_le(buf + 30), 4);  /* extra[1].player_id */
    ASSERT_EQ_INT(read_i32_le(buf + 34), 9);  /* extra[1].score */
}

TEST(score_init_team_sync)
{
    u8 buf[32];
    int len = bc_build_score_init(buf, sizeof(buf), 5, 2, 1, 17, 1);

    ASSERT_EQ_INT(len, 18);
    ASSERT_EQ(buf[0], BC_MSG_SCORE_INIT);
    ASSERT_EQ_INT(read_i32_le(buf + 1), 5);    /* player_id */
    ASSERT_EQ_INT(read_i32_le(buf + 5), 2);    /* kills */
    ASSERT_EQ_INT(read_i32_le(buf + 9), 1);    /* deaths */
    ASSERT_EQ_INT(read_i32_le(buf + 13), 17);  /* score */
    ASSERT_EQ(buf[17], 1);                     /* team */
}

TEST(team_score_update)
{
    u8 buf[32];
    int len = bc_build_team_score(buf, sizeof(buf), 0, 12, 4200);

    ASSERT_EQ_INT(len, 10);
    ASSERT_EQ(buf[0], BC_MSG_TEAM_SCORE);
    ASSERT_EQ(buf[1], 0);
    ASSERT_EQ_INT(read_i32_le(buf + 2), 12);   /* team kills */
    ASSERT_EQ_INT(read_i32_le(buf + 6), 4200); /* team score */
}

TEST(restart_game_message)
{
    u8 buf[8];
    int len = bc_build_restart_game(buf, sizeof(buf));
    ASSERT_EQ_INT(len, 1);
    ASSERT_EQ(buf[0], BC_MSG_RESTART);
}

/* === StateUpdate === */

TEST(state_update_with_fields)
{
    u8 field_data[] = { 0x11, 0x22, 0x33, 0x44, 0x55 };
    u8 buf[64];
    int len = bc_build_state_update(buf, sizeof(buf),
                                     bc_make_ship_id(0), 10.5f, 0x1E,
                                     field_data, sizeof(field_data));

    ASSERT(len == 15); /* 1+4+4+1+5 */
    ASSERT_EQ(buf[0], BC_OP_STATE_UPDATE);
    ASSERT_EQ(buf[9], 0x1E); /* dirty flags */
    ASSERT_EQ(buf[10], 0x11); /* first field byte */
}

/* === Event forward === */

TEST(event_forward_generic)
{
    u8 data[] = { 0xAA, 0xBB, 0xCC };
    u8 buf[32];
    int len = bc_build_event_forward(buf, sizeof(buf), BC_OP_START_FIRING, data, 3);

    ASSERT(len == 4);
    ASSERT_EQ(buf[0], BC_OP_START_FIRING);
    ASSERT_EQ(buf[1], 0xAA);
    ASSERT_EQ(buf[3], 0xCC);
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    /* Object ID */
    RUN(object_id_slot0);
    RUN(object_id_slot2);
    RUN(ship_id_slot1);

    /* ObjectCreateTeam */
    RUN(object_create_team_header);

    /* TorpedoFire */
    RUN(torpedo_fire_no_target);
    RUN(torpedo_fire_with_target);
    RUN(torpedo_roundtrip);

    /* BeamFire */
    RUN(beam_fire_no_target);
    RUN(beam_fire_with_target);
    RUN(beam_roundtrip);

    /* Explosion */
    RUN(explosion_14_bytes);
    RUN(explosion_roundtrip);

    /* DestroyObject */
    RUN(destroy_obj_5_bytes);

    /* Chat */
    RUN(chat_all);
    RUN(chat_team);

    /* Score */
    RUN(score_message_single_player);
    RUN(score_change_with_killer);
    RUN(score_change_environmental_kill);
    RUN(score_change_extra_entries);
    RUN(score_init_team_sync);
    RUN(team_score_update);
    RUN(restart_game_message);

    /* StateUpdate */
    RUN(state_update_with_fields);

    /* Event forward */
    RUN(event_forward_generic);
TEST_MAIN_END()
