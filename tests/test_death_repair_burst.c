/* Issue #61: death repair burst unit tests.
 * Verifies that collision death sends ADD_TO_REPAIR_LIST for all damaged
 * subsystems without bucket limiting, and that bc_repair_add() deduplication
 * prevents double-sends for subsystems already queued. */

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

/* Death burst covers all damaged subsystems, not just bucket-limited ones.
 * Pre-damage 10 subsystems, queue 3 (simulating bucket limiting), then
 * verify the death burst picks up the remaining 7. */
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

    /* Death burst: iterate all subsystems, add damaged ones to repair queue.
     * This mimics the generate_death_repair_events() logic. */
    int death_burst_events = 0;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition) {
            if (bc_repair_add(&ship, (u8)i)) {
                death_burst_events++;
            }
        }
    }

    /* 10 damaged - 3 pre-queued = 7 new events from death burst */
    ASSERT_EQ(death_burst_events, 7);
    ASSERT_EQ(ship.repair_count, 10);
}

/* When all damaged subsystems are already queued, the death burst should
 * produce zero additional events (deduplication). */
TEST(death_burst_deduplicates)
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

    /* Queue ALL damaged subsystems (simulating pre-death events covered all) */
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition)
            bc_repair_add(&ship, (u8)i);
    }
    ASSERT_EQ(ship.repair_count, total_damaged);

    /* Death burst should produce zero new events */
    int death_burst_events = 0;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition) {
            if (bc_repair_add(&ship, (u8)i))
                death_burst_events++;
        }
    }
    ASSERT_EQ(death_burst_events, 0);
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

    /* Death burst should only produce 2 events */
    int death_burst_events = 0;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship.subsystem_hp[i] < cls->subsystems[i].max_condition) {
            if (bc_repair_add(&ship, (u8)i))
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

TEST_MAIN_BEGIN()
    RUN(load_registry);
    RUN(death_burst_covers_all_damaged);
    RUN(death_burst_deduplicates);
    RUN(death_burst_skips_undamaged);
    RUN(death_burst_uses_valid_subsystem_ids);
TEST_MAIN_END()
