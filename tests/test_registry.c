#include "test_util.h"
#include "openbc/ship_data.h"
#include <string.h>
#include <math.h>

#define REGISTRY_PATH "data\\vanilla-1.1.json"

static bc_game_registry_t g_reg;

/* === Load registry === */

TEST(load_registry)
{
    ASSERT(bc_registry_load(&g_reg, REGISTRY_PATH));
    ASSERT(g_reg.loaded);
    ASSERT_EQ(g_reg.ship_count, 16);
    ASSERT_EQ(g_reg.projectile_count, 15);
}

/* === Ship lookups === */

TEST(galaxy_stats)
{
    const bc_ship_class_t *s = bc_registry_find_ship(&g_reg, 3); /* Galaxy */
    ASSERT(s != NULL);
    ASSERT(strcmp(s->name, "Galaxy") == 0);
    ASSERT(strcmp(s->faction, "Federation") == 0);
    ASSERT(fabsf(s->hull_hp - 15000.0f) < 1.0f);
    ASSERT(fabsf(s->mass - 120.0f) < 1.0f);
    ASSERT(fabsf(s->max_speed - 6.3f) < 0.1f);
    ASSERT(!s->can_cloak);
    ASSERT(s->has_tractor);
    ASSERT_EQ(s->torpedo_tubes, 6);
    ASSERT_EQ(s->phaser_banks, 8);
    ASSERT_EQ(s->tractor_beams, 4);
    ASSERT(s->subsystem_count > 20);

    /* Shield HP: front=8000, others=4000 */
    ASSERT(fabsf(s->shield_hp[0] - 8000.0f) < 1.0f);
    ASSERT(fabsf(s->shield_hp[1] - 4000.0f) < 1.0f);
}

TEST(shuttle_stats)
{
    const bc_ship_class_t *s = bc_registry_find_ship(&g_reg, 15); /* Shuttle */
    ASSERT(s != NULL);
    ASSERT(strcmp(s->name, "Shuttle") == 0);
    ASSERT(fabsf(s->hull_hp - 1600.0f) < 1.0f);
    ASSERT(fabsf(s->mass - 15.0f) < 1.0f);
    ASSERT_EQ(s->phaser_banks, 1);
    ASSERT_EQ(s->torpedo_tubes, 0);
}

TEST(bop_cloak)
{
    const bc_ship_class_t *s = bc_registry_find_ship(&g_reg, 6); /* BirdOfPrey */
    ASSERT(s != NULL);
    ASSERT(s->can_cloak);
    ASSERT(!s->has_tractor);
    ASSERT_EQ(s->pulse_weapons, 2);
    ASSERT(fabsf(s->hull_hp - 4000.0f) < 1.0f);
}

TEST(sovereign_shields)
{
    const bc_ship_class_t *s = bc_registry_find_ship(&g_reg, 5); /* Sovereign */
    ASSERT(s != NULL);
    /* Front shields = 11000 */
    ASSERT(fabsf(s->shield_hp[0] - 11000.0f) < 1.0f);
    /* Total shields = 49500 */
    f32 total = 0;
    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) total += s->shield_hp[i];
    ASSERT(fabsf(total - 49500.0f) < 1.0f);
}

TEST(all_ships_present)
{
    for (int i = 0; i < g_reg.ship_count; i++) {
        const bc_ship_class_t *s = bc_registry_get_ship(&g_reg, i);
        ASSERT(s != NULL);
        ASSERT(s->species_id >= 1 && s->species_id <= 16);
        ASSERT(s->hull_hp > 0);
        ASSERT(s->mass > 0);
        ASSERT(s->subsystem_count > 0);
    }
}

/* === Projectile lookups === */

TEST(photon_torpedo)
{
    const bc_projectile_def_t *p = bc_registry_get_projectile(&g_reg, 2); /* PHOTON */
    ASSERT(p != NULL);
    ASSERT(strcmp(p->name, "Photon") == 0);
    ASSERT(fabsf(p->damage - 500.0f) < 1.0f);
    ASSERT(fabsf(p->launch_speed - 19.0f) < 0.1f);
    ASSERT(fabsf(p->guidance_lifetime - 6.0f) < 0.1f);
}

TEST(disruptor_unguided)
{
    const bc_projectile_def_t *p = bc_registry_get_projectile(&g_reg, 1); /* DISRUPTOR */
    ASSERT(p != NULL);
    ASSERT(fabsf(p->damage - 400.0f) < 1.0f);
    ASSERT(fabsf(p->guidance_lifetime) < 0.01f); /* unguided */
    ASSERT(fabsf(p->lifetime - 8.0f) < 0.1f);
}

TEST(kessok_disruptor_massive)
{
    const bc_projectile_def_t *p = bc_registry_get_projectile(&g_reg, 11); /* KESSOK */
    ASSERT(p != NULL);
    ASSERT(fabsf(p->damage - 2000.0f) < 1.0f);
}

TEST(all_projectiles_present)
{
    for (int i = 0; i < g_reg.projectile_count; i++) {
        ASSERT(g_reg.projectiles[i].net_type_id >= 1);
        ASSERT(g_reg.projectiles[i].damage > 0);
        ASSERT(g_reg.projectiles[i].launch_speed > 0);
    }
}

/* === Subsystem details === */

TEST(galaxy_subsystem_phaser)
{
    const bc_ship_class_t *s = bc_registry_find_ship(&g_reg, 3); /* Galaxy */
    ASSERT(s != NULL);

    /* Find first phaser subsystem */
    const bc_subsystem_def_t *phaser = NULL;
    for (int i = 0; i < s->subsystem_count; i++) {
        if (strcmp(s->subsystems[i].type, "phaser") == 0) {
            phaser = &s->subsystems[i];
            break;
        }
    }
    ASSERT(phaser != NULL);
    ASSERT(phaser->max_damage > 0);
    ASSERT(phaser->max_charge > 0);
    ASSERT(phaser->max_damage_distance > 0);
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    RUN(load_registry);
    RUN(galaxy_stats);
    RUN(shuttle_stats);
    RUN(bop_cloak);
    RUN(sovereign_shields);
    RUN(all_ships_present);
    RUN(photon_torpedo);
    RUN(disruptor_unguided);
    RUN(kessok_disruptor_massive);
    RUN(all_projectiles_present);
    RUN(galaxy_subsystem_phaser);
TEST_MAIN_END()
