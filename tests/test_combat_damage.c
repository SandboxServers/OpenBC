/*
 * test_combat_damage.c — Regression tests for issue #87
 *
 * Verifies the three structural fixes to bc_combat_apply_damage:
 *
 *   A. Subsystems absorb BEFORE hull (ordering fix).
 *      Old: hull took full overflow; subsystems took overflow*0.5 additionally.
 *      New: subsystems absorb from overflow first; hull gets the remainder.
 *
 *   B. Full per-contact damage to each hit subsystem (no 0.5x multiplier),
 *      and no 25% propagation to parent containers.
 *
 *   C. search_radius parameter expands each subsystem's effective AABB radius
 *      for the hit test (was incorrectly used as shield HP multiplier).
 *
 * Test ship: Galaxy-class (species_id=3, loaded from registry).
 * Subsystems referenced:
 *   Forward Torpedo 1..4  torpedo_tube  pos=[0,-0.25,-0.25]  r=0.20  HP=2400
 *   Sensor Array          sensor        pos=[0,-0.45,-0.50]  r=0.28  HP=8000
 *   Shield Generator      shield        pos=[0,-0.40, 0.20]  r=0.50  HP=12000
 *   Hull                  hull          pos=[0,-1.50,-0.50]  r=1.00  HP=15000
 *
 * All tests use a default-oriented ship (fwd={0,1,0}, up={0,0,1}),
 * so local_impact == impact_dir inside bc_combat_apply_damage.
 */

#include "test_util.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/combat.h"
#include "openbc/game_builders.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#define REGISTRY_DIR "data/vanilla-1.1"

static bc_game_registry_t g_reg;

TEST(load_registry)
{
    ASSERT(bc_registry_load_dir(&g_reg, REGISTRY_DIR));
}

/*
 * Issue #87-A and #87-B: Ordering fix + full per-contact absorption.
 *
 * Galaxy, shields stripped, impact at Forward Torpedo position [0,-0.25,-0.25]
 * with damage_radius=0.1. Several subsystems are within this radius (4 torpedo
 * tubes at HP=2400 each, Sensor Array at HP=8000, Shield Generator at HP=12000).
 * damage = 3000 HP.
 *
 * OLD (bug): each tube took overflow*0.5 = 1500 HP (survived at 900 HP),
 *            hull took full 3000 HP damage regardless.
 *
 * NEW (fix): each tube absorbs min(3000, 2400) = 2400 HP independently;
 *            combined absorption easily exceeds 3000, so hull takes 0 damage.
 */
TEST(subsystem_absorbs_full_before_hull)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3); /* Galaxy */
    ASSERT(cls != NULL);

    /* Locate first torpedo tube (all forward tubes share [0,-0.25,-0.25]) */
    int torp_ss = -1;
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, "torpedo_tube") == 0 &&
            cls->subsystems[i].radius > 0.0f) {
            torp_ss = i;
            break;
        }
    }
    ASSERT(torp_ss >= 0);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) ship.shield_hp[i] = 0.0f;

    f32 hull_before = ship.hull_hp;
    bc_vec3_t impact = cls->subsystems[torp_ss].position; /* [0, -0.25, -0.25] */
    f32 damage = 3000.0f;

    bc_combat_apply_damage(&ship, cls, damage, 0.1f, impact, false, 1.0f);

    /* Torpedo tube must have absorbed its full HP (2400), not 0.5*3000=1500.
     * HP=2400-2400=0, not HP=2400-1500=900. */
    ASSERT(ship.subsystem_hp[torp_ss] < 0.01f);

    /* Hull must have received 0 damage — subsystems collectively absorbed all
     * of overflow (total absorption >> 3000), so hull_damage=max(0,3000-N)=0.
     * Old code gave hull the full 3000 regardless of subsystem absorption. */
    f32 hull_delta = hull_before - ship.hull_hp;
    ASSERT(hull_delta < 0.01f);
}

/*
 * Issue #87-B: No 25% propagation to parent container.
 *
 * Torpedo tubes have a parent container ("Torpedoes" system HP slot).
 * The old code propagated 25% of sub_dmg to the parent's HP slot.
 * The new code only modifies the directly hit subsystem's HP.
 */
TEST(no_parent_propagation)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    /* Find first torpedo tube that has a parent container */
    int child_ss = -1;
    int parent_slot = -1;
    for (int i = 0; i < cls->subsystem_count; i++) {
        int p = cls->subsystems[i].parent_idx;
        if (strcmp(cls->subsystems[i].type, "torpedo_tube") == 0 &&
            cls->subsystems[i].radius > 0.0f && p >= 0) {
            child_ss = i;
            parent_slot = p;
            break;
        }
    }
    if (child_ss < 0) return; /* Skip — ship has no parent-child torpedo pair */

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) ship.shield_hp[i] = 0.0f;

    f32 parent_hp_before = ship.subsystem_hp[parent_slot]; /* "Torpedoes" HP */

    /* damage_radius=0.1 at the torpedo position triggers subsystem search and
     * hits the torpedo tube (and nearby subsystems — see file header).
     * The torpedo parent slot is a container with no spatial position/radius,
     * so it is not in the hit list itself. */
    bc_vec3_t impact = cls->subsystems[child_ss].position;
    bc_combat_apply_damage(&ship, cls, 3000.0f, 0.1f, impact, false, 1.0f);

    /* Parent HP slot must be unchanged — no 25% propagation.
     * Old code: parent_hp -= overflow*0.5*0.25 = 375 (observable change).
     * New code: parent_hp untouched. */
    ASSERT(fabsf(ship.subsystem_hp[parent_slot] - parent_hp_before) < 0.01f);
}

/*
 * Issue #87-C: search_radius expands each subsystem's effective AABB radius.
 *
 * Impact placed at (ss_pos.y + ss_r * 1.2) on the Y axis — just outside the
 * 1.0x radius boundary but inside the 1.5x boundary.
 *
 * search_radius=1.0: effective ss radius = ss_r → impact outside → torpedo tube NOT hit → HP unchanged.
 * search_radius=1.5: effective ss radius = 1.5*ss_r → impact inside → torpedo tube HIT → HP decreases.
 *
 * The torpedo tube HP is checked directly (not hull_delta) to avoid interference
 * from other large-radius subsystems (e.g. Shield Generator r=0.50) that may
 * overlap the impact point independently of search_radius.
 *
 * A tiny positive damage_radius (0.001) is used to trigger the subsystem search
 * path (effective_radius > 0) without meaningfully expanding the hit zone.
 */
TEST(search_radius_expands_hit_set)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    /* Use first torpedo tube: pos=[0,-0.25,-0.25], radius=0.2 */
    int torp_ss = -1;
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, "torpedo_tube") == 0 &&
            cls->subsystems[i].radius > 0.0f) {
            torp_ss = i;
            break;
        }
    }
    ASSERT(torp_ss >= 0);

    f32 ss_r = cls->subsystems[torp_ss].radius;  /* 0.2 */
    bc_vec3_t ss_pos = cls->subsystems[torp_ss].position; /* [0,-0.25,-0.25] */

    /* Impact at 1.2*ss_r beyond the subsystem center on Y (= -0.25 + 0.24 = -0.01).
     * With sr=1.0: ss_r_test=0.2, impact_y=-0.01 > ss_pos.y+0.2=-0.05 → NOT hit.
     * With sr=1.5: ss_r_test=0.3, impact_y=-0.01 <= ss_pos.y+0.3=0.05  → HIT. */
    bc_vec3_t impact = { ss_pos.x, ss_pos.y + ss_r * 1.2f, ss_pos.z };
    f32 damage = 1000.0f;
    f32 damage_radius = 0.001f; /* tiny positive value to enter subsystem search */

    /* --- search_radius=1.0: impact outside torpedo tube effective radius → tube not hit --- */
    {
        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
        for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) ship.shield_hp[i] = 0.0f;
        f32 torp_hp_before = ship.subsystem_hp[torp_ss];

        bc_combat_apply_damage(&ship, cls, damage, damage_radius, impact, false, 1.0f);

        /* Torpedo tube is outside 1.0x effective radius → HP must be unchanged.
         * Other subsystems (e.g. Shield Generator) may still absorb, but that does
         * not affect this assertion which targets only the torpedo tube slot. */
        ASSERT(fabsf(ship.subsystem_hp[torp_ss] - torp_hp_before) < 0.01f);
    }

    /* --- search_radius=1.5: impact inside expanded radius → torpedo tube is hit --- */
    {
        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);
        for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) ship.shield_hp[i] = 0.0f;
        f32 torp_hp_before = ship.subsystem_hp[torp_ss];

        bc_combat_apply_damage(&ship, cls, damage, damage_radius, impact, false, 1.5f);

        /* Torpedo tube is inside 1.5x effective radius → HP must have decreased. */
        ASSERT(ship.subsystem_hp[torp_ss] < torp_hp_before - 0.01f);
    }
}

TEST_MAIN_BEGIN()
    RUN(load_registry);
    RUN(subsystem_absorbs_full_before_hull);
    RUN(no_parent_propagation);
    RUN(search_radius_expands_hit_set);
TEST_MAIN_END()
