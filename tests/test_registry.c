#include "test_util.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/game_builders.h"
#include "openbc/opcodes.h"
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

/* === Ship state === */

TEST(ship_init_galaxy)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    i32 oid = bc_make_ship_id(0);
    bc_ship_init(&ship, cls, 2, oid, 0, 1);

    ASSERT(ship.alive);
    ASSERT(fabsf(ship.hull_hp - 15000.0f) < 1.0f);
    ASSERT(fabsf(ship.shield_hp[0] - 8000.0f) < 1.0f);
    ASSERT_EQ(ship.owner_slot, 0);
    ASSERT_EQ(ship.team_id, 1);
    ASSERT_EQ(ship.cloak_state, BC_CLOAK_DECLOAKED);
    ASSERT_EQ(ship.tractor_target_id, -1);

    /* All subsystems at full HP */
    for (int i = 0; i < cls->subsystem_count; i++) {
        ASSERT(fabsf(ship.subsystem_hp[i] - cls->subsystems[i].max_condition) < 1.0f);
    }
}

TEST(ship_init_all_types)
{
    /* Init all 16 ship types, verify HP matches registry */
    for (int i = 0; i < g_reg.ship_count; i++) {
        const bc_ship_class_t *cls = bc_registry_get_ship(&g_reg, i);
        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, i, bc_make_ship_id(i), (u8)i, 0);
        ASSERT(ship.alive);
        ASSERT(fabsf(ship.hull_hp - cls->hull_hp) < 1.0f);
    }
}

TEST(ship_serialize_galaxy)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 1);
    ship.pos = (bc_vec3_t){100.0f, 200.0f, 300.0f};

    u8 blob[1024];
    int len = bc_ship_serialize(&ship, cls, blob, (int)sizeof(blob));
    ASSERT(len > 0);
    /* Minimum: 4(oid) + 2(species) + 12(pos) + 16(quat) + 24(fwd+up)
     * + 4(speed) + 4(hull) + 24(shields) + 2(ss_count) + 4*N(ss_hp) + 2 */
    ASSERT(len > 80);
}

TEST(ship_create_packet)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 1);

    u8 pkt[1024];
    int len = bc_ship_build_create_packet(&ship, cls, pkt, (int)sizeof(pkt));
    ASSERT(len > 0);
    ASSERT_EQ(pkt[0], BC_OP_OBJ_CREATE_TEAM);
    ASSERT_EQ(pkt[1], 0); /* owner slot */
    ASSERT_EQ(pkt[2], 1); /* team id */
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
    RUN(ship_init_galaxy);
    RUN(ship_init_all_types);
    RUN(ship_serialize_galaxy);
    RUN(ship_create_packet);
TEST_MAIN_END()
