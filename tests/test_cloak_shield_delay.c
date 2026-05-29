/*
 * test_cloak_shield_delay.c — Regression tests for Issue #192
 *
 * Wire-protocol trace analysis of a stock dedicated server session shows the
 * shield-active state change is deferred by ShieldDelay (1.0s) relative to the
 * cloak transition:
 *
 *   - Cloak-up: shields stay ACTIVE for the first ~1.0s (vulnerability window),
 *     then go inactive — even though the cloak field begins rendering at t=0.
 *   - Decloak: when the decloak transition completes the ship is fully visible
 *     but shields stay INACTIVE for the first ~1.0s (grace window), then re-enable.
 *
 * These tests exercise the deferred timer in combat.c via the dt-based cloak
 * state machine (bc_cloak_start / bc_cloak_tick / bc_cloak_shields_active).
 */

#include "test_util.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/combat.h"
#include "openbc/game_builders.h"
#include <string.h>
#include <math.h>

#define REGISTRY_DIR "data/vanilla-1.1"

static bc_game_registry_t g_reg;

TEST(load_registry)
{
    ASSERT(bc_registry_load_dir(&g_reg, REGISTRY_DIR));
}

/* Find a cloak-capable ship class in the registry. */
static const bc_ship_class_t *find_cloaker(void)
{
    for (int i = 0; i < g_reg.ship_count; i++) {
        if (g_reg.ships[i].can_cloak) return &g_reg.ships[i];
    }
    return NULL;
}

/*
 * #192-A: Fresh ship spawns with shields active and no pending change.
 */
TEST(spawn_shields_active)
{
    const bc_ship_class_t *cls = find_cloaker();
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 0, bc_make_ship_id(0), 0, 0);

    ASSERT(bc_cloak_shields_active(&ship));
    ASSERT(ship.shield_delay_timer == 0.0f);
}

/*
 * #192-B: Cloak-up vulnerability window.
 * After bc_cloak_start, shields must remain ACTIVE for ~1.0s, then go inactive.
 */
TEST(cloak_up_vulnerability_window)
{
    const bc_ship_class_t *cls = find_cloaker();
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 0, bc_make_ship_id(0), 0, 0);

    ASSERT(bc_cloak_start(&ship, cls));
    /* Immediately after cloak begins, shields are STILL up (vulnerability). */
    ASSERT(bc_cloak_shields_active(&ship));

    /* Advance 0.5s — still inside the 1.0s window: shields stay up. */
    bc_cloak_tick(&ship, 1.0f, 0.5f);
    ASSERT(bc_cloak_shields_active(&ship));

    /* Advance past the 1.0s ShieldDelay (total 1.1s): shields now inactive. */
    bc_cloak_tick(&ship, 1.0f, 0.6f);
    ASSERT(!bc_cloak_shields_active(&ship));

    /* Shield HP must be PRESERVED, not zeroed, through the transition. */
    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++)
        ASSERT(ship.shield_hp[i] == cls->shield_hp[i]);
}

/*
 * #192-C: Decloak grace window.
 * After the decloak transition completes, the ship is fully visible but shields
 * stay INACTIVE for ~1.0s, then re-enable.
 */
TEST(decloak_grace_window)
{
    const bc_ship_class_t *cls = find_cloaker();
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 0, bc_make_ship_id(0), 0, 0);

    /* Cloak fully: start + run through CloakTime + ShieldDelay. */
    ASSERT(bc_cloak_start(&ship, cls));
    bc_cloak_tick(&ship, 1.0f, BC_CLOAK_TRANSITION_TIME + BC_CLOAK_SHIELD_DELAY);
    ASSERT(ship.cloak_state == BC_CLOAK_CLOAKED);
    ASSERT(!bc_cloak_shields_active(&ship));

    /* Begin decloak and run the full decloak transition (no ShieldDelay yet). */
    ASSERT(bc_cloak_stop(&ship));
    bc_cloak_tick(&ship, 1.0f, BC_CLOAK_TRANSITION_TIME);
    ASSERT(ship.cloak_state == BC_CLOAK_DECLOAKED);

    /* Decloak just completed: fully visible, but shields STILL down (grace). */
    ASSERT(!bc_cloak_shields_active(&ship));

    /* Advance 0.5s — still inside the grace window. */
    bc_cloak_tick(&ship, 1.0f, 0.5f);
    ASSERT(!bc_cloak_shields_active(&ship));

    /* Advance past the 1.0s ShieldDelay (total 1.1s): shields re-enable. */
    bc_cloak_tick(&ship, 1.0f, 0.6f);
    ASSERT(bc_cloak_shields_active(&ship));
}

/*
 * #192-D: Damage gate honors the deferred state.
 * During the cloak-up vulnerability window shields must still absorb damage;
 * after the window they must not.
 */
TEST(damage_gate_respects_delay)
{
    const bc_ship_class_t *cls = find_cloaker();
    ASSERT(cls != NULL);

    bc_vec3_t dir = { 0.0f, 1.0f, 0.0f }; /* front facing */

    /* --- During vulnerability window: shields absorb (HP drops on a facing). */
    {
        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, 0, bc_make_ship_id(0), 0, 0);
        ASSERT(bc_cloak_start(&ship, cls));        /* shields still up */
        f32 front_before = ship.shield_hp[BC_SHIELD_FRONT];
        ASSERT(front_before > 0.0f);
        bc_combat_apply_damage(&ship, cls, 10.0f, 0.0f, dir, false, 1.0f);
        ASSERT(ship.shield_hp[BC_SHIELD_FRONT] < front_before);
    }

    /* --- After window: shields inactive, hull takes the hit directly. */
    {
        bc_ship_state_t ship;
        bc_ship_init(&ship, cls, 0, bc_make_ship_id(0), 0, 0);
        ASSERT(bc_cloak_start(&ship, cls));
        bc_cloak_tick(&ship, 1.0f, BC_CLOAK_SHIELD_DELAY + 0.1f); /* close window */
        ASSERT(!bc_cloak_shields_active(&ship));

        f32 front_before = ship.shield_hp[BC_SHIELD_FRONT];
        f32 hull_before  = ship.hull_hp;
        bc_combat_apply_damage(&ship, cls, 10.0f, 0.0f, dir, false, 1.0f);
        /* Shields untouched (cloaked), hull absorbed the damage. */
        ASSERT(ship.shield_hp[BC_SHIELD_FRONT] == front_before);
        ASSERT(ship.hull_hp < hull_before);
    }
}

TEST_MAIN_BEGIN()
    RUN(load_registry);
    RUN(spawn_shields_active);
    RUN(cloak_up_vulnerability_window);
    RUN(decloak_grace_window);
    RUN(damage_gate_respects_delay);
TEST_MAIN_END()
