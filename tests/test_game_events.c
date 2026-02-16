#include "test_util.h"
#include "openbc/game_events.h"
#include "openbc/buffer.h"
#include "openbc/opcodes.h"
#include <string.h>
#include <math.h>

/* === Object ID to slot mapping === */

TEST(object_id_slot0)
{
    /* Slot 0: base ID = 0x3FFFFFFF */
    ASSERT_EQ_INT(bc_object_id_to_slot(0x3FFFFFFF), 0);
    /* Slot 0 + offset within range */
    ASSERT_EQ_INT(bc_object_id_to_slot(0x3FFFFFFF + 100), 0);
}

TEST(object_id_slot1)
{
    /* Slot 1: base + (1 << 18) = 0x3FFFFFFF + 0x40000 */
    ASSERT_EQ_INT(bc_object_id_to_slot(0x3FFFFFFF + 0x40000), 1);
}

TEST(object_id_out_of_range)
{
    /* Below base */
    ASSERT_EQ_INT(bc_object_id_to_slot(0), -1);
    ASSERT_EQ_INT(bc_object_id_to_slot(0x3FFFFFFE), -1);

    /* Beyond slot 5 */
    ASSERT_EQ_INT(bc_object_id_to_slot(0x3FFFFFFF + 6 * 0x40000), -1);
}

/* === TorpedoFire parser === */

static int build_torpedo_no_target(u8 *buf, int buf_size)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    bc_buf_write_u8(&b, BC_OP_TORPEDO_FIRE);
    bc_buf_write_i32(&b, 0x3FFFFFFF);  /* shooter_id (slot 0) */
    bc_buf_write_u8(&b, 6);            /* subsys_index */
    bc_buf_write_u8(&b, 0x00);         /* flags: no target */
    bc_buf_write_cv3(&b, 1.0f, 0.0f, 0.0f);  /* velocity direction */

    return (int)b.pos;
}

static int build_torpedo_with_target(u8 *buf, int buf_size)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    bc_buf_write_u8(&b, BC_OP_TORPEDO_FIRE);
    bc_buf_write_i32(&b, 0x3FFFFFFF);          /* shooter_id (slot 0) */
    bc_buf_write_u8(&b, 6);                    /* subsys_index */
    bc_buf_write_u8(&b, 0x02);                 /* flags: has_target (bit 1) */
    bc_buf_write_cv3(&b, 0.0f, 0.0f, 1.0f);   /* velocity direction */
    bc_buf_write_i32(&b, 0x3FFFFFFF + 0x40000); /* target_id (slot 1) */
    bc_buf_write_cv4(&b, 50.0f, 30.0f, 0.0f); /* impact point */

    return (int)b.pos;
}

TEST(torpedo_no_target)
{
    u8 buf[64];
    int len = build_torpedo_no_target(buf, sizeof(buf));

    bc_torpedo_event_t ev;
    ASSERT(bc_parse_torpedo_fire(buf, len, &ev));
    ASSERT_EQ((u32)ev.shooter_id, 0x3FFFFFFF);
    ASSERT_EQ(ev.subsys_index, 6);
    ASSERT(!ev.has_target);
    ASSERT(fabsf(ev.vel_x - 1.0f) < 0.02f);
}

TEST(torpedo_with_target)
{
    u8 buf[64];
    int len = build_torpedo_with_target(buf, sizeof(buf));

    bc_torpedo_event_t ev;
    ASSERT(bc_parse_torpedo_fire(buf, len, &ev));
    ASSERT(ev.has_target);
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.shooter_id), 0);
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.target_id), 1);
    ASSERT(fabsf(ev.impact_x - 50.0f) < 2.0f);
}

TEST(torpedo_truncated)
{
    u8 buf[64];
    build_torpedo_no_target(buf, sizeof(buf));

    bc_torpedo_event_t ev;
    /* Too short -- should fail */
    ASSERT(!bc_parse_torpedo_fire(buf, 5, &ev));
}

/* === BeamFire parser === */

static int build_beam_no_target(u8 *buf, int buf_size)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    bc_buf_write_u8(&b, BC_OP_BEAM_FIRE);
    bc_buf_write_i32(&b, 0x3FFFFFFF);          /* shooter_id (slot 0) */
    bc_buf_write_u8(&b, 0x00);                 /* flags */
    bc_buf_write_cv3(&b, 0.0f, 1.0f, 0.0f);   /* target direction */
    bc_buf_write_u8(&b, 0x00);                 /* more_flags: no target */

    return (int)b.pos;
}

static int build_beam_with_target(u8 *buf, int buf_size)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    bc_buf_write_u8(&b, BC_OP_BEAM_FIRE);
    bc_buf_write_i32(&b, 0x3FFFFFFF);          /* shooter_id (slot 0) */
    bc_buf_write_u8(&b, 0x01);                 /* flags */
    bc_buf_write_cv3(&b, 1.0f, 0.0f, 0.0f);   /* target direction */
    bc_buf_write_u8(&b, 0x01);                 /* more_flags: has target */
    bc_buf_write_i32(&b, 0x3FFFFFFF + 0x40000); /* target_id (slot 1) */

    return (int)b.pos;
}

TEST(beam_no_target)
{
    u8 buf[64];
    int len = build_beam_no_target(buf, sizeof(buf));

    bc_beam_event_t ev;
    ASSERT(bc_parse_beam_fire(buf, len, &ev));
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.shooter_id), 0);
    ASSERT(!ev.has_target);
}

TEST(beam_with_target)
{
    u8 buf[64];
    int len = build_beam_with_target(buf, sizeof(buf));

    bc_beam_event_t ev;
    ASSERT(bc_parse_beam_fire(buf, len, &ev));
    ASSERT(ev.has_target);
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.target_id), 1);
}

TEST(beam_truncated)
{
    u8 buf[64];
    build_beam_no_target(buf, sizeof(buf));

    bc_beam_event_t ev;
    ASSERT(!bc_parse_beam_fire(buf, 4, &ev));
}

/* === Explosion parser === */

static int build_explosion(u8 *buf, int buf_size)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    bc_buf_write_u8(&b, BC_OP_EXPLOSION);
    bc_buf_write_i32(&b, 0x3FFFFFFF + 0x40000); /* object_id (slot 1) */
    bc_buf_write_cv4(&b, 10.0f, 20.0f, 30.0f);  /* impact position */
    bc_buf_write_cf16(&b, 150.0f);               /* damage */
    bc_buf_write_cf16(&b, 45.0f);                /* radius */

    return (int)b.pos;
}

TEST(explosion_happy_path)
{
    u8 buf[64];
    int len = build_explosion(buf, sizeof(buf));

    bc_explosion_event_t ev;
    ASSERT(bc_parse_explosion(buf, len, &ev));
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.object_id), 1);
    ASSERT(fabsf(ev.damage - 150.0f) < 2.0f);
    ASSERT(fabsf(ev.radius - 45.0f) < 1.0f);
}

TEST(explosion_truncated)
{
    u8 buf[64];
    build_explosion(buf, sizeof(buf));

    bc_explosion_event_t ev;
    ASSERT(!bc_parse_explosion(buf, 8, &ev));
}

/* === DestroyObject parser === */

TEST(destroy_happy_path)
{
    u8 buf[8];
    bc_buffer_t b;
    bc_buf_init(&b, buf, sizeof(buf));
    bc_buf_write_u8(&b, BC_OP_DESTROY_OBJ);
    bc_buf_write_i32(&b, 0x3FFFFFFF + 0x40000);

    bc_destroy_event_t ev;
    ASSERT(bc_parse_destroy_obj(buf, (int)b.pos, &ev));
    ASSERT_EQ_INT(bc_object_id_to_slot(ev.object_id), 1);
}

TEST(destroy_truncated)
{
    u8 buf[2] = { BC_OP_DESTROY_OBJ, 0x00 };

    bc_destroy_event_t ev;
    ASSERT(!bc_parse_destroy_obj(buf, 2, &ev));
}

/* === ObjectCreate header parser === */

TEST(object_create_team)
{
    u8 buf[8] = { 3, 2, 1 };  /* type_tag=3, owner_slot=2, team_id=1 */

    bc_object_create_header_t hdr;
    ASSERT(bc_parse_object_create_header(buf, 3, &hdr));
    ASSERT_EQ(hdr.type_tag, 3);
    ASSERT_EQ(hdr.owner_slot, 2);
    ASSERT_EQ(hdr.team_id, 1);
    ASSERT(hdr.has_team);
}

TEST(object_create_no_team)
{
    u8 buf[8] = { 2, 0 };  /* type_tag=2, owner_slot=0 */

    bc_object_create_header_t hdr;
    ASSERT(bc_parse_object_create_header(buf, 2, &hdr));
    ASSERT_EQ(hdr.type_tag, 2);
    ASSERT_EQ(hdr.owner_slot, 0);
    ASSERT(!hdr.has_team);
}

/* === Chat parser === */

static int build_chat(u8 *buf, int buf_size, u8 opcode, u8 slot,
                      const char *msg)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    bc_buf_write_u8(&b, opcode);
    bc_buf_write_u8(&b, slot);
    bc_buf_write_u8(&b, 0x00);  /* pad */
    bc_buf_write_u8(&b, 0x00);  /* pad */
    bc_buf_write_u8(&b, 0x00);  /* pad */
    bc_buf_write_u16(&b, (u16)strlen(msg));
    bc_buf_write_bytes(&b, (const u8 *)msg, strlen(msg));

    return (int)b.pos;
}

TEST(chat_happy_path)
{
    u8 buf[128];
    int len = build_chat(buf, sizeof(buf), BC_MSG_CHAT, 0, "gg");

    bc_chat_event_t ev;
    ASSERT(bc_parse_chat_message(buf, len, &ev));
    ASSERT_EQ(ev.sender_slot, 0);
    ASSERT_EQ_INT(ev.message_len, 2);
    ASSERT(strcmp(ev.message, "gg") == 0);
}

TEST(chat_team)
{
    u8 buf[128];
    int len = build_chat(buf, sizeof(buf), BC_MSG_TEAM_CHAT, 3, "help");

    bc_chat_event_t ev;
    ASSERT(bc_parse_chat_message(buf, len, &ev));
    ASSERT_EQ(ev.sender_slot, 3);
    ASSERT_EQ_INT(ev.message_len, 4);
    ASSERT(strcmp(ev.message, "help") == 0);
}

TEST(chat_truncated)
{
    u8 buf[128];
    build_chat(buf, sizeof(buf), BC_MSG_CHAT, 0, "hello");

    bc_chat_event_t ev;
    /* Too short to have padding + str_len */
    ASSERT(!bc_parse_chat_message(buf, 3, &ev));
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    /* Object ID mapping */
    RUN(object_id_slot0);
    RUN(object_id_slot1);
    RUN(object_id_out_of_range);

    /* TorpedoFire */
    RUN(torpedo_no_target);
    RUN(torpedo_with_target);
    RUN(torpedo_truncated);

    /* BeamFire */
    RUN(beam_no_target);
    RUN(beam_with_target);
    RUN(beam_truncated);

    /* Explosion */
    RUN(explosion_happy_path);
    RUN(explosion_truncated);

    /* DestroyObject */
    RUN(destroy_happy_path);
    RUN(destroy_truncated);

    /* ObjectCreate */
    RUN(object_create_team);
    RUN(object_create_no_team);

    /* Chat */
    RUN(chat_happy_path);
    RUN(chat_team);
    RUN(chat_truncated);
TEST_MAIN_END()
