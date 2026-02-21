#include "test_util.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/movement.h"
#include "openbc/combat.h"
#include "openbc/game_builders.h"
#include "openbc/opcodes.h"
#include <string.h>
#include <math.h>

#define REGISTRY_PATH "data/vanilla-1.1.json"

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

/* === Movement === */

TEST(move_tick_advances_position)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    ship.pos = (bc_vec3_t){0, 0, 0};
    ship.fwd = (bc_vec3_t){0, 1, 0};
    bc_ship_set_speed(&ship, cls, 5.0f);

    bc_ship_move_tick(&ship, 1.0f, 1.0f);
    ASSERT(fabsf(ship.pos.y - 5.0f) < 0.01f);
    ASSERT(fabsf(ship.pos.x) < 0.01f);

    bc_ship_move_tick(&ship, 1.0f, 0.5f);
    ASSERT(fabsf(ship.pos.y - 7.5f) < 0.01f);
}

TEST(speed_clamped_to_max)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    bc_ship_set_speed(&ship, cls, 999.0f);
    ASSERT(fabsf(ship.speed - cls->max_speed) < 0.01f);

    bc_ship_set_speed(&ship, cls, -10.0f);
    ASSERT(fabsf(ship.speed) < 0.01f);
}

TEST(turn_toward_respects_rate)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    ship.pos = (bc_vec3_t){0, 0, 0};
    ship.fwd = (bc_vec3_t){0, 1, 0};

    /* Target is to the right (+X) */
    bc_vec3_t target = {100, 0, 0};
    bc_ship_turn_toward(&ship, cls, target, 0.1f);

    /* Ship should have started turning but not fully turned */
    ASSERT(ship.fwd.x > 0.0f); /* started turning right */
    ASSERT(ship.fwd.y > 0.0f); /* still has forward component */
}

TEST(state_update_dirty_flags)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t prev, cur;
    bc_ship_init(&prev, cls, 2, bc_make_ship_id(0), 0, 0);
    memcpy(&cur, &prev, sizeof(cur));

    /* No change -> 0 bytes */
    u8 buf[256];
    int len = bc_ship_build_state_update(&cur, &prev, 1.0f, buf, sizeof(buf));
    ASSERT_EQ(len, 0);

    /* Move position */
    cur.pos.x = 100.0f;
    len = bc_ship_build_state_update(&cur, &prev, 1.0f, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT_EQ(buf[0], 0x1C); /* StateUpdate opcode */
    /* dirty flags at offset 9 should have POS_ABS bit set */
    ASSERT(buf[9] & BC_DIRTY_POS_ABS);
}

TEST(state_update_speed_change)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t prev, cur;
    bc_ship_init(&prev, cls, 2, bc_make_ship_id(0), 0, 0);
    memcpy(&cur, &prev, sizeof(cur));

    cur.speed = 5.0f;
    u8 buf[256];
    int len = bc_ship_build_state_update(&cur, &prev, 1.0f, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(buf[9] & BC_DIRTY_SPEED);
    ASSERT(!(buf[9] & BC_DIRTY_POS_ABS)); /* position didn't change */
}

/* === Combat: Phaser fire === */

TEST(phaser_requires_min_charge)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    /* Full charge -> can fire */
    ASSERT(bc_combat_can_fire_phaser(&ship, cls, 0));

    /* Deplete charge */
    ship.phaser_charge[0] = 0.0f;
    ASSERT(!bc_combat_can_fire_phaser(&ship, cls, 0));

    /* Partial charge below min -> still can't */
    ship.phaser_charge[0] = 1.0f;
    ASSERT(!bc_combat_can_fire_phaser(&ship, cls, 0));
}

TEST(phaser_fire_resets_charge)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    u8 buf[128];
    int len = bc_combat_fire_phaser(&ship, cls, 0, -1, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(ship.phaser_charge[0] < 0.01f); /* charge reset */
}

TEST(cloaked_cannot_fire)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    ship.cloak_state = BC_CLOAK_CLOAKED;
    ASSERT(!bc_combat_can_fire_phaser(&ship, cls, 0));
    ASSERT(!bc_combat_can_fire_torpedo(&ship, cls, 0));
}

/* === Combat: Torpedo fire === */

TEST(torpedo_respects_cooldown)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    /* Initially ready */
    ASSERT(bc_combat_can_fire_torpedo(&ship, cls, 0));

    /* Fire */
    u8 buf[128];
    bc_vec3_t dir = {0, 1, 0};
    int len = bc_combat_fire_torpedo(&ship, cls, 0, -1, dir, buf, sizeof(buf));
    ASSERT(len > 0);

    /* Now on cooldown */
    ASSERT(!bc_combat_can_fire_torpedo(&ship, cls, 0));
    ASSERT(ship.torpedo_cooldown[0] > 0.0f);

    /* After enough ticks, ready again */
    bc_combat_torpedo_tick(&ship, cls, 999.0f);
    ASSERT(bc_combat_can_fire_torpedo(&ship, cls, 0));
}

TEST(torpedo_type_switch_imposes_reload)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    bc_combat_switch_torpedo_type(&ship, cls, 3); /* switch to quantum */
    ASSERT(ship.torpedo_switching);
    ASSERT(!bc_combat_can_fire_torpedo(&ship, cls, 0));

    /* After timer expires */
    bc_combat_torpedo_tick(&ship, cls, 999.0f);
    ASSERT(!ship.torpedo_switching);
    ASSERT(bc_combat_can_fire_torpedo(&ship, cls, 0));
}

/* === Combat: Damage === */

TEST(shield_absorbs_damage)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    f32 hull_before = ship.hull_hp;
    f32 front_before = ship.shield_hp[BC_SHIELD_FRONT];

    /* Hit from the front (impact_dir = -fwd, i.e. toward the ship's front) */
    bc_vec3_t impact = {0, 1, 0}; /* toward the ship's forward */
    bc_combat_apply_damage(&ship, cls, 100.0f, 0.0f, impact, false, 1.0f);

    /* Shield should have absorbed it */
    ASSERT(ship.shield_hp[BC_SHIELD_FRONT] < front_before);
    ASSERT(fabsf(ship.hull_hp - hull_before) < 0.01f); /* hull untouched */
}

TEST(overflow_penetrates_hull)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    /* Drop front shields */
    ship.shield_hp[BC_SHIELD_FRONT] = 10.0f;
    f32 hull_before = ship.hull_hp;

    bc_vec3_t impact = {0, 1, 0};
    bc_combat_apply_damage(&ship, cls, 100.0f, 0.0f, impact, false, 1.0f);

    /* Shield gone, hull damaged by overflow */
    ASSERT(ship.shield_hp[BC_SHIELD_FRONT] < 0.01f);
    ASSERT(ship.hull_hp < hull_before);
    f32 expected_hull = hull_before - 90.0f; /* 100 - 10 shield */
    ASSERT(fabsf(ship.hull_hp - expected_hull) < 1.0f);
}

TEST(hull_zero_means_death)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 15); /* Shuttle: 1600 HP */
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 14, bc_make_ship_id(0), 0, 0);

    /* Strip all shields */
    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) ship.shield_hp[i] = 0;

    bc_vec3_t impact = {0, 1, 0};
    bc_combat_apply_damage(&ship, cls, 99999.0f, 0.0f, impact, false, 1.0f);
    ASSERT(!ship.alive);
    ASSERT(ship.hull_hp < 0.01f);
}

TEST(shield_recharge_tick)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    ship.shield_hp[0] = 0.0f;
    bc_combat_shield_tick(&ship, cls, 1.0f, 1.0f);
    ASSERT(ship.shield_hp[0] > 0.0f); /* recharging */
    ASSERT(ship.shield_hp[0] <= cls->shield_hp[0]); /* not over max */
}

TEST(charge_tick_recharges)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    ship.phaser_charge[0] = 0.0f;
    bc_combat_charge_tick(&ship, cls, 1.0f, 1.0f);
    ASSERT(ship.phaser_charge[0] > 0.0f);
}

/* === Cloaking tests === */

TEST(cloak_full_cycle)
{
    /* BirdOfPrey (species 6) can cloak */
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 6);
    ASSERT(cls != NULL);
    ASSERT(cls->can_cloak);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 5, bc_make_ship_id(0), 0, 0);
    ASSERT_EQ((int)ship.cloak_state, BC_CLOAK_DECLOAKED);

    /* Start cloaking */
    ASSERT(bc_cloak_start(&ship, cls));
    ASSERT_EQ((int)ship.cloak_state, BC_CLOAK_CLOAKING);
    ASSERT(ship.shield_hp[0] > 0.0f); /* shields preserved (functionally disabled) */

    /* Can't fire while cloaking */
    ASSERT(!bc_cloak_can_fire(&ship));
    ASSERT(!bc_cloak_shields_active(&ship));

    /* Tick through transition */
    bc_cloak_tick(&ship, BC_CLOAK_TRANSITION_TIME + 0.1f);
    ASSERT_EQ((int)ship.cloak_state, BC_CLOAK_CLOAKED);

    /* Still can't fire while cloaked */
    ASSERT(!bc_cloak_can_fire(&ship));

    /* Start decloaking */
    ASSERT(bc_cloak_stop(&ship));
    ASSERT_EQ((int)ship.cloak_state, BC_CLOAK_DECLOAKING);

    /* Vulnerability window: visible but no shields/weapons */
    ASSERT(!bc_cloak_can_fire(&ship));
    ASSERT(!bc_cloak_shields_active(&ship));

    /* Tick through decloak transition */
    bc_cloak_tick(&ship, BC_CLOAK_TRANSITION_TIME + 0.1f);
    ASSERT_EQ((int)ship.cloak_state, BC_CLOAK_DECLOAKED);

    /* Now can fire and shields active again */
    ASSERT(bc_cloak_can_fire(&ship));
    ASSERT(bc_cloak_shields_active(&ship));
}

TEST(non_cloaker_cannot_cloak)
{
    /* Galaxy (species 3) has no cloak */
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(!cls->can_cloak);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    ASSERT(!bc_cloak_start(&ship, cls));
    ASSERT_EQ((int)ship.cloak_state, BC_CLOAK_DECLOAKED);
}

TEST(cloak_prevents_phaser_fire)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 6);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 5, bc_make_ship_id(0), 0, 0);

    /* Verify can fire when decloaked */
    ASSERT(bc_combat_can_fire_phaser(&ship, cls, 0));

    /* Cloak and verify can't fire */
    bc_cloak_start(&ship, cls);
    ASSERT(!bc_combat_can_fire_phaser(&ship, cls, 0));

    /* Complete cloak, still can't fire */
    bc_cloak_tick(&ship, BC_CLOAK_TRANSITION_TIME + 0.1f);
    ASSERT(!bc_combat_can_fire_phaser(&ship, cls, 0));

    /* Decloak, still can't fire during transition */
    bc_cloak_stop(&ship);
    ASSERT(!bc_combat_can_fire_phaser(&ship, cls, 0));

    /* Complete decloak, now can fire */
    bc_cloak_tick(&ship, BC_CLOAK_TRANSITION_TIME + 0.1f);
    ASSERT(bc_combat_can_fire_phaser(&ship, cls, 0));
}

TEST(cloak_no_charge_while_cloaked)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 6);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 5, bc_make_ship_id(0), 0, 0);

    ship.phaser_charge[0] = 0.0f;
    bc_cloak_start(&ship, cls);
    bc_cloak_tick(&ship, BC_CLOAK_TRANSITION_TIME + 0.1f); /* fully cloaked */

    bc_combat_charge_tick(&ship, cls, 1.0f, 5.0f);
    ASSERT(ship.phaser_charge[0] == 0.0f); /* no recharge while cloaked */
}

TEST(cloak_disabled_subsystem_prevents_cloak)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 6);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 5, bc_make_ship_id(0), 0, 0);

    /* Find and destroy the cloaking subsystem */
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, "cloak") == 0) {
            ship.subsystem_hp[i] = 0.0f;
            break;
        }
    }

    ASSERT(!bc_cloak_start(&ship, cls));
    ASSERT_EQ((int)ship.cloak_state, BC_CLOAK_DECLOAKED);
}

/* === Tractor beam tests === */

TEST(tractor_engage_disengage)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3); /* Galaxy */
    ASSERT(cls->has_tractor);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    ASSERT(bc_combat_can_tractor(&ship, cls, 0));
    int ss = bc_combat_tractor_engage(&ship, cls, 0, 42);
    ASSERT(ss >= 0);
    ASSERT_EQ(ship.tractor_target_id, 42);

    /* Can't engage again while locked */
    ASSERT(!bc_combat_can_tractor(&ship, cls, 0));

    bc_combat_tractor_disengage(&ship);
    ASSERT_EQ(ship.tractor_target_id, -1);
    ASSERT(bc_combat_can_tractor(&ship, cls, 0));
}

TEST(tractor_applies_drag)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship, target;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    bc_ship_init(&target, cls, 2, bc_make_ship_id(1), 1, 1);

    /* Place target within tractor range */
    ship.pos = (bc_vec3_t){0, 0, 0};
    target.pos = (bc_vec3_t){10, 0, 0};
    target.speed = 50.0f;

    bc_combat_tractor_engage(&ship, cls, 0, target.object_id);
    f32 orig_speed = target.speed;
    bc_combat_tractor_tick(&ship, &target, cls, 1.0f);
    ASSERT(target.speed < orig_speed); /* drag applied */
}

TEST(tractor_auto_releases_out_of_range)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship, target;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    bc_ship_init(&target, cls, 2, bc_make_ship_id(1), 1, 1);

    ship.pos = (bc_vec3_t){0, 0, 0};
    target.pos = (bc_vec3_t){200, 0, 0}; /* beyond max_damage_distance (100) */

    bc_combat_tractor_engage(&ship, cls, 0, target.object_id);
    bc_combat_tractor_tick(&ship, &target, cls, 1.0f);
    ASSERT_EQ(ship.tractor_target_id, -1); /* auto-released */
}

TEST(no_tractor_when_cloaked)
{
    /* BOP has both tractor and cloak */
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 6);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 5, bc_make_ship_id(0), 0, 0);

    if (cls->has_tractor) {
        ASSERT(bc_combat_can_tractor(&ship, cls, 0));
        bc_cloak_start(&ship, cls);
        ASSERT(!bc_combat_can_tractor(&ship, cls, 0));
    }
}

/* === Repair system tests === */

TEST(repair_heals_subsystem)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    /* Damage subsystem 0 */
    f32 orig = ship.subsystem_hp[0];
    ship.subsystem_hp[0] = orig * 0.1f; /* 10% HP */

    bc_repair_add(&ship, 0);
    ASSERT_EQ(ship.repair_count, 1);

    f32 before = ship.subsystem_hp[0];
    bc_repair_tick(&ship, cls, 1.0f);
    ASSERT(ship.subsystem_hp[0] > before); /* healed */
}

TEST(repair_removes_when_full)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    /* Slightly damaged */
    f32 max_hp = cls->subsystems[0].max_condition;
    ship.subsystem_hp[0] = max_hp - 1.0f;

    bc_repair_add(&ship, 0);
    /* Repair for long time -> should fully heal and auto-remove */
    bc_repair_tick(&ship, cls, 100.0f);
    ASSERT(ship.subsystem_hp[0] >= max_hp - 0.01f);
    ASSERT_EQ(ship.repair_count, 0); /* removed from queue */
}

TEST(repair_auto_queue_disabled)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    /* Damage a subsystem below disabled threshold */
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (cls->subsystems[i].disabled_pct > 0.0f) {
            f32 threshold = cls->subsystems[i].max_condition *
                            (1.0f - cls->subsystems[i].disabled_pct);
            ship.subsystem_hp[i] = threshold - 1.0f;
            break;
        }
    }

    bc_repair_auto_queue(&ship, cls);
    ASSERT(ship.repair_count > 0);
}

TEST(repair_no_duplicates)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    ASSERT(bc_repair_add(&ship, 0));
    ASSERT(!bc_repair_add(&ship, 0)); /* duplicate rejected */
    ASSERT_EQ(ship.repair_count, 1);
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
    RUN(move_tick_advances_position);
    RUN(speed_clamped_to_max);
    RUN(turn_toward_respects_rate);
    RUN(state_update_dirty_flags);
    RUN(state_update_speed_change);
    RUN(phaser_requires_min_charge);
    RUN(phaser_fire_resets_charge);
    RUN(cloaked_cannot_fire);
    RUN(torpedo_respects_cooldown);
    RUN(torpedo_type_switch_imposes_reload);
    RUN(shield_absorbs_damage);
    RUN(overflow_penetrates_hull);
    RUN(hull_zero_means_death);
    RUN(shield_recharge_tick);
    RUN(charge_tick_recharges);
    RUN(cloak_full_cycle);
    RUN(non_cloaker_cannot_cloak);
    RUN(cloak_prevents_phaser_fire);
    RUN(cloak_no_charge_while_cloaked);
    RUN(cloak_disabled_subsystem_prevents_cloak);
    RUN(tractor_engage_disengage);
    RUN(tractor_applies_drag);
    RUN(tractor_auto_releases_out_of_range);
    RUN(no_tractor_when_cloaked);
    RUN(repair_heals_subsystem);
    RUN(repair_removes_when_full);
    RUN(repair_auto_queue_disabled);
    RUN(repair_no_duplicates);
TEST_MAIN_END()
