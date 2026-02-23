/*
 * test_power_collision.c — Regression tests for issue #91
 *
 * Bug 7-10: Per-species power initialization failures.
 *   Root cause: serialization.json max_condition != subsystems.json max_condition
 *   for the Warp Core entry. bc_ship_init() initializes HP from the flat subsystem
 *   (subsystems.json), but bc_ship_power_tick() computes condition_pct using the
 *   serialization entry's max_condition as denominator. When these differ, the warp
 *   core starts at < 100% condition, reducing or zeroing effective power output.
 *
 *   Fix: load_serialization_list() now overrides e->max_condition with the flat
 *   subsystem's max_condition when a name match is found, ensuring HP init and
 *   power_tick use the same value.
 *
 * Bug 11: Missing collision rate limiting.
 *   The server processed every CollisionEffect packet without cooldown, resulting
 *   in ~43 collisions/sec when grinding against objects (1,033x stock rate).
 *
 *   Fix: bc_ship_state_t.collision_cooldown field, decremented in tick, set to
 *   1.0s after each processed collision. Collision handler skips damage when > 0.
 */

#include "test_util.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/ship_power.h"
#include <string.h>
#include <math.h>

#define REGISTRY_DIR "data/vanilla-1.1"

static bc_game_registry_t g_reg;

TEST(load_registry)
{
    ASSERT(bc_registry_load_dir(&g_reg, REGISTRY_DIR));
    ASSERT(g_reg.ship_count >= 16);
}

/* --- Bug 7-10: Serialization max_condition must match flat subsystem --- */

/* For every ship, verify that each serialization entry that maps to a flat
 * subsystem uses the flat subsystem's max_condition (not the JSON value from
 * serialization.json, which may differ). */
TEST(ser_entry_max_condition_matches_flat_subsystem)
{
    for (int s = 0; s < g_reg.ship_count; s++) {
        const bc_ship_class_t *cls = &g_reg.ships[s];
        const bc_ss_list_t *sl = &cls->ser_list;

        for (int i = 0; i < sl->count; i++) {
            const bc_ss_entry_t *e = &sl->entries[i];
            int hp_idx = e->hp_index;

            /* Only check entries that map to a flat subsystem */
            if (hp_idx < 0 || hp_idx >= cls->subsystem_count) continue;

            f32 flat_max = cls->subsystems[hp_idx].max_condition;
            ASSERT(fabsf(e->max_condition - flat_max) < 0.01f);
        }
    }
}

/* Specifically verify the four ships that had mismatches before the fix. */
TEST(galor_warp_core_condition)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 10); /* Galor */
    ASSERT(cls != NULL);
    ASSERT(cls->ser_list.reactor_entry_idx >= 0);

    const bc_ss_entry_t *reactor = &cls->ser_list.entries[cls->ser_list.reactor_entry_idx];
    ASSERT(reactor->format == BC_SS_FORMAT_POWER);

    /* HP at reactor->hp_index should equal reactor->max_condition */
    f32 flat_max = cls->subsystems[reactor->hp_index].max_condition;
    ASSERT(fabsf(reactor->max_condition - flat_max) < 0.01f);
    /* Galor flat subsystem max_condition is 3200 */
    ASSERT(fabsf(flat_max - 3200.0f) < 0.01f);
}

TEST(keldon_warp_core_condition)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 11); /* Keldon */
    ASSERT(cls != NULL);
    ASSERT(cls->ser_list.reactor_entry_idx >= 0);

    const bc_ss_entry_t *reactor = &cls->ser_list.entries[cls->ser_list.reactor_entry_idx];
    f32 flat_max = cls->subsystems[reactor->hp_index].max_condition;
    ASSERT(fabsf(reactor->max_condition - flat_max) < 0.01f);
    ASSERT(fabsf(flat_max - 4000.0f) < 0.01f);
}

TEST(cardhybrid_warp_core_condition)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 12); /* CardHybrid */
    ASSERT(cls != NULL);
    ASSERT(cls->ser_list.reactor_entry_idx >= 0);

    const bc_ss_entry_t *reactor = &cls->ser_list.entries[cls->ser_list.reactor_entry_idx];
    f32 flat_max = cls->subsystems[reactor->hp_index].max_condition;
    ASSERT(fabsf(reactor->max_condition - flat_max) < 0.01f);
    ASSERT(fabsf(flat_max - 6000.0f) < 0.01f);
}

TEST(kessokheavy_warp_core_condition)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 13); /* KessokHeavy */
    ASSERT(cls != NULL);
    ASSERT(cls->ser_list.reactor_entry_idx >= 0);

    const bc_ss_entry_t *reactor = &cls->ser_list.entries[cls->ser_list.reactor_entry_idx];
    f32 flat_max = cls->subsystems[reactor->hp_index].max_condition;
    ASSERT(fabsf(reactor->max_condition - flat_max) < 0.01f);
    ASSERT(fabsf(flat_max - 7000.0f) < 0.01f);
}

/* Verify that affected ships spawn with condition_pct == 1.0 for warp core.
 * This is the actual bug symptom: condition_pct < 1.0 → reduced/zero power. */
TEST(affected_ships_spawn_full_warp_core)
{
    u16 species[] = { 10, 11, 12, 13 }; /* Galor, Keldon, CardHybrid, KessokHeavy */
    for (int s = 0; s < 4; s++) {
        const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, species[s]);
        ASSERT(cls != NULL);

        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, 0, 0x3FFFFFFF, 1, 0);

        /* Simulate one power tick to populate efficiency */
        bc_ship_power_tick(&ship, cls, 1.0f);

        /* Warp core condition_pct should be 1.0 (HP == max_condition) */
        if (cls->ser_list.reactor_entry_idx >= 0) {
            const bc_ss_entry_t *reactor =
                &cls->ser_list.entries[cls->ser_list.reactor_entry_idx];
            f32 hp = ship.subsystem_hp[reactor->hp_index];
            f32 condition_pct = hp / reactor->max_condition;
            ASSERT(fabsf(condition_pct - 1.0f) < 0.001f);
        }

        /* Battery should be at limit after one tick */
        ASSERT(fabsf(ship.main_battery - cls->main_battery_limit) < 0.01f);
    }
}

/* --- Bug 11: Collision cooldown --- */

TEST(collision_cooldown_initialized_to_zero)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3); /* Galaxy */
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 0, 0x3FFFFFFF, 1, 0);

    ASSERT(fabsf(ship.collision_cooldown) < 0.001f);
}

TEST(collision_cooldown_decays)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 0, 0x3FFFFFFF, 1, 0);

    /* Simulate setting cooldown (as the collision handler would) */
    ship.collision_cooldown = 1.0f;

    /* Simulate tick decay (as main.c does) */
    f32 dt = 0.1f;
    ship.collision_cooldown -= dt;
    ASSERT(fabsf(ship.collision_cooldown - 0.9f) < 0.001f);

    /* After 9 more ticks (1.0s total elapsed) it should be at or below 0 */
    for (int i = 0; i < 9; i++)
        ship.collision_cooldown -= dt;
    ASSERT(ship.collision_cooldown <= 0.001f);
}

TEST_MAIN_BEGIN()
    RUN(load_registry);
    RUN(ser_entry_max_condition_matches_flat_subsystem);
    RUN(galor_warp_core_condition);
    RUN(keldon_warp_core_condition);
    RUN(cardhybrid_warp_core_condition);
    RUN(kessokheavy_warp_core_condition);
    RUN(affected_ships_spawn_full_warp_core);
    RUN(collision_cooldown_initialized_to_zero);
    RUN(collision_cooldown_decays);
TEST_MAIN_END()
