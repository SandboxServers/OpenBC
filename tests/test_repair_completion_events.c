/* Repair-tick wire-event emission tests.
 *
 * Wire-protocol trace analysis of a stock dedicated server session shows the
 * host announces repair-queue transitions to clients as PythonEvent (0x06):
 *   - REPAIR_COMPLETED (0x00800074): subsystem reached max condition.
 *     Factory 0x0101 base TGEvent (17 bytes on wire).
 *   - REPAIR_CANNOT_BE_COMPLETED (0x00800075): subsystem destroyed while
 *     queued. Factory 0x010C TGObjPtrEvent (21 bytes on wire).
 *
 * These tests verify bc_repair_tick() reports those transitions through its
 * out-parameter (the simulation side) and that the matching builders produce
 * the expected wire bytes (the protocol side). */

#include "test_util.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/combat.h"
#include "openbc/game_builders.h"
#include "openbc/opcodes.h"
#include <string.h>

#define REGISTRY_DIR "data/vanilla-1.1"

static bc_game_registry_t g_reg;

static i32 rd_i32(const u8 *p)
{
    return (i32)((u32)p[0] | ((u32)p[1] << 8) |
                 ((u32)p[2] << 16) | ((u32)p[3] << 24));
}

TEST(load_registry)
{
    memset(&g_reg, 0, sizeof(g_reg));
    ASSERT(bc_registry_load_dir(&g_reg, REGISTRY_DIR));
    ASSERT(g_reg.ship_count > 0);
}

/* A queued subsystem that finishes repair reports a COMPLETED event and is
 * dropped from the queue. */
TEST(repair_completed_emits_event)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3); /* Galaxy */
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    bc_ship_assign_subsystem_ids(&ship, cls);

    /* Almost-full subsystem; a long tick completes the repair. */
    f32 max_hp = cls->subsystems[0].max_condition;
    ASSERT(max_hp > 0.0f);
    ship.subsystem_hp[0] = max_hp - 1.0f;
    ASSERT(bc_repair_add(&ship, 0));

    bc_repair_event_t evts[BC_MAX_SUBSYSTEMS];
    int n = -1;
    bc_repair_tick(&ship, cls, 100.0f, evts, BC_MAX_SUBSYSTEMS, &n);

    ASSERT_EQ(n, 1);
    ASSERT_EQ(evts[0].subsys_index, 0);
    ASSERT_EQ(evts[0].kind, BC_REPAIR_EVT_COMPLETED);
    ASSERT_EQ(ship.repair_count, 0);          /* dropped from queue */
    ASSERT(ship.subsystem_hp[0] >= max_hp - 0.01f);
}

/* A queued subsystem that drops to 0 HP while waiting reports a CANNOT event
 * and is dropped from the queue (previously a silent skip that left the entry
 * stuck in the queue forever). */
TEST(repair_destroyed_emits_cannot)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3); /* Galaxy */
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    bc_ship_assign_subsystem_ids(&ship, cls);

    /* Queue a subsystem, then destroy it (0 HP) before the next tick. */
    ship.subsystem_hp[1] = cls->subsystems[1].max_condition * 0.5f;
    ASSERT(bc_repair_add(&ship, 1));
    ship.subsystem_hp[1] = 0.0f; /* destroyed while queued */

    bc_repair_event_t evts[BC_MAX_SUBSYSTEMS];
    int n = -1;
    bc_repair_tick(&ship, cls, 1.0f, evts, BC_MAX_SUBSYSTEMS, &n);

    ASSERT_EQ(n, 1);
    ASSERT_EQ(evts[0].subsys_index, 1);
    ASSERT_EQ(evts[0].kind, BC_REPAIR_EVT_CANNOT);
    ASSERT_EQ(ship.repair_count, 0); /* dropped, no longer stuck */
}

/* An in-progress repair (not yet complete, not destroyed) reports no event. */
TEST(repair_in_progress_no_event)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    bc_ship_assign_subsystem_ids(&ship, cls);

    ship.subsystem_hp[0] = cls->subsystems[0].max_condition * 0.1f;
    ASSERT(bc_repair_add(&ship, 0));

    bc_repair_event_t evts[BC_MAX_SUBSYSTEMS];
    int n = -1;
    /* Small dt: heals a little but does not complete. */
    bc_repair_tick(&ship, cls, 0.01f, evts, BC_MAX_SUBSYSTEMS, &n);

    ASSERT_EQ(n, 0);
    ASSERT_EQ(ship.repair_count, 1); /* still queued */
}

/* NULL out-parameters are tolerated (existing callers that don't want events). */
TEST(repair_tick_null_outparams)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    f32 max_hp = cls->subsystems[0].max_condition;
    ship.subsystem_hp[0] = max_hp - 1.0f;
    ASSERT(bc_repair_add(&ship, 0));

    bc_repair_tick(&ship, cls, 100.0f, NULL, 0, NULL);
    ASSERT_EQ(ship.repair_count, 0); /* still drops the completed entry */
}

/* REPAIR_COMPLETED wire bytes: opcode 0x06, factory 0x0101, event 0x00800074. */
TEST(repair_completed_wire_format)
{
    u8 buf[32];
    int len = bc_build_python_subsystem_event(
        buf, sizeof(buf),
        BC_EVENT_REPAIR_COMPLETED,
        0x40010001, /* repair subsystem obj id (source) */
        0x40010005  /* repaired subsystem obj id (dest) */);

    ASSERT_EQ(len, 17);
    ASSERT_EQ(buf[0], BC_OP_PYTHON_EVENT);
    ASSERT_EQ((u32)rd_i32(&buf[1]), (u32)BC_FACTORY_SUBSYSTEM_EVENT); /* 0x0101 */
    ASSERT_EQ((u32)rd_i32(&buf[5]), (u32)BC_EVENT_REPAIR_COMPLETED);  /* 0x00800074 */
    ASSERT_EQ((u32)rd_i32(&buf[9]), 0x40010001u);
    ASSERT_EQ((u32)rd_i32(&buf[13]), 0x40010005u);
}

/* REPAIR_CANNOT_BE_COMPLETED wire bytes: opcode 0x06, factory 0x010C,
 * event 0x00800075, plus the trailing obj_ptr int32. */
TEST(repair_cannot_wire_format)
{
    u8 buf[32];
    int len = bc_build_python_obj_ptr_event(
        buf, sizeof(buf),
        BC_EVENT_REPAIR_CANNOT,
        0x40010001, /* repair subsystem obj id (source) */
        0x40010007, /* destroyed subsystem obj id (dest) */
        0x40010007  /* obj_ptr = subsystem obj id */);

    ASSERT_EQ(len, 21);
    ASSERT_EQ(buf[0], BC_OP_PYTHON_EVENT);
    ASSERT_EQ((u32)rd_i32(&buf[1]), (u32)BC_FACTORY_OBJ_PTR_EVENT); /* 0x010C */
    ASSERT_EQ((u32)rd_i32(&buf[5]), (u32)BC_EVENT_REPAIR_CANNOT);   /* 0x00800075 */
    ASSERT_EQ((u32)rd_i32(&buf[9]), 0x40010001u);
    ASSERT_EQ((u32)rd_i32(&buf[13]), 0x40010007u);
    ASSERT_EQ((u32)rd_i32(&buf[17]), 0x40010007u);
}

TEST_MAIN_BEGIN()
    RUN(load_registry);
    RUN(repair_completed_emits_event);
    RUN(repair_destroyed_emits_cannot);
    RUN(repair_in_progress_no_event);
    RUN(repair_tick_null_outparams);
    RUN(repair_completed_wire_format);
    RUN(repair_cannot_wire_format);
TEST_MAIN_END()
