/*
 * Event Coverage Tests (Unit)
 *
 * Pure builder/parser verification for game event codes that previously
 * had zero test coverage. No network -- only buffer construction and
 * byte-level layout assertions.
 *
 * Covers: EndGame, TeamMessage, ObjPtrEvent variants (weapon_fired,
 * phaser_started/stopped, tractor_started/stopped, repair_priority),
 * SubsystemEvent variants (repair_completed, repair_cannot),
 * and StopAtTarget.
 */

#include "test_util.h"
#include "openbc/game_builders.h"
#include "openbc/game_events.h"
#include "openbc/opcodes.h"
#include <string.h>

static i32 read_i32_le(const u8 *p)
{
    return (i32)((u32)p[0] |
                 ((u32)p[1] << 8) |
                 ((u32)p[2] << 16) |
                 ((u32)p[3] << 24));
}

/* === EndGame builder === */

TEST(end_game_layout)
{
    u8 buf[16];
    int len = bc_build_end_game(buf, sizeof(buf), BC_END_REASON_FRAG_LIMIT);

    ASSERT_EQ_INT(len, 5);
    ASSERT_EQ(buf[0], BC_MSG_END_GAME);
    ASSERT_EQ_INT(read_i32_le(buf + 1), BC_END_REASON_FRAG_LIMIT);
}

TEST(end_game_buffer_too_small)
{
    u8 buf[4];
    int len = bc_build_end_game(buf, sizeof(buf), BC_END_REASON_OVER);
    ASSERT_EQ_INT(len, -1);
}

/* === TeamMessage builder === */

TEST(team_message_layout)
{
    u8 buf[16];
    int len = bc_build_team_message(buf, sizeof(buf), 3, 1);

    ASSERT_EQ_INT(len, 6);
    ASSERT_EQ(buf[0], BC_MSG_TEAM_MESSAGE);
    ASSERT_EQ_INT(read_i32_le(buf + 1), 3);
    ASSERT_EQ(buf[5], 1);
}

TEST(team_message_buffer_too_small)
{
    u8 buf[5];
    int len = bc_build_team_message(buf, sizeof(buf), 1, 0);
    ASSERT_EQ_INT(len, -1);
}

/* === ObjPtrEvent builders (factory=0x010C) === */

TEST(weapon_fired_event_layout)
{
    u8 buf[32];
    i32 src = bc_make_ship_id(1);
    i32 dst = bc_make_ship_id(0);
    i32 weapon = bc_make_object_id(1, 3);
    int len = bc_build_python_obj_ptr_event(buf, sizeof(buf),
                                             BC_EVENT_WEAPON_FIRED,
                                             src, dst, weapon);

    ASSERT_EQ_INT(len, 21);
    ASSERT_EQ(buf[0], BC_OP_PYTHON_EVENT);
    ASSERT_EQ_INT(read_i32_le(buf + 1), BC_FACTORY_OBJ_PTR_EVENT);
    ASSERT_EQ_INT(read_i32_le(buf + 5), BC_EVENT_WEAPON_FIRED);
    ASSERT_EQ_INT(read_i32_le(buf + 9), src);
    ASSERT_EQ_INT(read_i32_le(buf + 13), dst);
    ASSERT_EQ_INT(read_i32_le(buf + 17), weapon);
}

TEST(phaser_started_event_layout)
{
    u8 buf[32];
    i32 src = bc_make_ship_id(2);
    i32 dst = bc_make_ship_id(0);
    i32 target = bc_make_ship_id(1);
    int len = bc_build_python_obj_ptr_event(buf, sizeof(buf),
                                             BC_EVENT_PHASER_STARTED,
                                             src, dst, target);

    ASSERT_EQ_INT(len, 21);
    ASSERT_EQ(buf[0], BC_OP_PYTHON_EVENT);
    ASSERT_EQ_INT(read_i32_le(buf + 1), BC_FACTORY_OBJ_PTR_EVENT);
    ASSERT_EQ_INT(read_i32_le(buf + 5), BC_EVENT_PHASER_STARTED);
}

TEST(phaser_stopped_event_layout)
{
    u8 buf[32];
    int len = bc_build_python_obj_ptr_event(buf, sizeof(buf),
                                             BC_EVENT_PHASER_STOPPED,
                                             bc_make_ship_id(1),
                                             bc_make_ship_id(0),
                                             bc_make_ship_id(2));

    ASSERT_EQ_INT(len, 21);
    ASSERT_EQ_INT(read_i32_le(buf + 5), BC_EVENT_PHASER_STOPPED);
}

TEST(tractor_started_event_layout)
{
    u8 buf[32];
    int len = bc_build_python_obj_ptr_event(buf, sizeof(buf),
                                             BC_EVENT_TRACTOR_STARTED,
                                             bc_make_ship_id(0),
                                             bc_make_ship_id(1),
                                             bc_make_ship_id(1));

    ASSERT_EQ_INT(len, 21);
    ASSERT_EQ_INT(read_i32_le(buf + 5), BC_EVENT_TRACTOR_STARTED);
}

TEST(tractor_stopped_event_layout)
{
    u8 buf[32];
    int len = bc_build_python_obj_ptr_event(buf, sizeof(buf),
                                             BC_EVENT_TRACTOR_STOPPED,
                                             bc_make_ship_id(0),
                                             bc_make_ship_id(1),
                                             bc_make_ship_id(1));

    ASSERT_EQ_INT(len, 21);
    ASSERT_EQ_INT(read_i32_le(buf + 5), BC_EVENT_TRACTOR_STOPPED);
}

TEST(repair_priority_event_layout)
{
    u8 buf[32];
    i32 src = bc_make_ship_id(0);
    i32 dst = bc_make_ship_id(0);
    i32 subsys = bc_make_object_id(0, 5);
    int len = bc_build_python_obj_ptr_event(buf, sizeof(buf),
                                             BC_EVENT_REPAIR_PRIORITY,
                                             src, dst, subsys);

    ASSERT_EQ_INT(len, 21);
    ASSERT_EQ_INT(read_i32_le(buf + 5), BC_EVENT_REPAIR_PRIORITY);
    ASSERT_EQ_INT(read_i32_le(buf + 17), subsys);
}

/* === SubsystemEvent builders (factory=0x101) === */

TEST(repair_completed_event_layout)
{
    u8 buf[32];
    i32 src = bc_make_ship_id(1);
    i32 dst = bc_make_ship_id(1);
    int len = bc_build_python_subsystem_event(buf, sizeof(buf),
                                               BC_EVENT_REPAIR_COMPLETED,
                                               src, dst);

    ASSERT_EQ_INT(len, 17);
    ASSERT_EQ(buf[0], BC_OP_PYTHON_EVENT);
    ASSERT_EQ_INT(read_i32_le(buf + 1), BC_FACTORY_SUBSYSTEM_EVENT);
    ASSERT_EQ_INT(read_i32_le(buf + 5), BC_EVENT_REPAIR_COMPLETED);
    ASSERT_EQ_INT(read_i32_le(buf + 9), src);
    ASSERT_EQ_INT(read_i32_le(buf + 13), dst);
}

TEST(repair_cannot_event_layout)
{
    u8 buf[32];
    i32 src = bc_make_ship_id(2);
    i32 dst = bc_make_ship_id(2);
    int len = bc_build_python_subsystem_event(buf, sizeof(buf),
                                               BC_EVENT_REPAIR_CANNOT,
                                               src, dst);

    ASSERT_EQ_INT(len, 17);
    ASSERT_EQ_INT(read_i32_le(buf + 5), BC_EVENT_REPAIR_CANNOT);
}

/* === StopAtTarget (ObjPtrEvent with factory=0x010C) === */

TEST(stop_at_target_event_layout)
{
    u8 buf[32];
    i32 src = bc_make_ship_id(0);
    i32 dst = bc_make_ship_id(0);
    i32 target = bc_make_ship_id(1);
    int len = bc_build_python_obj_ptr_event(buf, sizeof(buf),
                                             BC_EVENT_STOP_AT_TARGET,
                                             src, dst, target);

    ASSERT_EQ_INT(len, 21);
    ASSERT_EQ(buf[0], BC_OP_PYTHON_EVENT);
    ASSERT_EQ_INT(read_i32_le(buf + 1), BC_FACTORY_OBJ_PTR_EVENT);
    ASSERT_EQ_INT(read_i32_le(buf + 5), BC_EVENT_STOP_AT_TARGET);
    ASSERT_EQ_INT(read_i32_le(buf + 17), target);
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    /* EndGame */
    RUN(end_game_layout);
    RUN(end_game_buffer_too_small);

    /* TeamMessage */
    RUN(team_message_layout);
    RUN(team_message_buffer_too_small);

    /* ObjPtrEvent variants */
    RUN(weapon_fired_event_layout);
    RUN(phaser_started_event_layout);
    RUN(phaser_stopped_event_layout);
    RUN(tractor_started_event_layout);
    RUN(tractor_stopped_event_layout);
    RUN(repair_priority_event_layout);

    /* SubsystemEvent variants */
    RUN(repair_completed_event_layout);
    RUN(repair_cannot_event_layout);

    /* StopAtTarget */
    RUN(stop_at_target_event_layout);
TEST_MAIN_END()
