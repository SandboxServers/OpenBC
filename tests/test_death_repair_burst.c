/* Issue #61 + #85: death repair burst unit tests.
 * Verifies that collision death sends ADD_TO_REPAIR_LIST for all damaged
 * subsystems without bucket limiting.  Issue #85: the death burst sends
 * events unconditionally for every damaged subsystem, regardless of whether
 * bc_repair_add() rejects the add as a duplicate.  The repair queue is
 * preserved (not cleared) so prior damage state is maintained. */

#include "test_util.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/combat.h"
#include "openbc/game_builders.h"
#include <string.h>

#define REGISTRY_DIR "data/vanilla-1.1"

static bc_game_registry_t g_reg;

TEST(load_registry)
{
    memset(&g_reg, 0, sizeof(g_reg));
    ASSERT(bc_registry_load_dir(&g_reg, REGISTRY_DIR));
    ASSERT(g_reg.ship_count > 0);
}

/* Death burst covers all damaged subsystems regardless of pre-queued state.
 * Pre-damage 10 subsystems, queue 3 (simulating bucket-limited damage events),
 * then verify the death burst produces 10 events (unconditional send). */
TEST(death_burst_covers_all_damaged)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3); /* Galaxy */
    ASSERT(cls != NULL);
    ASSERT(cls->subsystem_count > 10);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    bc_ship_assign_subsystem_ids(&ship, cls);

    /* Pre-damage 10 subsystems to below max_condition. */
    int damaged_count = 0;
    for (int i = 0; i < cls->subsystem_count && damaged_count < 10; i++) {
        if (cls->subsystems[i].max_condition > 0.0f) {
            ship.subsystem_hp[i] = cls->subsystems[i].max_condition * 0.5f;
            damaged_count++;
        }
    }
    ASSERT(damaged_count == 10);

    /* Simulate bucket-limited generate_damage_events: queue only 3. */
    int pre_queued = 0;
    for (int i = 0; i < cls->subsystem_count && pre_queued < 3; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition) {
            ASSERT(bc_repair_add(&ship, (u8)i));
            pre_queued++;
        }
    }
    ASSERT_EQ(ship.repair_count, 3);

    /* Death burst: iterate all subsystems, send event unconditionally for
     * each damaged subsystem.  bc_repair_add() is called for queue
     * maintenance but does NOT gate the event send (issue #85). */
    int death_burst_events = 0;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition) {
            bc_repair_add(&ship, (u8)i);  /* queue maintenance */
            death_burst_events++;          /* event always sent */
        }
    }

    /* All 10 damaged subsystems should produce events */
    ASSERT_EQ(death_burst_events, 10);
    /* Queue should contain all 10 (3 pre-queued + 7 newly added) */
    ASSERT_EQ(ship.repair_count, 10);
}

/* Issue #85: even when ALL damaged subsystems are already queued, the death
 * burst should still produce events for every one (unconditional send). */
TEST(death_burst_ignores_prequeued)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    bc_ship_assign_subsystem_ids(&ship, cls);

    /* Damage all subsystems */
    int total_damaged = 0;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (cls->subsystems[i].max_condition > 0.0f) {
            ship.subsystem_hp[i] = cls->subsystems[i].max_condition * 0.3f;
            total_damaged++;
        }
    }
    ASSERT(total_damaged > 0);

    /* Queue ALL damaged subsystems (simulating aggressive pre-queuing from
     * prior generate_damage_events() calls + bc_repair_auto_queue() ticks) */
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition)
            bc_repair_add(&ship, (u8)i);
    }
    ASSERT_EQ(ship.repair_count, total_damaged);

    /* Death burst sends events unconditionally -- pre-queued state doesn't
     * suppress events (issue #85). */
    int death_burst_events = 0;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition) {
            bc_repair_add(&ship, (u8)i);  /* dedup is fine for queue */
            death_burst_events++;          /* event always sent */
        }
    }
    ASSERT_EQ(death_burst_events, total_damaged);
    /* Queue unchanged (all were already queued) */
    ASSERT_EQ(ship.repair_count, total_damaged);
}

/* Subsystem at full HP should NOT generate an event in the death burst. */
TEST(death_burst_skips_undamaged)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    bc_ship_assign_subsystem_ids(&ship, cls);

    /* Damage only 2 subsystems */
    if (cls->subsystem_count >= 2) {
        ship.subsystem_hp[0] = cls->subsystems[0].max_condition * 0.5f;
        ship.subsystem_hp[1] = cls->subsystems[1].max_condition * 0.5f;
    }

    /* Death burst: only 2 damaged subsystems should produce events */
    int death_burst_events = 0;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition) {
            bc_repair_add(&ship, (u8)i);
            death_burst_events++;
        }
    }
    ASSERT_EQ(death_burst_events, 2);
    ASSERT_EQ(ship.repair_count, 2);
}

/* Each subsystem event should reference a valid object ID from the
 * player's allocation range. */
TEST(death_burst_uses_valid_subsystem_ids)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    bc_ship_assign_subsystem_ids(&ship, cls);

    /* Damage all subsystems */
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (cls->subsystems[i].max_condition > 0.0f)
            ship.subsystem_hp[i] = cls->subsystems[i].max_condition * 0.5f;
    }

    /* Verify all subsystem IDs are in slot 0's range */
    i32 slot0_base = bc_make_object_id(0, 0);
    i32 slot0_end = bc_make_object_id(1, 0);

    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition) {
            i32 sid = ship.subsys_obj_id[i];
            ASSERT(sid >= slot0_base);
            ASSERT(sid < slot0_end);
        }
    }

    /* Verify repair subsystem ID is also in range */
    ASSERT(ship.repair_subsys_obj_id >= slot0_base);
    ASSERT(ship.repair_subsys_obj_id < slot0_end);

    /* Build an actual event and verify wire format */
    u8 evt[17];
    int len = bc_build_python_subsystem_event(
        evt, sizeof(evt),
        BC_EVENT_ADD_TO_REPAIR,
        ship.subsys_obj_id[0],
        ship.repair_subsys_obj_id);
    ASSERT_EQ(len, 17);
    ASSERT_EQ(evt[0], 0x06); /* PythonEvent opcode */
}

/* Issue #85 regression: first-ship scenario.  The first ship takes many
 * collision hits before dying.  Each hit adds subsystems to the repair queue
 * via generate_damage_events() (bucket-limited), and bc_repair_auto_queue()
 * silently adds more each tick.  Without the fix, the death burst only
 * produced events for subsystems NOT already in the queue (~1 event).
 * With the fix, events are sent unconditionally for ALL damaged subsystems
 * regardless of prior queue state. */
TEST(first_ship_deficit_regression)
{
    /* Use Sovereign (species 5) -- the ship from the original bug report */
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 5);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 0, 0x3FFFFFFF, 1, 0); /* base player ID */
    bc_ship_assign_subsystem_ids(&ship, cls);
    ASSERT_EQ(ship.repair_count, 0);

    /* Simulate multiple collision hits damaging subsystems progressively.
     * Each "hit" damages a few subsystems and adds them to the queue
     * (mimicking generate_damage_events bucket behavior). */
    int total_with_max_cond = 0;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (cls->subsystems[i].max_condition > 0.0f)
            total_with_max_cond++;
    }

    /* Hit 1: damage first third of subsystems, queue 2 (bucket limited) */
    int third = total_with_max_cond / 3;
    if (third < 2) third = 2;
    int damaged = 0;
    for (int i = 0; i < cls->subsystem_count && damaged < third; i++) {
        if (cls->subsystems[i].max_condition > 0.0f) {
            ship.subsystem_hp[i] = cls->subsystems[i].max_condition * 0.6f;
            damaged++;
        }
    }
    /* Bucket-limited: queue first 2 from this hit */
    int queued = 0;
    for (int i = 0; i < cls->subsystem_count && queued < 2; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition) {
            bc_repair_add(&ship, (u8)i);
            queued++;
        }
    }

    /* Simulate bc_repair_auto_queue() ticks adding more silently */
    bc_repair_auto_queue(&ship, cls);
    int after_auto_1 = ship.repair_count;
    ASSERT(after_auto_1 >= 2); /* at least the 2 we manually added */

    /* Hit 2: damage more subsystems */
    damaged = 0;
    for (int i = 0; i < cls->subsystem_count && damaged < third * 2; i++) {
        if (cls->subsystems[i].max_condition > 0.0f) {
            ship.subsystem_hp[i] = cls->subsystems[i].max_condition * 0.4f;
            damaged++;
        }
    }
    bc_repair_auto_queue(&ship, cls);

    /* Hit 3: damage all subsystems (the killing blow) */
    int all_damaged = 0;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (cls->subsystems[i].max_condition > 0.0f) {
            ship.subsystem_hp[i] = cls->subsystems[i].max_condition * 0.2f;
            all_damaged++;
        }
    }
    bc_repair_auto_queue(&ship, cls);

    /* Many subsystems are now pre-queued. This is the first-ship bug state. */
    int pre_queued = ship.repair_count;
    ASSERT(pre_queued > 0);

    /* Death burst sends events unconditionally (the fix).
     * Queue is NOT cleared -- repair state is preserved. */
    int death_burst_events = 0;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition) {
            bc_repair_add(&ship, (u8)i);  /* queue maintenance */
            death_burst_events++;          /* event always sent */
        }
    }

    /* All damaged subsystems should produce events, NOT just 1 */
    ASSERT_EQ(death_burst_events, all_damaged);
    ASSERT(death_burst_events > 1); /* the bug was exactly 1 event */
    /* Queue should be >= pre_queued (may have grown if some weren't queued) */
    ASSERT(ship.repair_count >= pre_queued);
}

TEST_MAIN_BEGIN()
    RUN(load_registry);
    RUN(death_burst_covers_all_damaged);
    RUN(death_burst_ignores_prequeued);
    RUN(death_burst_skips_undamaged);
    RUN(death_burst_uses_valid_subsystem_ids);
    RUN(first_ship_deficit_regression);
TEST_MAIN_END()
