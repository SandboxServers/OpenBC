#include "test_util.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/game_builders.h"
#include "openbc/game_events.h"
#include <string.h>
#include <math.h>

#define REGISTRY_DIR  "data/vanilla-1.1"

static bc_game_registry_t g_reg;

TEST(load_registry)
{
    ASSERT(bc_registry_load_dir(&g_reg, REGISTRY_DIR));
    ASSERT(g_reg.loaded);
    ASSERT(g_reg.ship_count > 0);
}

/* === Power init === */

TEST(power_init_batteries)
{
    for (int i = 0; i < g_reg.ship_count; i++) {
        const bc_ship_class_t *cls = bc_registry_get_ship(&g_reg, i);
        ASSERT(cls != NULL);

        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, i, bc_make_ship_id(0), 0, 0);

        ASSERT(fabsf(ship.main_battery - cls->main_battery_limit) < 0.01f);
        ASSERT(fabsf(ship.backup_battery - cls->backup_battery_limit) < 0.01f);
    }
}

TEST(power_init_conduits)
{
    for (int i = 0; i < g_reg.ship_count; i++) {
        const bc_ship_class_t *cls = bc_registry_get_ship(&g_reg, i);
        ASSERT(cls != NULL);

        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, i, bc_make_ship_id(0), 0, 0);

        ASSERT(fabsf(ship.main_conduit_remaining - cls->main_conduit_capacity) < 0.01f);
        ASSERT(fabsf(ship.backup_conduit_remaining - cls->backup_conduit_capacity) < 0.01f);
    }
}

TEST(power_init_allocations)
{
    for (int i = 0; i < g_reg.ship_count; i++) {
        const bc_ship_class_t *cls = bc_registry_get_ship(&g_reg, i);
        ASSERT(cls != NULL);

        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, i, bc_make_ship_id(0), 0, 0);

        const bc_ss_list_t *sl = &cls->ser_list;
        for (int j = 0; j < sl->count && j < BC_SS_MAX_ENTRIES; j++) {
            ASSERT_EQ(ship.power_pct[j], 100);
            ASSERT(ship.subsys_enabled[j]);
            ASSERT(fabsf(ship.efficiency[j] - 1.0f) < 0.01f);
        }
    }
}

/* === Object ID round-trip === */

TEST(object_id_round_trip)
{
    for (int slot = 0; slot < 6; slot++) {
        i32 oid = bc_make_ship_id(slot);
        int resolved = bc_object_id_to_slot(oid);
        ASSERT_EQ_INT(resolved, slot);
    }
}

/* === Subsystem IDs === */

TEST(subsystem_ids_all_assigned)
{
    for (int i = 0; i < g_reg.ship_count; i++) {
        const bc_ship_class_t *cls = bc_registry_get_ship(&g_reg, i);
        ASSERT(cls != NULL);

        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, i, bc_make_ship_id(0), 0, 0);
        bc_ship_assign_subsystem_ids(&ship, cls);

        /* Every flat subsystem referenced in the ser_list should have an ID */
        const bc_ss_list_t *sl = &cls->ser_list;
        for (int j = 0; j < sl->count; j++) {
            int idx = sl->entries[j].hp_index;
            if (idx >= 0 && idx < cls->subsystem_count && idx < BC_MAX_SUBSYSTEMS) {
                ASSERT(ship.subsys_obj_id[idx] > 0);
            }
            for (int c = 0; c < sl->entries[j].child_count; c++) {
                int cidx = sl->entries[j].child_hp_index[c];
                if (cidx >= 0 && cidx < cls->subsystem_count && cidx < BC_MAX_SUBSYSTEMS) {
                    ASSERT(ship.subsys_obj_id[cidx] > 0);
                }
            }
        }
    }
}

TEST(subsystem_ids_sequential)
{
    for (int i = 0; i < g_reg.ship_count; i++) {
        const bc_ship_class_t *cls = bc_registry_get_ship(&g_reg, i);
        ASSERT(cls != NULL);

        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, i, bc_make_ship_id(0), 0, 0);
        bc_ship_assign_subsystem_ids(&ship, cls);

        /* IDs should be sequential in ser_list traversal order */
        i32 expected = ship.object_id + 1;
        const bc_ss_list_t *sl = &cls->ser_list;
        for (int j = 0; j < sl->count; j++) {
            int idx = sl->entries[j].hp_index;
            if (idx >= 0 && idx < cls->subsystem_count && idx < BC_MAX_SUBSYSTEMS) {
                ASSERT_EQ_INT(ship.subsys_obj_id[idx], expected);
            }
            expected++;

            for (int c = 0; c < sl->entries[j].child_count; c++) {
                int cidx = sl->entries[j].child_hp_index[c];
                if (cidx >= 0 && cidx < cls->subsystem_count && cidx < BC_MAX_SUBSYSTEMS) {
                    ASSERT_EQ_INT(ship.subsys_obj_id[cidx], expected);
                }
                expected++;
            }
        }
    }
}

/* === Collision ownership === */

TEST(collision_ownership_ship)
{
    for (int slot = 0; slot < 6 && slot < g_reg.ship_count; slot++) {
        const bc_ship_class_t *cls = bc_registry_get_ship(&g_reg, slot);
        ASSERT(cls != NULL);

        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, slot, bc_make_ship_id(slot), (u8)slot, 0);

        int resolved = bc_object_id_to_slot(ship.object_id);
        ASSERT_EQ_INT(resolved, slot);
    }
}

TEST(collision_ownership_subsystem)
{
    for (int i = 0; i < g_reg.ship_count; i++) {
        const bc_ship_class_t *cls = bc_registry_get_ship(&g_reg, i);
        ASSERT(cls != NULL);

        int slot = i % 6;
        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, i, bc_make_ship_id(slot), (u8)slot, 0);
        bc_ship_assign_subsystem_ids(&ship, cls);

        /* Every assigned subsystem ID should resolve to the same owner slot */
        for (int j = 0; j < cls->subsystem_count && j < BC_MAX_SUBSYSTEMS; j++) {
            if (ship.subsys_obj_id[j] <= 0) continue;
            int resolved = bc_object_id_to_slot(ship.subsys_obj_id[j]);
            ASSERT_EQ_INT(resolved, slot);
        }
    }
}

TEST(collision_ownership_multiship)
{
    /* Two ships in different slots: object IDs resolve independently */
    const bc_ship_class_t *cls_a = bc_registry_get_ship(&g_reg, 0);
    const bc_ship_class_t *cls_b = bc_registry_get_ship(&g_reg, 1);
    ASSERT(cls_a != NULL);
    ASSERT(cls_b != NULL);

    bc_ship_state_t ship_a, ship_b;
    bc_ship_init(&ship_a, cls_a, 0, bc_make_ship_id(0), 0, 0);
    bc_ship_init(&ship_b, cls_b, 1, bc_make_ship_id(1), 1, 1);
    bc_ship_assign_subsystem_ids(&ship_a, cls_a);
    bc_ship_assign_subsystem_ids(&ship_b, cls_b);

    ASSERT_EQ_INT(bc_object_id_to_slot(ship_a.object_id), 0);
    ASSERT_EQ_INT(bc_object_id_to_slot(ship_b.object_id), 1);

    /* No subsystem ID overlap */
    for (int i = 0; i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship_a.subsys_obj_id[i] <= 0) continue;
        for (int j = 0; j < BC_MAX_SUBSYSTEMS; j++) {
            if (ship_b.subsys_obj_id[j] <= 0) continue;
            ASSERT(ship_a.subsys_obj_id[i] != ship_b.subsys_obj_id[j]);
        }
    }
}

/* === Hull + subsystem HP === */

TEST(hull_subsystem_hp)
{
    for (int i = 0; i < g_reg.ship_count; i++) {
        const bc_ship_class_t *cls = bc_registry_get_ship(&g_reg, i);
        ASSERT(cls != NULL);

        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, i, bc_make_ship_id(0), 0, 0);

        /* Hull matches registry */
        ASSERT(fabsf(ship.hull_hp - cls->hull_hp) < 1.0f);

        /* Flat subsystems at max HP */
        for (int j = 0; j < cls->subsystem_count && j < BC_MAX_SUBSYSTEMS; j++) {
            ASSERT(fabsf(ship.subsystem_hp[j] - cls->subsystems[j].max_condition) < 0.01f);
        }

        /* Container/child HP slots initialized from ser_list */
        const bc_ss_list_t *sl = &cls->ser_list;
        for (int j = 0; j < sl->count; j++) {
            const bc_ss_entry_t *e = &sl->entries[j];
            if (e->hp_index >= cls->subsystem_count && e->hp_index < BC_MAX_SUBSYSTEMS) {
                ASSERT(ship.subsystem_hp[e->hp_index] > 0.0f);
            }
            for (int c = 0; c < e->child_count; c++) {
                int cidx = e->child_hp_index[c];
                if (cidx >= cls->subsystem_count && cidx < BC_MAX_SUBSYSTEMS) {
                    ASSERT(ship.subsystem_hp[cidx] > 0.0f);
                }
            }
        }
    }
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    RUN(load_registry);
    RUN(power_init_batteries);
    RUN(power_init_conduits);
    RUN(power_init_allocations);
    RUN(object_id_round_trip);
    RUN(subsystem_ids_all_assigned);
    RUN(subsystem_ids_sequential);
    RUN(collision_ownership_ship);
    RUN(collision_ownership_subsystem);
    RUN(collision_ownership_multiship);
    RUN(hull_subsystem_hp);
TEST_MAIN_END()
